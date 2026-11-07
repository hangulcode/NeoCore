#include <stdio.h>
#include <math.h>
#include "NeoParser.h"
#include "NeoCoreBuiltins.h"
#include "NeoTextLoader.h"
#include "NeoExport.h"
#include "NeoVMFusion.h"
#include "NeoVMProgram.h"   // DebugInstructionFormatContext / FormatDebugOperation

namespace NeoScript
{

void	SetCompileError(CArchiveRdWC& ar, const char*	lpszString, ...);

enum EExportCompileError
{
	ECE_INVALID_OPCODE,
	ECE_INVALID_YIELD_SUBTYPE,
	ECE_FUNCTION_COUNT_MISMATCH,
	ECE_INVALID_VARIABLE_TYPE,
	ECE_COUNT,
};

static const char* g_sExportCompileErrors[ECE_COUNT] =
{
	"Invalid opcode while writing bytecode (%d)",
	"Invalid yield subtype while disassembling bytecode (%d)",
	"Function count mismatch while writing bytecode",
	"Invalid variable type while writing bytecode (%d)",
};

u8 ChangeIndex(int staticCount, int localCount, int curFunStatkSize, SVMOperation& op, int argIndex)
{
	short* n = nullptr;
	if (argIndex == 1)		{ n = &op.n1; }
	else if (argIndex == 2) { n = &op.n2; }
	else if (argIndex == 3) { n = &op.n3; }

	if (*n == -1)
		return 0;

	if (*n == STACK_POS_RETURN)
	{
		*n = CompileShort("return slot", curFunStatkSize);
		if (argIndex == 1) return NEOS_ARG_N1_LOCAL;
		else if (argIndex == 2) return NEOS_ARG_N2_LOCAL;
		else if (argIndex == 3) return NEOS_ARG_N3_LOCAL;
	}
	if(*n <= COMPILE_GLOBAL_VAR_BEGIN)
	{
		// Negative globals are independent of the constant pool size.
		// -1 stays reserved; -2 is the first declared global.
		return 0;
	}

	if (*n >= COMPILE_LOCALTMP_VAR_BEGIN)
	{
		if (*n >= COMPILE_STATIC_VAR_BEGIN)
		{
			if (*n >= COMPILE_CALLARG_VAR_BEGIN)
			{
				*n = CompileShort("call argument stack slot", (*n - COMPILE_CALLARG_VAR_BEGIN) + curFunStatkSize);
				if (argIndex == 1) return NEOS_ARG_N1_LOCAL;
				else if (argIndex == 2) return NEOS_ARG_N2_LOCAL;
				else if (argIndex == 3) return NEOS_ARG_N3_LOCAL;
			}
			*n = (*n - COMPILE_STATIC_VAR_BEGIN);
			return 0;
		}
		*n = CompileShort("temporary stack slot", *n - COMPILE_LOCALTMP_VAR_BEGIN + localCount);
		if (argIndex == 1) return NEOS_ARG_N1_LOCAL;
		else if (argIndex == 2) return NEOS_ARG_N2_LOCAL;
		else if (argIndex == 3) return NEOS_ARG_N3_LOCAL;
	}
	if (argIndex == 1) return NEOS_ARG_N1_LOCAL;
	else if (argIndex == 2) return NEOS_ARG_N2_LOCAL;
	else if (argIndex == 3) return NEOS_ARG_N3_LOCAL;
	return 0;
}

static bool ChangeRangeOperand(int staticCount, int localCount, int curFunStatkSize, short& operand)
{
	SVMOperation temp;
	memset(&temp, 0, sizeof(temp));
	temp.n1 = operand;
	const bool isLocal = (ChangeIndex(staticCount, localCount, curFunStatkSize, temp, 1) & NEOS_ARG_N1_LOCAL) != 0;
	operand = temp.n1;
	return isLocal;
}
struct STempDebug
{
	std::vector<BoundaryValue>				_staticVars;
	std::vector<std::string>			_globalVars;
	std::vector< debug_info>			debugInfo;
};

std::string GetFunctionName(SFunctions& funs, short nID)
{
	auto it = funs._funIDs.find(nID);
	if(it == funs._funIDs.end())
		return "Error";

	return (*it).second->GetFullName();
}

#include "NeoStorageCompiler.inl"

void WriteFun(CArchiveRdWC& arText, CNArchive& ar, SFunctions& funs, SFunctionInfo& fi, SVars& vars, std::map<int, SFunctionTableForWriter>& funPos, std::vector< debug_info>& debugInfo)
{
	int iOPCount = 0;

	SFunctionTableForWriter fun;
	fun._codePtr = ar.GetBufferOffset() - sizeof(SNeoVMHeader);
	fun._argsCount = CompileShort("function arguments", (int)fi._args.size());
	fun._localTempMax = CompileShort("temporary variables", fi._localTempMax);
	fun._localVarCount = fi._localVarCount;
	fun._funType = fi._funType;
	fun._name = fi._name;
    fun._calls = fi._calls; fun._resultType = fi._returnStorage;
	fun._captures.clear();
	fun._captures.reserve(fi._captures.size());
	for (const SClosureCapture& capture : fi._captures)
		fun._captures.push_back(std::make_pair(capture._sourceSlot, capture._localSlot));

	int curFunStatkSize = 1 + fun._argsCount + fun._localVarCount + fun._localTempMax;
    int stackSlotCount = curFunStatkSize;
	CheckCompileRange("function stack slots", curFunStatkSize, 0, SHRT_MAX);
	for (const auto& entry : fi._storage)
	{
		SlotLayout slot;
		slot.slot = CompileShort("storage slot", entry.first >= COMPILE_LOCALTMP_VAR_BEGIN
			? entry.first - COMPILE_LOCALTMP_VAR_BEGIN + fi._localVarCount : entry.first);
		slot.typeId = entry.second.typeId;
		slot.kind = entry.second.kind;
		fun._storage.push_back(slot);
	}

	funPos[fi._funID] = fun;

	//if (fi._funType == FUNT_IMPORT)
	//	return;


	int staticCount = (int)funs._staticVars.size();
	int localCount = fi._localVarCount;

	CNArchive arRead((u8*)funs._codeFinal.GetData() + fi._iCode_Begin, fi._iCode_Size);
	arRead.SetPointer(0, SEEK_SET);

	int iNewCodeBegin = ar.GetBufferOffset();

	SVMOperation v;
	memset(&v, 0, sizeof(v));

	while (arRead.GetBufferOffset() < arRead.GetBufferSize())
	{
		if (arText._debug)
		{
			int off = (arRead.GetBufferOffset() + fi._iCode_Begin) / 8;
			debug_info di = (funs.m_sDebugFinal)[off];
			debugInfo.push_back(di);
		}
		arRead.Read(&v, sizeof(v));
		u8 argFlag = v.argFlag;
		const auto& storage = v.n1 <= COMPILE_GLOBAL_VAR_BEGIN ? vars._globalStorage : fi._storage;
		const auto destination = storage.find(v.n1);
		const SlotKind destinationKind = destination == storage.end() ? SlotKind::Boxed : destination->second.kind;

		iOPCount++;

		//if (arText._debug)
		//	ar << v.dbg;

		switch (v.Code())
		{

		case NOP_PREPARE_CALL: break;
		case NOP_MOV:
		case NOP_LOG_NOT:
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);

			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 2);
			break;

		case NOP_MOVI:
		case NOP_MOVF:
		case NOP_LOADK:
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);
			break;

