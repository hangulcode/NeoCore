// 확인된 IR 타입과 명령 계약을 컴파일 단계의 고정 슬롯 배치로 전파한다.
// 실행 시 값이나 디버그 이름을 보고 타입을 추측하지 않는다.
class StorageCompiler
{
    SFunctions& functions_;
    SVars& variables_;
    bool changed_ = false;
    SFunctionInfo* activeFunction_ = nullptr;
    CompileCallStorage* activeCall_ = nullptr;
    std::map<const SFunctionInfo*, std::map<int, SlotType>> legacyLocals_;
    std::map<int, const NeoCore::IRFunction*> nativeByReceiver_;
    bool nativeIndexed_ = false;
    std::map<char, SlotType> builtinResults_;

    SlotType Primitive(NeoCore::TypeKind kind) const
    {
        if (functions_._typedIR) return CoreStorageType(functions_._typedIR->types, static_cast<NeoCore::TypeId>(kind));
        return Physical(kind == NeoCore::TypeKind::Int ? SlotKind::Int : kind == NeoCore::TypeKind::Float ? SlotKind::Float :
            kind == NeoCore::TypeKind::Bool ? SlotKind::Bool : kind == NeoCore::TypeKind::String ? SlotKind::String : SlotKind::Boxed);
    }
    SlotType Physical(SlotKind kind) const { SlotType type; type.kind = kind; return type; }
    SlotType Get(SFunctionInfo& function, int slot) const
    {
        if (slot == STACK_POS_RETURN || slot >= COMPILE_CALLARG_VAR_BEGIN)
        {
            if (&function != activeFunction_ || !activeCall_) return SlotType();
            if (slot == STACK_POS_RETURN || slot == COMPILE_CALLARG_VAR_BEGIN) return activeCall_->returnType;
            const int argument = slot - COMPILE_CALLARG_VAR_BEGIN - 1;
            return argument >= 0 && argument < static_cast<int>(activeCall_->types.size()) ? activeCall_->types[argument] : SlotType();
        }
        if (slot == -1) return SlotType();
        if (IsCompileConstant(slot))
        {
            const int index = StaticPoolIndex(slot);
            if (index < 0 || index >= static_cast<int>(functions_._staticVars.size())) return SlotType();
            switch (functions_._staticVars[index].GetType())
            {
            case VAR_INT: return Primitive(NeoCore::TypeKind::Int);
            case VAR_FLOAT: return Primitive(NeoCore::TypeKind::Float);
            case VAR_BOOL: return Primitive(NeoCore::TypeKind::Bool);
            case VAR_STRING: return Primitive(NeoCore::TypeKind::String);
            default: return SlotType();
            }
        }
        const auto* locals = &function._storage;
        const auto legacy = legacyLocals_.find(&function);
        if (!functions_._typedIR && legacy != legacyLocals_.end()) locals = &legacy->second;
        const auto& slots = slot < 0 ? variables_._globalStorage : *locals;
        const auto found = slots.find(slot);
        return found != slots.end() ? found->second : SlotType();
    }
    void Bind(SFunctionInfo& function, int slot, SlotType type)
    {
        if (type.kind == SlotKind::Boxed || slot == -1 || slot == 0) return;
        if (slot == STACK_POS_RETURN || slot >= COMPILE_CALLARG_VAR_BEGIN)
        {
            if (&function != activeFunction_ || !activeCall_) return;
            SlotType* target = nullptr;
            if (slot == STACK_POS_RETURN || slot == COMPILE_CALLARG_VAR_BEGIN) target = &activeCall_->returnType;
            else if (slot - COMPILE_CALLARG_VAR_BEGIN - 1 < static_cast<int>(activeCall_->types.size()))
                target = &activeCall_->types[slot - COMPILE_CALLARG_VAR_BEGIN - 1];
            if (target && target->kind == SlotKind::Boxed) { *target = type; changed_ = true; }
            return;
        }
        if (slot >= COMPILE_STATIC_VAR_BEGIN) return;
        auto& slots = slot < 0 ? variables_._globalStorage : functions_._typedIR ? function._storage : legacyLocals_[&function];
        SlotType& current = slots[slot];
        if (current.kind != SlotKind::Boxed && current.kind != type.kind)
        {
            // 어느 슬롯이 어떤 종류끼리 부딪혔는지 남긴다. 호출 창 슬롯이면 활성 계획 번호도 적는다.
            // 함수 이름을 함께 남겨 생성된 소스 위치만으로는 찾기 어려운 경우에도 원인 함수를 바로 찾게 한다.
            char detail[320];
            const int call = activeCall_ ? static_cast<int>(activeCall_ - function._calls.data()) : -1;
            snprintf(detail, sizeof(detail), "conflicting compiler slot types (slot %d: kind %d vs %d, function %d '%.120s', call %d)",
                slot, static_cast<int>(current.kind), static_cast<int>(type.kind), function._funID, function._name.c_str(), call);
            throw std::logic_error(detail);
        }
        if (current.kind == SlotKind::Boxed || (current.kind == type.kind && current.typeId == 0 && type.typeId != 0))
        { current = type; changed_ = true; }
    }
    void Copy(SFunctionInfo& function, int destination, int source)
    { Bind(function, destination, Get(function, source)); Bind(function, source, Get(function, destination)); }
    // 루프 상수 hoisting 이 static 상수를 숨은 지역 슬롯으로 옮긴 오퍼랜드는 원래 상수로 되짚는다.
    // 구조체 필드 키가 그렇게 옮겨지면 필드 타입을 찾지 못해 읽기 결과 슬롯이 엉뚱한 종류로 고정됐다.
    int ConstantOperand(const SFunctionInfo& function, int slot) const
    {
        for (const std::pair<short, short>& hoisted : function._hoisted)
            if (hoisted.first == slot) return hoisted.second;
        return slot;
    }
    SlotType Element(SFunctionInfo& function, int owner, int key) const
    {
        if (!functions_._typedIR) return SlotType();
        const NeoCore::TypeDesc& type = functions_._typedIR->types.Get(Get(function, owner).typeId);
        NeoCore::TypeId element = type.element;
        if (type.kind == NeoCore::TypeKind::Map) element = type.value;
        if (type.kind == NeoCore::TypeKind::Struct)
        {
            // 구조체 필드는 항상 상수 첨자다. 찾지 못하면 종류를 지어내지 않고 추론에서 뺀다.
            const int constant = ConstantOperand(function, key);
            if (!IsCompileConstant(constant)) return SlotType();
            const BoundaryValue& index = functions_._staticVars[StaticPoolIndex(constant)];
            if (index.GetType() != VAR_INT || index._int < 0 || index._int >= static_cast<NS_INT>(type.fields.size())) return SlotType();
            element = type.fields[static_cast<size_t>(index._int)].type;
        }
        if (type.kind >= NeoCore::TypeKind::Vector2 && type.kind <= NeoCore::TypeKind::Quaternion)
            element = static_cast<NeoCore::TypeId>(NeoCore::TypeKind::Float);
        return CoreStorageType(functions_._typedIR->types, element);
    }
    // 수신자 슬롯으로 네이티브 호출 서명을 찾는다. 서명 이름의 전역 슬롯은 추론 중에 바뀌지 않으므로 표를 한 번만
    // 만든다. 호출마다 모든 서명의 이름을 다시 찾으면 호출 수 × 서명 수만큼 문자열 조회가 든다. 같은 슬롯을 가리키는
    // 서명이 여럿이면 앞선 것이 남는다(순서대로 처음 맞는 서명을 쓰던 것과 같다).
    const NeoCore::IRFunction* NativeSignature(int receiver)
    {
        if (!nativeIndexed_)
        {
            nativeIndexed_ = true;
            for (const NeoCore::IRFunction& signature : functions_._typedIR->nativeCalls)
            {
                const int slot = variables_.FindVar(signature.backendName);
                if (slot != -1) nativeByReceiver_.insert(std::make_pair(slot, &signature));
            }
        }
        const auto found = nativeByReceiver_.find(receiver);
        return found != nativeByReceiver_.end() ? found->second : nullptr;
    }
    // 내장 멤버 서명의 결과 저장형. 결과 타입이 타입 표에 없으면 새로 만들어야 해서 표의 사본에서 구하는데, 값은
    // 결과 코드에만 달려 있으므로 코드마다 한 번만 계산한다(호출마다 표 전체를 복사하지 않는다).
    SlotType BuiltinResult(char code)
    {
        const auto found = builtinResults_.find(code);
        if (found != builtinResults_.end()) return found->second;
        NeoCore::TypeRegistry types = functions_._typedIR->types;
        const SlotType result = CoreStorageType(types, NeoCore::BuiltinSignatureType(types, code));
        builtinResults_.insert(std::make_pair(code, result));
        return result;
    }
    void BindNativeCall(SFunctionInfo& function, CompileCallStorage& call)
    {
        if (!functions_._typedIR || call.receiver == -1) return;
        const auto& ir = *functions_._typedIR;
        SlotType result;
        if (const NeoCore::IRFunction* signature = NativeSignature(call.receiver))
        {
            result = CoreStorageType(ir.types, signature->returnType);
            for (std::size_t i = 0; i < call.sources.size() && i < signature->parameterTypes.size(); ++i)
            {
                call.types[i] = CoreStorageType(ir.types, signature->parameterTypes[i]);
                Bind(function, call.sources[i], call.types[i]);
            }
        }
        if (IsCompileConstant(call.member))
        {
            const BoundaryValue& key = functions_._staticVars[StaticPoolIndex(call.member)];
            if (key.GetType() == VAR_STRING)
            {
                const std::string& name = key._str->_str;
                const SlotType receiver = Get(function, call.receiver);
                const auto& type = ir.types.Get(receiver.typeId);
                if (const NeoCore::BuiltinSignature* signature = NeoCore::FindBuiltinSignature(type.name, name))
                    result = BuiltinResult(signature->result);
                if (type.kind == NeoCore::TypeKind::Set)
                {
                    if (name == "add" || name == "remove" || name == "contains") result = Primitive(NeoCore::TypeKind::Bool);
                    else if (name == "clone") result = receiver;
                }
                if (type.kind == NeoCore::TypeKind::List || type.kind == NeoCore::TypeKind::Map || type.kind == NeoCore::TypeKind::Array)
                {
                    if (name == "clone") result = receiver;
                    else if (name == "contains" || name == "remove") result = Primitive(NeoCore::TypeKind::Bool);
                    else if (name == "append" || name == "insert" || name == "indexof") result = Primitive(NeoCore::TypeKind::Int);
                }
            }
        }
        if (result.kind != SlotKind::Boxed && call.returnType.kind == SlotKind::Boxed)
        { call.returnType = result; changed_ = true; }
    }
    void BindCalls(SFunctionInfo& function)
    {
        for (CompileCallStorage& call : function._calls)
        {
            BindNativeCall(function, call);
            SFunctionInfo* callee = call.function < 0 ? nullptr : functions_.FindFun(call.function);
            for (std::size_t i = 0; i < call.sources.size(); ++i)
            {
                SlotType type = Get(function, call.sources[i]);
                if (callee && i < callee->_parameterStorage.size())
                {
                    if (type.kind == SlotKind::Boxed) type = callee->_parameterStorage[i];
                    Bind(function, call.sources[i], type); Bind(*callee, static_cast<int>(i + 1), type);
                }
                if (type.kind != SlotKind::Boxed) call.types[i] = type;
            }
            const SlotType result = callee ? callee->_returnStorage : Get(function, call.result);
            if (result.kind != SlotKind::Boxed) call.returnType = result;
        }
    }
    void Infer(SFunctionInfo& function, const SVMOperation& op)
    {
        if (op.op == NOP_PREPARE_CALL)
        { activeCall_ = &function._calls[op.n1]; return; }
        using NeoCore::TypeKind;
        const SlotType integer = Primitive(TypeKind::Int), number = Primitive(TypeKind::Float), boolean = Primitive(TypeKind::Bool);
        if (const TypedOpInfo* typed = GetTypedOpInfo(op.Code()))
        {
            const bool floating = op.op == NOP_F64_ARITH || op.op == NOP_F64_COMPARE || op.op == NOP_JMP_F64_COMPARE || op.Code() == OP_F64_TO_I64;
            Bind(function, op.n2, floating ? number : integer);
            if (typed->arity == 2) Bind(function, op.n3, floating ? number : integer);
            if (!typed->jump) Bind(function, op.n1, typed->comparison ? boolean : op.Code() == OP_I64_TO_F64 ? number : op.Code() == OP_F64_TO_I64 ? integer : floating ? number : integer);
            return;
        }
        if (const AggregateOpInfo* aggregate = GetAggregateOpInfo(op.Code()))
        {
            if (aggregate->shape == AggregateOpShape::Store)
            { Bind(function, op.n3, Element(function, op.n1, op.n2)); return; }
            const std::string name = aggregate->suffix;
            if (op.op == NOP_CELL_NEW) Bind(function, op.n1, Physical(SlotKind::List));
            else if (op.op == NOP_CELL_GET) Bind(function, op.n1, Element(function, op.n2, -1));
            else if (op.op == NOP_CELL_SET) Copy(function, op.n1, op.n2);
            else if (name.find("contains") != std::string::npos || name.find("remove") != std::string::npos ||
                (name.size() > 1 && name[0] == 's' && name.find("add") != std::string::npos)) Bind(function, op.n1, boolean);
            else if (op.op == NOP_ARRAY_LEN) Bind(function, op.n1, integer);
            else if (name.find("read") != std::string::npos || name.find("load") != std::string::npos)
                Bind(function, op.n1, Element(function, op.n2, op.n3));
            else if (name.size() > 1 && name[0] == 'v')
            {
                const SlotType vector = Primitive(static_cast<TypeKind>(static_cast<unsigned>(TypeKind::Vector2) + aggregate->dimension - 2));
                Bind(function, op.n1, name.find("get") != std::string::npos ? number : vector);
                if (name.find("make") != std::string::npos)
                {
                    if (aggregate->shape == AggregateOpShape::Window)
                        for (int i = 0; i < aggregate->arity; ++i) Bind(function, op.n2 + i, number);
                    else { Bind(function, op.n2, number); Bind(function, op.n3, number); }
                }
                else if (name.find("set") != std::string::npos)
                { Bind(function, op.n2, vector); Bind(function, op.n2 + 1, number); }
                else
                {
                    Bind(function, op.n2, vector);
                    if (aggregate->shape == AggregateOpShape::Binary)
                        Bind(function, op.n3, (op.argFlag & NEOS_OP_SUB_MASK) == static_cast<unsigned>(ArithmeticOp::Mul) ||
                            (op.argFlag & NEOS_OP_SUB_MASK) == static_cast<unsigned>(ArithmeticOp::Div) ? number : vector);
                }
            }
            else if (name.size() > 1 && name[0] == 'a')
            {
                if (name.find("new") != std::string::npos) Bind(function, op.n1, Physical(SlotKind::Array));
                else if (name.find("get") != std::string::npos) Bind(function, op.n1, Element(function, op.n2, op.n3));
                else if (name.find("set") != std::string::npos)
                {
                    const SlotType value = Element(function, op.n2 + 1, op.n2 + 2);
                    Bind(function, op.n2 + 2, integer); Bind(function, op.n2, value); Bind(function, op.n1, value);
                }
                else if (name.find("append") != std::string::npos) Bind(function, op.n1, integer);
            }
            return;
        }
        switch (op.op)
        {
        case NOP_MOV:
            if ((op.argFlag & NEOS_OP_SUB_MASK) == MOV_SUB_IMMEDIATE_INT) Bind(function, op.n1, integer);
            else if ((op.argFlag & NEOS_OP_SUB_MASK) == MOV_SUB_IMMEDIATE_FLOAT) Bind(function, op.n1, number);
            else Copy(function, op.n1, op.n2);
            break;
        case NOP_LOADK: Bind(function, op.n1, Get(function, StaticCompileIndex(op.n23))); break;
        case NOP_LOG_NOT: Bind(function, op.n1, boolean); Bind(function, op.n2, boolean); break;
        case NOP_JMP_FOR:
            for (int i = -2; i <= 1; ++i) Bind(function, op.n3 + i, integer);
            break;
        case NOP_JMP_FOREACH:
        {
            const SlotType source = Get(function, op.n2);
            if (functions_._typedIR)
            {
                const auto& types = functions_._typedIR->types;
                const auto& owner = types.Get(source.typeId);
                Bind(function, op.n3, owner.kind == TypeKind::Map ? CoreStorageType(types, owner.key) :
                    owner.kind == TypeKind::String ? Primitive(TypeKind::String) : Element(function, op.n2, -1));
                if (owner.kind == TypeKind::Map) Bind(function, op.n3 + 1, CoreStorageType(types, owner.value));
            }
            Bind(function, op.n3 + 2, Physical(SlotKind::Iterator));
            Bind(function, op.n3 + 3, integer);
            break;
        }
        case NOP_STR_ADD: Bind(function, op.n1, Primitive(TypeKind::String)); break;
        case NOP_COMPARE:
            Bind(function, op.n1, boolean); Copy(function, op.n2, op.n3); break;
        case NOP_JMP_COMPARE:
            Copy(function, op.n2, op.n3); break;
        case NOP_CALL:
        {
            SFunctionInfo* target = functions_.FindFun(op.n1);
            if (target) Bind(function, op.n3, target->_returnStorage);
            break;
        }
        case NOP_FMOV1:
        case NOP_FMOV2:
        {
            if (op.op == NOP_FMOV1) Bind(function, op.n1, Physical(SlotKind::Function));
            SFunctionInfo* target = functions_.FindFun(op.op == NOP_FMOV1 ? op.n2 : op.n3);
            if (target)
                for (const SClosureCapture& capture : target->_captures)
                {
                    Bind(*target, capture._localSlot, Get(function, capture._sourceSlot));
                    Bind(function, capture._sourceSlot, Get(*target, capture._localSlot));
                }
            break;
        }
        case NOP_CLT_MOV: Bind(function, op.n3, Element(function, op.n1, op.n2)); break;
        case NOP_LIST_ALLOC: Bind(function, op.n1, Physical(SlotKind::List)); break;
        case NOP_TABLE_ALLOC: Bind(function, op.n1, Physical(SlotKind::Map)); break;
        case NOP_CLT_READ: Bind(function, op.n3, Element(function, op.n1, op.n2)); break;
        case NOP_RETURN:
            if (!functions_._typedIR && function._returnStorage.kind == SlotKind::Boxed)
            {
                const SlotType result = Get(function, op.n1);
                if (result.kind != SlotKind::Boxed) { function._returnStorage = result; changed_ = true; }
            }
            Bind(function, op.n1, function._returnStorage); break;
        case NOP_TOSIZE: Bind(function, op.n1, integer); break;
        case NOP_TOINT: Bind(function, op.n1, integer); break;
        case NOP_TOFLOAT: Bind(function, op.n1, number); break;
        case NOP_TOSTRING: Bind(function, op.n1, Primitive(TypeKind::String)); break;
        case NOP_CHANGE_INT: Bind(function, op.n1, integer); break;
        default: break;
        }
    }
public:
    StorageCompiler(SFunctions& functions, SVars& variables) : functions_(functions), variables_(variables) {}
    void Run()
    {
        do
        {
            changed_ = false;
            for (SFunctionInfo* function : functions_._funSequence)
            {
                activeFunction_ = function; activeCall_ = nullptr;
                BindCalls(*function);
                const SVMOperation* code = reinterpret_cast<const SVMOperation*>(static_cast<const u8*>(functions_._codeFinal.GetData()) + function->_iCode_Begin);
                // 활성 계획은 기록된 준비 위치로 찾는다. PREPARE_CALL이 없는 직접 호출도 창 쓰기의 종류를 받는다.
                // 옮겨 쓴 루프 조건 때문에 벡터 순서와 위치 순서가 다를 수 있어 위치 순으로 훑는다.
                const std::vector<std::size_t> order = function->CallPlanOrder();
                std::size_t nextCall = 0;
                for (int i = 0; i < function->_iCode_Size / static_cast<int>(sizeof(SVMOperation)); ++i)
                {
                    const int offset = i * static_cast<int>(sizeof(SVMOperation));
                    while (nextCall < order.size() && function->_calls[order[nextCall]].prepareOffset <= offset)
                        activeCall_ = &function->_calls[order[nextCall++]];
                    Infer(*function, code[i]);
                }
            }
        } while (changed_);
        if (!functions_._typedIR)
            for (SFunctionInfo* function : functions_._funSequence)
                for (const auto& slot : legacyLocals_[function])
                    function->SetStorage(slot.first, slot.second);
    }
};
