#include <cassert>
#include <math.h>
#include <stdlib.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <iomanip>
#include <locale>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <iterator>

#include "NeoVMInternal.h"
#include "NeoVMWorker.h"
#include "NeoArchive.h"
#include "NeoJson.h"
#include "NeoStringLimits.h"
#include "NeoNumberFormat.h"
#include "NeoTime.h"
#include "UTFString.h"
#include <chrono>

#define MATH_PI				3.14159265358979323846 // 원주율은 스칼라 정밀도를 따른다.
NeoScript::NeoVMSystem::IO_Print NeoScript::NeoVMSystem::m_pFunPrint = nullptr;
NeoScript::NeoVMSystem::IO_Print NeoScript::NeoVMSystem::m_pFunError = nullptr;

namespace NeoScript
{



struct neo_libs
{
	// 프런트엔드가 float 인자를 double로 변환한다. 호스트용 범용 변환기는 거치지 않는다.
	static NEOS_FORCEINLINE NS_FLOAT FloatArgument(CNeoVMWorker* worker, int index)
	{
		ValueRef value = worker->GetStack(index);
		assert(value.GetType() == VAR_FLOAT);
		return value->_float;
	}

	// VM values are not C++ smart pointers. Hold references across callbacks,
	// return-slot replacement and exceptions, then release them on every exit.
	struct HeldValue
	{
		CNeoVMWorker* worker;
		BoundaryValue value;
		HeldValue(CNeoVMWorker* w, ValueRef source) : worker(w)
		{ Move_DestNoRelease(&value, source); }
		~HeldValue() { worker->Var_Release(&value); }
		HeldValue(const HeldValue&) = delete;
		HeldValue& operator=(const HeldValue&) = delete;
	};

	static bool Str_sub(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_STRING);
		assert(args == 2 && pN->GetStack(1).GetType() == VAR_INT && pN->GetStack(2).GetType() == VAR_INT);

		int len = pVar->_str->_StringLen;
		std::string* p = &pVar->_str->_str;
		int p1 = NeoNarrowInt(pN->GetStack(1)->_int);
		int p2 = NeoNarrowInt(pN->GetStack(2)->_int);

		// Character offsets are clamped; empty input and non-positive counts
		// produce an empty string without making a normal boundary an error.
		p1 = std::max(0, std::min(p1, len));
		if (p1 == len || p2 <= 0)
		{
			pN->ReturnValue("");
			return true;
		}

		p1 = utf_string::UTF8_OFFSET(*p, 0, p1);
		p2 = utf_string::UTF8_OFFSET(*p, p1, p2) - p1;

		std::string sTempString = p->substr(p1, p2);
		pN->ReturnValue(sTempString.c_str());
		return true;
	}
	static bool Str_len(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_STRING);
		assert(args == 0);