		case NOP_VAR_CLEAR:
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);

			break;

		case OP_GREAT:		// >
		case OP_GREAT_EQ:	// >=
		case OP_LESS:		// <
		case OP_LESS_EQ:	// <=
		case OP_EQUAL2:	// ==
		case OP_NEQUAL:	// !=
		case NOP_STR_ADD: // ..
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 2);
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 3);

			break;

		case NOP_JMP:
			//ar << optype << v.n1;
			break;
		case NOP_JMP_FALSE:
		case NOP_JMP_TRUE:
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 2);
			break;

		case OP_JMP_GREAT:		// >
		case OP_JMP_GREAT_EQ:	// >=
		case OP_JMP_LESS:		// <
		case OP_JMP_LESS_EQ:	// <=
		case OP_JMP_EQUAL2:	// ==
		case OP_JMP_NEQUAL:	// !=
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 2);
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 3);

			break;
		case NOP_JMP_FOR:	// 
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 2);
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 3);

			break;
		case NOP_JMP_FOREACH:	//
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 2);
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 3);

			break;
		case NOP_SWITCH:	// n1 = switch table index (변환 금지), n2 = 조건식 위치
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 2);
			break;
		case NOP_TOSTRING:
		case NOP_TOINT:
		case NOP_TOFLOAT:
		case NOP_TOSIZE:
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 2);

			break;
		case NOP_SLEEP:
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 2);
			break;

		case NOP_FMOV1:
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);
			break;
		case NOP_FMOV2:
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);

			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 2);
			break;

		case NOP_CALL:
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 3);
			break;
		case NOP_PTRCALL:
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 2);
			break;
		case NOP_PTRCALL2:
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 3);
			break;
		case NOP_NATIVECALL: // 컴파일에 나오지 않고, LoadVM 에서 NOP_PTRCALL2 인경우 System 함수이름을 찾어서 함수 Index 화
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 3);
			break;
		case NOP_RETURN:
			v.n2 = CompileShort("return frame layout", fi._funID);
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);
			if((argFlag & NEOS_ARG_N1_LOCAL) && v.n1 == 0)
				argFlag |= NEOS_OP_CALL_NORESULT;
			break;
		//case NOP_FUNEND:
		//	//ar << optype;
		//	break;
		case NOP_TABLE_ALLOC:
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);
			//ar << optype << v.n1;
			break;
		case NOP_CLT_READ:
		case NOP_CLT_MOV:

				argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);

			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 2);

				argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 3);

			break;

		case NOP_LIST_ALLOC:
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);
			//ar << optype << v.n1;
			break;
