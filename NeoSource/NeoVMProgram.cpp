#include <limits.h>

#include "NeoVMProgram.h"
#include "NeoVMInternal.h"
#include "NeoArchive.h"
#include "NeoTextLoader.h"   // FormatAsm / FormatBytes
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace NeoScript
{

extern u32 GetHashCode(u8* buffer, int len);
extern std::string GetDataType(VAR_TYPE t);   // NeoVMWorker.cpp

// ---- switch/case 테이블 ----
// 런타임 key(StringInfo)의 해시와 같은 알고리즘이어야 조회가 일치한다.
static u32 SwitchKeyHash(const ProgramSwitchKey& k)
{
	switch (k._type)
	{
	case VAR_INT:    return HashIntegerKey(k._int);
	case VAR_STRING: return GetHashCode(k._str);
	default:         return 0u;
	}
}

void ProgramSwitchTable::Build()
{
	buckets.clear();
	if (entries.empty())
		return;

	size_t capa = 8;
	while (capa < entries.size() * 2)
		capa <<= 1;
	buckets.assign(capa, -1);

	const u32 mask = (u32)(capa - 1);
	for (int i = 0; i < (int)entries.size(); ++i)
	{
		u32 b = SwitchKeyHash(entries[i].key) & mask;
		entries[i].next = buckets[b];
		buckets[b] = i;
	}
}

int ProgramSwitchTable::Find(ValueRef pKey) const
{
	if (buckets.empty() || pKey == nullptr)
		return defaultOffset;

	// strict type match. case key 로 쓸 수 없는 타입(float/map/list/vector/null 등)은 default 로.
	VAR_TYPE t = pKey.GetType();
	u32 h;
	switch (t)
	{
	case VAR_INT:    h = HashIntegerKey(pKey->_int); break;
	case VAR_STRING: h = pKey->_str->GetHash(); break;
	default: return defaultOffset;
	}

	for (int i = buckets[h & (u32)(buckets.size() - 1)]; i >= 0; i = entries[i].next)
	{
		const ProgramSwitchEntry& e = entries[i];
		if (e.key._type != t)
			continue;
		switch (t)
		{
		case VAR_INT:    if (e.key._int == pKey->_int) return e.jumpOffset; break;
		case VAR_STRING: if (e.key._str == pKey->_str->_str) return e.jumpOffset; break; // UTF-8 바이트 일치
		default: break;
		}
	}
	return defaultOffset;
}

struct ImageLoadError { const char* message; };
static size_t ImageBytesRemaining(CNArchive& ar)
{
	return (size_t)(ar.GetBufferSize() - ar.GetBufferOffset());
}
static void RequireImageCount(CNArchive& ar, size_t count, size_t minimumBytes, const char* message)
{
	if (minimumBytes == 0 || count > ImageBytesRemaining(ar) / minimumBytes)
		throw ImageLoadError{message};
}
template<typename T> static void ReadRequired(CNArchive& ar, T& value)
{
	if (ar.Read(&value, sizeof(value)) != sizeof(value))
		throw ImageLoadError{"Truncated script image (required field)"};
}

static bool ReadImageBool(CNArchive& ar)
{
	u8 value = 0;
	ReadRequired(ar, value);
	if (value > 1) throw ImageLoadError{"Invalid boolean constant"};
	return value != 0;
}

static bool ReadString(CNArchive& ar, std::string& str)
{
	short nLen = 0;
	if (ar.Read(&nLen, sizeof(nLen)) == 0)
		return false;
	if (nLen < 0 || (size_t)nLen > ImageBytesRemaining(ar))
		return false;

	str.resize(nLen);
	if (nLen == 0)
		return true;
	return ar.Read((char*)str.data(), nLen) != 0;
}
static bool ReadCount(CNArchive& ar, u32& out)
{
	u16 wCount = 0;
	if (ar.Read(&wCount, sizeof(wCount)) == 0)
		return false;
	if (wCount < 0xFFFF)
	{
		out = wCount;
		return true;
	}
	u32 dwCount = 0;
	if (ar.Read(&dwCount, sizeof(dwCount)) == 0)
		return false;
	out = dwCount;
	return true;
}

static void SetLoadError(std::string* err, const char* msg)
{
	if (err != nullptr)
	{
		try { *err = msg; } catch (const std::bad_alloc&) {}
	}
}

CNeoVMProgram* CNeoVMProgram::Create(const void* pBuffer, int iSize, std::string* err)
{
	if (pBuffer == nullptr || iSize <= (int)sizeof(SNeoVMHeader))
	{
		SetLoadError(err, "Invalid script image");
		return nullptr;
	}

	try
	{
		CNeoVM::InitLib();
		CNArchive ar(const_cast<void*>(pBuffer), iSize);
		auto release = [](CNeoVMProgram* program) { if (program) program->Release(); };
		std::unique_ptr<CNeoVMProgram, decltype(release)> p(new CNeoVMProgram(), release);
		p->byteSize = iSize;
		if (!p->Load(ar, err)) return nullptr;
		p->PatchNativeCalls();
		p->PatchStaticStringReads();
		if (!p->PatchLocalOps(err)) return nullptr;
		return p.release();
	}
	catch (const ImageLoadError& error) { SetLoadError(err, error.message); }
	catch (const std::length_error&) { SetLoadError(err, "Invalid script image allocation size"); }
	catch (const std::bad_alloc&)
	{
		// 메모리 부족 메시지 작성 중에도 예외가 로더 API 밖으로 새어 나가지 않게 한다.
		try { SetLoadError(err, "Out of memory while loading script image"); } catch (...) {}
	}
	return nullptr;
}

bool CNeoVMProgram::Load(CNArchive& ar, std::string* err)
{
	memset(&header, 0, sizeof(header));
	if (ar.Read(&header, sizeof(header)) == 0)
	{
		SetLoadError(err, "Truncated script image (header)");
		return false;
	}

	if (header._dwFileType != FILE_NEOS)
	{
		SetLoadError(err, "Not a NeoScript image");
		return false;
	}
	if (header._dwNeoVersion != NEO_VER)
	{
		SetLoadError(err, "NeoScript image version mismatch");
		return false;
	}

	bool IsDataSinglePrecision = (header._dwFlag & NEO_HEADER_FLAG_SINGLE_PRECISION) ? true : false;
	if (IsDataSinglePrecision != NeoVMSystem::IsSinglePrecision())
	{
		SetLoadError(err, "NeoScript image float precision mismatch");
		return false;
	}

	if (header._iCodeSize < 0 || (header._iCodeSize % (int)sizeof(SVMOperation)) != 0)
	{
		SetLoadError(err, "Invalid code size");
		return false;
	}
	if (header._iFunctionCount < 0 || header._iStaticVarCount < 0 ||
		header._iGlobalVarCount < 0 || header._iGlobalVarCount > 32767 || header._iExportVarCount < 0 ||
		header._iStaticVarCount > INT_MAX - header._iGlobalVarCount - 1 ||
		header._iFunctionCount > SHRT_MAX + 1 || header.m_iDebugCount < 0)
	{
		SetLoadError(err, "Invalid script image counts");
		return false;
	}

	RequireImageCount(ar, header._iCodeSize, 1, "Truncated script image (code size)");
	code.resize(header._iCodeSize / sizeof(SVMOperation));
	if (header._iCodeSize > 0 && ar.Read(code.data(), header._iCodeSize) == 0)
	{
		SetLoadError(err, "Truncated script image (code)");
		return false;
	}

	RequireImageCount(ar, header._iFunctionCount, 19, "Truncated script image (function count)");
	functions.resize(header._iFunctionCount);
	std::vector<bool> functionIDs(header._iFunctionCount, false);
	std::string name;
	for (int i = 0; i < header._iFunctionCount; i++)
	{
		int iID = -1;
		SFunctionTable fun{};

		if (ar.Read(&iID, sizeof(iID)) == 0)
		{
			SetLoadError(err, "Truncated script image (function table)");
			return false;
		}
		ReadRequired(ar, fun._codePtr); ReadRequired(ar, fun._argsCount);
		ReadRequired(ar, fun._localTempMax); ReadRequired(ar, fun._localVarCount); ReadRequired(ar, fun._funType);
		if (fun._argsCount < 0 || fun._localTempMax < 0 || fun._localVarCount < 0 || fun._funType > FUNT_BUILT_IN ||
			(int64_t)fun._argsCount + fun._localTempMax + fun._localVarCount + 1 > SHRT_MAX)
		{
			SetLoadError(err, "Invalid function local counts");
			return false;
		}
		u16 captureCount = 0;
		ReadRequired(ar, captureCount);
		RequireImageCount(ar, captureCount, 2 * sizeof(short), "Truncated script image (captures)");
		if (captureCount > (u16)fun._localVarCount)
		{
			SetLoadError(err, "Invalid function closure capture count");
			return false;
		}
		fun._captures.resize(captureCount);
		for (u16 c = 0; c < captureCount; ++c)
		{
			ReadRequired(ar, fun._captures[c].first); ReadRequired(ar, fun._captures[c].second);
			if (fun._captures[c].first <= 0 || fun._captures[c].second <= 0)
			{
				SetLoadError(err, "Invalid function closure capture slot");
				return false;
			}
		}
		if (iID < 0 || iID >= header._iFunctionCount || functionIDs[iID])
		{
			SetLoadError(err, "Invalid function id");
			return false;
		}
		if (fun._funType != FUNT_NORMAL && fun._funType != FUNT_ANONYMOUS)
		{
			if (false == ReadString(ar, name))
			{
				SetLoadError(err, "Truncated script image (function name)");
				return false;
			}
			exportFunctions[name] = iID;
		}

		fun._localAddCount = 1 + fun._argsCount + fun._localVarCount + fun._localTempMax;
		if (fun._localAddCount <= 0)
		{
			SetLoadError(err, "Invalid function local range");
			return false;
		}
		for (const std::pair<short, short>& capture : fun._captures)
		{
			if (capture.second >= fun._localAddCount)
			{
				SetLoadError(err, "Invalid function closure local slot");
				return false;
			}
		}
		functionIDs[iID] = true;
		functions[iID] = std::move(fun);
	}

	// FMOV가 읽는 capture sourceSlot은 생성한 함수(직속 부모)의 스택 기준이다.
	// 이미지가 손상돼 슬롯/함수 번호가 어긋나면 호출 전에 거부해 프레임 밖 읽기나
	// 쓰기로 이어지지 않게 한다.
	std::vector<std::pair<int, int>> functionStarts;
	functionStarts.reserve(functions.size());
	for (int i = 0; i < (int)functions.size(); ++i)
	{
		const int codePtr = functions[i]._codePtr;
		if (codePtr < 2 * (int)sizeof(SVMOperation) || codePtr >= header._iCodeSize ||
			(codePtr % (int)sizeof(SVMOperation)) != 0)
		{
			SetLoadError(err, "Invalid function code offset");
			return false;
		}
		functionStarts.push_back(std::make_pair(codePtr / (int)sizeof(SVMOperation), i));
	}
	std::sort(functionStarts.begin(), functionStarts.end());
	std::vector<int> codeOwners(code.size(), -1);
	for (size_t i = 0; i < functionStarts.size(); ++i)
	{
		const int begin = functionStarts[i].first;
		const int end = (i + 1 < functionStarts.size()) ? functionStarts[i + 1].first : (int)code.size();
		if (begin >= end)
		{
			SetLoadError(err, "Invalid function code range");
			return false;
		}
		for (int opIndex = begin; opIndex < end; ++opIndex)
			codeOwners[opIndex] = functionStarts[i].second;
	}
	// 호출 인자를 만드는 명령은 CALL보다 먼저 실행된다. 호출자 진입 시 전체 인자
	// 범위를 확보하되, 다음 프레임의 시작 위치는 기존 _localAddCount로 유지한다.
	for (SFunctionTable& fun : functions) fun._stackSlotCount = fun._localAddCount;
	for (size_t i = 0; i < code.size(); ++i)
	{
		if (codeOwners[i] < 0) continue;
		SFunctionTable& fun = functions[codeOwners[i]];
		const SVMOperation& op = code[i];
		int outgoing = -1;
		switch (op.Code())
		{
		case NOP_CALL:
		case NOP_CALL_L: outgoing = op.n2; break;
		case NOP_PTRCALL:
		case NOP_PTRCALL_L: outgoing = op.n3; break;
		case NOP_PTRCALL2:
		case NOP_PTRCALL2_L:
		case NOP_NATIVECALL:
		case NOP_VEC_MAKE: outgoing = op.n2; break;
		default: break;
		}
		if (outgoing >= 0)
			fun._stackSlotCount = (std::max)(fun._stackSlotCount, fun._localAddCount + outgoing + 1);
	}
	// 피연산자 값으로 프레임을 늘리면 잘못된 큰 인덱스도 정상으로 인정하게 된다. 범위는 호출 인자 수로만 정한다.
	// 내보내기가 접던 getter의 창 반환 슬롯 쓰기는 파서 접기로 사라져 따로 허용하지 않는다.
	// WREF consumes its next instruction only for stored vectors. Validate the
	// immutable pair once here, so ordinary collection writes need no hot checks.
	auto IsRefWriteOpcode = [](OperationCode op) {
		switch (op) {
		case NOP_CLT_MOV:
		case NOP_CLT_MOV_L: return true;
		default: return false;
		}
	};
	for (size_t i = 0; i < code.size(); ++i)
	{
		const SVMOperation& op = code[i];
		if (IsRefWriteOpcode(op.Code()) && (op.argFlag & NEOS_OP_REF_WRITE) &&
			(i == 0 || code[i - 1].Code() != NOP_CLT_WRITE_REF))
		{
			SetLoadError(err, "Orphan chained write");
			return false;
		}
		if (op.Code() != NOP_CLT_WRITE_REF) continue;
		if (i + 1 >= code.size() || codeOwners[i] < 0 || codeOwners[i] != codeOwners[i + 1])
		{
			SetLoadError(err, "Incomplete chained write");
			return false;
		}
		const SVMOperation& write = code[i + 1];
		const u8 locals = NEOS_ARG_N1_LOCAL | NEOS_ARG_N2_LOCAL | NEOS_ARG_N3_LOCAL;
		const auto& fun = functions[codeOwners[i]];
		auto ValidSlot = [&](short slot, bool local, bool writable) {
			return local ? slot >= 0 && slot < fun._localAddCount :
				(writable ? IsGlobalVariable(slot) : IsGlobalOperand(slot));
		};
		if (!IsRefWriteOpcode(write.Code()) || !(write.argFlag & NEOS_OP_REF_WRITE) ||
			(op.argFlag & ~(locals | NEOS_OP_REF_LOADED)) ||
			(write.argFlag & ~(locals | NEOS_OP_REF_WRITE)) ||
			write.n1 != op.n3 ||
			((write.argFlag & NEOS_ARG_N1_LOCAL) != 0) != ((op.argFlag & NEOS_ARG_N3_LOCAL) != 0) ||
			(write.Code() == NOP_CLT_MOV_L && (write.argFlag & locals) != locals) ||
			!ValidSlot(op.n1, op.argFlag & NEOS_ARG_N1_LOCAL, false) ||
			!ValidSlot(op.n2, op.argFlag & NEOS_ARG_N2_LOCAL, false) ||
			!ValidSlot(op.n3, op.argFlag & NEOS_ARG_N3_LOCAL, true) ||
			!ValidSlot(write.n2, write.argFlag & NEOS_ARG_N2_LOCAL, false) ||
			!ValidSlot(write.n3, write.argFlag & NEOS_ARG_N3_LOCAL, false))
		{
			SetLoadError(err, "Invalid chained write operands");
			return false;
		}
	}
	for (int opIndex = 0; opIndex < (int)code.size(); ++opIndex)
	{
		const SVMOperation& op = code[opIndex];
		int functionID = -1;
		if (op.Code() == NOP_FMOV1) functionID = op.n2;
		else if (op.Code() == NOP_FMOV2) functionID = op.n3;
		else continue;
		if (functionID < 0 || functionID >= (int)functions.size() || codeOwners[opIndex] < 0)
		{
			SetLoadError(err, "Invalid closure function reference");
			return false;
		}
		const SFunctionTable& parent = functions[codeOwners[opIndex]];
		for (const std::pair<short, short>& capture : functions[functionID]._captures)
		{
			if (capture.first >= parent._localAddCount)
			{
				SetLoadError(err, "Invalid closure source slot");
				return false;
			}
		}
	}

	RequireImageCount(ar, header._iExportVarCount, sizeof(int) + sizeof(short), "Truncated script image (export count)");
	for (int i = 0; i < header._iExportVarCount; i++)
	{
		int idx = -1;
		if (ar.Read(&idx, sizeof(idx)) == 0)
		{
			SetLoadError(err, "Truncated script image (export vars)");
			return false;
		}
		if (false == ReadString(ar, name))
		{
			SetLoadError(err, "Truncated script image (export var name)");
			return false;
		}
		if (!IsGlobalVariable(idx))
		{
			SetLoadError(err, "Invalid exported global slot");
			return false;
		}
		exportVariables[name] = idx;
	}

	RequireImageCount(ar, header._iStaticVarCount, sizeof(VAR_TYPE) + sizeof(bool), "Truncated script image (constant count)");
	staticValues.resize(header._iStaticVarCount);
	for (int i = 0; i < header._iStaticVarCount; i++)
	{
		SStaticConst& sc = staticValues[i];
		VAR_TYPE type = VAR_NONE;
		if (ar.Read(&type, sizeof(type)) == 0)
		{
			SetLoadError(err, "Truncated script image (static values)");
			return false;
		}
		sc._type = type;
		switch (type)
		{
		case VAR_INT:
			ReadRequired(ar, sc._int);
			break;
		case VAR_FLOAT:
			ReadRequired(ar, sc._float);
			break;
		case VAR_BOOL:
			sc._bl = ReadImageBool(ar);
			break;
		case VAR_STRING:
			if (false == ReadString(ar, sc._str))
			{
				SetLoadError(err, "Truncated script image (static string)");
				return false;
			}
			break;
		default:
			SetLoadError(err, "Error Invalid VAR Type");
			return false;
		}
	}

	if (header.m_iDebugCount > 0)
	{
		RequireImageCount(ar, header.m_iDebugCount, sizeof(debug_info), "Truncated script image (debug count)");
		debugData.resize(header.m_iDebugCount);
		if (ar.Read(&debugData[0], (int)(sizeof(debug_info) * (size_t)header.m_iDebugCount)) == 0)
		{
			SetLoadError(err, "Truncated script image (debug info)");
			return false;
		}
	}

	// 선택 청크: 디버그 변수명(BVDN) / 함수명(NFDN). 미지의 magic 을 만나면 되돌리고 종료.
    bool hasStorage = false;
	while (ImageBytesRemaining(ar) != 0)
	{
		u32 magic = 0;
		int oldOffset = ar.GetBufferOffset();
		ReadRequired(ar, magic);
		if (magic == 0x4E445642)
		{
			u32 funCount = 0;
			if (false == ReadCount(ar, funCount))
				throw ImageLoadError{"Invalid or truncated script image chunk"};
			RequireImageCount(ar, funCount, sizeof(int) + sizeof(short), "Invalid debug function count");
			for (u32 i = 0; i < funCount; ++i)
			{
				int funId = -1;
				if (ar.Read(&funId, sizeof(funId)) == 0)
					throw ImageLoadError{"Invalid or truncated script image chunk"};
				u32 nameCount = 0;
				if (false == ReadCount(ar, nameCount))
					throw ImageLoadError{"Invalid or truncated script image chunk"};
				RequireImageCount(ar, nameCount, sizeof(int) + sizeof(short), "Invalid debug variable count");
				if (funId < -1 || funId >= (int)functions.size()) throw ImageLoadError{"Invalid debug function id"};
				std::map<int, std::string>& names = (funId == -1) ? debugGlobalNames : debugVarNames[funId];
				for (u32 n = 0; n < nameCount; ++n)
				{
					int slot = -1;
					if (ar.Read(&slot, sizeof(slot)) == 0)
						throw ImageLoadError{"Invalid or truncated script image chunk"};
					if (false == ReadString(ar, name))
						throw ImageLoadError{"Invalid or truncated script image chunk"};
					names[slot] = name;
				}
			}
		}
		else if (magic == 0x4E44464E)
		{
			u32 funCount = 0;
			if (false == ReadCount(ar, funCount))
				throw ImageLoadError{"Invalid or truncated script image chunk"};
			RequireImageCount(ar, funCount, sizeof(int) + sizeof(short), "Invalid debug function count");
			for (u32 i = 0; i < funCount; ++i)
			{
				int funId = -1;
				if (ar.Read(&funId, sizeof(funId)) == 0)
					throw ImageLoadError{"Invalid or truncated script image chunk"};
				if (false == ReadString(ar, name))
					throw ImageLoadError{"Invalid or truncated script image chunk"};
				if (funId < 0 || funId >= (int)functions.size()) throw ImageLoadError{"Invalid debug function id"};
				debugFunctionNames[funId] = name;
			}
		}
		else if (magic == 0x54495753)   // 'SWIT' : switch/case 테이블
		{
			u32 tableCount = 0;
			if (false == ReadCount(ar, tableCount))
				throw ImageLoadError{"Invalid or truncated script image chunk"};
			RequireImageCount(ar, tableCount, sizeof(int) + sizeof(u16), "Invalid switch table count");
			if (tableCount > (u32)USHRT_MAX + 1) throw ImageLoadError{"Invalid switch table count"};
			switchTables.resize(tableCount);
			for (u32 t = 0; t < tableCount; ++t)
			{
				ProgramSwitchTable& tbl = switchTables[t];
				if (ar.Read(&tbl.defaultOffset, sizeof(tbl.defaultOffset)) == 0)
					throw ImageLoadError{"Invalid or truncated script image chunk"};
				u32 caseCount = 0;
				if (false == ReadCount(ar, caseCount))
					throw ImageLoadError{"Invalid or truncated script image chunk"};
				RequireImageCount(ar, caseCount, sizeof(u8) + sizeof(bool) + sizeof(int), "Invalid switch case count");
				tbl.entries.resize(caseCount);
				for (u32 c = 0; c < caseCount; ++c)
				{
					ProgramSwitchEntry& e = tbl.entries[c];
					u8 keyType = 0;
					if (ar.Read(&keyType, sizeof(keyType)) == 0)
						throw ImageLoadError{"Invalid or truncated script image chunk"};
					e.key._type = (VAR_TYPE)keyType;
					switch (e.key._type)
					{
					case VAR_INT:
						if (ar.Read(&e.key._int, sizeof(e.key._int)) == 0) throw ImageLoadError{"Invalid or truncated script image chunk"};
						break;
					case VAR_STRING:
						if (false == ReadString(ar, e.key._str)) throw ImageLoadError{"Invalid or truncated script image chunk"};
						break;
					default:
						throw ImageLoadError{"Invalid or truncated script image chunk"};   // 손상된 이미지
					}
					if (ar.Read(&e.jumpOffset, sizeof(e.jumpOffset)) == 0)
						throw ImageLoadError{"Invalid or truncated script image chunk"};
				}
				tbl.Build();
			}
		}
		else if (magic == 0x45474E52)   // 'RNGE' : range jump descriptors
		{
			u32 rangeCount = 0;
			if (false == ReadCount(ar, rangeCount) || rangeCount > (u32)USHRT_MAX + 1)
				throw ImageLoadError{"Invalid or truncated script image chunk"};
			RequireImageCount(ar, rangeCount, 2 * sizeof(short) + sizeof(u8), "Invalid range descriptor count");
			rangeJumps.resize(rangeCount);
			for (u32 i = 0; i < rangeCount; ++i)
			{
				ProgramRangeJump& range = rangeJumps[i];
				if (ar.Read(&range.lower, sizeof(range.lower)) == 0 ||
					ar.Read(&range.upper, sizeof(range.upper)) == 0 ||
					ar.Read(&range.flags, sizeof(range.flags)) == 0)
				{
					throw ImageLoadError{"Invalid or truncated script image chunk"};
				}
				if (range.flags & ~(NEOS_RANGE_LOWER_LOCAL | NEOS_RANGE_UPPER_LOCAL |
					NEOS_RANGE_LOWER_INCLUSIVE | NEOS_RANGE_UPPER_INCLUSIVE | NEOS_RANGE_UPPER_FIRST))
					throw ImageLoadError{"Invalid or truncated script image chunk"};
			}
		}
        else if (magic == 0x544f4c53)
        {
            u32 count = 0;
            if (hasStorage || !ReadCount(ar, count) || count != functions.size())
                throw ImageLoadError{"Invalid function storage count"};
            hasStorage = true;
            std::vector<bool> seen(functions.size(), false);
            for (u32 i = 0; i < count; ++i)
            {
                int id = -1; u32 slots = 0; SlotKind resultKind;
                ReadRequired(ar, id); ReadRequired(ar, resultKind);
                if (id < 0 || static_cast<std::size_t>(id) >= functions.size() || seen[id] ||
                    !ReadCount(ar, slots) || slots >= static_cast<u32>(functions[id]._localAddCount))
                    throw ImageLoadError{"Invalid function storage layout"};
                seen[id] = true;
                SFunctionTable& function = functions[id];
                if (resultKind > SlotKind::Iterator) throw ImageLoadError{"Invalid function result storage"};
                function._resultType = StorageValueType(resultKind);
                RequireImageCount(ar, slots, 7, "Truncated function storage layout");
                function._storage.resize(slots);
                int previous = 0;
                for (SlotLayout& slot : function._storage)
                {
                    ReadRequired(ar, slot.slot); ReadRequired(ar, slot.kind); ReadRequired(ar, slot.typeId);
                    if (slot.slot <= previous || slot.slot >= function._localAddCount ||
                        slot.kind > SlotKind::Iterator)
                        throw ImageLoadError{"Invalid function storage slot"};
                    previous = slot.slot;
                }
                u32 owned = 0, infoCount = 0;
                if (!ReadCount(ar, owned) || !ReadCount(ar, infoCount) || infoCount != static_cast<u32>(function._localAddCount) ||
                    owned > infoCount)
                    throw ImageLoadError{"Invalid function slot layout"};
                RequireImageCount(ar, infoCount, 1, "Truncated function slot layout");
                function._slotInfo.resize(infoCount);
                if (infoCount && ar.Read(function._slotInfo.data(), static_cast<int>(infoCount)) == 0)
                    throw ImageLoadError{"Truncated function slot layout"};
                function._ownedCount = static_cast<int>(owned);
            }
            if (!ReadCount(ar, count) || count != static_cast<u32>(header._iGlobalVarCount))
                throw ImageLoadError{"Invalid global storage count"};
            RequireImageCount(ar, count, 7, "Truncated global storage layout");
            globalStorage.resize(count);
            int previous = -header._iGlobalVarCount - 2;
            for (SlotLayout& slot : globalStorage)
            {
                ReadRequired(ar, slot.slot); ReadRequired(ar, slot.kind); ReadRequired(ar, slot.typeId);
                if (slot.slot <= previous || slot.slot >= -1 || slot.kind > SlotKind::Iterator)
                    throw ImageLoadError{"Invalid global storage slot"};
                previous = slot.slot;
            }
        }
        else if (magic == 0x4c4c4143)
        {
            u32 count = 0;
            if (!ReadCount(ar, count) || count != functions.size()) throw ImageLoadError{"Invalid call layout count"};
            for (u32 index = 0; index < count; ++index)
            {
                int id = -1; u32 calls = 0; ReadRequired(ar, id);
                if (id != static_cast<int>(index) || !ReadCount(ar, calls) || calls > code.size())
                    throw ImageLoadError{"Invalid call storage layout"};
                functions[id]._calls.resize(calls);
                for (CallStoragePlan& call : functions[id]._calls)
                {
                    SlotKind result; u32 args = 0; ReadRequired(ar, result);
                    if (result > SlotKind::Iterator || !ReadCount(ar, args) || args > SHRT_MAX)
                        throw ImageLoadError{"Invalid call signature"};
                    call.types.push_back(StorageValueType(result));
                    call.policies.push_back(static_cast<u8>(PolicyOf(result)));
                    for (u32 arg = 0; arg < args; ++arg)
                    {
                        SlotKind kind; ReadRequired(ar, kind);
                        if (kind > SlotKind::Iterator) throw ImageLoadError{"Invalid call argument storage"};
                        call.types.push_back(StorageValueType(kind));
                        call.policies.push_back(static_cast<u8>(PolicyOf(kind)));
                    }
                    // 숫자 payload 슬롯은 비울 필요가 없다. 정책 복사가 이전 값을 놓는 슬롯만 0으로 채운다.
                    call.zeroFirst = call.zeroEnd = 0;
                    for (std::size_t slot = 0; slot < call.policies.size(); ++slot)
                    {
                        if (static_cast<StoragePolicy>(call.policies[slot]) == StoragePolicy::Payload) continue;
                        if (call.zeroEnd == 0) call.zeroFirst = static_cast<int>(slot);
                        call.zeroEnd = static_cast<int>(slot) + 1;
                    }
                }
            }
        }
		else
		{
			ar.SetBufferOffset(oldOffset);
			break;
		}
	}

    if (!hasStorage) { SetLoadError(err, "Missing compiler storage layouts"); return false; }
    globalTypes.assign(GetGlobalSlotCount(), VAR_NONE);
    for (const SlotLayout& slot : globalStorage)
    {
        if (slot.kind == SlotKind::Boxed) { SetLoadError(err, "Missing global storage type"); return false; }
        globalTypes[ConstantBase() + slot.slot] = StorageValueType(slot.kind);
    }
    for (int i = 0; i < header._iStaticVarCount; ++i) globalTypes[ConstantBase() + i] = staticValues[i]._type;
    for (SFunctionTable& function : functions)
    {
        // 창 슬롯은 호출 계획이 타입을 주므로 프레임 표에서는 VAR_NONE이다.
        function._slotTypes.assign(function._stackSlotCount, VAR_NONE);
        function._slotTypes[0] = function._resultType;
        for (const SlotLayout& slot : function._storage) function._slotTypes[slot.slot] = StorageValueType(slot.kind);
        if (!function._captures.empty())
        {
            std::shared_ptr<std::vector<VAR_TYPE>> types(new std::vector<VAR_TYPE>);
            types->reserve(function._captures.size());
            for (const std::pair<short, short>& capture : function._captures)
            {
                const SlotKind kind = FindSlotKind(function._storage, capture.second);
                if (kind == SlotKind::Boxed) { SetLoadError(err, "Missing capture storage type"); return false; }
                types->push_back(StorageValueType(kind));
            }
            function._captureTypes = types;
        }
        // 로더는 컴파일러의 정리 계획을 검증할 뿐 새 최적화를 선택하지 않는다.
        // 슬롯 정책은 저장표의 종류와 일치해야 하고, 소유 슬롯은 인자 뒤 연속 구간 안에만 있어야 한다.
        if (static_cast<int>(function._slotInfo.size()) != function._localAddCount)
        { SetLoadError(err, "Missing function slot layout"); return false; }
        const int ownedEnd = 1 + function._argsCount + function._ownedCount;
        if (ownedEnd > function._localAddCount) { SetLoadError(err, "Invalid function owned range"); return false; }
        std::size_t typed = 0;
        for (int i = 0; i < function._localAddCount; ++i)
        {
            SlotKind kind = SlotKind::Boxed;
            if (typed < function._storage.size() && function._storage[typed].slot == i)
                kind = function._storage[typed++].kind;
            const StoragePolicy policy = SlotInfoPolicy(function._slotInfo[i]);
            if (i == 0)
            {
                if (policy != PolicyOfType(function._resultType) && policy != StoragePolicy::Boxed)
                { SetLoadError(err, "Invalid function result policy"); return false; }
                continue;
            }
            if (policy != PolicyOf(kind) && !(kind == SlotKind::Boxed && i <= function._argsCount))
            { SetLoadError(err, "Invalid function slot policy"); return false; }
            if ((function._slotInfo[i] & ~(SLOT_INFO_POLICY_MASK | SLOT_INFO_TEMPORARY)) != 0)
            { SetLoadError(err, "Invalid function slot flags"); return false; }
            if (i >= ownedEnd && policy != StoragePolicy::Payload)
            { SetLoadError(err, "Owned slot outside the owned range"); return false; }
        }
        function._releaseCount = ownedEnd;
        // 숫자 인자만 있는 함수는 반환 정리가 슬롯을 하나도 보지 않는다.
        function._releaseFirst = ownedEnd;
        for (int i = 1; i < ownedEnd; ++i)
        {
            if (SlotInfoPolicy(function._slotInfo[i]) == StoragePolicy::Payload) continue;
            function._releaseFirst = i;
            break;
        }
    }
	// range jump은 descriptor를 반드시 참조해야 한다. descriptor의 local slot은 해당
	// opcode를 소유한 함수 프레임 기준이고, global slot은 상수 0 기준 signed offset이다.
	for (int opIndex = 0; opIndex < (int)code.size(); ++opIndex)
	{
		const SVMOperation& op = code[opIndex];
		if (op.Code() == NOP_LOADK)
		{
			const bool local = (op.argFlag & NEOS_ARG_N1_LOCAL) != 0;
			if (op.n23 < 0 || op.n23 >= header._iStaticVarCount || codeOwners[opIndex] < 0 ||
				(local ? op.n1 < 0 : !IsGlobalVariable(op.n1)))
			{
				SetLoadError(err, "Invalid LOADK operand");
				return false;
			}
		}
		if (op.Code() != NOP_JMP_RANGE_INSIDE && op.Code() != NOP_JMP_RANGE_OUTSIDE)
			continue;
		const int rangeIndex = (int)(u16)op.n3;
		if (rangeIndex < 0 || rangeIndex >= (int)rangeJumps.size() || codeOwners[opIndex] < 0)
		{
			SetLoadError(err, "Invalid range jump descriptor");
			return false;
		}
		const SFunctionTable& fun = functions[codeOwners[opIndex]];
		const ProgramRangeJump& range = rangeJumps[rangeIndex];
		auto IsValidRangeOperand = [&](short slot, bool isLocal)
		{
			if (isLocal)
				return slot >= 0 && slot < fun._localAddCount;
			return IsGlobalOperand(slot);
		};
		if (IsValidRangeOperand(op.n2, (op.argFlag & NEOS_ARG_N2_LOCAL) != 0) == false ||
			IsValidRangeOperand(range.lower, (range.flags & NEOS_RANGE_LOWER_LOCAL) != 0) == false ||
			IsValidRangeOperand(range.upper, (range.flags & NEOS_RANGE_UPPER_LOCAL) != 0) == false)
		{
			SetLoadError(err, "Invalid range jump operand");
			return false;
		}
	}
    if (!VerifyCode(codeOwners, err)) return false;
    return true;
}

// 저장된 명령 형식을 한 번 검증한다. 로드 후 치환용 opcode는 파일에서 받지 않고,
// 아래의 패치 단계에서 원본 명령 검증을 마친 뒤에만 만든다.
bool CNeoVMProgram::VerifyCode(const std::vector<int>& owners, std::string* err)
{
    auto reject = [&](const char* reason) { SetLoadError(err, reason); return false; };
    if (functions.empty() || code.size() < 3 || code[0].Code() != NOP_ERROR || code[1].Code() != NOP_IDLE ||
        owners[2] < 0)
        return reject("Invalid script entry instructions");
    const u8 L1 = NEOS_ARG_N1_LOCAL, L2 = NEOS_ARG_N2_LOCAL, L3 = NEOS_ARG_N3_LOCAL;
    for (size_t i = 2; i < code.size(); ++i)
    {
        if (owners[i] < 0) return reject("Instruction outside function");
        const SVMOperation& op = code[i];
        const SFunctionTable& fun = functions[owners[i]];
        unsigned slots = 0, writes = 0;
        int argumentCount = -1;
        bool jump = false, terminal = false;
        const bool noResult = (op.argFlag & NEOS_OP_CALL_NORESULT) != 0;
        const bool immediateMove = IsImmediateMove(op);
        const bool fixedCopy = op.op == NOP_MOV && !immediateMove;
        const OperationCode checkedCode = (op.op == NOP_MOV || op.op == NOP_LOADK || op.op == NOP_VAR_CLEAR)
            ? static_cast<OperationCode>(op.op) : op.Code();
        // 창 슬롯(지역 배치 밖)은 호출 계획이 종류를 정한다. 고정 숫자 명령은 payload만 쓰므로 창 목적지를 받아들인다.
        if (IsFixedArithmetic(op) && !((op.argFlag & L1) && op.n1 >= fun._localAddCount))
        {
            const SlotKind actual = FindSlotKind((op.argFlag & L1) ? fun._storage : globalStorage, op.n1);
            const SlotKind expected = op.op == NOP_F64_FIXED ? SlotKind::Float : SlotKind::Int;
            if (actual != expected) return reject("Invalid fixed storage destination");
        }
        if (immediateMove)
        {
            // 즉값은 숫자 슬롯에만 쓴다. 소유 참조 슬롯에 payload를 덮어쓰는 이미지는 거부한다.
            const SlotKind actual = FindSlotKind((op.argFlag & L1) ? fun._storage : globalStorage, op.n1);
            const SlotKind expected = (op.argFlag & NEOS_OP_SUB_MASK) == MOV_SUB_IMMEDIATE_FLOAT ? SlotKind::Float : SlotKind::Int;
            if (actual != expected && actual != SlotKind::Boxed) return reject("Invalid immediate destination");
            if (op.argFlag & NEOS_OP_MODIFIER) return reject("Invalid immediate flags");
        }
        // 정책을 실은 복사·해제는 목적지(와 원본)의 선언 정책과 같아야 한다. 창 슬롯은 호출 계획이 정한다.
        if (fixedCopy || op.op == NOP_LOADK || op.op == NOP_VAR_CLEAR)
        {
            const StoragePolicy policy = static_cast<StoragePolicy>(op.argFlag & NEOS_OP_SUB_MASK);
            if (static_cast<unsigned>(policy) > static_cast<unsigned>(StoragePolicy::Callable) && policy != StoragePolicy::Boxed)
                return reject("Invalid storage policy");
            auto SlotPolicyOf = [&](short slot, bool local) -> StoragePolicy
            {
                if (!local) return slot >= 0 ? PolicyOfType(staticValues[slot < header._iStaticVarCount ? slot : 0]._type)
                    : PolicyOf(FindSlotKind(globalStorage, slot));
                if (slot >= 0 && slot < fun._localAddCount) return fun.SlotPolicy(slot);
                return StoragePolicy::Boxed; // 창 슬롯: 호출 계획이 정한다.
            };
            if (policy != StoragePolicy::Boxed)
            {
                const StoragePolicy destination = SlotPolicyOf(op.n1, (op.argFlag & L1) != 0);
                if (destination != StoragePolicy::Boxed && destination != policy) return reject("Invalid storage policy destination");
                if (fixedCopy)
                {
                    const StoragePolicy source = SlotPolicyOf(op.n2, (op.argFlag & L2) != 0);
                    if (source != StoragePolicy::Boxed && source != policy) return reject("Invalid storage policy source");
                }
            }
        }
        switch (checkedCode)
        {
        case NOP_PREPARE_CALL:
            if (op.argFlag || op.n1 < 0 || static_cast<std::size_t>(op.n1) >= fun._calls.size() || op.n23)
                return reject("Invalid call preparation");
            break;
        case NOP_MOV: slots = immediateMove ? 1 : 3; writes = 1; break;
        case NOP_LOADK:
            slots = writes = 1;
            if (op.n23 < 0 || op.n23 >= header._iStaticVarCount) return reject("Invalid constant operand");
            break;
        case NOP_LOG_NOT:
            slots = 3; writes = 1; break;
        case NOP_VAR_CLEAR: slots = writes = 1; break;
        case OP_EQUAL2:
        case OP_LESS:
        case OP_LESS_EQ:
        case OP_NEQUAL:
        case OP_GREAT:
        case OP_GREAT_EQ: slots = 7; writes = 1; break;
        case NOP_JMP: jump = terminal = true; break;
        case NOP_JMP_FALSE:
        case NOP_JMP_TRUE: slots = 2; jump = true; break;
        case OP_JMP_EQUAL2:
        case OP_JMP_LESS:
        case OP_JMP_LESS_EQ:
        case OP_JMP_NEQUAL:
        case OP_JMP_GREAT:
        case OP_JMP_GREAT_EQ: slots = 6; jump = true; break;
        case NOP_JMP_FOR:
            slots = writes = 2; jump = true;
            if (!(op.argFlag & L2) || op.n2 < 0 || op.n2 > fun._localAddCount - 4)
                return reject("Invalid for frame span");
            break;
        case NOP_JMP_FOREACH:
            slots = 6; writes = 4; jump = true;
            if (op.argFlag & L3) {
                if (op.n3 < 0 || op.n3 > fun._localAddCount - 4) return reject("Invalid foreach frame span");
            } else if (!IsGlobalVariable(op.n3) || !IsGlobalVariable((int)op.n3 + 3))
                return reject("Invalid foreach global span");
            break;
        case NOP_SWITCH: slots = 2; terminal = true; break;
        case NOP_STR_ADD: slots = 7; writes = 1; break;
        case NOP_TOSTRING:
        case NOP_TOINT:
        case NOP_TOFLOAT:
        case NOP_TOSIZE: slots = 3; writes = 1; break;
        case NOP_SLEEP: slots = 2; break;
        case NOP_FMOV1: slots = writes = 1; break;
        case NOP_FMOV2: slots = 3; writes = 1; break;
        case NOP_CALL:
            slots = writes = noResult ? 0 : 4;
            argumentCount = op.n2;
            if (op.n1 < 0 || op.n1 >= (int)functions.size() || argumentCount != functions[op.n1]._argsCount)
                return reject("Invalid call target or argument count");
            break;
        case NOP_PTRCALL:
            slots = ((op.argFlag & L2) || op.n2 != -1) ? 3 : 1;
            argumentCount = op.n3;
            break;
        case NOP_PTRCALL2:
            slots = noResult ? 1 : 5; writes = noResult ? 0 : 4; argumentCount = op.n2; break;
        case NOP_RETURN:
            if (op.n2 != owners[i]) return reject("Invalid return frame layout");
            slots = noResult ? 0 : 1; terminal = true; break;
        case NOP_TABLE_ALLOC: slots = writes = 1; break;
        case NOP_CLT_READ: slots = 7; writes = 4; break;
        case NOP_CLT_MOV: slots = 7; writes = 1; break;
        case NOP_LIST_ALLOC: slots = writes = 1; break;
        case NOP_CHANGE_INT: slots = writes = 1; break;
        case NOP_YIELD: break;
        case NOP_JMP_RANGE_INSIDE:
        case NOP_JMP_RANGE_OUTSIDE: slots = 2; jump = true; break;
        case NOP_CLT_WRITE_REF: slots = 7; writes = 5; break;
        case NOP_NONE: break;
        case NOP_ERROR: terminal = true; break;
        default:
        {
            const TypedOpInfo* typed = GetTypedOpInfo(op.Code());
            const AggregateOpInfo* aggregate = GetAggregateOpInfo(op.Code());
            if (typed == nullptr && aggregate == nullptr) return reject("Invalid saved instruction opcode");
            slots = typed ? (IsArithmeticUpdate(op) ? 3 : typed->slots) : aggregate->Slots();
            writes = typed ? typed->writes : aggregate->shape == AggregateOpShape::Store ? 0 : 1;
            jump = typed && typed->jump;
            const u8 allowedFlags = static_cast<u8>(((slots & 1) ? L1 : 0) | ((slots & 2) ? L2 : 0) | ((slots & 4) ? L3 : 0)
                | NEOS_OP_SUB_MASK | (typed && typed->comparison ? NEOS_OP_COMPARE_INVERT : 0)
                | (SupportsArithmeticUpdate(op.Code()) ? NEOS_OP_ARITH_UPDATE : 0));
            if (op.argFlag & ~allowedFlags) return reject("Invalid typed instruction flags");
            if (IsArithmeticUpdate(op) && op.n3 != 0) return reject("Invalid in-place arithmetic operand");
            if (aggregate && aggregate->Slots() != 7)
            {
                if (op.n3 < 0 || op.n3 > aggregate->controlMax) return reject("Invalid typed aggregate control");
                if (aggregate->shape == AggregateOpShape::Window &&
                    (!(op.argFlag & L2) || op.n2 < 0 ||
                        static_cast<int64_t>(op.n2) + aggregate->arity > fun._stackSlotCount))
                    return reject("Invalid typed aggregate argument window");
            }
            break;
        }
        }
        if ((op.Code() == NOP_CALL || op.Code() == NOP_PTRCALL || op.Code() == NOP_PTRCALL2) && argumentCount < 0)
            return reject("Invalid call argument count");
        if ((op.Code() == NOP_TABLE_ALLOC || op.Code() == NOP_LIST_ALLOC) && op.n23 < 0)
            return reject("Invalid collection allocation count");
        const short values[] = {op.n1, op.n2, op.n3};
        const u8 flags[] = {L1, L2, L3};
        for (unsigned n = 0; n < 3; ++n)
        {
            if (!(slots & (1u << n))) continue;
            const int value = values[n];
            if (op.argFlag & flags[n]) {
                if (value < 0 || value >= fun._stackSlotCount) return reject("Invalid local operand");
            } else if ((writes & (1u << n)) ? !IsGlobalVariable(value) : !IsGlobalOperand(value))
                return reject("Invalid global operand");
        }
        // 고정 숫자 슬롯에 다른 타입을 쓰는 이미지는 실행 전에 거부한다.
        if (fixedCopy || checkedCode == NOP_LOADK)
        {
            if (fixedCopy && (op.argFlag & NEOS_OP_MODIFIER) && !PolicyOwns(static_cast<StoragePolicy>(op.argFlag & NEOS_OP_SUB_MASK)))
                return reject("Invalid ownership transfer copy");
            const SlotKind target = FindSlotKind((op.argFlag & L1) ? fun._storage : globalStorage, op.n1);
            if (target >= SlotKind::Int && target <= SlotKind::Bool)
            {
                const int source = checkedCode == NOP_LOADK ? op.n23 : op.n2;
                const bool local = checkedCode == NOP_MOV && (op.argFlag & L2);
                if (!local && source >= 0)
                {
                    const VAR_TYPE type = staticValues[source]._type;
                    if ((target == SlotKind::Int && type != VAR_INT) ||
                        (target == SlotKind::Float && type != VAR_FLOAT) ||
                        (target == SlotKind::Bool && type != VAR_BOOL))
                        return reject("Incompatible fixed storage write");
                }
                else
                {
                    const SlotKind from = FindSlotKind(local ? fun._storage : globalStorage, static_cast<short>(source));
                    if (from != SlotKind::Boxed && from != target)
                        return reject("Incompatible fixed storage copy");
                }
            }
        }
        auto validJump = [&](int offset) {
            const int64_t target = (int64_t)i + 1 + offset;
            if (target < 2 || target >= (int64_t)code.size() || owners[(size_t)target] != owners[i]) return false;
            const auto targetOp = code[(size_t)target].Code();
            const bool writeTail = targetOp == NOP_CLT_MOV;
            return !(writeTail && (code[(size_t)target].argFlag & NEOS_OP_REF_WRITE));
        };
        if (jump && !validJump(op.n1)) return reject("Invalid branch target");
        if (op.Code() == NOP_SWITCH)
        {
            const size_t tableIndex = (u16)op.n1;
            if (tableIndex >= switchTables.size()) return reject("Invalid switch table reference");
            const auto& table = switchTables[tableIndex];
            if (!validJump(table.defaultOffset)) return reject("Invalid switch default target");
            for (const auto& entry : table.entries)
                if (!validJump(entry.jumpOffset)) return reject("Invalid switch case target");
        }
        if (!terminal && (i + 1 >= code.size() || owners[i + 1] != owners[i]))
            return reject("Instruction falls through function boundary");
    }
    return true;
}

void CNeoVMProgram::PatchNativeCalls()
{
	const int staticCount = (int)staticValues.size();
	const int opCount = (int)code.size();
	for (int i = 0; i < opCount; ++i)
	{
		SVMOperation& op = code[i];
		if (op.Code() != NOP_PTRCALL2)
			continue;

		if (op.argFlag & NEOS_ARG_N1_LOCAL)
			continue;

		// n1 은 전역 슬롯 인덱스. native 이름 상수는 반드시 static 구간에 있다.
		if (op.n1 < 0 || op.n1 >= staticCount)
			continue;

		const SStaticConst& sc = staticValues[op.n1];
		if (sc._type != VAR_STRING)
			continue;

		int nativeIndex = CNeoVM::FindDefaultNativeIndex(sc._str);
		if (nativeIndex < 0 || nativeIndex > SHRT_MAX)
			continue;

		// intrinsic 이 지정된 native(벡터 생성 등)는 전용 opcode 로 패치 (n2=인자수, n3=결과 유지).
		OperationCode intrinsic = (OperationCode)CNeoVM::GetDefaultNativeIntrinsic(nativeIndex);
		if (intrinsic != NOP_NONE)
		{
			op.SetCode(intrinsic);
			continue;
		}

		op.SetCode(NOP_NATIVECALL);
		op.n1 = (short)nativeIndex;
	}
}

void CNeoVMProgram::PatchStaticStringReads()
{
	const int staticCount = (int)staticValues.size();
	const int opCount = (int)code.size();
	for (int i = 0; i < opCount; ++i)
	{
		SVMOperation& op = code[i];
		if (op.Code() != NOP_CLT_READ)
			continue;

		// READ의 n2가 static 영역의 문자열 상수여야 한다. local/global 변수와
		// immediate key는 값이 바뀔 수 있으므로 범용 경로를 유지한다.
		if ((op.argFlag & (NEOS_ARG_N2_LOCAL)) != 0)
			continue;
		if (op.n2 < 0 || op.n2 >= staticCount)
			continue;
		if (staticValues[op.n2]._type != VAR_STRING)
			continue;

		op.SetCode(NOP_CLT_READ_STATIC_STRING);
	}
}

// 오퍼랜드를 전부 로컬 스택에서 뽑는 op 를 _L 변형으로 바꾼다.
// 판정 기준은 "VM 이 실제로 GetVarRaw* 로 페치하는 슬롯"이며, 점프 offset / 인자 수 /
// 함수 인덱스 / 즉값처럼 변수 참조가 아닌 필드는 검사 대상이 아니다.
// NOP_JMP(페치 0개)와 NOP_JMP_FOR(이미 GetVarRaw_L 고정)은 _L 이 원본과 동일해 치환하지 않는다.
bool CNeoVMProgram::PatchLocalOps(std::string* err)
{
	const u8 L1 = NEOS_ARG_N1_LOCAL, L2 = NEOS_ARG_N2_LOCAL, L3 = NEOS_ARG_N3_LOCAL;
	const int opCount = (int)code.size();
	int patched = 0;
	// 범위 점프의 타입 선택은 명령이 속한 함수의 배치표를 본다. 함수 시작 위치로 소유 함수를 매긴다.
	std::vector<int> owners(code.size(), -1);
	{
		std::vector<std::pair<int, int> > starts;
		for (size_t i = 0; i < functions.size(); ++i)
			starts.push_back(std::make_pair(functions[i]._codePtr / (int)sizeof(SVMOperation), (int)i));
		std::sort(starts.begin(), starts.end());
		for (size_t i = 0; i < starts.size(); ++i)
		{
			const int begin = starts[i].first;
			const int end = (i + 1 < starts.size()) ? starts[i + 1].first : opCount;
			for (int opIndex = begin; opIndex < end && opIndex >= 0; ++opIndex) owners[opIndex] = starts[i].second;
		}
	}
	auto SlotTypeOf = [&](int owner, short slot, bool local) -> VAR_TYPE
	{
		if (local)
		{
			if (owner < 0 || slot < 0 || slot >= (short)functions[owner]._slotTypes.size()) return VAR_NONE;
			return functions[owner]._slotTypes[slot];
		}
		const int index = ConstantBase() + slot;
		if (index < 0 || index >= (int)globalTypes.size()) return VAR_NONE;
		return globalTypes[index];
	};
	for (int i = 0; i < opCount; ++i)
	{
		SVMOperation& op = code[i];
		const u8 f = op.argFlag;
		const bool noResult = (f & NEOS_OP_CALL_NORESULT) != 0;
		OperationCode to = NOP_NONE;

		switch (op.Code())
		{

		case OP_JMP_EQUAL2:   if ((f & (L2|L3)) == (L2|L3)) to = OP_JMP_EQUAL2_L;   break;
		case NOP_STR_ADD:      if ((f & (L1|L2|L3)) == (L1|L2|L3)) to = NOP_STR_ADD_L; break;

		case NOP_TOINT:        if ((f & (L1|L2)) == (L1|L2)) to = NOP_TOINT_L;      break;

		case NOP_CALL:         if (!noResult && (f & L3)) to = NOP_CALL_L;           break;
		case NOP_PTRCALL:      if ((f & (L1|L2)) == (L1|L2)) to = NOP_PTRCALL_L;     break;
		case NOP_PTRCALL2:
			if ((f & L1) && (noResult || (f & L3))) to = NOP_PTRCALL2_L;
			break;
		case NOP_RETURN:       if (!noResult && (f & L1)) to = NOP_RETURN_L;         break;
		case NOP_CLT_READ:     if ((f & (L1|L2|L3)) == (L1|L2|L3)) to = NOP_CLT_READ_L;  break;
		case NOP_CLT_MOV:      if ((f & (L1|L2|L3)) == (L1|L2|L3)) to = NOP_CLT_MOV_L;   break;

		case NOP_LIST_ALLOC:   if (f & L1) to = NOP_LIST_ALLOC_L;                   break;
		case NOP_CHANGE_INT:   if (f & L1) to = NOP_CHANGE_INT_L;                   break;

		case NOP_JMP_RANGE_INSIDE:
		case NOP_JMP_RANGE_OUTSIDE:
		{
			if ((size_t)(u16)op.n3 >= rangeJumps.size()) break;
			const ProgramRangeJump& range = rangeJumps[(u16)op.n3];
			const bool inside = op.Code() == NOP_JMP_RANGE_INSIDE;
			// 검사값과 두 경계가 모두 int이거나 모두 float이면 뷰와 타입 검사 없이 payload만 비교하는
			// 런타임 opcode로 바꾼다. 섞인 타입은 기존 범용 경로에 남긴다.
			const VAR_TYPE test = SlotTypeOf(owners[i], op.n2, (f & L2) != 0);
			const VAR_TYPE lower = SlotTypeOf(owners[i], range.lower, (range.flags & NEOS_RANGE_LOWER_LOCAL) != 0);
			const VAR_TYPE upper = SlotTypeOf(owners[i], range.upper, (range.flags & NEOS_RANGE_UPPER_LOCAL) != 0);
			if (test == VAR_INT && lower == VAR_INT && upper == VAR_INT)
			{
				op.op = inside ? NOP_RT_JMP_I64_RANGE_INSIDE : NOP_RT_JMP_I64_RANGE_OUTSIDE;
				++patched;
				continue;
			}
			if (test == VAR_FLOAT && lower == VAR_FLOAT && upper == VAR_FLOAT)
			{
				op.op = inside ? NOP_RT_JMP_F64_RANGE_INSIDE : NOP_RT_JMP_F64_RANGE_OUTSIDE;
				++patched;
				continue;
			}
			if ((f & L2) && (range.flags & (NEOS_RANGE_LOWER_LOCAL | NEOS_RANGE_UPPER_LOCAL)) ==
				(NEOS_RANGE_LOWER_LOCAL | NEOS_RANGE_UPPER_LOCAL))
				to = inside ? NOP_JMP_RANGE_INSIDE_L : NOP_JMP_RANGE_OUTSIDE_L;
			break;
		}
		case OP_JMP_LESS:     if ((f & (L2|L3)) == (L2|L3)) to = OP_JMP_LESS_L;     break;
		case OP_JMP_LESS_EQ:  if ((f & (L2|L3)) == (L2|L3)) to = OP_JMP_LESS_EQ_L;  break;
		case OP_JMP_NEQUAL:   if ((f & (L2|L3)) == (L2|L3)) to = OP_JMP_NEQUAL_L;   break;

		case OP_JMP_GREAT:    if ((f & (L2|L3)) == (L2|L3)) to = OP_JMP_GREAT_L;    break;
		case OP_JMP_GREAT_EQ: if ((f & (L2|L3)) == (L2|L3)) to = OP_JMP_GREAT_EQ_L; break;
		default:
			break;
		}

		if (to != NOP_NONE)
		{
			op.SetCode(to);
			++patched;
			continue;
		}
		// 숫자 계열은 (기본 opcode, 세부 연산, 제자리 여부)를 한 바이트 런타임 opcode로 펼친다.
		// 디스패치 루프가 switch 한 번으로 고르고, 표기는 NormalizeRuntimeOp이 되돌린다.
		const eNOperation runtime = RuntimeOpcode(ArithmeticMeaning(op.Code()), IsArithmeticUpdate(op));
		if (runtime != NOP_NONE) op.op = runtime;
	}
	localOpCount = patched;
	return true;
}

CNeoVMProgram* NeoVMSystem::CreateProgram(const void* pBuffer, int iSize, std::string* err)
{
	return CNeoVMProgram::Create(pBuffer, iSize, err);
}

// ---------------------------------------------------------------------------
// 어셈블리 표기 (컴파일 리스팅 / 디버거 어셈블리 뷰 공용)
//
// opcode 별 표기는 이 파일의 FormatDebugOperation 한 곳에만 있다. 심볼(오퍼랜드 이름,
// 함수 이름, 점프 라벨)은 컴파일 타임과 런타임이 서로 다른 표에서 오므로 resolver 로 받는다.
// 예전엔 이 switch 가 NeoExport.cpp 안에 있었고 std::cout 으로 직접 찍었다.
// ---------------------------------------------------------------------------

// 로드 후 PatchLocalOps 가 만든 _L 변형을 원래 opcode 로 되돌린다.
// _L 은 "오퍼랜드가 전부 로컬"이라는 실행 최적화 표시일 뿐 의미가 같으므로, 표기도 같아야
// 한다. 이 정규화가 없으면 런타임 코드의 상당수가 default 로 빠져 빈 줄이 된다
// (치환 대상이 MOV/ADD/CALL/RETURN 같은 최빈 op 이다 — PatchLocalOps 참조).
static OperationCode NormalizeLocalOp(OperationCode op, bool* outIsLocal)
{
	OperationCode base = op;
	switch (op)
	{
	case OP_JMP_EQUAL2_L:   base = OP_JMP_EQUAL2;   break;
	case NOP_STR_ADD_L:      base = NOP_STR_ADD;      break;
	case NOP_TOINT_L:        base = NOP_TOINT;        break;
	case NOP_CALL_L:         base = NOP_CALL;         break;
	case NOP_PTRCALL_L:      base = NOP_PTRCALL;      break;
	case NOP_PTRCALL2_L:     base = NOP_PTRCALL2;     break;
	case NOP_RETURN_L:       base = NOP_RETURN;       break;
	case NOP_CLT_READ_L:     base = NOP_CLT_READ;     break;
	case NOP_CLT_MOV_L:      base = NOP_CLT_MOV;      break;
	case NOP_LIST_ALLOC_L:   base = NOP_LIST_ALLOC;   break;
	case NOP_CHANGE_INT_L:   base = NOP_CHANGE_INT;   break;
	case NOP_JMP_RANGE_INSIDE_L:  base = NOP_JMP_RANGE_INSIDE;  break;
	case NOP_JMP_RANGE_OUTSIDE_L: base = NOP_JMP_RANGE_OUTSIDE; break;
	case OP_JMP_LESS_L:     base = OP_JMP_LESS;     break;
	case OP_JMP_LESS_EQ_L:  base = OP_JMP_LESS_EQ;  break;
	case OP_JMP_NEQUAL_L:   base = OP_JMP_NEQUAL;   break;
	case OP_JMP_GREAT_L:    base = OP_JMP_GREAT;    break;
	case OP_JMP_GREAT_EQ_L: base = OP_JMP_GREAT_EQ; break;
	default: break;
	}
	if (outIsLocal != nullptr)
		*outIsLocal = (base != op);
	return base;
}

bool FormatDebugOperation(const DebugInstructionFormatContext& context, std::string& outAssembly,
	int* outByteCount)
{
	outAssembly.clear();
	if (outByteCount != nullptr)
		*outByteCount = 0;
	if (context.operation == nullptr)
		return false;

	// 원본 switch 가 v 를 쓰고 있어 이름을 유지한다(치환 실수를 줄인다).
	SVMOperation v = *context.operation;
	bool isLocalVariant = false;
	// 로드가 펼친 런타임 opcode는 이미지 형식으로 되돌려 같은 표기를 쓴다.
	NormalizeRuntimeOp(v);
	v.SetCode(NormalizeLocalOp((OperationCode)v.Code(), &isLocalVariant));
	const bool fixedStore = IsFixedArithmetic(v);
	// MOV·LOADK·CLR은 하위 4비트에 정책이나 즉값 종류를 싣는다. 표기 뒤에 정책 이름을 붙인다.
	const unsigned storageSub = v.argFlag & NEOS_OP_SUB_MASK;
	const bool storageOp = v.op == NOP_MOV || v.op == NOP_LOADK || v.op == NOP_VAR_CLEAR;
	const bool immediateMove = IsImmediateMove(v);
	if (storageOp) v.SetCode(static_cast<OperationCode>(v.op));

	// 원본 WriteFunLog 의 지역 상수 그대로.
	const int OpFlagByteChars = 2;
	int byteCount = 0;
	bool known = true;

	switch (v.Code())
	{
	case NOP_MOV:
		if (storageSub == MOV_SUB_IMMEDIATE_INT)
		{
			byteCount = OpFlagByteChars + 2 * 3;
			outAssembly = FormatAsm("MOVI %s = %d", context.operand(1).c_str(), v.n23);
		}
		else if (storageSub == MOV_SUB_IMMEDIATE_FLOAT)
		{
			byteCount = OpFlagByteChars + 2 * 3;
			float value;
			std::memcpy(&value, &v.n23, sizeof(value));
			outAssembly = FormatAsm("MOVF %s = %.17g", context.operand(1).c_str(), static_cast<double>(value));
		}
		else
		{
			byteCount = OpFlagByteChars + 2 * 2;
			outAssembly = FormatAsm("MOV  %s = %s%s", context.operand(1).c_str(), context.operand(2).c_str(),
				(v.argFlag & NEOS_OP_MODIFIER) ? " (take)" : "");
		}
		break;
	case NOP_LOADK:
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("LOADK %s = K[%d]", context.operand(1).c_str(), v.n23);
		break;
	case NOP_LOG_NOT:
		byteCount = OpFlagByteChars + 2 * 2;
		outAssembly = FormatAsm("NOT  %s = !%s", context.operand(1).c_str(), context.operand(2).c_str());
		break;
	case NOP_VAR_CLEAR:
		byteCount = OpFlagByteChars + 2 * 1;
		outAssembly = FormatAsm("CLR  %s", context.operand(1).c_str());
		break;
	case OP_EQUAL2:	// ==
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("EQ   %s = %s == %s", context.operand(1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
		break;
	case NOP_JMP:
		byteCount = OpFlagByteChars + 2 * 1;
		outAssembly = FormatAsm("JMP  %d%s", v.n1, context.jumpLabel(context.opIndex + 1 + v.n1).c_str());
		break;
	case NOP_JMP_FALSE:
		byteCount = OpFlagByteChars + 2 * 2;
		outAssembly = FormatAsm("JF   %d%s %s is False", v.n1, context.jumpLabel(context.opIndex + 1 + v.n1).c_str(), context.operand(2).c_str());
		break;
	case NOP_JMP_TRUE:
		byteCount = OpFlagByteChars + 2 * 2;
		outAssembly = FormatAsm("JT   %d%s %s is True", v.n1, context.jumpLabel(context.opIndex + 1 + v.n1).c_str(), context.operand(2).c_str());
		break;
	case OP_JMP_EQUAL2:	// ==
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("JEQ  %d%s,  %s == %s", v.n1, context.jumpLabel(context.opIndex + 1 + v.n1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
		break;
	case NOP_JMP_FOR:	// for
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("JFOR %d%s,  C[S.%d] < E[S.%d]", v.n1, context.jumpLabel(context.opIndex + 1 + v.n1).c_str(), v.n2, v.n3 + 1);
		break;
	case NOP_JMP_FOREACH:	// foreach
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("JFRE %d%s,  T%s, K[S.%d], V[S.%d]", v.n1, context.jumpLabel(context.opIndex + 1 + v.n1).c_str(), context.operand(2).c_str(), v.n3, v.n3+1);
		break;
	case NOP_SWITCH:
		byteCount = OpFlagByteChars + 2 * 2;
		outAssembly = FormatAsm("SWTC table[%u], key %s", (unsigned)(u16)v.n1, context.operand(2).c_str());
		break;
	case NOP_STR_ADD:	// ..
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("SADD %s = ToStr%s + ToStr%s", context.operand(1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
		break;
	case NOP_TOSTRING:
		byteCount = OpFlagByteChars + 2 * 2;
		outAssembly = FormatAsm("ToStr %s = %s", context.operand(1).c_str(), context.operand(2).c_str());
		break;
	case NOP_TOINT:
		byteCount = OpFlagByteChars + 2 * 2;
		outAssembly = FormatAsm("ToInt %s = %s", context.operand(1).c_str(), context.operand(2).c_str());
		break;
	case NOP_TOFLOAT:
		byteCount = OpFlagByteChars + 2 * 2;
		outAssembly = FormatAsm("ToFlo %s = %s", context.operand(1).c_str(), context.operand(2).c_str());
		break;
	case NOP_TOSIZE:
		byteCount = OpFlagByteChars + 2 * 2;
		outAssembly = FormatAsm("ToSize %s = %s", context.operand(1).c_str(), context.operand(2).c_str());
		break;
	case NOP_SLEEP:
		byteCount = OpFlagByteChars + 2 * 2;
		outAssembly = FormatAsm("Sleep %s", context.operand(2).c_str());
		break;
	case NOP_FMOV1:
		byteCount = OpFlagByteChars + 2 * 2;
		outAssembly = FormatAsm("MOV  %s = &%s", context.operand(1).c_str(), context.functionShort(v.n2).c_str());
		break;
	case NOP_FMOV2:
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("MOV  %s.%s = &%s", context.operand(1).c_str(), context.operand(2).c_str(), context.functionShort(v.n3).c_str());
		break;
	case NOP_CALL:
		if(v.argFlag & NEOS_OP_CALL_NORESULT)
		{
			byteCount = OpFlagByteChars + 2 * 2;
			outAssembly = FormatAsm("CALL %s arg:%d", context.functionFull(v.n1).c_str(), v.n2);
		}
		else
		{
			byteCount = OpFlagByteChars + 2 * 3;
			outAssembly = FormatAsm("CALL %s = %s arg:%d", context.operand(3).c_str(), context.functionFull(v.n1).c_str(), v.n2);
		}
		break;
	case NOP_PTRCALL:
		byteCount = OpFlagByteChars + 2 * 3;
		if(v.n2 != -1)
			outAssembly = FormatAsm("PCAL %s.%s arg:%d", context.operand(1).c_str(), context.operand(2).c_str(), v.n3);
		else
			outAssembly = FormatAsm("PCAL %s arg:%d", context.operand(1).c_str(), v.n3);
		break;
	case NOP_PTRCALL2:
		if (v.argFlag & NEOS_OP_CALL_NORESULT)
		{
			byteCount = OpFlagByteChars + 2 * 2;
			outAssembly = FormatAsm("PCA2 %s arg:%d", context.operand(1).c_str(), v.n2);
		}
		else
		{
			byteCount = OpFlagByteChars + 2 * 3;
			outAssembly = FormatAsm("PCA2 %s = %s arg:%d", context.operand(3).c_str(), context.operand(1).c_str(), v.n2);
		}
		break;
	case NOP_NATIVECALL: // 컴파일에 나오지 않고, LoadVM 에서 NOP_PTRCALL2 인경우 System 함수이름을 찾어서 함수 Index 화
		if (v.argFlag & NEOS_OP_CALL_NORESULT)
		{
			byteCount = OpFlagByteChars + 2 * 2;
			outAssembly = FormatAsm("NCAL native:%d arg:%d", v.n1, v.n2);
		}
		else
		{
			byteCount = OpFlagByteChars + 2 * 3;
			outAssembly = FormatAsm("NCAL %s = native:%d arg:%d", context.operand(3).c_str(), v.n1, v.n2);
		}
		break;
	case NOP_RETURN:
		// NORESULT = 값 없는 'return;'. n1(반환값 슬롯)을 쓰지 않으므로 op/flag 2바이트만 덤프한다.
		if (v.argFlag & NEOS_OP_CALL_NORESULT)
		{
			byteCount = OpFlagByteChars;
			outAssembly = FormatAsm("RET");
		}
		else
		{
			byteCount = OpFlagByteChars + 2 * 1;
			outAssembly = FormatAsm("RET  %s", context.operand(1).c_str());
		}
		break;
	//case NOP_FUNEND:
	//	outAssembly = FormatAsm("- End -");
	//	break;
	case NOP_TABLE_ALLOC:
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("Table Alloc %s, %d", context.operand(1).c_str(), v.n23);
		break;
	case NOP_CLT_READ:
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("READ %s = %s.%s", context.operand(3).c_str(), context.operand(1).c_str(), context.operand(2).c_str());
		break;
	case NOP_CLT_MOV:
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("MOV  %s.%s = %s", context.operand(1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
		break;
	case NOP_LIST_ALLOC:
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("List Alloc %s, %d", context.operand(1).c_str(), v.n23);
		break;
	case NOP_CHANGE_INT:
		byteCount = OpFlagByteChars + 2 * 1;
		outAssembly = FormatAsm("Change Int %s", context.operand(1).c_str());
		break;
	case NOP_YIELD:
		byteCount = OpFlagByteChars + 2 * 2;
		if(v.n1 == YILED_RETURN)
			outAssembly = FormatAsm("yield return");
		else
			{ outAssembly = FormatAsm("?? yield sub(%d)", v.n1); known = false; }
		break;
	case NOP_CLT_READ_STATIC_STRING:
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("READ.S %s = %s.%s", context.operand(3).c_str(), context.operand(1).c_str(), context.operand(2).c_str());
		break;
	case NOP_JMP_RANGE_INSIDE:
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("JRIN %d%s,  %s in range[%u]", v.n1,
			context.jumpLabel(context.opIndex + 1 + v.n1).c_str(), context.operand(2).c_str(), (unsigned)(u16)v.n3);
		break;
	case NOP_JMP_RANGE_OUTSIDE:
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("JROT %d%s,  %s outside range[%u]", v.n1,
			context.jumpLabel(context.opIndex + 1 + v.n1).c_str(), context.operand(2).c_str(), (unsigned)(u16)v.n3);
		break;
	case NOP_CLT_WRITE_REF:
        byteCount = OpFlagByteChars + 2 * 3;
        outAssembly = FormatAsm("WREF %s.%s -> %s", context.operand(1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
        break;
    case NOP_PREPARE_CALL:
        byteCount = OpFlagByteChars + 2;
        outAssembly = FormatAsm("PREPARE_CALL plan[%d]", v.n1); break;
    case NOP_NONE:
        // 정수 슬롯의 CHANGE_INT처럼 내보내기가 비운 명령. 실행은 건너뛴다.
        byteCount = OpFlagByteChars;
        outAssembly = FormatAsm("NONE"); break;
	case NOP_ERROR:
		outAssembly = FormatAsm("Begin");
		break;
	case OP_LESS:		// <
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("LS   %s = %s < %s", context.operand(1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
		break;
	case OP_JMP_LESS:		// <
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("%s %d%s,  %s < %s", (v.argFlag & NEOS_OP_COMPARE_INVERT) ? "JLS.NOT" : "JLS", v.n1, context.jumpLabel(context.opIndex + 1 + v.n1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
		break;
	case OP_LESS_EQ:	// <=
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("LE   %s = %s <= %s", context.operand(1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
		break;
	case OP_JMP_LESS_EQ:	// <=
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("%s %d%s,  %s <= %s", (v.argFlag & NEOS_OP_COMPARE_INVERT) ? "JLE.NOT" : "JLE", v.n1, context.jumpLabel(context.opIndex + 1 + v.n1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
		break;
	case OP_NEQUAL:	// !=
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("NE   %s = %s != %s", context.operand(1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
		break;
	case OP_JMP_NEQUAL:	// !=
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("JNE  %d%s,  %s != %s", v.n1, context.jumpLabel(context.opIndex + 1 + v.n1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
		break;
	case OP_GREAT:		// >
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("GR   %s = %s > %s", context.operand(1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
		break;
	case OP_JMP_GREAT:		// >
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("%s %d%s,  %s > %s", (v.argFlag & NEOS_OP_COMPARE_INVERT) ? "JGR.NOT" : "JGR", v.n1, context.jumpLabel(context.opIndex + 1 + v.n1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
		break;
	case OP_GREAT_EQ:	// >=
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("GE   %s = %s >= %s", context.operand(1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
		break;
	case OP_JMP_GREAT_EQ:	// >=
		byteCount = OpFlagByteChars + 2 * 3;
		outAssembly = FormatAsm("%s %d%s,  %s >= %s", (v.argFlag & NEOS_OP_COMPARE_INVERT) ? "JGE.NOT" : "JGE", v.n1, context.jumpLabel(context.opIndex + 1 + v.n1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
		break;
	default:
	{
		const TypedOpInfo* typed = GetTypedOpInfo(v.Code());
		const AggregateOpInfo* aggregate = GetAggregateOpInfo(v.Code());
		if (typed != nullptr)
		{
			byteCount = OpFlagByteChars + 2 * (typed->arity + 1);
			const char* invert = typed->comparison && (v.argFlag & NEOS_OP_COMPARE_INVERT) ? ".NOT" : "";
            if (IsArithmeticUpdate(v))
            {
                static const char* signs[] = { "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=" };
                const unsigned sub = v.argFlag & NEOS_OP_SUB_MASK;
                byteCount = OpFlagByteChars + 4;
                outAssembly = FormatAsm("%s2 %s %s %s", typed->name, context.operand(1).c_str(),
                    sub < sizeof(signs) / sizeof(signs[0]) ? signs[sub] : "?=", context.operand(2).c_str());
            }
            else if (typed->jump)
                outAssembly = FormatAsm("%s%s %d%s, %s, %s", typed->name, invert, v.n1,
					context.jumpLabel(context.opIndex + 1 + v.n1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
			else if (typed->arity == 2)
				outAssembly = FormatAsm("%s%s %s = %s, %s", typed->name, invert, context.operand(1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
			else outAssembly = FormatAsm("%s %s = %s", typed->name, context.operand(1).c_str(), context.operand(2).c_str());
		}
		else if (aggregate != nullptr)
		{
			byteCount = static_cast<int>(sizeof(SVMOperation));
			if (aggregate->shape == AggregateOpShape::Binary)
				outAssembly = FormatAsm("%s %s = %s, %s", aggregate->name, context.operand(1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
            else if (aggregate->shape == AggregateOpShape::Store)
                outAssembly = FormatAsm("%s %s[%s] = %s", aggregate->name, context.operand(1).c_str(), context.operand(2).c_str(), context.operand(3).c_str());
			else if (aggregate->shape == AggregateOpShape::Window)
				outAssembly = FormatAsm("%s %s = window(%s, %d), control(%d)", aggregate->name, context.operand(1).c_str(), context.operand(2).c_str(), aggregate->arity, v.n3);
			else if (aggregate->controlMax)
				outAssembly = FormatAsm("%s %s = %s[%d]", aggregate->name, context.operand(1).c_str(), context.operand(2).c_str(), v.n3);
			else
				outAssembly = FormatAsm("%s %s = %s", aggregate->name, context.operand(1).c_str(), context.operand(2).c_str());
		}
		else { outAssembly = FormatAsm("?? op(%d)", (int)v.Code()); known = false; }
		break;
	}
	}

	if (outByteCount != nullptr)
		*outByteCount = byteCount;
	// 로컬 전용 변형이었다는 표시. 같은 명령이지만 실행 경로가 다르다는 걸 볼 수 있게 한다.
	if (isLocalVariant)
		outAssembly += "  ;L";
	if (fixedStore) outAssembly += "  ;fixed";
	if (storageOp && !immediateMove)
	{
		outAssembly += "  ;";
		outAssembly += StoragePolicyName(static_cast<StoragePolicy>(storageSub));
	}
	return known;
}

// 컴파일 타임 GetValueString(NeoExport.cpp)의 런타임 짝. static 상수 하나를 표기한다.
// 컴파일 타임은 BoundaryValue, 런타임은 SStaticConst 라 타입이 달라 본문을 공유할 수 없다.
static std::string StaticConstToString(const SStaticConst& c)
{
	char ch[128] = { 0, };
	switch (c._type)
	{
	case VAR_INT:
		snprintf(ch, sizeof(ch), "%lld", (long long)c._int);
		break;
	case VAR_FLOAT:
#ifdef NS_SINGLE_PRECISION
		snprintf(ch, sizeof(ch), "%f", c._float);
#else
		snprintf(ch, sizeof(ch), "%lf", c._float);
#endif
		break;
	case VAR_BOOL:
		snprintf(ch, sizeof(ch), "'%s'", c._bl ? "true" : "false");
		break;
	case VAR_STRING:
	{
		// 제어문자는 컴파일 리스팅과 같은 방식으로 이스케이프한다.
		std::string r = "'";
		for (auto it = c._str.begin(); it != c._str.end(); ++it)
		{
			char x = *it;
			if (x == '\n')       r += "\\n";
			else if (x == '\r')  r += "\\r";
			else if (x == '\t')  r += "\\t";
			else                  r += x;
		}
		r += "'";
		return r;
	}
	default:
		snprintf(ch, sizeof(ch), "unknown type (%d)", (int)c._type);
		break;
	}
	return ch;
}

// 프로그램 이미지용 심볼 캐시. GetDebugInstructions 는 code 전체를 훑으므로
// 역맵과 함수 경계를 op 마다 다시 만들면 O(N*F) 가 된다. 한 번 만들어 재사용한다.
struct ProgramSymbolCache
{
	const CNeoVMProgram* program = nullptr;
	std::map<int, std::string> globalNames;       // 전역 슬롯 -> 이름
	std::vector<std::pair<int, int>> funStarts;   // (시작 op 인덱스, 함수 ID) 오름차순

	void Build(const CNeoVMProgram& p)
	{
		program = &p;
		globalNames = p.debugGlobalNames;
		// 디버그 정보가 없으면 export 된 것만이라도 이름을 붙인다.
		for (auto it = p.exportVariables.begin(); it != p.exportVariables.end(); ++it)
		{
			if (globalNames.find((*it).second) == globalNames.end())
				globalNames[(*it).second] = (*it).first;
		}
		funStarts.reserve(p.functions.size());
		for (int i = 0; i < (int)p.functions.size(); ++i)
			funStarts.push_back(std::make_pair(p.functions[i]._codePtr / (int)sizeof(SVMOperation), i));
		std::sort(funStarts.begin(), funStarts.end());
	}

	// op 가 속한 함수 ID. 프롤로그(NOP_ERROR/NOP_IDLE)는 어느 함수에도 안 속하므로 -1.
	int FunctionOf(int opIndex) const
	{
		int found = -1;
		for (size_t i = 0; i < funStarts.size(); ++i)
		{
			if (funStarts[i].first > opIndex)
				break;
			found = funStarts[i].second;
		}
		return found;
	}

	std::string FunctionName(int id) const
	{
		if (program == nullptr)
			return "";
		auto it = program->debugFunctionNames.find(id);
		if (it != program->debugFunctionNames.end())
			return (*it).second;
		for (auto e = program->exportFunctions.begin(); e != program->exportFunctions.end(); ++e)
		{
			if ((*e).second == id)
				return (*e).first;
		}
		char ch[64];
		snprintf(ch, sizeof(ch), "function#%d", id);
		return ch;
	}

	// 컴파일 타임 GetLog 와 같은 표기를 낸다.
	std::string Operand(const SVMOperation& op, int funId, int argIndex) const
	{
		if (program == nullptr)
			return "";
		int v = 0;
		bool isLocal = false;
		if (argIndex == 1)      { v = op.n1; isLocal = (op.argFlag & NEOS_ARG_N1_LOCAL) != 0; }
		else if (argIndex == 2) { v = op.n2; isLocal = (op.argFlag & NEOS_ARG_N2_LOCAL) != 0; }
		else if (argIndex == 3) { v = op.n3; isLocal = (op.argFlag & NEOS_ARG_N3_LOCAL) != 0; }

		char ch[256];
		if (isLocal)
		{
			// 로컬 슬롯 이름은 함수별 표에 있다. 임시/인자 슬롯은 이름이 없다.
			if (funId >= 0)
			{
				auto f = program->debugVarNames.find(funId);
				if (f != program->debugVarNames.end())
				{
					auto n = (*f).second.find(v);
					if (n != (*f).second.end())
					{
						snprintf(ch, sizeof(ch), "[S.%d %s]", v, (*n).second.c_str());
						return ch;
					}
				}
			}
			snprintf(ch, sizeof(ch), "[S.%d]", v);
			return ch;
		}

		// 전역 영역. 음수 오퍼랜드(-1 = 사용 안 함)가 이미지에 남을 수 있으므로 하한도 막는다.
		const int staticCount = (int)program->staticValues.size();
		if (v >= 0 && v < staticCount)
		{
			snprintf(ch, sizeof(ch), "[G.%d %s]", v, StaticConstToString(program->staticValues[v]).c_str());
			return ch;
		}
		if (program->IsGlobalVariable(v))
		{
			auto it = globalNames.find(v);
			if (it != globalNames.end())
			{
				snprintf(ch, sizeof(ch), "[G.%d %s]", v, (*it).second.c_str());
				return ch;
			}
		}
		snprintf(ch, sizeof(ch), "[G.%d ???]", v);
		return ch;
	}
};



void NeoVMSystem::ProgramAddRef(CNeoVMProgram* pProgram)
{
	if (pProgram != nullptr)
		pProgram->AddRef();
}

void NeoVMSystem::ProgramRelease(CNeoVMProgram* pProgram)
{
	if (pProgram != nullptr)
		pProgram->Release();
}

CNeoVMProgram* NeoVMSystem::CompileToProgram(const NeoCompilerParam& param)
{
	CNArchive ar;
	if (NeoVMSystem::Compile(ar, param) == false)
		return nullptr;

	// 옛 CompileAndLoadVM 과 동일: putASM(진단) 시 컴파일 성공 + 코드 용량 출력.
	// (ANSI 색상 리터럴 인라인 — NeoParser.h 의존 회피, "\x1b[32m"=green, "\x1b[0m"=reset)
	if (param.putASM)
		printf("\x1b[32m" "Compile Success. Code : %d bytes !!" "\x1b[0m" "\n", ar.GetBufferOffset());

	return CNeoVMProgram::Create(ar.GetData(), ar.GetBufferOffset(), param.err);
}



};