		pN->ReturnValue(pVar->_str->_StringLen);
		return true;
	}
    static bool Str_codepoint(CNeoVMWorker* pN, ValueRef pVar, short args)
    {
        assert(pVar.GetType() == VAR_STRING && args == 1);
        ValueRef index = pN->GetStack(1);
        assert(index.GetType() == VAR_INT);
        pN->ReturnValue(utf_string::CodePointAt(pVar->_str->_str, NeoNarrowInt(index->_int)));
        return true;
    }
	static bool Str_find(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_STRING);
		assert(args == 1);

		std::string* p = &pVar->_str->_str;
		ValueRef needle = pN->GetStack(1);
		assert(needle.GetType() == VAR_STRING || needle.GetType() == VAR_NONE);
		if (needle.GetType() == VAR_NONE) return false;
		const char* p2 = needle->_str->_str.c_str();

		const size_t found = p->find(p2);
		int iFind = found == std::string::npos ? -1 : utf_string::UTF8_INDEX2OFFSET(*p, (int)found);
		pN->ReturnValue((int)iFind);
		return true;
	}
	static bool Str_upper(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_STRING);
		assert(args == 0);

		std::string str = pVar->_str->_str;
		std::transform(str.begin(), str.end(), str.begin(), ::toupper);
		pN->ReturnValue(str.c_str());
		return true;
	}
	static bool Str_lower(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_STRING);
		assert(args == 0);

		std::string str = pVar->_str->_str;
		std::transform(str.begin(), str.end(), str.begin(), ::tolower);
		pN->ReturnValue(str.c_str());
		return true;
	}
	static bool Str_trim(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_STRING);
		assert(args == 0);

		std::string drop = " ";
		std::string str = pVar->_str->_str;
		str = str.erase(str.find_last_not_of(drop) + 1);
		str = str.erase(0, str.find_first_not_of(drop));
		pN->ReturnValue(str.c_str());
		return true;
	}
	static bool Str_ltrim(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_STRING);
		assert(args == 0);

		std::string drop = " ";
		std::string str = pVar->_str->_str;
		str = str.erase(0, str.find_first_not_of(drop));
		pN->ReturnValue(str.c_str());
		return true;
	}
	static bool Str_rtrim(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_STRING);
		assert(args == 0);

		std::string drop = " ";
		std::string str = pVar->_str->_str;
		str = str.erase(str.find_last_not_of(drop) + 1);
		pN->ReturnValue(str.c_str());
		return true;
	}
	static bool Str_replace(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_STRING);
		assert(args == 2);

		ValueRef pFind = pN->GetStack(1);
		ValueRef pReplace = pN->GetStack(2);
		assert(pFind.GetType() == VAR_STRING || pFind.GetType() == VAR_NONE);
		if (pFind.GetType() == VAR_NONE) return false;
		assert(pReplace.GetType() == VAR_STRING || pReplace.GetType() == VAR_NONE);
		if (pReplace.GetType() == VAR_NONE) return false;

		std::string str = pVar->_str->_str;
		const size_t found = str.find(pFind->_str->_str);
		if (found != std::string::npos)
		{
			CheckRuntimeStringGrowth(str.size() - pFind->_str->_str.size(), pReplace->_str->_str.size());
			str.replace(found, pFind->_str->_str.length(), pReplace->_str->_str);
		}
		pN->ReturnValue(str.c_str());
		return true;
	}
	static bool Str_replaceAll(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_STRING && args == 2);
		ValueRef pFind = pN->GetStack(1);
		ValueRef pReplace = pN->GetStack(2);
		assert((pFind.GetType() == VAR_STRING || pFind.GetType() == VAR_NONE) &&
			(pReplace.GetType() == VAR_STRING || pReplace.GetType() == VAR_NONE));
		if (pFind.GetType() == VAR_NONE || pReplace.GetType() == VAR_NONE) return false;
		const std::string& source = pVar->_str->_str;
		const std::string& needle = pFind->_str->_str;
		const std::string& replacement = pReplace->_str->_str;
		// Match the original input once, left to right. Never search inserted
		// text, and define an empty needle as a no-op to guarantee progress.
		std::string result;
		size_t begin = 0;
		if (!needle.empty())
		{
			for (size_t found = source.find(needle); found != std::string::npos;
				found = source.find(needle, begin))
			{
				CheckRuntimeStringGrowth(result.size(), found - begin);
				result.append(source, begin, found - begin);
				CheckRuntimeStringGrowth(result.size(), replacement.size());
				result += replacement;
				begin = found + needle.size();
			}
		}
		CheckRuntimeStringGrowth(result.size(), source.size() - begin);
		result.append(source, begin, std::string::npos);
		pN->ReturnValue(result.c_str());
		return true;
	}
	// 결과를 완성한 뒤 반환 슬롯에 기록해 수신자와 슬롯이 겹치는 경우를 보호한다.
	static bool StringRegex(CNeoVMWorker* pN, ValueRef pVar, short args, int operation)
	{
		const int required = operation == 2 ? 2 : 1;
		assert(pVar.GetType() == VAR_STRING && (args == required || args == required + 1));
		for (int i = 1; i <= required; ++i)
		{
			assert(pN->GetStack(i).GetType() == VAR_STRING || pN->GetStack(i).GetType() == VAR_NONE);
			if (pN->GetStack(i).GetType() == VAR_NONE) return false;
		}
		assert(args <= required || pN->GetStack(args).GetType() == VAR_BOOL);
		if (pN->GetStack(1)->_str->_str.size() > pN->GetVM()->_maxRegexPatternBytes)
		{
			pN->SetError("regular expression pattern exceeds the host byte limit");
			return false;
		}
		// 정규식 자체의 작업량 제한은 호스트의 VM 시간 예산과 별개다.
		if (pN->IsSynchronousExecutionLimited())
		{
			pN->SetError("regular expressions are not allowed during time-limited execution");
			return false;
		}
		RegexResult result;
		try
		{
			result = pN->GetVM()->_regex.Run(pVar->_str->_str,
				pN->GetStack(1)->_str->_str, operation == 2 ? pN->GetStack(2)->_str->_str : std::string(),
				operation, args > required && pN->GetStack(args)->_bl);
		}
		catch (const std::runtime_error& error)
		{
			pN->SetError(error.what());
			return false;
		}
		if (operation == 0) pN->ReturnValue(result.matched);
		else if (operation == 2) pN->Var_SetStringA(pN->GetReturnVar(), result.text);
		else if (!result.matched) pN->ReturnValue();
		else
		{
			ListInfo* list = pN->GetVM()->ListAlloc();
			pN->Var_SetList(pN->GetStack(0), list);
			for (const auto& capture : result.captures) list->InsertLast(capture);
		}
		return true;
	}
	static bool Str_regex_match(CNeoVMWorker* pN, ValueRef pVar, short args) { return StringRegex(pN, pVar, args, 0); }
	static bool Str_regex_search(CNeoVMWorker* pN, ValueRef pVar, short args) { return StringRegex(pN, pVar, args, 1); }
	static bool Str_regex_replace(CNeoVMWorker* pN, ValueRef pVar, short args) { return StringRegex(pN, pVar, args, 2); }

	static bool Str_split(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_STRING);
		assert(args == 1);

		ValueRef pFind = pN->GetStack(1);
		assert(pFind.GetType() == VAR_STRING || pFind.GetType() == VAR_NONE);
		if (pFind.GetType() == VAR_NONE) return false;
		const std::string& findstr = pFind->_str->_str;
		// 빈 구분자는 검색 위치가 전진하지 않으므로 결과 리스트를 만들기 전에 거절한다.
		if (findstr.empty())
		{
			pN->SetError("split separator must not be empty");
			return false;
		}
		std::string str = pVar->_str->_str;

		ValueRef pRet = pN->GetStack(0);
		ListInfo* pListR = pN->GetVM()->ListAlloc();
		pN->Var_SetList(pRet, pListR); // Set Return Value

		size_t previous = 0, current;
		current = str.find(findstr);
		while (current != std::string::npos)
		{
			std::string substring = str.substr(previous, current - previous);
			pListR->InsertLast(substring);

			// 구분자 길이만큼 넘어가야 한다. 1 씩 넘기면 두 글자 이상 구분자에서
			// 남은 글자가 다음 토큰 앞에 붙는다("a::b".split("::") -> ["a", ":b"]).
			previous = current + findstr.length();
			current = str.find(findstr, previous);
		}
		pListR->InsertLast(str.substr(previous, current - previous)); // Last
		return true;
	}


	static bool Collection_clear(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 0);
		HeldValue receiver(pN, pVar);
		switch (receiver.value.GetType())
		{
		case VAR_ARRAY: pN->GetVM()->ArrayResize(receiver.value._array, 0); break;
		case VAR_MAP: receiver.value._tbl->Clear(); break;
		case VAR_LIST: receiver.value._lst->Resize(0); break;
		case VAR_SET: receiver.value._set->Clear(); break;
		default: return false;
		}
		pN->ReturnValue();
		return true;
	}
	static bool ReadCollectionSize(CNeoVMWorker* worker, int argument, int& size)
	{
		const NS_INT value = worker->GetStack(argument)->_int;
		if (value > INT32_MAX)
		{
			worker->SetError("collection capacity exceeds limit");
			return false;
		}
		// 음수 resize/map.reserve의 기존 빈 컬렉션 규칙은 유지한다.
		size = value < 0 ? 0 : static_cast<int>(value);
		return true;
	}
	static bool Sequence_reserve(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1 && pN->GetStack(1).GetType() == VAR_INT);
		int capacity;
		if (pN->GetStack(1)->_int < 0)
		{
			pN->SetError("reserve capacity must not be negative");
			return false;
		}
		if (!ReadCollectionSize(pN, 1, capacity)) return false;
		if (pVar.GetType() == VAR_LIST) pVar->_lst->Reserve(capacity);
		else if (pVar.GetType() == VAR_ARRAY) pN->GetVM()->ArrayReserve(pVar->_array, capacity);
		else return false;
		pN->ReturnValue();
		return true;
	}
	static bool CollectionValueEquals(CNeoVMWorker* pN, ValueRef a, ValueRef b)
	{
		// 검색은 대입 변환 없이 스크립트 == 규칙을 따른다. 정수끼리는 정밀도를 잃지 않는다.
		if (a.GetType() == VAR_INT && b.GetType() == VAR_INT) return a->_int == b->_int;
		if (a.IsNumber() && b.IsNumber()) return a.GetFloatNumber() == b.GetFloatNumber();
		return pN->CompareEQRare(a, b);
	}
	static int SequenceFind(CNeoVMWorker* pN, ValueRef pVar, ValueRef value)
	{
		if (pVar.GetType() == VAR_LIST)
		{
			ListInfo* list = pVar->_lst;
			for (int i = 0; i < list->GetCount(); ++i)
				if (CollectionValueEquals(pN, list->GetValue(i), value)) return i;
		}
		else
		{
			ArrayInfo* array = pVar->_array;
			BoundaryValue item;
			for (int i = 0; i < array->_count; ++i)
			{
				ValueRef candidate = &item;
				switch (array->_elementType)
				{
				case NeoArrayElementType::Bool: item = BoundaryValue(VAR_BOOL); item._bl = array->GetBool(i); break;
				case NeoArrayElementType::Int: item = BoundaryValue(VAR_INT); item._int = array->IntData()[i]; break;
				case NeoArrayElementType::Float: item = BoundaryValue(VAR_FLOAT); item._float = array->FloatData()[i]; break;
				case NeoArrayElementType::Vector2:
				case NeoArrayElementType::Vector3:
				case NeoArrayElementType::Vector4: candidate = array->VectorAt(i); break;
				case NeoArrayElementType::Int32: item = BoundaryValue(VAR_INT); item._int = array->Int32Data()[i]; break;
				case NeoArrayElementType::Float32: item = BoundaryValue(VAR_FLOAT); item._float = array->Float32Data()[i]; break;
				}
				if (CollectionValueEquals(pN, candidate, value)) return i;
			}
		}
		return -1;
	}
	static bool Sequence_find(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(!(args != 1 || (pVar.GetType() != VAR_LIST && pVar.GetType() != VAR_ARRAY)));
		pN->ReturnValue(SequenceFind(pN, pVar, pN->GetStack(1)));
		return true;
	}
	static bool Sequence_contains(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(!(args != 1 || (pVar.GetType() != VAR_LIST && pVar.GetType() != VAR_ARRAY)));
		pN->ReturnValue(SequenceFind(pN, pVar, pN->GetStack(1)) >= 0);
		return true;
	}
	static bool Array_fill(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_ARRAY && args == 1);
		ArrayInfo* array = pVar->_array;
		ValueRef value = pN->GetStack(1);
		if (!array->CanAssignValue(value))
		{
			pN->SetArrayAssignError(array, value);
			return false;
		}
		HeldValue held(pN, value);
		// 기존 원소만 바꾸며, resize가 사용할 생성 시 초기값은 보존한다.
		for (int i = 0; i < array->_count; ++i) array->AssignValue(i, &held.value);
		pN->ReturnValue();
		return true;
	}
	static bool Set_add(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_SET && args == 1);
		if (pN->GetStack(1).GetType() == VAR_NONE) { pN->SetError("set element cannot be null"); return false; }
		HeldValue receiver(pN, pVar);
		SetInfo* set = receiver.value._set;
		const int before = set->GetCount();
		if (!set->Insert(pN->GetStack(1))) return false;
		pN->ReturnValue(set->GetCount() != before);
		return true;
	}
	static bool Set_remove(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_SET && args == 1);
		HeldValue receiver(pN, pVar), key(pN, pN->GetStack(1));
		SetInfo* set = receiver.value._set;
		const int before = set->GetCount();
		set->Remove(&key.value);
		pN->ReturnValue(set->GetCount() != before);
		return true;
	}
	static bool Set_contains(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_SET && args == 1);
		pN->ReturnValue(pVar->_set->Find(pN->GetStack(1)));
		return true;
	}
	static bool Set_values(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_SET && args == 0);
		HeldValue receiver(pN, pVar);
		SetInfo* source = receiver.value._set;
		ListInfo* dest = pN->GetVM()->ListAlloc();
		pN->Var_SetList(pN->GetStack(0), dest);
		dest->Resize(source->GetCount());
		int index = 0;
		for (CollectionIterator it = source->FirstNode(); it._pSetNode; source->NextNode(it))
			dest->SetValue(index++, &it._pSetNode->data->key);
		return true;
	}
	static bool Map_contains(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_MAP && args == 1);
		pN->ReturnValue(pVar->_tbl->Find(pN->GetStack(1)) != nullptr);
		return true;
	}
	static bool Map_remove(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_MAP && args == 1);
		HeldValue receiver(pN, pVar), key(pN, pN->GetStack(1));
		MapInfo* map = receiver.value._tbl;
		const int before = map->GetCount();
		map->Remove(&key.value);
		pN->ReturnValue(map->GetCount() != before);
		return true;
	}
	static bool Collection_clone(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 0);
		// 반환 슬롯과 수신자가 겹쳐도 원본은 유지한다. 중첩 컨테이너는 얕게 공유한다.
		HeldValue receiver(pN, pVar);
		CNeoVM* vm = pN->GetVM();
		switch (receiver.value.GetType())
		{
		case VAR_ARRAY:
		{
			ArrayInfo* source = receiver.value._array;
			BoundaryValue initial;
			switch (source->_elementType)
			{
			case NeoArrayElementType::Bool: initial = BoundaryValue(VAR_BOOL); initial._bl = source->_initialBool; break;
			case NeoArrayElementType::Int: initial = BoundaryValue(VAR_INT); initial._int = source->_initialInt; break;
			case NeoArrayElementType::Float: initial = BoundaryValue(VAR_FLOAT); initial._float = source->_initialFloat; break;
			case NeoArrayElementType::Vector2:
			case NeoArrayElementType::Vector3:
			case NeoArrayElementType::Vector4: initial = BoundaryValue(ValueRef(&source->_initialVector, source->VectorType())); break;
			case NeoArrayElementType::Int32: initial = BoundaryValue(VAR_INT); initial._int = source->_initialInt; break;
			case NeoArrayElementType::Float32: initial = BoundaryValue(VAR_FLOAT); initial._float = source->_initialFloat; break;
			}
			ArrayInfo* dest = vm->ArrayAlloc(source->_elementType, source->_count, initial);
			if (dest == nullptr) return false;
			pN->Var_SetArray(pN->GetStack(0), dest);
			if (source->VectorDimension() != 0)
			{
				for (int i = 0; i < source->_count; ++i) dest->AssignValue(i, source->VectorAt(i));
			}
			else if (source->_count != 0) memcpy(dest->_data, source->_data, dest->BufferBytes());
			break;
		}
		case VAR_MAP:
		{
			MapInfo* source = receiver.value._tbl;
			MapInfo* dest = vm->TableAlloc();
			pN->Var_SetTable(pN->GetStack(0), dest);
			dest->Reserve(source->GetCount());
			for (CollectionIterator it = source->FirstNode(); it._pTableNode; source->NextNode(it))
			{
				MapData* data = it._pTableNode->data;
				ValueRef slot = dest->Insert(&data->key);
				if (slot == nullptr) return false;
				if (data->value.IsContainerType()) dest->MarkContainerChild();
				vm->Move(slot, &data->value);
			}
			break;
		}
		case VAR_LIST:
		{
			ListInfo* source = receiver.value._lst;
			ListInfo* dest = vm->ListAlloc();
			pN->Var_SetList(pN->GetStack(0), dest);
			dest->Resize(source->GetCount());
			for (int i = 0; i < source->GetCount(); ++i) dest->SetValue(i, source->GetValue(i));
			break;
		}
		case VAR_SET:
		{
			SetInfo* source = receiver.value._set;
			SetInfo* dest = vm->SetAlloc();
			pN->Var_SetSet(pN->GetStack(0), dest);
			dest->Reserve(source->GetCount());
			for (CollectionIterator it = source->FirstNode(); it._pSetNode; source->NextNode(it))
				if (!dest->Insert(&it._pSetNode->data->key)) return false;
			break;
		}
		default: return false;
		}
		return true;
	}
	static bool List_resize(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_LIST);
		assert(args == 1 && pN->GetStack(1).GetType() == VAR_INT);

		int size;
		if (!ReadCollectionSize(pN, 1, size)) return false;
		pVar->_lst->Resize(size);
		pN->ReturnValue();
		return true;
	}
	static bool List_len(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_LIST);
		assert(args == 0);

		pN->ReturnValue((int)pVar->_lst->GetCount());
		return true;
	}
	static bool Array_len(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_ARRAY);
		assert(args == 0);

		pN->ReturnValue(pVar->_array->_count);
		return true;
	}
	static bool Array_resize(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_ARRAY);
		assert(args == 1 && pN->GetStack(1).GetType() == VAR_INT);

		int size;
		if (!ReadCollectionSize(pN, 1, size)) return false;
		pN->GetVM()->ArrayResize(pVar->_array, size);
		pN->ReturnValue();
		return true;
	}
	static bool Array_append(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_ARRAY);
		assert(args == 1);

		ArrayInfo* array = pVar->_array;
		ValueRef value = pN->GetStack(1);
		if (array->CanAssignValue(value) == false)
		{
			pN->SetArrayAssignError(array, value);
			return false;
		}
		if (array->_count == INT32_MAX)
			return false;

		const int index = array->_count;
		pN->GetVM()->ArrayResize(array, index + 1);
		array->AssignValue(index, value);
		pN->ReturnValue();
		return true;
	}
	static bool List_append(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_LIST);
		if (args == 1)
		{
			pVar->_lst->InsertLast(pN->GetStack(1));
			pN->ReturnValue();
			return true;
		}
		else if (args == 2)
		{
			ValueRef pIndex = pN->GetStack(2);
			assert(pIndex.GetType() == VAR_INT);
			if (false == pVar->_lst->Insert(NeoNarrowInt(pIndex->_int), pN->GetStack(1)))
				return false;
			pN->ReturnValue();
			return true;
		}
		return false;
	}
	static bool List_insert(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_LIST && args == 2);
		ValueRef index = pN->GetStack(1);
		assert(index.GetType() == VAR_INT);
		if (!pVar->_lst->Insert(NeoNarrowInt(index->_int), pN->GetStack(2)))
		{
			pN->SetError("list insert index out of range");
			return false;
		}
		pN->ReturnValue();
		return true;
	}
	static bool ListRemoveAt(CNeoVMWorker* pN, ValueRef pVar, int offset, bool swapLast)
	{
		HeldValue receiver(pN, pVar);
		ListInfo* list = receiver.value._lst;
		ValueRef item = list->GetValue(offset);
		if (item == nullptr)
		{
			pN->SetError("list remove index out of range");
			return false;
		}
		// 삭제할 값과 수신자를 보존해 자기 참조와 반환 슬롯 중첩을 처리한다.
		HeldValue removed(pN, item);
		if (swapLast)
		{
			const int last = list->GetCount() - 1;
			if (offset != last) list->SetValue(offset, list->GetValue(last));
			list->Resize(last);
		}
		else list->Remove(offset);
		pN->ReturnValue(&removed.value);
		return true;
	}
	static bool List_remove(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_LIST && args == 1 && pN->GetStack(1).GetType() == VAR_INT);
		return ListRemoveAt(pN, pVar, NeoNarrowInt(pN->GetStack(1)->_int), false);
	}
	static bool List_remove_swap(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_LIST && args == 1 && pN->GetStack(1).GetType() == VAR_INT);
		return ListRemoveAt(pN, pVar, NeoNarrowInt(pN->GetStack(1)->_int), true);
	}
	static bool List_pop(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_LIST && args == 0);
		const int count = pVar->_lst->GetCount();
		if (count == 0)
		{
			pN->SetError("cannot pop an empty list");
			return false;
		}
		return ListRemoveAt(pN, pVar, count - 1, false);
	}
	static bool List_sort(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_LIST && args == 1);
		ValueRef compare = pN->GetStack(1);
		assert(compare.GetType() == VAR_NONE || compare.GetType() == VAR_FUN || compare.GetType() == VAR_CLOSURE);
		if (compare.GetType() == VAR_NONE) return false;
		// The native sort cannot suspend and resume its C++ frame. Never disable
		// a host's time limit to run a comparator: it may loop forever.
		if (pN->IsSynchronousExecutionLimited())
		{
			pN->SetError("list.sort is not allowed during time-limited execution");
			return false;
		}
		HeldValue receiver(pN, pVar), callback(pN, compare);
		// A synchronous native sort cannot preserve its C++ frame across a
		// script suspension or coroutine switch. Use the VM's nested-call guard.
		struct SynchronousCall
		{
			CNeoVMWorker* worker;
			explicit SynchronousCall(CNeoVMWorker* value) : worker(value) { worker->BeginNestedScriptCall(); }
			~SynchronousCall() { worker->EndNestedScriptCall(); }
		} synchronousCall(pN);
		ListInfo* list = receiver.value._lst;
		const size_t count = static_cast<size_t>(list->GetCount());
		const u32 version = list->_mutationVersion;
		struct Snapshot
		{
			CNeoVMWorker* worker;
			std::vector<BoundaryValue> values;
			~Snapshot() { for (auto& value : values) worker->Var_Release(&value); }
		} snapshot{pN, std::vector<BoundaryValue>(count)};
		std::vector<size_t> order(count), next(count);
		for (size_t i = 0; i < count; ++i)
		{
			Move_DestNoRelease(&snapshot.values[i], list->GetValue(static_cast<int>(i)));
			order[i] = i;
		}
		// Stable, bounded mergesort: even an inconsistent script comparator must
		// not violate std::sort's strict-weak-ordering precondition or read past
		// a buffer. Never retain a list bucket or VM stack pointer across calls.
		for (size_t width = 1; width < count; width *= 2)
		{
			for (size_t begin = 0; begin < count; begin += width * 2)
			{
				const size_t middle = std::min(begin + width, count);
				const size_t end = std::min(begin + width * 2, count);
				size_t left = begin, right = middle;
				for (size_t dest = begin; dest < end; ++dest)
				{
					bool takeRight = left == middle;
					if (left < middle && right < end)
					{
						BoundaryValue callArgs[2] = {snapshot.values[order[right]], snapshot.values[order[left]]};
						ValueRef result = pN->testCall(&callback.value, callArgs, 2);
						if (pN->GetVM()->IsLastErrorMsg()) return false;
						if (list->_mutationVersion != version)
						{
							pN->SetError("list was modified during sort");
							return false;
						}
						if (result == nullptr)
						{
							pN->SetError("list sort comparator must accept two arguments and return bool");
							return false;
						}
						assert(result.GetType() == VAR_BOOL);
						takeRight = result->_bl;
					}
					next[dest] = takeRight ? order[right++] : order[left++];
				}
			}
			order.swap(next);
		}
		if (count > 1)
		{
			for (size_t i = 0; i < count; ++i)
				list->SetValue(static_cast<int>(i), &snapshot.values[order[i]]);
			++list->_mutationVersion;
		}
		pN->ReturnValue();
		return true;
	}
	static bool Set_len(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_SET && args == 0);
		pN->ReturnValue(pVar->_set->GetCount());
		return true;
	}
	static bool Math_abs(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT v = FloatArgument(pN, 1);
		pN->ReturnValue(AbsValue(v));
		return true;
	}
	static bool Math_acos(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT v = FloatArgument(pN, 1);
		pN->ReturnValue(::acos(v));
		return true;
	}
	static bool Math_asin(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT v = FloatArgument(pN, 1);
		pN->ReturnValue(::asin(v));
		return true;
	}
	static bool Math_atan(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT v = FloatArgument(pN, 1);
		pN->ReturnValue(::atan(v));
		return true;
	}
	static bool Math_ceil(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT v = FloatArgument(pN, 1);
		pN->ReturnValue(::ceil(v));
		return true;
	}
	static bool Math_floor(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT v = FloatArgument(pN, 1);
		pN->ReturnValue(::floor(v));
		return true;
	}
	static bool Math_round(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT v = FloatArgument(pN, 1);
		pN->ReturnValue(::round(v));
		return true;
	}
	static bool Math_sin(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT v = FloatArgument(pN, 1);
		pN->ReturnValue(::sin(v));
		return true;
	}
	static bool Math_cos(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT v = FloatArgument(pN, 1);
		pN->ReturnValue(::cos(v));
		return true;
	}
	static bool Math_tan(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT v = FloatArgument(pN, 1);
		pN->ReturnValue(::tan(v));
		return true;
	}
	static bool Math_log(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT v = FloatArgument(pN, 1);
		pN->ReturnValue(::log(v));
		return true;
	}
	static bool Math_log10(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT v = FloatArgument(pN, 1);
		pN->ReturnValue(::log10(v));
		return true;
	}
	static bool Math_exp(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT v = FloatArgument(pN, 1);
		pN->ReturnValue(::exp(v));
		return true;
	}
	static bool Math_pow(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);

		NS_FLOAT v1 = FloatArgument(pN, 1);
		NS_FLOAT v2 = FloatArgument(pN, 2);
		pN->ReturnValue(::pow(v1, v2));
		return true;
	}
	static bool Math_deg(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT radian = FloatArgument(pN, 1);
		pN->ReturnValue(DegValue(radian));
		return true;
	}
	static bool Math_rad(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT degree = FloatArgument(pN, 1);
		pN->ReturnValue(RadValue(degree));
		return true;
	}
	static bool Math_sqrt(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		NS_FLOAT v = FloatArgument(pN, 1);
		pN->ReturnValue(::sqrt(v));
		return true;
	}

	// ── 게임 스크립트가 자주 찾는 것들 ─────────────────────────────────────────
	//
	// atan2 가 없어서 스크립트가 사분면을 손으로 나누고 있었다. 각도·보간·거리
	// 도우미를 한 번에 넣는다. 각도는 전부 라디안이고, WrapAngle 은 (-pi, pi] 다.
	static constexpr NS_FLOAT kNsPi = (NS_FLOAT)3.14159265358979323846;
	static constexpr NS_FLOAT kNsTwoPi = (NS_FLOAT)6.28318530717958647692;

	static NS_FLOAT NsWrapAngle(NS_FLOAT a)
	{
		a = static_cast<NS_FLOAT>(::fmod(a, kNsTwoPi));
		if (a > kNsPi) a -= kNsTwoPi;
		if (a <= -kNsPi) a += kNsTwoPi;
		return a;
	}
	static double NsWrapAngleWide(double a)
	{
		// Keep the language's existing float pi/tau, but reduce before large
		// endpoints are subtracted or a small angular step is added to them.
		if (a > kNsPi || a <= -kNsPi)
		{
			a = ::fmod(a, static_cast<double>(kNsTwoPi));
			if (a > kNsPi) a -= kNsTwoPi;
			if (a <= -kNsPi) a += kNsTwoPi;
		}
		return a;
	}
	static NS_FLOAT NsAngleResult(double a)
	{
		const NS_FLOAT result = static_cast<NS_FLOAT>(NsWrapAngleWide(a));
		// Rounding a double just above -pi can land on the excluded endpoint.
		return result <= -kNsPi ? kNsPi : result;
	}
	static NS_FLOAT NsRepeat(NS_FLOAT x, NS_FLOAT length)
	{
		if (length <= (NS_FLOAT)0) return (NS_FLOAT)0;
		NS_FLOAT result = static_cast<NS_FLOAT>(::fmod(x, length));
		if (result < 0) result += length;
		// Adding a tiny negative remainder can round up to length.
		return result == (NS_FLOAT)0 || result >= length ? (NS_FLOAT)0 : result;
	}
	static NS_FLOAT NsMoveToward(NS_FLOAT cur, NS_FLOAT target, NS_FLOAT maxDelta)
	{
		NS_FLOAT d = target - cur;
		if (::fabs(d) <= maxDelta) return target;
		return cur + (d > (NS_FLOAT)0 ? maxDelta : -maxDelta);
	}

	static bool Math_atan2(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);
		pN->ReturnValue(::atan2(FloatArgument(pN, 1), FloatArgument(pN, 2)));
		return true;
	}
	static bool Math_hypot(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);
		NS_FLOAT x = FloatArgument(pN, 1);
		NS_FLOAT y = FloatArgument(pN, 2);
		pN->ReturnValue(HypotValue(x, y));
		return true;
	}
	static bool Math_fmod(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);
		NS_FLOAT x = FloatArgument(pN, 1);
		NS_FLOAT y = FloatArgument(pN, 2);
		pN->ReturnValue(FmodValue(x, y));
		return true;
	}
	static bool Math_trunc(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);
		pN->ReturnValue(::trunc(FloatArgument(pN, 1)));
		return true;
	}
	static bool Math_fract(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);
		pN->ReturnValue(FractValue(FloatArgument(pN, 1)));
		return true;
	}
	static bool Math_sinh(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);
		pN->ReturnValue(::sinh(FloatArgument(pN, 1)));
		return true;
	}
	static bool Math_cosh(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);
		pN->ReturnValue(::cosh(FloatArgument(pN, 1)));
		return true;
	}
	static bool Math_tanh(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);
		pN->ReturnValue(::tanh(FloatArgument(pN, 1)));
		return true;
	}
	static bool Math_Sign(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);
		NS_FLOAT x = FloatArgument(pN, 1);
		pN->ReturnValue(SignValue(x));
		return true;
	}
	static bool Math_Min(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);
		NS_FLOAT a = FloatArgument(pN, 1);
		NS_FLOAT b = FloatArgument(pN, 2);
		pN->ReturnValue(MinValue(a, b));
		return true;
	}
	static bool Math_Max(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);
		NS_FLOAT a = FloatArgument(pN, 1);
		NS_FLOAT b = FloatArgument(pN, 2);
		pN->ReturnValue(MaxValue(a, b));
		return true;
	}
	static bool Math_Repeat(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);
		pN->ReturnValue(NsRepeat(FloatArgument(pN, 1), FloatArgument(pN, 2)));
		return true;
	}
	static bool Math_PingPong(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);
		NS_FLOAT x = FloatArgument(pN, 1);
		NS_FLOAT length = FloatArgument(pN, 2);
		if (length > 0 && std::isfinite(length) && std::isfinite(x))
		{
			// 두 배 주기가 double 범위를 넘으면 유한 입력은 첫 왕복 안에 있다.
			if (length > (std::numeric_limits<double>::max)() * 0.5)
			{
				const double magnitude = ::fabs(x);
				pN->ReturnValue(magnitude <= length ? magnitude : length - (magnitude - length));
				return true;
			}
			const double period = static_cast<double>(length) * 2.0;
			const double t = ::fmod(::fabs(static_cast<double>(x)), period);
			pN->ReturnValue(static_cast<NS_FLOAT>(t <= length ? t : period - t));
			return true;
		}
		NS_FLOAT t = NsRepeat(x, length * (NS_FLOAT)2);
		pN->ReturnValue(length - ::fabs(t - length));
		return true;
	}
	static bool Math_WrapAngle(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);
		pN->ReturnValue(NsWrapAngle(FloatArgument(pN, 1)));
		return true;
	}
	static bool Math_DeltaAngle(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);
		const NS_FLOAT from = FloatArgument(pN, 1), to = FloatArgument(pN, 2);
		if (std::isfinite(from) && std::isfinite(to))
			pN->ReturnValue(NsAngleResult(NsWrapAngleWide(to) - NsWrapAngleWide(from)));
		else
			pN->ReturnValue(NsWrapAngle(to - from));
		return true;
	}
	static bool Math_LerpAngle(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 3);
		NS_FLOAT a = FloatArgument(pN, 1);
		NS_FLOAT b = FloatArgument(pN, 2), t = FloatArgument(pN, 3);
		if (std::isfinite(a) && std::isfinite(b) && std::isfinite(t))
		{
			if (t == 0) pN->ReturnValue(NsWrapAngle(a));
			else if (t == 1) pN->ReturnValue(NsWrapAngle(b));
			else
			{
				const double start = NsWrapAngleWide(a);
				const double delta = NsWrapAngleWide(NsWrapAngleWide(b) - start);
				// 곱이 넘칠 때만 매개변수를 한 주기 안으로 줄인다.
				const double product = delta * t;
				const double offset = std::isfinite(product) ? product : delta * ::fmod(t, kNsTwoPi / ::fabs(delta));
				pN->ReturnValue(NsAngleResult(start + NsWrapAngleWide(offset)));
			}
		}
		else
		{
			NS_FLOAT d = NsWrapAngle(b - a);
			pN->ReturnValue(NsWrapAngle(a + d * t));
		}
		return true;
	}
	static bool Math_MoveToward(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 3);
		pN->ReturnValue(NsMoveToward(FloatArgument(pN, 1), FloatArgument(pN, 2), FloatArgument(pN, 3)));
		return true;
	}
	static bool Math_MoveTowardAngle(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 3);
		NS_FLOAT cur = FloatArgument(pN, 1);
		NS_FLOAT target = FloatArgument(pN, 2), maxDelta = FloatArgument(pN, 3);
		if (std::isfinite(cur) && std::isfinite(target) && std::isfinite(maxDelta) && maxDelta >= 0)
		{
			const double start = NsWrapAngleWide(cur);
			const double delta = NsWrapAngleWide(NsWrapAngleWide(target) - start);
			pN->ReturnValue(::fabs(delta) <= maxDelta ? NsWrapAngle(target) :
				NsAngleResult(start + (delta > 0 ? static_cast<double>(maxDelta) : -static_cast<double>(maxDelta))));
		}
		else
		{
			NS_FLOAT d = NsWrapAngle(target - cur);
			pN->ReturnValue(NsWrapAngle(NsMoveToward(cur, cur + d, maxDelta)));
		}
		return true;
	}
	static bool Math_InverseLerp(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 3);
		NS_FLOAT a = FloatArgument(pN, 1);
		NS_FLOAT b = FloatArgument(pN, 2);
		NS_FLOAT v = FloatArgument(pN, 3);
		pN->ReturnValue(InverseLerpValue(a, b, v));
		return true;
	}
	static bool Math_Remap(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 5);
		NS_FLOAT v = FloatArgument(pN, 1);
		NS_FLOAT inMin = FloatArgument(pN, 2);
		NS_FLOAT inMax = FloatArgument(pN, 3);
		NS_FLOAT outMin = FloatArgument(pN, 4);
		NS_FLOAT outMax = FloatArgument(pN, 5);
		NS_FLOAT t = inMax == inMin ? (NS_FLOAT)0 : (v - inMin) / (inMax - inMin);
		pN->ReturnValue(outMin + (outMax - outMin) * t);
		return true;
	}
	static bool Math_SmoothStep(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 3);
		NS_FLOAT e0 = FloatArgument(pN, 1);
		NS_FLOAT e1 = FloatArgument(pN, 2);
		NS_FLOAT x = FloatArgument(pN, 3);
		pN->ReturnValue(SmoothStepValue(e0, e1, x));
		return true;
	}
	static bool Math_Distance2(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 4);
		NS_FLOAT dx = FloatArgument(pN, 3) - FloatArgument(pN, 1);
		NS_FLOAT dy = FloatArgument(pN, 4) - FloatArgument(pN, 2);
		pN->ReturnValue(::sqrt(dx * dx + dy * dy));
		return true;
	}
	static bool Math_Dot3(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);
		float a[3], b[3];
		ReadVec3(pN->GetStackVar(1), a);
		ReadVec3(pN->GetStackVar(2), b);
		pN->ReturnValue((NS_FLOAT)(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]));
		return true;
	}
	static bool Math_Length3(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);
		float v[3];
		ReadVec3(pN->GetStackVar(1), v);
		pN->ReturnValue(static_cast<NS_FLOAT>(static_cast<float>(::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]))));
		return true;
	}
	static bool Math_Distance3(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);
		float a[3], b[3];
		ReadVec3(pN->GetStackVar(1), a);
		ReadVec3(pN->GetStackVar(2), b);
		float dx = a[0] - b[0];
		float dy = a[1] - b[1];
		float dz = a[2] - b[2];
		pN->ReturnValue(::sqrt(dx * dx + dy * dy + dz * dz));
		return true;
	}
	static bool Math_Vector2(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);
		pN->Var_SetVec2(pN->GetReturnVar(), NeoNarrowFloat32(FloatArgument(pN, 1)), NeoNarrowFloat32(FloatArgument(pN, 2)));
		return true;
	}
	static bool Math_Vector3(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 3);
		pN->Var_SetVec3(pN->GetReturnVar(), NeoNarrowFloat32(FloatArgument(pN, 1)), NeoNarrowFloat32(FloatArgument(pN, 2)), NeoNarrowFloat32(FloatArgument(pN, 3)));
		return true;
	}
	static bool Math_Vector4(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 4);
		pN->Var_SetVec4(pN->GetReturnVar(), NeoNarrowFloat32(FloatArgument(pN, 1)), NeoNarrowFloat32(FloatArgument(pN, 2)),
			NeoNarrowFloat32(FloatArgument(pN, 3)), NeoNarrowFloat32(FloatArgument(pN, 4)));
		return true;
	}
	static bool Math_Quaternion(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 4);
		pN->Var_SetQuat(pN->GetReturnVar(), NeoNarrowFloat32(FloatArgument(pN, 1)), NeoNarrowFloat32(FloatArgument(pN, 2)),
			NeoNarrowFloat32(FloatArgument(pN, 3)), NeoNarrowFloat32(FloatArgument(pN, 4)));
		return true;
	}
	static NS_FLOAT MathClamp01Value(NS_FLOAT v)
	{
		if (v < (NS_FLOAT)0.0) return (NS_FLOAT)0.0;
		if (v > (NS_FLOAT)1.0) return (NS_FLOAT)1.0;
		return v;
	}
	static NS_FLOAT MathClampValue(NS_FLOAT v, NS_FLOAT minValue, NS_FLOAT maxValue)
	{
		if (v < minValue) return minValue;
		if (v > maxValue) return maxValue;
		return v;
	}
	// 아래 값 함수는 네이티브 래퍼와 워커의 직접 계산 경로(kScalarNatives)가 함께 쓴다. 한 곳에서만 고친다.
	static NS_FLOAT AbsValue(NS_FLOAT v) { return ::abs(v); }
	static NS_FLOAT DegValue(NS_FLOAT radian) { return ((radian) * (180.0f / MATH_PI)); }
	static NS_FLOAT RadValue(NS_FLOAT degree) { return ((degree) * (MATH_PI / 180.0f)); }
	static NS_FLOAT HypotValue(NS_FLOAT x, NS_FLOAT y) { return ::sqrt(x * x + y * y); }
	// C 의 fmod(x, 0) 은 NaN 이다. 스크립트에 NaN 을 흘리지 않는다.
	static NS_FLOAT FmodValue(NS_FLOAT x, NS_FLOAT y) { return y == (NS_FLOAT)0 ? (NS_FLOAT)0 : ::fmod(x, y); }
	static NS_FLOAT FractValue(NS_FLOAT x)
	{
		NS_FLOAT result = x - ::floor(x);
		// The exact fraction is below one, but a tiny negative input can round
		// up to one in binary32. Keep finite results inside the promised range.
		if (result == (NS_FLOAT)1.0) result = std::nextafter((NS_FLOAT)1.0, (NS_FLOAT)0.0);
		return result;
	}
	static NS_FLOAT SignValue(NS_FLOAT x) { return x > (NS_FLOAT)0 ? (NS_FLOAT)1 : (x < (NS_FLOAT)0 ? (NS_FLOAT)-1 : (NS_FLOAT)0); }
	static NS_FLOAT MinValue(NS_FLOAT a, NS_FLOAT b) { return a < b ? a : b; }
	static NS_FLOAT MaxValue(NS_FLOAT a, NS_FLOAT b) { return a > b ? a : b; }
	static NS_FLOAT SmoothStep01Value(NS_FLOAT v)
	{
		NS_FLOAT x = MathClamp01Value(v);
		return x * x * ((NS_FLOAT)3.0 - (NS_FLOAT)2.0 * x);
	}
	static NS_FLOAT LerpValue(NS_FLOAT a, NS_FLOAT b, NS_FLOAT t) { return a + (b - a) * t; }
	static NS_FLOAT InverseLerpValue(NS_FLOAT a, NS_FLOAT b, NS_FLOAT v) { return b == a ? (NS_FLOAT)0 : (v - a) / (b - a); }
	static NS_FLOAT SmoothStepValue(NS_FLOAT e0, NS_FLOAT e1, NS_FLOAT x)
	{
		NS_FLOAT t = e1 == e0 ? (x < e0 ? (NS_FLOAT)0 : (NS_FLOAT)1) : MathClamp01Value((x - e0) / (e1 - e0));
		return t * t * ((NS_FLOAT)3.0 - (NS_FLOAT)2.0 * t);
	}
	static bool Math_Clamp01(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);
		pN->ReturnValue(MathClamp01Value(FloatArgument(pN, 1)));
		return true;
	}
	static bool Math_Clamp(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 3);
		pN->ReturnValue(MathClampValue(FloatArgument(pN, 1), FloatArgument(pN, 2), FloatArgument(pN, 3)));
		return true;
	}
	static bool Math_SmoothStep01(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);
		pN->ReturnValue(SmoothStep01Value(FloatArgument(pN, 1)));
		return true;
	}
	static bool Math_Lerp(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 3);
		NS_FLOAT a = FloatArgument(pN, 1);
		NS_FLOAT b = FloatArgument(pN, 2);
		NS_FLOAT t = FloatArgument(pN, 3);
		pN->ReturnValue(LerpValue(a, b, t));
		return true;
	}
	// Read 는 BoundaryValue::GetVec* 로 벡터 값타입(VAR_VEC*)만 받는다 (리스트 폴백 없음).
	static void ReadVec3(ValueRef pVar, float out[3])
	{
		assert(pVar != nullptr && pVar.VectorComponentCount() == 3);
		pVar.CopyVectorComponents<3>(out);
	}
	// Write 는 워커 Var_SetVec3(release 처리)로 인라인 값타입을 반환한다(힙 리스트 할당 제거).
	static bool WriteVec3(CNeoVMWorker* pN, float x, float y, float z)
	{
		pN->Var_SetVec3(pN->GetReturnVar(), x, y, z);
		return true;
	}
	static void ReadQuat(ValueRef pVar, float out[4])
	{
		assert(pVar != nullptr && pVar.VectorComponentCount() == 4);
		pVar.CopyVectorComponents<4>(out);
	}
	// q 는 wxyz. 정규화한 뒤 반환한다 (인자는 건드리지 않는다 — 호출측이 계속 쓸 수 있게).
	static bool WriteQuat(CNeoVMWorker* pN, float w, float x, float y, float z)
	{
		float lenSq = w * w + x * x + y * y + z * z;
		if (lenSq < (float)0.000001)
		{
			pN->Var_SetQuat(pN->GetReturnVar(), 1.0f, 0.0f, 0.0f, 0.0f);
		}
		else
		{
			float invLen = (float)((float)1.0 / (float)::sqrt(lenSq));
			pN->Var_SetQuat(pN->GetReturnVar(), w * invLen, x * invLen, y * invLen, z * invLen);
		}
		return true;
	}
	static bool Math_Lerp3(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 3);

		float a[3], b[3];
		ReadVec3(pN->GetStackVar(1), a);
		ReadVec3(pN->GetStackVar(2), b);
		float t = NeoNarrowFloat32(FloatArgument(pN, 3));

		return WriteVec3(pN,
			(float)(a[0] + (b[0] - a[0]) * t),
			(float)(a[1] + (b[1] - a[1]) * t),
			(float)(a[2] + (b[2] - a[2]) * t));
	}
	static bool Math_DistanceSquared3(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);

		float a[3], b[3];
		ReadVec3(pN->GetStackVar(1), a);
		ReadVec3(pN->GetStackVar(2), b);

		float dx = a[0] - b[0];
		float dy = a[1] - b[1];
		float dz = a[2] - b[2];
		pN->ReturnValue(dx * dx + dy * dy + dz * dz);
		return true;
	}
	static bool Math_Normalize3(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);

		float v[3], fallback[3];
		ReadVec3(pN->GetStackVar(1), v);
		ReadVec3(pN->GetStackVar(2), fallback);
		float lenSq = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
		if (lenSq < (float)0.00000001)
			return WriteVec3(pN, fallback[0], fallback[1], fallback[2]);
		if (std::isinf(lenSq) && std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]))
		{
			// Only finite overflow needs the wider path. Preserve the usual float
			// calculation and its small-vector fallback threshold.
			const double x = v[0], y = v[1], z = v[2];
			const double invLen = 1.0 / ::sqrt(x * x + y * y + z * z);
			return WriteVec3(pN, static_cast<float>(x * invLen), static_cast<float>(y * invLen), static_cast<float>(z * invLen));
		}

		float invLen = (float)((float)1.0 / (float)::sqrt(lenSq));
		return WriteVec3(pN, v[0] * invLen, v[1] * invLen, v[2] * invLen);
	}
	static bool Math_Cross3(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);

		float a[3], b[3];
		ReadVec3(pN->GetStackVar(1), a);
		ReadVec3(pN->GetStackVar(2), b);
		return WriteVec3(pN,
			a[1] * b[2] - a[2] * b[1],
			a[2] * b[0] - a[0] * b[2],
			a[0] * b[1] - a[1] * b[0]);
	}
	static bool Math_RotateVectorByQuat(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);

		float q[4];   // wxyz
		ReadQuat(pN->GetStackVar(1), q);

		float v[3];
		ReadVec3(pN->GetStackVar(2), v);
		const float qw = q[0], qx = q[1], qy = q[2], qz = q[3];
		const float x = v[0], y = v[1], z = v[2];
		float tx = (float)2.0 * (qy * z - qz * y);
		float ty = (float)2.0 * (qz * x - qx * z);
		float tz = (float)2.0 * (qx * y - qy * x);
		return WriteVec3(pN,
			(float)(x + qw * tx + (qy * tz - qz * ty)),
			(float)(y + qw * ty + (qz * tx - qx * tz)),
			(float)(z + qw * tz + (qx * ty - qy * tx)));
	}
	// v[0..2] 를 제자리 정규화. 길이가 0 에 가까우면 실패(값은 그대로).
	static bool NormalizeVec3(float v[3])
	{
		float lenSq = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
		if (lenSq < (float)0.000001) return false;
		float invLen = (float)((float)1.0 / (float)::sqrt(lenSq));
		v[0] *= invLen;
		v[1] *= invLen;
		v[2] *= invLen;
		return true;
	}
	static bool Math_quat_from_basis(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 3);

		float right[3], up[3], fwd[3];
		ReadVec3(pN->GetStackVar(1), right);
		ReadVec3(pN->GetStackVar(2), up);
		ReadVec3(pN->GetStackVar(3), fwd);
		if (NormalizeVec3(right) == false) return false;
		if (NormalizeVec3(up) == false) return false;
		if (NormalizeVec3(fwd) == false) return false;

		float m00 = right[0], m01 = up[0], m02 = fwd[0];
		float m10 = right[1], m11 = up[1], m12 = fwd[1];
		float m20 = right[2], m21 = up[2], m22 = fwd[2];
		float w, x, y, z;
		float trace = m00 + m11 + m22;
		if (trace > (float)0.0)
		{
			float s = (float)::sqrt(trace + (float)1.0) * (float)2.0;
			w = (float)0.25 * s;
			x = (m21 - m12) / s;
			y = (m02 - m20) / s;
			z = (m10 - m01) / s;
		}
		else if (m00 > m11 && m00 > m22)
		{
			float s = (float)::sqrt((float)1.0 + m00 - m11 - m22) * (float)2.0;
			w = (m21 - m12) / s;
			x = (float)0.25 * s;
			y = (m01 + m10) / s;
			z = (m02 + m20) / s;
		}
		else if (m11 > m22)
		{
			float s = (float)::sqrt((float)1.0 + m11 - m00 - m22) * (float)2.0;
			w = (m02 - m20) / s;
			x = (m01 + m10) / s;
			y = (float)0.25 * s;
			z = (m12 + m21) / s;
		}
		else
		{
			float s = (float)::sqrt((float)1.0 + m22 - m00 - m11) * (float)2.0;
			w = (m10 - m01) / s;
			x = (m02 + m20) / s;
			y = (m12 + m21) / s;
			z = (float)0.25 * s;
		}

		return WriteQuat(pN, (float)w, (float)x, (float)y, (float)z);
	}
	static bool Math_quat_slerp(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 3);

		float a[4], b[4];   // wxyz
		ReadQuat(pN->GetStackVar(1), a);
		ReadQuat(pN->GetStackVar(2), b);
		float t = static_cast<float>(MathClamp01Value(NeoNarrowFloat32(FloatArgument(pN, 3))));

		float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
		if (dot < (float)0.0)
		{
			dot = -dot;
			b[0] = -b[0];
			b[1] = -b[1];
			b[2] = -b[2];
			b[3] = -b[3];
		}

		float scaleA;
		float scaleB;
		if (dot > (float)0.9995)
		{
			scaleA = (float)1.0 - t;
			scaleB = t;
		}
		else
		{
			float theta = (float)::acos(MathClampValue(dot, (float)-1.0, (float)1.0));
			float sinTheta = (float)::sin(theta);
			if (::fabs(sinTheta) < (float)0.000001)
			{
				scaleA = (float)1.0 - t;
				scaleB = t;
			}
			else
			{
				scaleA = (float)::sin(((float)1.0 - t) * theta) / sinTheta;
				scaleB = (float)::sin(t * theta) / sinTheta;
			}
		}

		return WriteQuat(pN,
			(float)(a[0] * scaleA + b[0] * scaleB),
			(float)(a[1] * scaleA + b[1] * scaleB),
			(float)(a[2] * scaleA + b[2] * scaleB),
			(float)(a[3] * scaleA + b[3] * scaleB));
	}
	static bool	Math_srand(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1 && pN->GetStack(1).GetType() == VAR_INT);

		int init = NeoNarrowInt(pN->GetStack(1)->_int);
		//::srand((u32)init);
		pN->m_sRand.seed(init);
		pN->ReturnValue();
		return true;
	}
	static bool	Math_rand(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 0);

		//pN->ReturnValue((int)::rand());
		pN->ReturnValue(pN->m_sRand.rnd());
		return true;
	}
	// 실수를 왕복 가능한 문자열로 만든다.
	//
	// 보통의 문자열 변환(".." 연결 포함)은 "%g" 라 유효숫자 6자리에서 잘린다. 화면과 로그에는
	// 그게 읽기 좋지만, 데이터 파일로 내보내면 원래 값을 되찾을 수 없다. double 은 17자리를
	// 적어야 정확히 왕복하므로 여기서는 "%.17g" 를 쓴다.
	//
	// 필요할 때만 지수 표기로 넘어가고(1.5e-07), 그 형태도 JSON 숫자 문법에 맞으므로
	// 그대로 파일에 넣어도 된다.
	static bool	Math_ToStr(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		char ch[64];
		snprintf(ch, sizeof(ch), "%.17g", (double)FloatArgument(pN, 1));
		pN->ReturnValue(ch);
		return true;
	}
	static bool Math_Fixed(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2 && pN->GetStack(2).GetType() == VAR_INT);
		const NS_INT decimals = pN->GetStack(2)->_int;
		if (decimals < 0 || decimals > 9)
		{
			pN->SetError("math.fixed decimals must be in [0, 9]");
			return false;
		}
		char text[FixedNumberBufferSize];
		const NS_FLOAT value = FloatArgument(pN, 1);
		// 특수값은 tostring과 같은 %g 표기를 쓰되 std::string 임시는 만들지 않는다.
		if (!FormatFixedNumber(value, static_cast<unsigned>(decimals), text))
			snprintf(text, sizeof(text), "%g", static_cast<double>(value));
		pN->ReturnValue(text);
		return true;
	}
	static bool Math_Rand01(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 0);
		pN->ReturnValue((NS_FLOAT)pN->m_sRand.rnd() / (NS_FLOAT)32767.0);
		return true;
	}
	static bool Math_RandRange(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);
		NS_FLOAT a = FloatArgument(pN, 1);
		NS_FLOAT b = FloatArgument(pN, 2);
		NS_FLOAT t = (NS_FLOAT)pN->m_sRand.rnd() / (NS_FLOAT)32767.0;
		pN->ReturnValue(a + (b - a) * t);
		return true;
	}
	static bool Math_Hash32(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1 && pN->GetStack(1).GetType() == VAR_INT);

		// Keep the exact unsigned 32-bit overflow behavior used by native gameplay code.
		std::uint32_t value = static_cast<std::uint32_t>(pN->GetStack(1)->_int);
		value ^= value >> 16;
		value *= 0x7feb352du;
		value ^= value >> 15;
		value *= 0x846ca68bu;
		value ^= value >> 16;
		pN->ReturnValue(static_cast<int>(value));
		return true;
	}
	static int MathColorByte(NS_FLOAT value)
	{
		// 색 채널의 NaN은 0으로 변환하고, 나머지는 기존 0..1 제한과 소수부 버림을 유지한다.
		return static_cast<int>(NeoFloatToInt(MathClamp01Value(value) * (NS_FLOAT)255.0));
	}
	static bool Math_ColorRGB(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 3);
		const int r = MathColorByte(FloatArgument(pN, 1));
		const int g = MathColorByte(FloatArgument(pN, 2));
		const int b = MathColorByte(FloatArgument(pN, 3));
		const int color = -16777216 + b * 65536 + g * 256 + r;
		pN->ReturnValue(color);
		return true;
	}
	static bool Math_ColorARGB(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 4);
		const int a = MathColorByte(FloatArgument(pN, 1));
		const int r = MathColorByte(FloatArgument(pN, 2));
		const int g = MathColorByte(FloatArgument(pN, 3));
		const int b = MathColorByte(FloatArgument(pN, 4));
		const int rgb = b * 65536 + g * 256 + r;
		// 각 채널은 0..255다. 알파를 먼저 부호 범위로 옮겨 int32 오버플로를 피한다.
		const int color = a < 128 ? a * 16777216 + rgb : (a - 256) * 16777216 + rgb;
		pN->ReturnValue(color);
		return true;
	}

	static bool map_len(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_MAP);
		assert(args == 0);

		pN->ReturnValue((int)pVar->_tbl->GetCount());
		return true;
	}
	static bool map_reserve(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_MAP);
		assert(args == 1 && pN->GetStack(1).GetType() == VAR_INT);

		int size;
		if (!ReadCollectionSize(pN, 1, size)) return false;
		pVar->_tbl->Reserve(size);
		pN->ReturnValue();
		return true;
	}
	static bool map_sort(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(pVar.GetType() == VAR_MAP && args == 1);
		ValueRef compare = pN->GetStack(1);
		assert(compare.GetType() == VAR_NONE || compare.GetType() == VAR_FUN || compare.GetType() == VAR_CLOSURE);
		if (compare.GetType() == VAR_NONE) return false;
		// The native sort cannot suspend and resume its C++ frame. Never disable
		// a host's time limit to run a comparator: it may loop forever.
		if (pN->IsSynchronousExecutionLimited())
		{
			pN->SetError("map.sort is not allowed during time-limited execution");
			return false;
		}
		HeldValue receiver(pN, pVar), callback(pN, compare);
		// A synchronous native sort cannot preserve its C++ frame across a
		// script suspension or coroutine switch. Use the VM's nested-call guard.
		struct SynchronousCall
		{
			CNeoVMWorker* worker;
			explicit SynchronousCall(CNeoVMWorker* value) : worker(value) { worker->BeginNestedScriptCall(); }
			~SynchronousCall() { worker->EndNestedScriptCall(); }
		} synchronousCall(pN);
		MapInfo* table = receiver.value._tbl;
		std::vector<ValueRef> slots;
		if (!table->ToListValues(slots)) return false;
		const size_t count = slots.size();
		const u32 version = table->_mutationVersion;
		struct Snapshot
		{
			CNeoVMWorker* worker;
			std::vector<BoundaryValue> values;
			~Snapshot() { for (auto& value : values) worker->Var_Release(&value); }
		} snapshot{pN, std::vector<BoundaryValue>(count)};
		std::vector<size_t> order(count), next(count);
		for (size_t i = 0; i < count; ++i)
		{
			Move_DestNoRelease(&snapshot.values[i], slots[i]);
			order[i] = i;
		}
		slots.clear(); // No bucket pointer may survive a script callback.
		// Stable, bounded mergesort: even an inconsistent script comparator must
		// not violate std::sort's strict-weak-ordering precondition or read past
		// a buffer. Never retain a list bucket or VM stack pointer across calls.
		for (size_t width = 1; width < count; width *= 2)
		{
			for (size_t begin = 0; begin < count; begin += width * 2)
			{
				const size_t middle = std::min(begin + width, count);
				const size_t end = std::min(begin + width * 2, count);
				size_t left = begin, right = middle;
				for (size_t dest = begin; dest < end; ++dest)
				{
					bool takeRight = left == middle;
					if (left < middle && right < end)
					{
						BoundaryValue callArgs[2] = {snapshot.values[order[right]], snapshot.values[order[left]]};
						ValueRef result = pN->testCall(&callback.value, callArgs, 2);
						if (pN->GetVM()->IsLastErrorMsg()) return false;
						if (table->_mutationVersion != version)
						{
							pN->SetError("map was modified during sort");
							return false;
						}
						if (result == nullptr)
						{
							pN->SetError("map sort comparator must accept two arguments");
							return false;
						}
						assert(result.GetType() == VAR_BOOL);
						takeRight = result->_bl;
					}
					next[dest] = takeRight ? order[right++] : order[left++];
				}
			}
			order.swap(next);
		}
		if (count > 1)
		{
			if (!table->ToListValues(slots) || slots.size() != count) return false;
			for (size_t i = 0; i < count; ++i)
				pN->Move(slots[i], &snapshot.values[order[i]]);
		}
		pN->ReturnValue();
		return true;
	}
	static bool map_keys(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 0);
		assert(pVar.GetType() == VAR_MAP);

		std::vector<ValueRef> lst;
		if (false == pVar->_tbl->ToListKeys(lst)) return false;
		
		ValueRef pRet = pN->GetStack(0);
		ListInfo* pR = pN->GetVM()->ListAlloc();
		pN->Var_SetList(pRet, pR); // return value
		pR->Resize((int)lst.size());

		ValueRef dest = pR->GetDataUnsafe();
		bool mayContainContainerChild = false;
		for (int i = 0; i < (int)lst.size(); i++)
		{
			mayContainContainerChild |= lst[i].IsContainerType();
			Move_DestNoRelease(&dest[i], lst[i]);
		}
		if (mayContainContainerChild)
			pR->MarkContainerChild();
		return true;
	}
	static bool map_values(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 0);
		assert(pVar.GetType() == VAR_MAP);

		std::vector<ValueRef> lst;
		if (false == pVar->_tbl->ToListValues(lst)) return false;

		ValueRef pRet = pN->GetStack(0);
		ListInfo* pR = pN->GetVM()->ListAlloc();
		pN->Var_SetList(pRet, pR); // return value
		pR->Resize((int)lst.size());

		ValueRef dest = pR->GetDataUnsafe();
		bool mayContainContainerChild = false;
		for (int i = 0; i < (int)lst.size(); i++)
		{
			mayContainContainerChild |= lst[i].IsContainerType();
			Move_DestNoRelease(&dest[i], lst[i]);
		}
		if (mayContainContainerChild)
			pR->MarkContainerChild();
		return true;
	}

	static bool async_get(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		if (pN->IsNativeScriptCallActive())
		{
			pN->SetErrorFormat(RTE_NESTED_NOT_ALLOWED, "async.get");
			return false;
		}
		assert(args == 3);
		assert(pVar.GetType() == VAR_ASYNC);
		AsyncInfo* pAsync = pVar->_async;
		if(pAsync->_state != ASYNC_READY) return false;

		ValueRef v1 = pN->GetStack(1);
		assert(v1.GetType() == VAR_INT);

		ValueRef v2 = pN->GetStack(2);
		assert(v2.GetType() == VAR_STRING || v2.GetType() == VAR_NONE);
		if (v2.GetType() == VAR_NONE) return false;

		ValueRef v3 = pN->GetStack(3);
		assert(v3.GetType() == VAR_NONE || v3.GetType() == VAR_FUN || v3.GetType() == VAR_CLOSURE);
		if (v3.GetType() == VAR_NONE) return false;

		pAsync->_type = ASYNC_GET;
		pAsync->_timeout = NeoNarrowInt(v1->_int);
		pAsync->_request = v2->_str->_str;
		pAsync->_fun_index = (v3.GetType() == VAR_FUN) ? v3.FunctionIndex() : v3->_closure->_funIndex;
		pN->Move(&pAsync->_callback, v3);

		pAsync->_ownerWorkerId = pN->GetWorkerID();
		++pN->_asyncPendingCount;
		pAsync->_event.reset();
		pAsync->_state = ASYNC_PENDING;
		if (pVar.IsContainerType() || v3.IsContainerType()) pAsync->_cycleState._mayContainContainerChild = true;
		pN->Move(&pAsync->_LockReferance, pVar);
		pN->GetVM()->AddHttp_Request(pAsync);
		pN->ReturnValue();
		return true;
	}
	static bool async_post(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		if (pN->IsNativeScriptCallActive())
		{
			pN->SetErrorFormat(RTE_NESTED_NOT_ALLOWED, "async.post");
			return false;
		}
		assert(args == 4);
		assert(pVar.GetType() == VAR_ASYNC);
		AsyncInfo* pAsync = pVar->_async;
		if (pAsync->_state != ASYNC_READY) return false;

		ValueRef v1 = pN->GetStack(1);
		assert(v1.GetType() == VAR_INT);

		ValueRef v2 = pN->GetStack(2);
		assert(v2.GetType() == VAR_STRING || v2.GetType() == VAR_NONE);
		if (v2.GetType() == VAR_NONE) return false;

		ValueRef v3 = pN->GetStack(3);
		assert(v3.GetType() == VAR_STRING || v3.GetType() == VAR_NONE);
		if (v3.GetType() == VAR_NONE) return false;

		ValueRef v4 = pN->GetStack(4);
		assert(v4.GetType() == VAR_NONE || v4.GetType() == VAR_FUN || v4.GetType() == VAR_CLOSURE);
		if (v4.GetType() == VAR_NONE) return false;

		pAsync->_type = ASYNC_POST;
		pAsync->_timeout = NeoNarrowInt(v1->_int);
		pAsync->_request = v2->_str->_str;
		pAsync->_body = v3->_str->_str;
		pAsync->_fun_index = (v4.GetType() == VAR_FUN) ? v4.FunctionIndex() : v4->_closure->_funIndex;
		pN->Move(&pAsync->_callback, v4);

		pAsync->_ownerWorkerId = pN->GetWorkerID();
		++pN->_asyncPendingCount;
		pAsync->_event.reset();
		pAsync->_state = ASYNC_PENDING;
		if (pVar.IsContainerType() || v4.IsContainerType()) pAsync->_cycleState._mayContainContainerChild = true;
		pN->Move(&pAsync->_LockReferance, pVar);
		pN->GetVM()->AddHttp_Request(pAsync);
		pN->ReturnValue();
		return true;
	}

	static bool async_add_header(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);
		assert(pVar.GetType() == VAR_ASYNC);
		AsyncInfo* pAsync = pVar->_async;
		if (pAsync->_state != ASYNC_READY) return false;

		ValueRef v1 = pN->GetStack(1);
		assert(v1.GetType() == VAR_STRING || v1.GetType() == VAR_NONE);
		if (v1.GetType() == VAR_NONE) return false;

		ValueRef v2 = pN->GetStack(2);
		assert(v2.GetType() == VAR_STRING || v2.GetType() == VAR_NONE);
		if (v2.GetType() == VAR_NONE) return false;

		std::pair<std::string, std::string> header = { v1->_str->_str, v2->_str->_str };
		pAsync->_headers.push_back(header);
		pN->ReturnValue();
		return true;
	}
	static bool async_wait(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		if (pN->IsNativeScriptCallActive())
		{
			pN->SetErrorFormat(RTE_NESTED_NOT_ALLOWED, "async.wait");
			return false;
		}
		assert(args == 0);
		assert(pVar.GetType() == VAR_ASYNC);
		AsyncInfo* pAsync = pVar->_async;
		if (pAsync->_state != ASYNC_PENDING)
		{
			const bool completed = pAsync->_state == ASYNC_COMPLETED;
			if (completed) pN->JumpAsyncMsg();
			pN->ReturnValue(completed);
			return true;
		}

		bool ok = pAsync->_event.wait(pAsync->_timeout);
		pN->JumpAsyncMsg();
		pN->ReturnValue(ok);
		return true;
	}
	static bool async_close(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 0);
		assert(pVar.GetType() == VAR_ASYNC);
		pN->ReturnValue();
		return true;
	}

	static bool core_fail(CNeoVMWorker* pN, ValueRef, short args)
	{
		ValueRef pArg1 = args >= 1 ? pN->GetStack(1) : nullptr;
		if (pArg1 != nullptr && pArg1.GetType() == VAR_STRING)
			pN->SetError(pArg1->_str->_str.c_str());
		else
			pN->SetError("script failure");
		return false;
	}
	static bool io_print(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		if (args == 1)
		{
			ValueRef pArg1 = pN->GetStack(1);
			std::string str = CNeoVMWorker::ToString(pArg1);
			if(NeoVMSystem::m_pFunPrint == nullptr)
				std::cout << str.c_str() << '\n';
			else
				NeoVMSystem::m_pFunPrint(str.c_str());
			pN->ReturnValue();
			return true;
		}
		else if (args == 2)
		{
			ValueRef pArg1 = pN->GetStack(1);
			ValueRef pArg2 = pN->GetStack(2);
			std::string str1 = CNeoVMWorker::ToString(pArg1);
			std::string str2 = CNeoVMWorker::ToString(pArg2);
			if (NeoVMSystem::m_pFunPrint == nullptr)
				std::cout << str1.c_str() << str2.c_str();
			else
				NeoVMSystem::m_pFunPrint((str1 + str2).c_str());
			pN->ReturnValue();
			return true;
		}
		return false;
	}

	static bool sys_time(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 0);

		// time_t가 32비트인 호스트에서도 초 값을 중간에 잘라내지 않는다.
		const auto now = std::chrono::system_clock::now().time_since_epoch();
		pN->ReturnValue(static_cast<NS_INT>(std::chrono::duration_cast<std::chrono::seconds>(now).count()));
		return true;
	}
	static bool DateDirective(char code, char modifier)
	{
		if (code == 0 || std::strchr("aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%", code) == nullptr)
			return false;
		if (modifier == 0) return true;
#ifdef _WIN32
		// The Windows CRT accepts # but treats unsupported E/O as an invalid
		// parameter, which may terminate the host before we can report an error.
		return modifier == '#';
#else
		if (modifier == 'E') return std::strchr("cCxXyY", code) != nullptr;
		if (modifier == 'O') return std::strchr("deHImMSuUVwWy", code) != nullptr;
		return false;
#endif
	}
	static bool DateFormat(const std::string& format, const std::tm& timeinfo, std::string& result, bool utc = false)
	{
		// Keep literal UTF-8 bytes out of locale-dependent narrow strftime.
		// Wide directive output also preserves localized month/time-zone names.
		for (size_t pos = 0; pos < format.size();)
		{
			if (format[pos] != '%')
			{
				if (format[pos] == 0 || result.size() == 79) return false;
				result += format[pos++];
				continue;
			}
			++pos;
			char modifier = 0;
			if (pos < format.size() && (format[pos] == '#' || format[pos] == 'E' || format[pos] == 'O'))
				modifier = format[pos++];
			if (pos == format.size() || !DateDirective(format[pos], modifier)) return false;
			// UTC에서는 CRT가 프로세스의 로컬 시간대를 참조하지 않도록 직접 출력한다.
			if (utc && (format[pos] == 'z' || format[pos] == 'Z'))
			{
				const std::string zone = format[pos++] == 'z' ? "+0000" : "UTC";
				if (zone.size() > 79 - result.size()) return false;
				result += zone;
				continue;
			}
			wchar_t directive[5] = {L'!', L'%', 0, 0, 0};
			int end = 2;
			if (modifier) directive[end++] = static_cast<wchar_t>(modifier);
			directive[end] = static_cast<wchar_t>(format[pos++]);
			wchar_t buffer[81];
			// A sentinel distinguishes valid empty %z/%Z output from overflow.
			const size_t count = std::wcsftime(buffer, 81, directive, &timeinfo);
			if (count == 0) return false;
			std::u16string utf16;
			for (size_t i = 1; i < count; ++i)
			{
				const u32 point = static_cast<u32>(buffer[i]);
				if (point <= 0xFFFF) utf16 += static_cast<char16_t>(point);
				else if (point <= 0x10FFFF)
				{
					utf16 += static_cast<char16_t>(0xD800 + ((point - 0x10000) >> 10));
					utf16 += static_cast<char16_t>(0xDC00 + ((point - 0x10000) & 0x3FF));
				}
				else return false;
			}
			std::string text;
			if (utf_string::UNICODE_UTF8(utf16.data(), static_cast<int>(utf16.size()), text) < 0 ||
				text.size() > 79 - result.size()) return false;
			result += text;
		}
		return true;
	}
	static bool sys_utc_date(CNeoVMWorker* worker, ValueRef, short args)
	{
		assert(args == 2 && worker->GetStack(2).GetType() == VAR_INT);
		assert(worker->GetStack(1).GetType() == VAR_STRING || worker->GetStack(1).GetType() == VAR_NONE);
		if (worker->GetStack(1).GetType() == VAR_NONE) return false;
		std::tm calendar;
		if (!TimeLibrary::UtcCalendar(worker->GetStack(2)->_int, calendar))
		{
			worker->SetError("system.utc_date timestamp must be in years 1900 through 9999");
			return false;
		}
		std::string result;
		if (!DateFormat(worker->GetStack(1)->_str->_str, calendar, result, true))
		{
			worker->SetError("invalid system.utc_date format or output exceeds 79 bytes");
			return false;
		}
		worker->ReturnValue(result.c_str());
		return true;
	}
	static bool sys_date(CNeoVMWorker* worker, ValueRef, short args)
	{
		assert(args == 2 && worker->GetStack(2).GetType() == VAR_INT);
		assert(worker->GetStack(1).GetType() == VAR_STRING || worker->GetStack(1).GetType() == VAR_NONE);
		if (worker->GetStack(1).GetType() == VAR_NONE) return false;
		const NS_INT seconds = worker->GetStack(2)->_int;
		std::tm calendar = std::tm();
		bool valid = false;
#ifdef _WIN32
		// CRT가 지원하지 않는 시각은 변환 전에 거부한다. 로컬 달력은 해당 시각의 서머타임을 따른다.
		if (seconds >= 0 && seconds <= 32503679999LL)
		{
			const __time64_t timestamp = static_cast<__time64_t>(seconds);
			valid = _localtime64_s(&calendar, &timestamp) == 0;
		}
#else
		if (seconds >= -2208988800LL && seconds <= 253402300799LL &&
			seconds >= (std::numeric_limits<std::time_t>::min)() &&
			seconds <= (std::numeric_limits<std::time_t>::max)())
		{
			const std::time_t timestamp = static_cast<std::time_t>(seconds);
			valid = localtime_r(&timestamp, &calendar) != nullptr;
		}
#endif
		if (!valid || calendar.tm_year < 0 || calendar.tm_year > 8099)
		{
			worker->SetError("system.date timestamp is outside the host local calendar range");
			return false;
		}
		std::string result;
		if (!DateFormat(worker->GetStack(1)->_str->_str, calendar, result))
		{
			worker->SetError("invalid system.date format or output exceeds 79 bytes");
			return false;
		}
		worker->ReturnValue(result.c_str());
		return true;
	}
	static bool sys_clock(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 0);

		pN->ReturnValue(NS_FLOAT((double)clock() / (double)CLOCKS_PER_SEC));
		return true;
	}
	static bool sys_array32(CNeoVMWorker* pN, ValueRef, short args)
	{
		assert(args == 2);
		ValueRef initial = pN->GetStack(1);
		ValueRef size = pN->GetStack(2);
		assert(size.GetType() == VAR_INT);
		if (size->_int < 0 || size->_int > INT32_MAX) return false;
		assert(initial.IsNumber());
		if (initial.GetType() == VAR_INT && (initial->_int < INT32_MIN || initial->_int > INT32_MAX))
		{
			pN->SetError("int32 array value out of range");
			return false;
		}
		const NeoArrayElementType type = initial.GetType() == VAR_INT ? NeoArrayElementType::Int32 : NeoArrayElementType::Float32;
		ArrayInfo* array = pN->GetVM()->ArrayAlloc(type, static_cast<int>(size->_int), *initial);
		if (!array) return false;
		pN->Var_SetArray(pN->GetStack(0), array);
		return true;
	}
	static bool sys_array(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 2);

		ValueRef initialValue = pN->GetStack(1);
		ValueRef sizeValue = pN->GetStack(2);
		assert(sizeValue.GetType() == VAR_INT);
		if (sizeValue->_int < 0 || sizeValue->_int > INT_MAX)
			return false;

		NeoArrayElementType elementType;
		switch (initialValue.GetType())
		{
		case VAR_INT:   elementType = NeoArrayElementType::Int; break;
		case VAR_FLOAT: elementType = NeoArrayElementType::Float; break;
		case VAR_BOOL:  elementType = NeoArrayElementType::Bool; break;
		case VAR_VEC:
		case VAR_VEC3:
		case VAR_VEC4:
			switch (initialValue.VectorComponentCount())
			{
			case 2: elementType = NeoArrayElementType::Vector2; break;
			case 3: elementType = NeoArrayElementType::Vector3; break;
			case 4: elementType = NeoArrayElementType::Vector4; break;
			default: return false;
			}
			break;
		default:        return false;
		}

		ArrayInfo* array = pN->GetVM()->ArrayAlloc(elementType, static_cast<int>(sizeValue->_int), *initialValue);
		if (array == nullptr)
			return false;
		pN->Var_SetArray(pN->GetStack(0), array);
		return true;
	}
	// system.load(source, name) — arg1 = 컴파일할 소스 텍스트, arg2 = 청크 이름(현재 미사용).
    static bool sys_jsonEncode(CNeoVMWorker* worker, ValueRef, short args)
    {
        JsonResult result; std::string text;
        assert(args >= 1 && args <= 2 && (args != 2 || worker->GetStack(2).GetType() == VAR_BOOL));
        JsonInternal::Encode(*worker->GetStack(1), text, result, args == 2 && worker->GetStack(2)->_bl);
        if (result.error.empty()) worker->Var_SetStringA(worker->GetStack(0), text);
        else worker->ReturnValue();
        return true;
    }
    static bool sys_jsonDecode(CNeoVMWorker* worker, ValueRef, short args)
    {
        JsonResult result;
        assert(args == 1);
        assert(worker->GetStack(1).GetType() == VAR_STRING || worker->GetStack(1).GetType() == VAR_NONE);
        if (worker->GetStack(1).GetType() == VAR_NONE)
            result.Fail("$", "jsonDecode expects a string");
        else {
            const auto& text = worker->GetStack(1)->_str->_str;
            JsonInternal::Decode(*worker, text.data(), text.size(), *worker->GetStack(0), result);
        }
        if (!result.error.empty()) worker->ReturnValue();
        return true;
    }
	static bool coroutine_create(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		ValueRef v = pN->GetStack(1);
		assert(v.GetType() == VAR_NONE || v.GetType() == VAR_FUN || v.GetType() == VAR_CLOSURE);
		if (v.GetType() == VAR_NONE) return false;

		CNeoVM* pVM = pN->GetVM();
		CoroutineInfo* pCI = pVM->CoroutineAlloc();
		pCI->_refCount = 0;
		pCI->_fun_index = (v.GetType() == VAR_FUN) ? v.FunctionIndex() : v->_closure->_funIndex;
		pN->Move(&pCI->_function, v);
		pCI->_state = COROUTINE_STATE_SUSPENDED;

		pN->ReturnValue(pCI);
		return true;
	}
	static bool coroutine_resume(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		if (pN->IsNativeScriptCallActive())
		{
			pN->SetErrorFormat(RTE_NESTED_NOT_ALLOWED, "coroutine.resume");
			return false;
		}
		assert(args >= 1);

		ValueRef v = pN->GetStack(1);
		assert(v.GetType() == VAR_COROUTINE || v.GetType() == VAR_NONE);
		if (v.GetType() == VAR_NONE) return false;

		CoroutineInfo* pCI = v->_cor;
		if (pCI->_state != COROUTINE_STATE_SUSPENDED) return false;

		pN->m_pRegisterActive = pCI;
		pCI->_sub_state = COROUTINE_SUB_START;
		pN->ReturnValue();
		return true;
	}
	static bool coroutine_status(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		ValueRef v = pN->GetStack(1);
		assert(v.GetType() == VAR_COROUTINE || v.GetType() == VAR_NONE);
		if (v.GetType() == VAR_NONE) return false;

		CNeoVM* pVM = pN->GetVM();
		switch (v->_cor->_state)
		{
		case COROUTINE_STATE_SUSPENDED:
			pN->ReturnValue(&pVM->m_sDefaultValue[NDF_SUSPENDED]);
			break;
		case COROUTINE_STATE_RUNNING:
			pN->ReturnValue(&pVM->m_sDefaultValue[NDF_RUNNING]);
			break;
		case COROUTINE_STATE_DEAD:
			pN->ReturnValue(&pVM->m_sDefaultValue[NDF_DEAD]);
			break;
		case COROUTINE_STATE_NORMAL:
			pN->ReturnValue(&pVM->m_sDefaultValue[NDF_NORMAL]);
			break;
		default:
			pN->ReturnValue();
			return false;
		}

		return true;
	}
	static bool coroutine_close(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		if (pN->IsNativeScriptCallActive())
		{
			pN->SetErrorFormat(RTE_NESTED_NOT_ALLOWED, "coroutine.close");
			return false;
		}
		assert(args < 2);

		CoroutineInfo* pCI;
		if(args == 1)
		{
			ValueRef v = pN->GetStack(1);
			assert(v.GetType() == VAR_COROUTINE || v.GetType() == VAR_NONE);
			if (v.GetType() == VAR_NONE) return false;
			pCI = v->_cor;
			if(pCI->_state == COROUTINE_STATE_DEAD)
			{
				return true;
			}
		}
		else
		{
			pCI = pN->m_pCur;
		}

		if (pN->IsMainCoroutine(pCI))
		{
			pN->SetError("coroutine.close cannot close the main context");
			return false;
		}

		pN->m_pRegisterActive = pCI;
		pCI->_sub_state = COROUTINE_SUB_CLOSE;

		return true;
	}

	static bool sys_aysnc_create(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 0);

		AsyncInfo* p = pN->GetVM()->AsyncAlloc();
		pN->ReturnValue(p);
		return true;
	}	

	static bool sys_set(CNeoVMWorker* pN, ValueRef pVar, short args)
	{
		assert(args == 1);

		ValueRef pRet = pN->GetStack(0);
		ValueRef pArg1 = pN->GetStack(1);
		switch(pArg1.GetType())
		{
		case VAR_LIST:
			{
				SetInfo* pSetR = pN->GetVM()->SetAlloc();
				pN->Var_SetSet(pRet, pSetR);

				ListInfo* pListV1 = pArg1->_lst;
				int sz = pListV1->GetCount();
				ValueRef src = pListV1->GetDataUnsafe();
				for (int i = 0; i < sz; i++)
				{
					if (src[i].GetType() == VAR_NONE) { pN->SetError("set element cannot be null"); return false; }
					if (!pSetR->Insert(&src[i])) return false;
				}
				return true;
			}
		default:
			pN->SetErrorFormat(RTE_SET_UNSUPPORTED, GetDataType(pArg1.GetType()).c_str());
			return false;
		}
		pN->ReturnValue();
		return false;
	}
};