/*		case NOP_LIST_MOV:
		case NOP_LIST_READ:
		case NOP_LIST_ADD2:
		case NOP_LIST_SUB2:
		case NOP_LIST_MUL2:
		case NOP_LIST_DIV2:
		case NOP_LIST_PERSENT2:
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 2);
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 3);

			break;*/
		case NOP_CHANGE_INT:
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);
			break;
		case NOP_YIELD:
			break;
		case NOP_JMP_RANGE_INSIDE:
		case NOP_JMP_RANGE_OUTSIDE:
		{
			// n3는 operand가 아니라 전역 range descriptor index다. 검사 값(n2)만
			// 일반 slot 변환을 하고, descriptor의 lower/upper는 여기서 같은 함수
			// 기준으로 따로 변환한다.
			const u16 rangeIndex = (u16)v.n3;
			if ((size_t)rangeIndex < funs._rangeJumps.size())
			{
				SRangeJumpCompile& range = funs._rangeJumps[rangeIndex];
				if (range._ownerFunction == fi._funID)
				{
					range._flags &= ~(NEOS_RANGE_LOWER_LOCAL | NEOS_RANGE_UPPER_LOCAL);
					if (ChangeRangeOperand(staticCount, localCount, curFunStatkSize, range._lower))
						range._flags |= NEOS_RANGE_LOWER_LOCAL;
					if (ChangeRangeOperand(staticCount, localCount, curFunStatkSize, range._upper))
						range._flags |= NEOS_RANGE_UPPER_LOCAL;
				}
			}
			argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 2);
			break;
		}
		case NOP_CLT_WRITE_REF:
            argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 1);
            argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 2);
            argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, 3);
            break;
		case NOP_ERROR:
			break;
		default:
		{
			const TypedOpInfo* typed = GetTypedOpInfo(v.Code());
			const AggregateOpInfo* aggregate = GetAggregateOpInfo(v.Code());
			if (typed != nullptr || aggregate != nullptr)
			{
				const unsigned slots = typed ? typed->slots : aggregate->Slots();
				for (unsigned n = 0; n < 3; ++n)
					if (slots & (1u << n))
						argFlag |= ChangeIndex(staticCount, localCount, curFunStatkSize, v, static_cast<int>(n + 1));
			}
			else
			{
				SetCompileError(arText, g_sExportCompileErrors[ECE_INVALID_OPCODE], v.Code());
			}
			break;
		}
		}

		v.argFlag = argFlag;
        const short locations[] = {v.n1, v.n2, v.n3};
        const u8 locationsFlags[] = {NEOS_ARG_N1_LOCAL, NEOS_ARG_N2_LOCAL, NEOS_ARG_N3_LOCAL};
        for (unsigned n = 0; n < 3; ++n)
            if (argFlag & locationsFlags[n]) stackSlotCount = (std::max)(stackSlotCount, static_cast<int>(locations[n]) + 1);
        int outgoing = -1;
        if (v.op == NOP_CALL || v.op == NOP_PTRCALL2 || v.op == NOP_VEC_MAKE) outgoing = v.n2;
        else if (v.op == NOP_PTRCALL) outgoing = v.n3;
        if (outgoing >= 0) stackSlotCount = (std::max)(stackSlotCount, curFunStatkSize + outgoing + 1);
        PackArithmeticUpdate(v);
		// 저장형은 이름이나 실행 중 태그가 아니라 프런트엔드 계약과 임시 슬롯 배치로 결정한다.
		if ((v.op == NOP_I64_ARITH && destinationKind == SlotKind::Int) ||
			(v.op == NOP_F64_ARITH && destinationKind == SlotKind::Float))
			v.op = v.op == NOP_I64_ARITH ? NOP_I64_FIXED : NOP_F64_FIXED;
		else if (v.op == NOP_MOV && destinationKind >= SlotKind::Int && destinationKind < SlotKind::Function)
			v.SetCode(MakeOperation(NOP_SLOT_COPY, static_cast<unsigned>(destinationKind)));
		else if ((v.op == NOP_MOVI && destinationKind == SlotKind::Int) || (v.op == NOP_MOVF && destinationKind == SlotKind::Float))
			v.argFlag |= NEOS_OP_MODIFIER;
		ar << v;
	}
    std::size_t typedSlot = 0;
    for (int i = 1; i < stackSlotCount; ++i)
    {
        SlotKind kind = SlotKind::Boxed;
        if (typedSlot < fun._storage.size() && fun._storage[typedSlot].slot == i)
            kind = fun._storage[typedSlot++].kind;
        if (kind == SlotKind::Int || kind == SlotKind::Float || kind == SlotKind::Bool ||
            kind == SlotKind::Vector2 || kind == SlotKind::ObjectId || kind == SlotKind::Iterator) continue;
        SlotCleanup cleanup;
        cleanup.slot = CompileShort("cleanup slot", i); cleanup.kind = kind;
        fun._cleanup.push_back(cleanup);
    }
	funPos[fi._funID] = std::move(fun);
	int iNewCodeEnd = ar.GetBufferOffset();
	fi._iCode_Begin = iNewCodeBegin;
	fi._iCode_Size = iNewCodeEnd - iNewCodeBegin;
}

static std::string ToPtringString(const std::string & str)
{
	std::string r;

	for (auto it = str.begin(); it != str.end(); it++)
	{
		char c = (*it);
		if (c == '\n')
			r += "\\n";
		else if (c == '\r')
			r += "\\r";
		else if (c == '\t')
			r += "\\t";
		else
			r += c;
	}
	return r;
}
#ifdef NS_SINGLE_PRECISION
#define FLOAT_FORMAT	"%f"
#else
#define FLOAT_FORMAT	"%lf"
#endif
std::string GetValueString(BoundaryValue& vi, std::map<int, std::string>* pFunTable = NULL)
{
	char ch[64] = { 0, };
	switch (vi.GetType())
	{
	case VAR_INT:
		snprintf(ch, _countof(ch), "%lld", (long long)vi._int);
		break;
	case VAR_FLOAT:
		snprintf(ch, _countof(ch), FLOAT_FORMAT, vi._float); // double ?
		break;
	case VAR_BOOL:
		snprintf(ch, _countof(ch), "'%s'", vi._bl ? "true" : "false");
		break;
	case VAR_FUN:
		//snprintf(ch, _countof(ch), "%s", mapFun[vi._fun_index].c_str());
		if(pFunTable == NULL)
			snprintf(ch, _countof(ch), "%d:fun", vi._fun_index);
		else
		{
			auto it = pFunTable->find(vi._fun_index);
			if(it != pFunTable->end())
				snprintf(ch, _countof(ch), "%d:'%s'", vi._fun_index, (*it).second.c_str());
			else
				snprintf(ch, _countof(ch), "%d:fun", vi._fun_index);
		}
		break;
	case VAR_STRING:
		snprintf(ch, _countof(ch), "'%s'", ToPtringString(vi._str->_str).c_str());
		break;
	default:
		snprintf(ch, _countof(ch), "unknown type (%d)", vi.GetType());
		break;
	}
	return ch;
}

