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
            throw std::logic_error("conflicting compiler slot types");
        }
        if (current.kind == SlotKind::Boxed || (current.kind == type.kind && current.typeId == 0 && type.typeId != 0))
        { current = type; changed_ = true; }
    }
    void Copy(SFunctionInfo& function, int destination, int source)
    { Bind(function, destination, Get(function, source)); Bind(function, source, Get(function, destination)); }
    SlotType Element(SFunctionInfo& function, int owner, int key) const
    {
        if (!functions_._typedIR) return SlotType();
        const NeoCore::TypeDesc& type = functions_._typedIR->types.Get(Get(function, owner).typeId);
        NeoCore::TypeId element = type.element;
        if (type.kind == NeoCore::TypeKind::Map) element = type.value;
        if (type.kind == NeoCore::TypeKind::Struct && IsCompileConstant(key))
        {
            const BoundaryValue& index = functions_._staticVars[StaticPoolIndex(key)];
            if (index.GetType() == VAR_INT && index._int >= 0 && index._int < static_cast<NS_INT>(type.fields.size()))
                element = type.fields[static_cast<size_t>(index._int)].type;
        }
        if (type.kind >= NeoCore::TypeKind::Vector2 && type.kind <= NeoCore::TypeKind::Quaternion)
            element = static_cast<NeoCore::TypeId>(NeoCore::TypeKind::Float);
        return CoreStorageType(functions_._typedIR->types, element);
    }
    void BindNativeCall(SFunctionInfo& function, CompileCallStorage& call)
    {
        if (!functions_._typedIR || call.receiver == -1) return;
        const auto& ir = *functions_._typedIR;
        SlotType result;
        for (const NeoCore::IRFunction& signature : ir.nativeCalls)
        {
            const int receiver = variables_.FindVar(signature.backendName);
            if (receiver != call.receiver) continue;
            result = CoreStorageType(ir.types, signature.returnType);
            for (std::size_t i = 0; i < call.sources.size() && i < signature.parameterTypes.size(); ++i)
            {
                call.types[i] = CoreStorageType(ir.types, signature.parameterTypes[i]);
                Bind(function, call.sources[i], call.types[i]);
            }
            break;
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
                {
                    NeoCore::TypeRegistry types = ir.types;
                    result = CoreStorageType(types, NeoCore::BuiltinSignatureType(types, signature->result));
                }
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
                for (int i = 0; i < function->_iCode_Size / static_cast<int>(sizeof(SVMOperation)); ++i) Infer(*function, code[i]);
            }
        } while (changed_);
        if (!functions_._typedIR)
            for (SFunctionInfo* function : functions_._funSequence)
                for (const auto& slot : legacyLocals_[function])
                    function->SetStorage(slot.first, slot.second);
    }
};