//typedef bool (ClassName::*TYPE_NeoLib)(CNeoVMWorker* pN, short args);
//typedef bool(*TYPE_NeoLib)(CNeoVMWorker* pN, short args);
typedef bool(*TYPE_NeoLib)(CNeoVMWorker* pN, ValueRef pVar, short args);

static VMHash<TYPE_NeoLib> g_sNeoFunLib_Default;
static VMHash<TYPE_NeoLib> g_sNeoFunLib_List;
static VMHash<TYPE_NeoLib> g_sNeoFunLib_Array;
static VMHash<TYPE_NeoLib> g_sNeoFunLib_String;
static VMHash<TYPE_NeoLib> g_sNeoFunLib_Map;
static VMHash<TYPE_NeoLib> g_sNeoFunLib_Set;
static VMHash<TYPE_NeoLib> g_sNeoFunLib_Async;
static std::vector<TYPE_NeoLib> g_sNeoFunLib_DefaultNative;
static std::vector<CNeoVM::ScalarNativeFunction> g_sNeoFunLib_DefaultScalar;
static std::vector<CNeoVM::ScalarNativeFunction2> g_sNeoFunLib_DefaultScalar2;
static std::vector<CNeoVM::ScalarNativeFunction3> g_sNeoFunLib_DefaultScalar3;
static std::vector<u8> g_sNeoFunLib_DefaultIntrinsic; // native index 별 intrinsic opcode (기본 NOP_NONE)
static std::unordered_map<std::string, int> g_sNeoFunLib_DefaultNativeIndex;