std::string GetLog(STempDebug& td, SVMOperation& op, int argIndex)
{
	char ch[64] = {0,};
	int v = 0;
	const char* c = ""; // N.

	if (argIndex == 1) { v = op.n1; }
	if (argIndex == 2) { v = op.n2; }
	if (argIndex == 3) { v = op.n3; }

	if (argIndex == 1) { c = (op.argFlag & NEOS_ARG_N1_LOCAL) ? "S." : "G."; }
	if (argIndex == 2) { c = (op.argFlag & NEOS_ARG_N2_LOCAL) ? "S." : "G."; }
	if (argIndex == 3) { c = (op.argFlag & NEOS_ARG_N3_LOCAL) ? "S." : "G."; }

	if (c[0] == 'G')
	{
		if(v >= 0 && v < (int)td._staticVars.size())
			snprintf(ch, _countof(ch), "[%s%d %s]", c, v, GetValueString(td._staticVars[v]).c_str());
		else
		{
			if(v <= -2 && -v - 2 < (int)td._globalVars.size())
				snprintf(ch, _countof(ch), "[%s%d %s]", c, v, td._globalVars[-v - 2].c_str());
			else
				snprintf(ch, _countof(ch), "[%s%d ???]", c, v);
		}
	}
	else
	{
		snprintf(ch, _countof(ch), "[%s%d]", c, v);
	}
	return ch;
}

std::string GetFunName(SFunctions& funs, int id)
{
	SFunctionInfo* pFun = funs.FindFun(id);
	if(pFun == NULL)
	{
		char ch[128];
		snprintf(ch, _countof(ch), "fun(%d)'", id);
		return ch;
	}
	return pFun->_name;
}

// opIndex: 점프 대상의 op 인덱스 (= 현재 op 인덱스 + 1 + n1).
std::string JumpMark(std::map<int, int>& sJumpMark, int opIndex)
{
	std::string r;
	auto it = sJumpMark.find(opIndex);
	if (it == sJumpMark.end())
		return r;
	char ch[128];
	snprintf(ch, _countof(ch), " 'go_%d'", (*it).second);
	r = ch;
	return r;
}