bool CNeoVM::_funInitLib = false;
FunctionPtrNative CNeoVM::_funLib_Default;
FunctionPtrNative CNeoVM::_funLib_List;
FunctionPtrNative CNeoVM::_funLib_Array;
FunctionPtrNative CNeoVM::_funLib_String;
FunctionPtrNative CNeoVM::_funLib_Map;
FunctionPtrNative CNeoVM::_funLib_Set;
FunctionPtrNative CNeoVM::_funLib_Async;


static bool Fun_Default(INeoVMWorker* pN, void* pUserData, const VMString* pStr, short args)
{
	TYPE_NeoLib f;
	if(false == g_sNeoFunLib_Default.TryGetValue(pStr, &f))
		return false;

	return (*f)((CNeoVMWorker*)pN, *static_cast<ValueRef*>(pUserData), args);
}
static bool Fun_String(INeoVMWorker* pN, void* pUserData, const VMString* pStr, short args)
{
	TYPE_NeoLib f;
	if (false == g_sNeoFunLib_String.TryGetValue(pStr, &f))
		return false;

	return (*f)((CNeoVMWorker*)pN, *static_cast<ValueRef*>(pUserData), args);
}
static bool Fun_List(INeoVMWorker* pN, void* pUserData, const VMString* pStr, short args)
{
	TYPE_NeoLib f;
	if (false == g_sNeoFunLib_List.TryGetValue(pStr, &f))
		return false;

	return (*f)((CNeoVMWorker*)pN, *static_cast<ValueRef*>(pUserData), args);
}
static bool Fun_Array(INeoVMWorker* pN, void* pUserData, const VMString* pStr, short args)
{
	TYPE_NeoLib f;
	if (false == g_sNeoFunLib_Array.TryGetValue(pStr, &f))
		return false;

	return (*f)((CNeoVMWorker*)pN, *static_cast<ValueRef*>(pUserData), args);
}
static bool Fun_Map(INeoVMWorker* pN, void* pUserData, const VMString* pStr, short args)
{
	TYPE_NeoLib f;
	if (false == g_sNeoFunLib_Map.TryGetValue(pStr, &f))
		return false;

	return (*f)((CNeoVMWorker*)pN, *static_cast<ValueRef*>(pUserData), args);
}
static bool Fun_Set(INeoVMWorker* pN, void* pUserData, const VMString* pStr, short args)
{
	TYPE_NeoLib f;
	if (!g_sNeoFunLib_Set.TryGetValue(pStr, &f)) return false;
	return (*f)((CNeoVMWorker*)pN, *static_cast<ValueRef*>(pUserData), args);
}
static bool Fun_Async(INeoVMWorker* pN, void* pUserData, const VMString* pStr, short args)
{
	TYPE_NeoLib f;
	if (false == g_sNeoFunLib_Async.TryGetValue(pStr, &f))
		return false;

	return (*f)((CNeoVMWorker*)pN, *static_cast<ValueRef*>(pUserData), args);
}

std::map<std::string, std::list< SystemFun>> g_sSystemFuns;
std::string g_sCurrentSystem;

// 직접 계산이 허용되는 순수 수학 네이티브. 래퍼(Math_*)와 같은 값 함수를 가리켜 두 경로의 결과가 같다.
// 인자 수에 맞는 칸 하나만 채운다. rand·time 처럼 상태를 쓰는 함수와 벡터·각도 보간처럼 분기가 긴 함수는 넣지 않는다.
struct ScalarNativeEntry
{
	TYPE_NeoLib fun;
	CNeoVM::ScalarNativeFunction f1;
	CNeoVM::ScalarNativeFunction2 f2;
	CNeoVM::ScalarNativeFunction3 f3;
};
typedef NS_FLOAT (*LibmUnary)(NS_FLOAT);
typedef NS_FLOAT (*LibmBinary)(NS_FLOAT, NS_FLOAT);
static const ScalarNativeEntry kScalarNatives[] = {
	{ &neo_libs::Math_abs,   &neo_libs::AbsValue, nullptr, nullptr },
	{ &neo_libs::Math_acos,  static_cast<LibmUnary>(&::acos), nullptr, nullptr },
	{ &neo_libs::Math_asin,  static_cast<LibmUnary>(&::asin), nullptr, nullptr },
	{ &neo_libs::Math_atan,  static_cast<LibmUnary>(&::atan), nullptr, nullptr },
	{ &neo_libs::Math_ceil,  static_cast<LibmUnary>(&::ceil), nullptr, nullptr },
	{ &neo_libs::Math_floor, static_cast<LibmUnary>(&::floor), nullptr, nullptr },
	{ &neo_libs::Math_round, static_cast<LibmUnary>(&::round), nullptr, nullptr },
	{ &neo_libs::Math_sin,   static_cast<LibmUnary>(&::sin), nullptr, nullptr },
	{ &neo_libs::Math_cos,   static_cast<LibmUnary>(&::cos), nullptr, nullptr },
	{ &neo_libs::Math_tan,   static_cast<LibmUnary>(&::tan), nullptr, nullptr },
	{ &neo_libs::Math_log,   static_cast<LibmUnary>(&::log), nullptr, nullptr },
	{ &neo_libs::Math_log10, static_cast<LibmUnary>(&::log10), nullptr, nullptr },
	{ &neo_libs::Math_exp,   static_cast<LibmUnary>(&::exp), nullptr, nullptr },
	{ &neo_libs::Math_deg,   &neo_libs::DegValue, nullptr, nullptr },
	{ &neo_libs::Math_rad,   &neo_libs::RadValue, nullptr, nullptr },
	{ &neo_libs::Math_sqrt,  static_cast<LibmUnary>(&::sqrt), nullptr, nullptr },
	{ &neo_libs::Math_trunc, static_cast<LibmUnary>(&::trunc), nullptr, nullptr },
	{ &neo_libs::Math_fract, &neo_libs::FractValue, nullptr, nullptr },
	{ &neo_libs::Math_sinh,  static_cast<LibmUnary>(&::sinh), nullptr, nullptr },
	{ &neo_libs::Math_cosh,  static_cast<LibmUnary>(&::cosh), nullptr, nullptr },
	{ &neo_libs::Math_tanh,  static_cast<LibmUnary>(&::tanh), nullptr, nullptr },
	{ &neo_libs::Math_Sign,  &neo_libs::SignValue, nullptr, nullptr },
	{ &neo_libs::Math_WrapAngle, &neo_libs::NsWrapAngle, nullptr, nullptr },
	{ &neo_libs::Math_Clamp01, &neo_libs::MathClamp01Value, nullptr, nullptr },
	{ &neo_libs::Math_SmoothStep01, &neo_libs::SmoothStep01Value, nullptr, nullptr },
	{ &neo_libs::Math_pow,   nullptr, static_cast<LibmBinary>(&::pow), nullptr },
	{ &neo_libs::Math_atan2, nullptr, static_cast<LibmBinary>(&::atan2), nullptr },
	{ &neo_libs::Math_hypot, nullptr, &neo_libs::HypotValue, nullptr },
	{ &neo_libs::Math_fmod,  nullptr, &neo_libs::FmodValue, nullptr },
	{ &neo_libs::Math_Min,   nullptr, &neo_libs::MinValue, nullptr },
	{ &neo_libs::Math_Max,   nullptr, &neo_libs::MaxValue, nullptr },
	{ &neo_libs::Math_Repeat, nullptr, &neo_libs::NsRepeat, nullptr },
	{ &neo_libs::Math_Clamp, nullptr, nullptr, &neo_libs::MathClampValue },
	{ &neo_libs::Math_Lerp,  nullptr, nullptr, &neo_libs::LerpValue },
	{ &neo_libs::Math_MoveToward, nullptr, nullptr, &neo_libs::NsMoveToward },
	{ &neo_libs::Math_InverseLerp, nullptr, nullptr, &neo_libs::InverseLerpValue },
	{ &neo_libs::Math_SmoothStep, nullptr, nullptr, &neo_libs::SmoothStepValue },
};