void WriteFunLog(CArchiveRdWC& arText, CNArchive& arw, SFunctions& funs, SFunctionInfo& fi, SVars& vars, STempDebug& td, const std::vector<ProgramFusion>& fusions)
{
	OutAsm("\n");
	OutAsm(ANSI_COLOR_YELLOW "Fun - %s [T:%d ID:%d] arg:%d var:%d varTemp:%d" ANSI_RESET_ALL "\n", fi.GetFullName().c_str(), fi._funType, fi._funID, (int)fi._args.size(), fi._localVarCount, fi._localTempMax);

	//if (fi._funType == FUNT_IMPORT)
	//	return;

	//CNArchive arRead((u8*)fi._code->GetData() + fi._iCode_Begin, fi._iCode_Size);
    // ASM은 확장 슬롯의 실제 연산과 임시 저장 생략을 함께 보여준다.
    std::vector<SVMOperation> displayed(static_cast<std::size_t>(fi._iCode_Size) / sizeof(SVMOperation));
    std::memcpy(displayed.data(), static_cast<u8*>(arw.GetData()) + fi._iCode_Begin, fi._iCode_Size);
    std::map<int, const ProgramFusion*> roots;
    std::map<int, bool> inner;
    const int functionBegin = (fi._iCode_Begin - sizeof(SNeoVMHeader)) / sizeof(SVMOperation);
    for (const ProgramFusion& plan : fusions)
        if (plan.start >= functionBegin && plan.start + plan.count <= functionBegin + static_cast<int>(displayed.size()))
        {
            const int first = plan.start - functionBegin;
            roots[first] = &plan;
            for (unsigned i = 0; i < plan.count; ++i)
            {
                displayed[first + i] = plan.operations[i];
                inner[first + i] = (plan.elide & (1u << i)) != 0;
            }
        }
    CNArchive arRead(displayed.data(), fi._iCode_Size);
	arRead.SetPointer(0, SEEK_SET);

	std::map<int, int> sJumpMark;
	SVMOperation v;
	while (arRead.GetBufferOffset() < arRead.GetBufferSize())
	{
		int off = arRead.GetBufferOffset();
		arRead >> v;

		switch (v.Code())
		{
		case NOP_JMP:
		case NOP_JMP_FALSE:
		case NOP_JMP_TRUE:
		case OP_JMP_GREAT:		// >
		case OP_JMP_GREAT_EQ:	// >=
		case OP_JMP_LESS:		// <
		case OP_JMP_LESS_EQ:	// <=
		case OP_JMP_EQUAL2:	// ==
		case OP_JMP_NEQUAL:	// !=
		case NOP_JMP_FOR:	// for
		case NOP_JMP_FOREACH:	// foreach
		case NOP_JMP_RANGE_INSIDE:
		case NOP_JMP_RANGE_OUTSIDE:
			// op 인덱스로 기록 (다음 op = off/8 + 1, 거기에 op 단위 상대 offset n1)
			sJumpMark[off / (int)sizeof(SVMOperation) + 1 + v.n1] = 0;
			break;
		default:
		{
			const TypedOpInfo* typed = GetTypedOpInfo(v.Code());
			if (typed != nullptr && typed->jump)
				sJumpMark[off / (int)sizeof(SVMOperation) + 1 + v.n1] = 0;
			break;
		}
		}
	}
	int idx = 0;
	for (auto it = sJumpMark.begin(); it != sJumpMark.end(); it++)
	{
		(*it).second = idx++;
	}

	arRead.SetBufferOffset(0);

	while (arRead.GetBufferOffset() < arRead.GetBufferSize())
	{
		int off = arRead.GetBufferOffset();
		auto it = sJumpMark.find(off / (int)sizeof(SVMOperation));
		if (it != sJumpMark.end())
		{
			OutAsm("\t\t\t\t\tgo_%d:\n", (*it).second);
		}

		if (arText._debug)
		{
			int off = fi._iCode_Begin - sizeof(SNeoVMHeader) + arRead.GetBufferOffset();
			OutAsm("%4d %6d : ", off/8, td.debugInfo[off/8]._lineseq);
		}

		arRead >> v;

		const int skipByteChars = 12;

		// opcode 표기는 공용 포맷터 한 곳에만 있다(NeoVMProgram.cpp: FormatDebugOperation).
		// 여기서는 컴파일 타임 심볼 표를 resolver 로 공급하고 결과를 콘솔로 내보내기만 한다.
		DebugInstructionFormatContext context;
		context.operation = &v;
		context.opIndex = off / (int)sizeof(SVMOperation);
		context.operand = [&](int argIndex) { return GetLog(td, v, argIndex); };
		context.functionFull = [&](int id) { return GetFunctionName(funs, id); };
		context.functionShort = [&](int id) { return GetFunName(funs, id); };
		context.jumpLabel = [&](int target) { return JumpMark(sJumpMark, target); };

		std::string asmText;
		int byteCount = 0;
		if (false == FormatDebugOperation(context, asmText, &byteCount))
			SetCompileError(arText, g_sExportCompileErrors[ECE_INVALID_OPCODE], v.Code());

		// NOP_ERROR 등 바이트를 안 찍던 op 는 byteCount 가 0 이라 빈 칸으로 나온다(기존과 동일).
        const int position = off / sizeof(SVMOperation);
        const auto root = roots.find(position);
        if (root != roots.end()) OutAsm("FUSED (%u primitive operations, one VM dispatch)\n", root->second->count);
        const auto step = inner.find(position);
        if (step != inner.end()) OutAsm("  | ");
        OutBytes((const u8*)&v, byteCount, skipByteChars);
        OutAsm("%s%s\n", asmText.c_str(), step != inner.end() && step->second ? " [temporary forwarded]" : "");
	}
//	OutAsm("Fun End [%s]", fi._name.c_str());
}

static void WriteString(CNArchive& ar, const std::string& str)
{
	short nLen = (short)CheckCompileRange("image string bytes", (int64_t)str.size(), 0, SHRT_MAX);
	ar << nLen;
	ar.Write((char*)str.data(), nLen);
}

// switch/case 테이블 청크 ('SWIT'). debug 여부와 무관하게 저장한다.
static void WriteSwitchTables(CNArchive& ar, SFunctions& funs)
{
	if (funs._switchTables.empty())
		return;

	const u32 magic = 0x54495753; // SWIT
	ar << magic;
	ar.WriteCount((u32)funs._switchTables.size());
	for (size_t t = 0; t < funs._switchTables.size(); ++t)
	{
		const SSwitchTableCompile& tbl = funs._switchTables[t];
		ar.Write(&tbl._defaultOffset, sizeof(tbl._defaultOffset));
		ar.WriteCount((u32)tbl._cases.size());
		for (size_t c = 0; c < tbl._cases.size(); ++c)
		{
			const SSwitchCaseCompile& e = tbl._cases[c];
			u8 keyType = (u8)e._type;
			ar.Write(&keyType, sizeof(keyType));
			switch (e._type)
			{
			case VAR_INT:    ar.Write((void*)&e._int, sizeof(e._int)); break;
			case VAR_STRING: WriteString(ar, e._str); break;
			default: break;
			}
			ar.Write((void*)&e._jumpOffset, sizeof(e._jumpOffset));
		}
	}
}

// range jump descriptor chunk ('RNGE'). n3에는 unsigned short index만 들어가므로
// table 자체도 0~65535개로 제한된다(파서가 생성 시 이미 같은 한계를 검사한다).
static void WriteRangeJumps(CNArchive& ar, const SFunctions& funs)
{
	if (funs._rangeJumps.empty())
		return;

	const u32 magic = 0x45474E52; // RNGE
	ar << magic;
	ar.WriteCount((u32)funs._rangeJumps.size());
	for (size_t i = 0; i < funs._rangeJumps.size(); ++i)
	{
		const SRangeJumpCompile& range = funs._rangeJumps[i];
		ar << range._lower << range._upper << range._flags;
	}
}