static int AddDefaultNativeFun(const std::string& nativeName, TYPE_NeoLib fun)
{
	auto it = g_sNeoFunLib_DefaultNativeIndex.find(nativeName);
	if (it != g_sNeoFunLib_DefaultNativeIndex.end())
		return it->second;

	int nativeIndex = (int)g_sNeoFunLib_DefaultNative.size();
	g_sNeoFunLib_DefaultNative.push_back(fun);
	// 호스트 콜백이나 VM 상태를 사용하지 않는 내장 수학 함수만 직접 계산을 허용한다(kScalarNatives).
	CNeoVM::ScalarNativeFunction scalar = nullptr;
	CNeoVM::ScalarNativeFunction2 scalar2 = nullptr;
	CNeoVM::ScalarNativeFunction3 scalar3 = nullptr;
	for (size_t i = 0; i < sizeof(kScalarNatives) / sizeof(kScalarNatives[0]); ++i)
	{
		if (kScalarNatives[i].fun != fun) continue;
		scalar = kScalarNatives[i].f1; scalar2 = kScalarNatives[i].f2; scalar3 = kScalarNatives[i].f3;
		break;
	}
	g_sNeoFunLib_DefaultScalar.push_back(scalar);
	g_sNeoFunLib_DefaultScalar2.push_back(scalar2);
	g_sNeoFunLib_DefaultScalar3.push_back(scalar3);
	g_sNeoFunLib_DefaultIntrinsic.push_back(NOP_NONE);
	g_sNeoFunLib_DefaultNativeIndex[nativeName] = nativeIndex;
	g_sNeoFunLib_Default.Add(nativeName, fun);
	return nativeIndex;
}
// 특정 native 함수를 로드 시 전용 opcode 로 대체하도록 표시 (문자열 하드코딩 대신 등록부에 명시).
static void SetSystemFunIntrinsic(const std::string& fname, OperationCode op)
{
	auto it = g_sNeoFunLib_DefaultNativeIndex.find("#" + fname);
	if (it != g_sNeoFunLib_DefaultNativeIndex.end())
		g_sNeoFunLib_DefaultIntrinsic[it->second] = (u8)op;
}

// 첫 항목 = 리턴 타입(필수, 없으면 "void"), 이후 파라미터를 "타입 이름" 문자열로 하나씩 나열.
// 인자 수는 파라미터 나열 개수에서 유도된다(별도 count 없음).
// 마지막에 "..." 을 넣으면 가변 인자(컴파일 타임 인자 수 검사 안 함 = argCount -1).
// 예) AddSystemFun("pow", &Math_pow, "float", "float base", "float exp");
//     AddSystemFun("rand", &Math_rand, "int");                                   // 인자 0개
//     AddSystemFun("resume", &coroutine_resume, "var", "coroutine co", "...");   // 가변
static void AddSystemFunImpl(const std::string& fname, TYPE_NeoLib fun, std::initializer_list<const char*> retAndParams)
{
	const std::string nativeName = "#" + fname;
	int nativeIndex = AddDefaultNativeFun(nativeName, fun);
	if (g_sCurrentSystem.empty())
		return;

	SystemFun v;
	v.fname = fname;
	v.nativeIndex = nativeIndex;
	bool variadic = false;
	bool first = true;
	for (const char* p : retAndParams)
	{
		if (first)
		{
			v.ret = (p != nullptr) ? p : "void";   // 첫 항목 = 리턴 타입
			first = false;
			continue;
		}
		if (p != nullptr && strcmp(p, "...") == 0)
			variadic = true;                 // 표시용으로도 넣고, 인자 수 검사는 끈다
		v.params.push_back(p);
	}
	if (v.ret.empty())
		v.ret = "void";
	v.argCount = variadic ? -1 : (int)v.params.size();

	auto it = g_sSystemFuns.find(g_sCurrentSystem);
	if (it == g_sSystemFuns.end())
	{
		std::list<SystemFun> lst;
		lst.push_back(v);
		g_sSystemFuns[g_sCurrentSystem] = lst;
	}
	else
	{
		(*it).second.push_back(v);
	}
}
template<typename... TParams>
static void AddSystemFun(const std::string& fname, TYPE_NeoLib fun, const char* ret, TParams... params)
{
	AddSystemFunImpl(fname, fun, { ret, params... });
}

int CNeoVM::FindDefaultNativeIndex(const VMString* pStr)
{
	if (pStr == nullptr)
		return -1;
    // 이전 이미지의 내부 이름만 새 이름으로 연결한다. 새 소스에는 예약 프리픽스만 노출한다.
    if (pStr->_str == "__neocore_fail") return FindDefaultNativeIndex("$neocore_fail");

	TYPE_NeoLib f;
	if (g_sNeoFunLib_Default.TryGetValue(pStr, &f) == false)
		return -1;

	auto it = g_sNeoFunLib_DefaultNativeIndex.find(pStr->_str);
	if (it == g_sNeoFunLib_DefaultNativeIndex.end())
		return -1;
	return it->second;
}

int CNeoVM::FindDefaultNativeIndex(const std::string& name)
{
	auto it = g_sNeoFunLib_DefaultNativeIndex.find(name == "__neocore_fail" ? "$neocore_fail" : name);
	if (it == g_sNeoFunLib_DefaultNativeIndex.end())
		return -1;
	return it->second;
}

CNeoVM::ScalarNativeFunction CNeoVM::GetDefaultNativeScalar(int nativeIndex)
{
    return nativeIndex >= 0 && nativeIndex < static_cast<int>(g_sNeoFunLib_DefaultScalar.size())
        ? g_sNeoFunLib_DefaultScalar[nativeIndex] : nullptr;
}
CNeoVM::ScalarNativeFunction2 CNeoVM::GetDefaultNativeScalar2(int nativeIndex)
{
    return nativeIndex >= 0 && nativeIndex < static_cast<int>(g_sNeoFunLib_DefaultScalar2.size())
        ? g_sNeoFunLib_DefaultScalar2[nativeIndex] : nullptr;
}
CNeoVM::ScalarNativeFunction3 CNeoVM::GetDefaultNativeScalar3(int nativeIndex)
{
    return nativeIndex >= 0 && nativeIndex < static_cast<int>(g_sNeoFunLib_DefaultScalar3.size())
        ? g_sNeoFunLib_DefaultScalar3[nativeIndex] : nullptr;
}
bool CNeoVM::HasDefaultNativeScalar(int nativeIndex, int argumentCount)
{
    switch (argumentCount)
    {
    case 1: return GetDefaultNativeScalar(nativeIndex) != nullptr;
    case 2: return GetDefaultNativeScalar2(nativeIndex) != nullptr;
    case 3: return GetDefaultNativeScalar3(nativeIndex) != nullptr;
    default: return false;
    }
}
bool CNeoVM::CallDefaultNativeByIndex(int nativeIndex, CNeoVMWorker* pWorker, short args)
{
	if (nativeIndex < 0 || nativeIndex >= (int)g_sNeoFunLib_DefaultNative.size())
		return false;
	return (*g_sNeoFunLib_DefaultNative[nativeIndex])(pWorker, nullptr, args);
}
const char* CNeoVM::GetDefaultNativeName(int nativeIndex)
{
	// 실패할 때만 역조회한다. 정상 호출에는 이름 검색이나 문자열 할당을 추가하지 않는다.
	for (const auto& entry : g_sNeoFunLib_DefaultNativeIndex)
		if (entry.second == nativeIndex)
			return entry.first.c_str() + (!entry.first.empty() && entry.first[0] == '#' ? 1 : 0);
	return "<unknown>";
}
// 로드 시 PTRCALL2 를 대체할 intrinsic opcode. 없으면 NOP_NONE.
int CNeoVM::GetDefaultNativeIntrinsic(int nativeIndex)
{
	if (nativeIndex < 0 || nativeIndex >= (int)g_sNeoFunLib_DefaultIntrinsic.size())
		return NOP_NONE;
	return g_sNeoFunLib_DefaultIntrinsic[nativeIndex];
}