static void WriteDebugVarNames(CNArchive& ar, SFunctions& funs, SVars& vars, int staticVarCount)
{
	const u32 magic = 0x4E445642; // NDVB
	ar << magic;
	u32 debugScopeCount = (u32)funs._funSequence.size();
	if (vars._varsFunction.size() == 1 && vars._varsFunction[0]->_varsLayer.size() == 1)
		++debugScopeCount;
	ar.WriteCount(debugScopeCount);
	if (vars._varsFunction.size() == 1 && vars._varsFunction[0]->_varsLayer.size() == 1)
	{
		SLocalVar* pLocalLayer = vars._varsFunction[0]->_varsLayer[0];
		ar << -1;
		ar.WriteCount((u32)pLocalLayer->_localVars.size());
		for (auto it = pLocalLayer->_localVars.begin(); it != pLocalLayer->_localVars.end(); ++it)
		{
			int idx = it->second;
			ar << idx;
			WriteString(ar, it->first);
		}
	}
	for (auto it = funs._funSequence.begin(); it != funs._funSequence.end(); it++)
	{
		SFunctionInfo& fi = *(*it);
		ar << fi._funID;
		ar.WriteCount((u32)fi._debugVarNames.size());
		for (auto itName = fi._debugVarNames.begin(); itName != fi._debugVarNames.end(); ++itName)
		{
			ar << itName->first;
			WriteString(ar, itName->second);
		}
	}
}

static void WriteDebugFunctionNames(CNArchive& ar, SFunctions& funs)
{
	const u32 magic = 0x4E44464E; // NDFN
	ar << magic;
	ar.WriteCount((u32)funs._funSequence.size());
	for (auto it = funs._funSequence.begin(); it != funs._funSequence.end(); it++)
	{
		SFunctionInfo& fi = *(*it);
		ar << fi._funID;
		WriteString(ar, fi.GetFullName());
	}
}

static void FoldGlobalGetters(SFunctions& funs)
{
	// 모듈 경계를 지키려고 둔 `fun Get(){ return value; }`는 호출 비용이
	// 필요 없다. 전체 몸통이 전역 RETURN 하나인 직접 호출만 MOV로 바꾼다.
	// 결과를 임시 슬롯에 복사하므로 뒤의 식이 원본을 바꿔도 평가 순서가 같다.
	// 함수값 호출, 인자, 부작용, 클로저는 제외한다. 디버그 정보 유무와 무관하게 적용한다.
	std::map<int, short> getters;
	u8* code = static_cast<u8*>(funs._codeFinal.GetData());
	for (SFunctionInfo* function : funs._funSequence)
	{
		if ((function->_funType != FUNT_NORMAL && function->_funType != FUNT_EXPORT) ||
			!function->_args.empty() || !function->_captures.empty() ||
			function->_iCode_Size != sizeof(SVMOperation))
			continue;
		SVMOperation op;
		memcpy(&op, code + function->_iCode_Begin, sizeof(op));
		if (op.Code() == NOP_RETURN && op.argFlag == 0 && op.n1 <= COMPILE_GLOBAL_VAR_BEGIN)
			getters[function->_funID] = op.n1;
	}
	if (getters.empty()) return;
	for (SFunctionInfo* function : funs._funSequence)
	{
		for (int offset = function->_iCode_Begin; offset < function->_iCode_Begin + function->_iCode_Size;
			offset += sizeof(SVMOperation))
		{
			SVMOperation op;
			memcpy(&op, code + offset, sizeof(op));
			if (op.Code() != NOP_CALL || op.argFlag != 0 || op.n2 != 0)
				continue;
			auto getter = getters.find(op.n1);
			if (getter == getters.end()) continue;
			op.SetCode(PrimaryOperation(NOP_MOV));
			op.n1 = op.n3;
			op.n2 = getter->second;
			op.n3 = 0;
			memcpy(code + offset, &op, sizeof(op));
		}
	}
}

bool Write(CArchiveRdWC& arText, CNArchive& ar, SFunctions& funs, SVars& vars)
{
	// 디버그 정보가 있어도 접는다(에디터 플레이). 한 줄 getter 안에는 중단점을 걸 수 없다.
	FoldGlobalGetters(funs);
	int iSaveOffset1 = ar.GetBufferOffset();
	SNeoVMHeader header;
	memset(&header, 0, sizeof(header));
	ar << header;

	header._dwFileType = FILE_NEOS;
	header._dwNeoVersion = NEO_VER;
	header._iFunctionCount = (int)(funs.GetFunCountAll());
	header._dwFlag = arText._debug ? NEO_HEADER_FLAG_DEBUG : 0;
	if (NeoVMSystem::IsSinglePrecision())
		header._dwFlag |= NEO_HEADER_FLAG_SINGLE_PRECISION;
	
	std::map<int, SFunctionTableForWriter> funPos;

	std::vector< debug_info> debugInfo;

	// Main 함수 코드 저장
	header._iMainFunctionOffset = ar.GetBufferOffset();
	//header._iGlobalVarCount = funs._cur->_localVarCount;
	header._iGlobalVarCount = vars._globalVarCount;

	//WriteFun(arText, ar, funs, funs._cur, vars, funPos, debugInfo);
	// Sub 함수 코드 저장
	//for (auto it = funs._funs.begin(); it != funs._funs.end(); it++)
	//{
	//	SFunctionInfo& fi = (*it).second;
	//	if (fi._funType == FUNT_IMPORT)
	//		WriteFun(arText, ar, funs, fi, vars, funPos, debugInfo);
	//}

	SVMOperation op;
	memset(&op, 0, sizeof(op));
	op.SetCode(NOP_ERROR);
	ar << op;
	op.SetCode(NOP_IDLE);
	ar << op;
    try { StorageCompiler(funs, vars).Run(); }
    catch (const std::logic_error& error)
    {
        SetCompileError(arText, error.what());
        return false;
    }

	for (auto it = funs._funSequence.begin(); it != funs._funSequence.end(); it++)
	{
		SFunctionInfo* fi = (*it);
		WriteFun(arText, ar, funs, *fi, vars, funPos, debugInfo);
	}



	header._iCodeSize = ar.GetBufferOffset() - sizeof(SNeoVMHeader);
	header.m_iDebugCount = (int)debugInfo.size();

	// 함수 포인터 저장
	if ((int)funPos.size() != header._iFunctionCount)
	{
		SetCompileError(arText, g_sExportCompileErrors[ECE_FUNCTION_COUNT_MISMATCH]);
		return false;
	}

	for (auto it = funPos.begin(); it != funPos.end(); it++)
	{
		int iID = (*it).first;
		SFunctionTableForWriter fun = (*it).second;
		ar << iID << fun._codePtr << fun._argsCount << fun._localTempMax << fun._localVarCount << fun._funType;
		const u16 captureCount = (u16)CheckCompileRange("closure captures", (int64_t)fun._captures.size(), 0, USHRT_MAX);
		ar << captureCount;
		for (const auto& capture : fun._captures)
			ar << capture.first << capture.second;
		if (fun._funType != FUNT_NORMAL && fun._funType != FUNT_ANONYMOUS)
		{
			WriteString(ar, fun._name);
		}
	}

	header._iStaticVarCount = (int)funs._staticVars.size();

	if (vars._varsFunction.size() != 1) return false;
	SLayerVar* pLayerVar = vars._varsFunction[0];
	if (pLayerVar->_varsLayer.size() != 1) return false;
	header._iExportVarCount = (int)vars._varsExport.size();
	for(int i = 0; i < header._iExportVarCount; i++)
	{
		int idx = vars._exportSlots.at(vars._varsExport[i]);
		ar << idx;
		WriteString(ar, vars._varsExport[i]);
	}


	// Static 변수 저장

	for (int i = 0; i < header._iStaticVarCount; i++)
	{
		BoundaryValue& vi = funs._staticVars[i];
		ar << vi.GetType();
		switch (vi.GetType())
		{
		case VAR_INT:
			ar.Write((void*)&vi._int, sizeof(vi._int));
			break;
		case VAR_FLOAT:
			ar << vi._float; // double
			break;
		case VAR_BOOL:
			ar << vi._bl;
			break;
		case VAR_STRING:
			WriteString(ar, vi._str->_str);
			break;
		//case VAR_FUN:
		//	ar << vi._fun_index;
		//	break;
		default:
			SetCompileError(arText, g_sExportCompileErrors[ECE_INVALID_VARIABLE_TYPE], vi.GetType());
			return false;
		}
	}

	// Debug 정보 Save
	if (header.m_iDebugCount)
	{
		header.m_iDebugOffset = ar.GetBufferOffset();
		debug_info di(0, 0);
		ar.Write(&di, sizeof(debug_info)); // First Error OPeration Insert
		ar.Write(&di, sizeof(debug_info)); // Second Idle OPeration Insert

		ar.Write(&debugInfo[0], sizeof(debug_info) * header.m_iDebugCount);
		header.m_iDebugCount += 2;
		WriteDebugVarNames(ar, funs, vars, header._iStaticVarCount);
		WriteDebugFunctionNames(ar, funs);
	}

	WriteSwitchTables(ar, funs);
	WriteRangeJumps(ar, funs);

    // 이름이나 디버그 정보가 아니라 프런트엔드의 선언 계약을 이미지에 보존한다.
    const u32 storageMagic = 0x544f4c53; // SLOT
    ar << storageMagic;
    ar.WriteCount(static_cast<u32>(funPos.size()));
    for (const auto& item : funPos)
    {
        ar << item.first;
        ar << item.second._resultType.kind;
        ar.WriteCount(static_cast<u32>(item.second._storage.size()));
        for (const SlotLayout& slot : item.second._storage)
            ar << slot.slot << slot.kind << slot.typeId;
        ar.WriteCount(static_cast<u32>(item.second._cleanup.size()));
        for (const SlotCleanup& slot : item.second._cleanup)
            ar << slot.slot << slot.kind;
    }
    ar.WriteCount(static_cast<u32>(header._iGlobalVarCount));
    for (int index = -header._iGlobalVarCount - 1; index <= -2; ++index)
    {
        const short slot = CompileShort("global storage slot", index);
        const auto found = vars._globalStorage.find(index);
        const SlotType type = found == vars._globalStorage.end() ? SlotType() : found->second;
        ar << slot << type.kind << type.typeId;
    }

    const u32 callMagic = 0x4c4c4143; // CALL
    ar << callMagic;
    ar.WriteCount(static_cast<u32>(funPos.size()));
    for (const auto& item : funPos)
    {
        ar << item.first;
        ar.WriteCount(static_cast<u32>(item.second._calls.size()));
        for (const CompileCallStorage& call : item.second._calls)
        {
            ar << call.returnType.kind;
            ar.WriteCount(static_cast<u32>(call.types.size()));
            for (const SlotType& type : call.types) ar << type.kind;
        }
    }

    // 타입을 잃지 않은 최종 명령 그래프에서 공통 융합을 고른다. 주소를 바꾸지 않아 점프·함수 오프셋은 유지된다.
    if (!arText._debug && arText.m_coreTypedIR)
    {
        std::vector<SVMOperation> code(static_cast<std::size_t>(header._iCodeSize) / sizeof(SVMOperation));
        std::memcpy(code.data(), static_cast<const u8*>(ar.GetData()) + sizeof(SNeoVMHeader), header._iCodeSize);
        std::vector<SFunctionTable> functions;
        for (const auto& item : funPos)
        {
            SFunctionTable function = {};
            function._codePtr = item.second._codePtr;
            function._argsCount = item.second._argsCount;
            function._localTempMax = item.second._localTempMax;
            function._localVarCount = item.second._localVarCount;
            function._storage = item.second._storage;
            functions.push_back(function);
        }
        std::vector<std::vector<int>> targets;
        for (const SSwitchTableCompile& table : funs._switchTables)
        {
            targets.push_back(std::vector<int>(1, table._defaultOffset));
            for (const SSwitchCaseCompile& item : table._cases) targets.back().push_back(item._jumpOffset);
        }
        const std::vector<ProgramFusion> plans = BuildTypedFusions(code, functions, targets);
        if (!plans.empty())
        {
            InstallTypedFusions(code, plans);
            std::memcpy(static_cast<u8*>(ar.GetData()) + sizeof(SNeoVMHeader), code.data(), header._iCodeSize);
            const u32 magic = 0x45535546; // FUSE
            ar.Write(&magic, sizeof(magic)); ar.WriteCount(static_cast<u32>(plans.size()));
            for (const ProgramFusion& plan : plans)
            {
                ar << plan.start << plan.count << plan.elide << plan.update;
                ar.Write(plan.operations, sizeof(SVMOperation) * plan.count);
            }
        }
    }

	int iSaveOffset2 = ar.GetBufferOffset();
	ar.SetPointer(iSaveOffset1, SEEK_SET);
	ar << header;
	ar.SetPointer(iSaveOffset2, SEEK_SET);

	return true;
}