static void AddGlobalLibFun()
{
	if (g_sNeoFunLib_Default.empty() == false)
		return;

	AddDefaultNativeFun("print", &neo_libs::io_print);
	// 정적 프런트엔드의 로우어링 헬퍼가 쓰는 런타임 오류 보고. 인자 문자열이 오류 메시지가 된다.
	AddDefaultNativeFun("$neocore_fail", &neo_libs::core_fail);

	g_sCurrentSystem = "math";
	AddSystemFun("abs", &neo_libs::Math_abs, "float", "float x");
	AddSystemFun("acos", &neo_libs::Math_acos, "float", "float x");
	AddSystemFun("asin", &neo_libs::Math_asin, "float", "float x");
	AddSystemFun("atan", &neo_libs::Math_atan, "float", "float x");
	AddSystemFun("ceil", &neo_libs::Math_ceil, "float", "float x");
	AddSystemFun("floor", &neo_libs::Math_floor, "float", "float x");
	AddSystemFun("round", &neo_libs::Math_round, "float", "float x");
	AddSystemFun("sin", &neo_libs::Math_sin, "float", "float radian");
	AddSystemFun("cos", &neo_libs::Math_cos, "float", "float radian");
	AddSystemFun("tan", &neo_libs::Math_tan, "float", "float radian");
	AddSystemFun("log", &neo_libs::Math_log, "float", "float x");
	AddSystemFun("log10", &neo_libs::Math_log10, "float", "float x");
	AddSystemFun("exp", &neo_libs::Math_exp, "float", "float x");
	AddSystemFun("pow", &neo_libs::Math_pow, "float", "float base", "float exp");
	AddSystemFun("deg", &neo_libs::Math_deg, "float", "float radian");
	AddSystemFun("rad", &neo_libs::Math_rad, "float", "float degree");
	AddSystemFun("sqrt", &neo_libs::Math_sqrt, "float", "float x");
	AddSystemFun("atan2", &neo_libs::Math_atan2, "float", "float y", "float x");
	AddSystemFun("hypot", &neo_libs::Math_hypot, "float", "float x", "float y");
	AddSystemFun("fmod", &neo_libs::Math_fmod, "float", "float x", "float y");
	AddSystemFun("trunc", &neo_libs::Math_trunc, "float", "float x");
	AddSystemFun("fract", &neo_libs::Math_fract, "float", "float x");
	AddSystemFun("sinh", &neo_libs::Math_sinh, "float", "float x");
	AddSystemFun("cosh", &neo_libs::Math_cosh, "float", "float x");
	AddSystemFun("tanh", &neo_libs::Math_tanh, "float", "float x");
	AddSystemFun("Sign", &neo_libs::Math_Sign, "float", "float x");
	AddSystemFun("Min", &neo_libs::Math_Min, "float", "float a", "float b");
	AddSystemFun("Max", &neo_libs::Math_Max, "float", "float a", "float b");
	AddSystemFun("Vector2", &neo_libs::Math_Vector2, "Vector2", "float x", "float y");
	AddSystemFun("Vector3", &neo_libs::Math_Vector3, "Vector3", "float x", "float y", "float z");
	AddSystemFun("Vector4", &neo_libs::Math_Vector4, "Vector4", "float x", "float y", "float z", "float w");
	AddSystemFun("Quaternion", &neo_libs::Math_Quaternion, "Quaternion", "float w", "float x", "float y", "float z");
	// 벡터 생성자는 LoadVM 에서 전용 opcode 로 대체 (native 호출 오버헤드 제거). native 구현은 폴백으로 유지.
	// 넷 다 같은 op. 성분 수는 호출 인자 수(n2)에서 나온다.
	SetSystemFunIntrinsic("Vector2", NOP_VEC_MAKE);
	SetSystemFunIntrinsic("Vector3", NOP_VEC_MAKE);
	SetSystemFunIntrinsic("Vector4", NOP_VEC_MAKE);
	SetSystemFunIntrinsic("Quaternion", NOP_VEC_MAKE);
	AddSystemFun("Clamp01", &neo_libs::Math_Clamp01, "float", "float x");
	AddSystemFun("Clamp", &neo_libs::Math_Clamp, "float", "float x", "float min", "float max");
	AddSystemFun("SmoothStep01", &neo_libs::Math_SmoothStep01, "float", "float t");
	AddSystemFun("Lerp", &neo_libs::Math_Lerp, "float", "float a", "float b", "float t");
	AddSystemFun("Lerp3", &neo_libs::Math_Lerp3, "Vector3", "Vector3 a", "Vector3 b", "float t");
	AddSystemFun("Repeat", &neo_libs::Math_Repeat, "float", "float x", "float length");
	AddSystemFun("PingPong", &neo_libs::Math_PingPong, "float", "float x", "float length");
	AddSystemFun("WrapAngle", &neo_libs::Math_WrapAngle, "float", "float radian");
	AddSystemFun("DeltaAngle", &neo_libs::Math_DeltaAngle, "float", "float from", "float to");
	AddSystemFun("LerpAngle", &neo_libs::Math_LerpAngle, "float", "float from", "float to", "float t");
	AddSystemFun("MoveToward", &neo_libs::Math_MoveToward, "float", "float current", "float target", "float maxDelta");
	AddSystemFun("MoveTowardAngle", &neo_libs::Math_MoveTowardAngle, "float", "float current", "float target", "float maxDelta");
	AddSystemFun("InverseLerp", &neo_libs::Math_InverseLerp, "float", "float a", "float b", "float value");
	AddSystemFun("Remap", &neo_libs::Math_Remap, "float", "float value", "float inMin", "float inMax", "float outMin", "float outMax");
	AddSystemFun("SmoothStep", &neo_libs::Math_SmoothStep, "float", "float edge0", "float edge1", "float x");
	AddSystemFun("Distance2", &neo_libs::Math_Distance2, "float", "float x0", "float y0", "float x1", "float y1");
	AddSystemFun("Dot3", &neo_libs::Math_Dot3, "float", "Vector3 a", "Vector3 b");
	AddSystemFun("Length3", &neo_libs::Math_Length3, "float", "Vector3 v");
	AddSystemFun("Distance3", &neo_libs::Math_Distance3, "float", "Vector3 a", "Vector3 b");
	AddSystemFun("DistanceSquared3", &neo_libs::Math_DistanceSquared3, "float", "Vector3 a", "Vector3 b");
	AddSystemFun("Normalize3", &neo_libs::Math_Normalize3, "Vector3", "Vector3 v", "Vector3 fallback");
	AddSystemFun("Cross3", &neo_libs::Math_Cross3, "Vector3", "Vector3 a", "Vector3 b");
	AddSystemFun("RotateVectorByQuat", &neo_libs::Math_RotateVectorByQuat, "Vector3", "Quaternion quat", "Vector3 v");
	AddSystemFun("quat_from_basis", &neo_libs::Math_quat_from_basis, "Quaternion", "Vector3 right", "Vector3 up", "Vector3 forward");
	AddSystemFun("quat_slerp", &neo_libs::Math_quat_slerp, "Quaternion", "Quaternion a", "Quaternion b", "float t");
	AddSystemFun("srand", &neo_libs::Math_srand, "void", "int seed");
	AddSystemFun("rand", &neo_libs::Math_rand, "int");
	AddSystemFun("tostr", &neo_libs::Math_ToStr, "string", "float value");
	AddSystemFun("fixed", &neo_libs::Math_Fixed, "string", "float value", "int decimals");
	AddSystemFun("Rand01", &neo_libs::Math_Rand01, "float");
	AddSystemFun("RandRange", &neo_libs::Math_RandRange, "float", "float min", "float max");
	AddSystemFun("Hash32", &neo_libs::Math_Hash32, "int", "int value");
	AddSystemFun("ColorRGB", &neo_libs::Math_ColorRGB, "int", "float r", "float g", "float b");
	AddSystemFun("ColorARGB", &neo_libs::Math_ColorARGB, "int", "float a", "float r", "float g", "float b");

	g_sCurrentSystem = "system";
	AddSystemFun("time", &neo_libs::sys_time, "int");
	AddSystemFun("clock", &neo_libs::sys_clock, "float");
	AddSystemFun("jsonEncode", &neo_libs::sys_jsonEncode, "var", "var value", "...");
	AddSystemFun("jsonDecode", &neo_libs::sys_jsonDecode, "var", "string text");
	AddSystemFun("array", &neo_libs::sys_array, "array", "var initial", "int size");
	AddSystemFun("array32", &neo_libs::sys_array32, "array", "var initial", "int size");
	AddSystemFun("aysnc_create", &neo_libs::sys_aysnc_create, "async");
	AddSystemFun("set", &neo_libs::sys_set, "set", "list l");


	g_sCurrentSystem = "coroutine";
	AddSystemFun("create", &neo_libs::coroutine_create, "coroutine", "fun f");
	AddSystemFun("resume", &neo_libs::coroutine_resume, "var", "coroutine co", "...");
	AddSystemFun("status", &neo_libs::coroutine_status, "string", "coroutine co");
	AddSystemFun("close", &neo_libs::coroutine_close, "void", "...");

	g_sCurrentSystem.clear();
	// 기존 네이티브 함수의 인덱스 순서를 유지하도록 새 함수는 등록부 끝에 추가한다.
	g_sCurrentSystem = "system";
	AddSystemFun("time_ms", &TimeLibrary::TimeMilliseconds, "int");
	AddSystemFun("date", &neo_libs::sys_date, "string", "string format", "int time");
	AddSystemFun("utc_date", &neo_libs::sys_utc_date, "string", "string format", "int time");
	AddSystemFun("time_span", &TimeLibrary::TimeSpan, "time_span", "int days", "int hours", "int minutes", "int seconds", "int milliseconds");
	AddSystemFun("time_span_ms", &TimeLibrary::TimeSpanMilliseconds, "time_span", "int milliseconds");
	AddSystemFun("stopwatch", &TimeLibrary::Stopwatch, "stopwatch");
	AddSystemFun("utc_time_ms", &TimeLibrary::UtcTimeMilliseconds, "int", "int year", "int month", "int day", "int hour", "int minute", "int second", "int millisecond");
	AddSystemFun("utc_parts", &TimeLibrary::UtcParts, "map", "int time_ms");
	AddSystemFun("utc_format_ms", &TimeLibrary::UtcFormatMilliseconds, "string", "int time_ms");
	AddSystemFun("utc_parse_ms", &TimeLibrary::UtcParseMilliseconds, "int", "string text");
	AddSystemFun("time_span_seconds", &TimeLibrary::TimeSpanSeconds, "time_span", "float seconds");

	g_sCurrentSystem.clear();
}
bool CNeoVM::IsGlobalLibFun(std::string& FunName)
{
	//InitLib();
	//return g_sNeoFunLib_Default.IsKey(FunName);
	return FunName == "print" || FunName == "$neocore_fail";
}
const std::list< SystemFun>* CNeoVM::GetSystemModule(const std::string& module)
{
	auto it = g_sSystemFuns.find(module);
	if(it == g_sSystemFuns.end())
		return nullptr;
	return &(*it).second;
}
void CNeoVM::RegLibrary(ValueRef pSystem, const char* pLibName)
{
	if (pSystem && pSystem.GetType() == VAR_FP_NATIVE)
		NeoVMSystem::RegisterTableCallBack(pSystem, nullptr, Fun_Default, nullptr);
	//AddGlobalLibFun();

	//_funDefaultLib = CNeoVM::RegisterNative(Fun);
}

void CNeoVM::RegObjLibrary()
{
	if (_funInitLib) return;
	_funInitLib = true;

	AddGlobalLibFun();
	_funLib_Default = NeoVMSystem::RegisterNative(Fun_Default);

	// String Lib
	_funLib_String = NeoVMSystem::RegisterNative(Fun_String);
	g_sNeoFunLib_String.Add("sub", &neo_libs::Str_sub);
	g_sNeoFunLib_String.Add("len", &neo_libs::Str_len);
	g_sNeoFunLib_String.Add("codepoint", &neo_libs::Str_codepoint);
	g_sNeoFunLib_String.Add("find", &neo_libs::Str_find);
	g_sNeoFunLib_String.Add("upper", &neo_libs::Str_upper);
	g_sNeoFunLib_String.Add("lower", &neo_libs::Str_lower);
	g_sNeoFunLib_String.Add("trim", &neo_libs::Str_trim);
	g_sNeoFunLib_String.Add("ltrim", &neo_libs::Str_ltrim);
	g_sNeoFunLib_String.Add("rtrim", &neo_libs::Str_rtrim);
	g_sNeoFunLib_String.Add("replace", &neo_libs::Str_replace);
	g_sNeoFunLib_String.Add("replaceAll", &neo_libs::Str_replaceAll);
	g_sNeoFunLib_String.Add("regex_match", &neo_libs::Str_regex_match);
	g_sNeoFunLib_String.Add("regex_search", &neo_libs::Str_regex_search);
	g_sNeoFunLib_String.Add("regex_replace", &neo_libs::Str_regex_replace);
	g_sNeoFunLib_String.Add("split", &neo_libs::Str_split);

	// List Lib
	_funLib_List = NeoVMSystem::RegisterNative(Fun_List);
	g_sNeoFunLib_List.Add("resize", &neo_libs::List_resize);
	g_sNeoFunLib_List.Add("len", &neo_libs::List_len);
	g_sNeoFunLib_List.Add("clear", &neo_libs::Collection_clear);
	g_sNeoFunLib_List.Add("clone", &neo_libs::Collection_clone);
	g_sNeoFunLib_List.Add("remove_swap", &neo_libs::List_remove_swap);
	g_sNeoFunLib_List.Add("pop", &neo_libs::List_pop);
	g_sNeoFunLib_List.Add("find", &neo_libs::Sequence_find);
	g_sNeoFunLib_List.Add("contains", &neo_libs::Sequence_contains);
	g_sNeoFunLib_List.Add("reserve", &neo_libs::Sequence_reserve);
	g_sNeoFunLib_List.Add("append", &neo_libs::List_append);
	g_sNeoFunLib_List.Add("insert", &neo_libs::List_insert);
	g_sNeoFunLib_List.Add("remove", &neo_libs::List_remove);
	g_sNeoFunLib_List.Add("sort", &neo_libs::List_sort);

	// Array Lib
	_funLib_Array = NeoVMSystem::RegisterNative(Fun_Array);
	g_sNeoFunLib_Array.Add("len", &neo_libs::Array_len);
	g_sNeoFunLib_Array.Add("clear", &neo_libs::Collection_clear);
	g_sNeoFunLib_Array.Add("clone", &neo_libs::Collection_clone);
	g_sNeoFunLib_Array.Add("find", &neo_libs::Sequence_find);
	g_sNeoFunLib_Array.Add("contains", &neo_libs::Sequence_contains);
	g_sNeoFunLib_Array.Add("reserve", &neo_libs::Sequence_reserve);
	g_sNeoFunLib_Array.Add("fill", &neo_libs::Array_fill);
	g_sNeoFunLib_Array.Add("resize", &neo_libs::Array_resize);
	g_sNeoFunLib_Array.Add("append", &neo_libs::Array_append);

	// Map Lib
	_funLib_Map = NeoVMSystem::RegisterNative(Fun_Map);
	g_sNeoFunLib_Map.Add("len", &neo_libs::map_len);
	g_sNeoFunLib_Map.Add("clear", &neo_libs::Collection_clear);
	g_sNeoFunLib_Map.Add("clone", &neo_libs::Collection_clone);
	g_sNeoFunLib_Map.Add("remove", &neo_libs::Map_remove);
	g_sNeoFunLib_Map.Add("contains", &neo_libs::Map_contains);
	g_sNeoFunLib_Map.Add("reserve", &neo_libs::map_reserve);
	g_sNeoFunLib_Map.Add("sort", &neo_libs::map_sort);
	g_sNeoFunLib_Map.Add("keys", &neo_libs::map_keys);
	g_sNeoFunLib_Map.Add("values", &neo_libs::map_values);

	_funLib_Set = NeoVMSystem::RegisterNative(Fun_Set);
	g_sNeoFunLib_Set.Add("len", &neo_libs::Set_len);
	g_sNeoFunLib_Set.Add("clear", &neo_libs::Collection_clear);
	g_sNeoFunLib_Set.Add("clone", &neo_libs::Collection_clone);
	g_sNeoFunLib_Set.Add("values", &neo_libs::Set_values);
	g_sNeoFunLib_Set.Add("add", &neo_libs::Set_add);
	g_sNeoFunLib_Set.Add("remove", &neo_libs::Set_remove);
	g_sNeoFunLib_Set.Add("contains", &neo_libs::Set_contains);

	// Async Lib
	_funLib_Async = NeoVMSystem::RegisterNative(Fun_Async);
	g_sNeoFunLib_Async.Add("get", &neo_libs::async_get);
	g_sNeoFunLib_Async.Add("post", &neo_libs::async_post);
	g_sNeoFunLib_Async.Add("add_header", &neo_libs::async_add_header);
	g_sNeoFunLib_Async.Add("wait", &neo_libs::async_wait);
	g_sNeoFunLib_Async.Add("close", &neo_libs::async_close);
}

void CNeoVM::InitLib()
{
/*	ValueRef pSystem = &m_sVarGlobal[0];
	Var_SetTable(pSystem, TableAlloc());

	RegLibrary(pSystem, "sys");*/
	RegObjLibrary();
}


};