bool WriteLog(CArchiveRdWC& arText, CNArchive& arw, SFunctions& funs, SVars& vars)
{
    std::vector<ProgramFusion> fusions;
    CNeoVMProgram* program = CNeoVMProgram::Create(arw.GetData(), arw.GetBufferOffset());
    if (program) { fusions = program->fusions; program->Release(); }

	SNeoVMHeader header;
	memset(&header, 0, sizeof(header));

	//header._iFunctionCount = (int)(funs._funs.size() + 1);
	//header._iStaticVarCount = (int)funs._staticVars.size();

	arw.SetBufferOffset(0);
	arw >> header;

	SVMOperation op;
	arw >> op;	// NOP_ERROR
	arw >> op;  // NOP_IDLE

	std::map<int, std::string> mapFun;
	for (auto it = funs._funIDs.begin(); it != funs._funIDs.end(); it++)
	{
		//auto it2 = funs._funs.find((*it));
		SFunctionInfo& fi = *(*it).second;
		mapFun[fi._funID] = fi.GetFullName();

		//OutAsm("Fun [%d] %s\n", fi._funID, fi._name.c_str());
	}

	for (auto it = mapFun.begin(); it != mapFun.end(); it++)
	{
		OutAsm("Fun [%d] %s\n", (*it).first, (*it).second.c_str());
	}

	STempDebug td;
	td._staticVars = funs._staticVars;
	// Static 변수
	for (int i = 0; i < header._iStaticVarCount; i++)
	{
		BoundaryValue& vi = funs._staticVars[i];
		OutAsm("Static   [%d] %s\n", i, GetValueString(vi, &mapFun).c_str());
	}

	std::set<int> tempExportVars;
	for (int i = 0; i < header._iExportVarCount; i++)
	{
		tempExportVars.insert(vars._exportSlots.at(vars._varsExport[i]));
	}

	// Global 변수
	if (vars._varsFunction.size() != 1) return false;
	SLayerVar* pLayerVar = vars._varsFunction[0];
	if (pLayerVar->_varsLayer.size() != 1) return false;
	SLocalVar* pLocalLayer = pLayerVar->_varsLayer[0];
	std::map<int, std::string> global;
	for (auto it2 = pLocalLayer->_localVars.begin(); it2 != pLocalLayer->_localVars.end(); it2++)
	{
		int idx = -(*it2).second + COMPILE_GLOBAL_VAR_BEGIN;
		global[idx] = (*it2).first;
	}
	for (auto it2 = global.begin(); it2 != global.end(); it2++)
	{
		int idx = (*it2).first;
		if (tempExportVars.end() == tempExportVars.find(-idx - 2))
			OutAsm("Global   [%d] %s\n", -idx - 2, (*it2).second.c_str());
		else
			OutAsm("Global E [%d] %s\n", -idx - 2, (*it2).second.c_str());
		td._globalVars.push_back((*it2).second);
	}
	if (header.m_iDebugCount > 0 && header.m_iDebugOffset > 0)
	{
		td.debugInfo.resize(header.m_iDebugCount);
		int off = arw.GetBufferOffset();
		arw.SetBufferOffset(header.m_iDebugOffset);
		arw.Read(&td.debugInfo[0], header.m_iDebugCount * sizeof(debug_info));
		arw.SetBufferOffset(off);
	}


	// Main 함수 코드 저장
	//WriteFunLog(arText, arw, funs, funs._cur, vars);
	// Sub 함수 코드 저장
	for (auto it = funs._funSequence.begin(); it != funs._funSequence.end(); it++)
	{
		SFunctionInfo& fi = *(*it);
		WriteFunLog(arText, arw, funs, fi, vars, td, fusions);
	}

	//// 함수 포인터 저장

	return true;
}

};
