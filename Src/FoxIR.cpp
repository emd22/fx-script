#include "FoxIR.hpp"

#include "FoxLog.hpp"

///////////////////////////////////////////////
// IR Asm Emitter
///////////////////////////////////////////////

#pragma region IrEmitter

#define MARK_REGISTER_USED(regn_)                                                                                                                    \
    {                                                                                                                                                \
        MarkRegisterUsed(regn_);                                                                                                                     \
    }
#define MARK_REGISTER_FREE(regn_)                                                                                                                    \
    {                                                                                                                                                \
        MarkRegisterFree(regn_);                                                                                                                     \
    }

using TT = FoxTokenizer::TokenType;

void FoxIREmitter::BeginEmitting(FoxAstNode* node)
{
    mStackSize = 1024;

    /*mStack = new uint8[1024];
    mStackStart = mStack;*/

    mBytecode.Create(4096);
    VarHandles.Create(64);

    Emit(node);

    PrintBytecode();
}

#define RETURN_IF_NO_NODE(node_)                                                                                                                     \
    if ((node_) == nullptr) {                                                                                                                        \
        return;                                                                                                                                      \
    }

#define RETURN_VALUE_IF_NO_NODE(node_, value_)                                                                                                       \
    if ((node_) == nullptr) {                                                                                                                        \
        return (value_);                                                                                                                             \
    }

void FoxIREmitter::Emit(FoxAstNode* node)
{
    RETURN_IF_NO_NODE(node);

    if (node->NodeType == FX_AST_BLOCK) {
        return EmitBlock(reinterpret_cast<FoxAstBlock*>(node), 0);
    }
    // else if (node->NodeType == FX_AST_ACTIONDECL) {
    //     return EmitFunction(reinterpret_cast<FoxAstFunctionDecl*>(node));
    // }
    else if (node->NodeType == FX_AST_PROCCALL) {
        return DoFunctionCall(reinterpret_cast<FoxAstFunctionCall*>(node));
    }
    else if (node->NodeType == FX_AST_ASSIGN) {
        return EmitAssign(reinterpret_cast<FoxAstAssign*>(node));
    }
    else if (node->NodeType == FX_AST_VARDECL) {
        DoVarDeclare(reinterpret_cast<FoxAstVarDecl*>(node));
        return;
    }
    else if (node->NodeType == FX_AST_RETURN) {
        FoxAstReturn* return_node = reinterpret_cast<FoxAstReturn*>(node);

        // Is there a return value provided?
        if (return_node->Rhs) {
            FoxAstNode* return_rhs = return_node->Rhs;

            // Check to see if its a literal
            if (return_rhs->NodeType == FX_AST_LITERAL) {
                FoxAstLiteral* literal = reinterpret_cast<FoxAstLiteral*>(return_rhs);

                if (literal->Value.Type == FoxValue::INT) {
                    EmitJumpReturnToCallerInt32(literal->Value.ValueInt);
                }
                else if (literal->Value.Type == FoxValue::REF) {
                    const FoxAstVarRef* var_ref = literal->Value.pValueRef;

                    // Get the variable and load it into a register.
                    FoxBytecodeVarHandle* var_to_return = FindVarHandle(var_ref->pName->GetHash());

                    FoxIRRegister return_result_reg = FX_IR_REG_RETURN_VALUE;

                    MARK_REGISTER_USED(return_result_reg);

                    if (var_to_return) {
                        if (var_to_return->Register != FX_IR_NONE) {
                            EmitMoveReg32(return_result_reg, var_to_return->Register);
                        }
                        else {
                            EmitVariableGetInt32(var_to_return->VarIndexInScope, return_result_reg);
                        }
                    }

                    // if (var_to_return) {
                    //     DoLoad(var_to_return->Offset, var_to_return_reg);
                    // }

                    // Free the register as we will not need it after scope end
                    MARK_REGISTER_FREE(return_result_reg);

                    // Return with the register
                    EmitJumpReturnToCallerReg32(return_result_reg);
                }
            }

            return;
        }

        // constexpr FoxHash return_val_hash = FoxHashStr(FX_SCRIPT_VAR_RETURN_VAL);

        // FoxBytecodeVarHandle* return_var = FindVarHandle(return_val_hash);

        // FoxIRRegister result_register = FindFreeReg32();
        // MARK_REGISTER_USED(result_register);

        // if (return_var) {
        //     DoLoad(return_var->Offset, result_register);
        // }

        // MARK_REGISTER_FREE(result_register);

        EmitJumpReturnToCaller();

        return;
    }
}

FoxBytecodeVarHandle* FoxIREmitter::FindVarHandle(FoxHash hashed_name)
{
    for (FoxBytecodeVarHandle& handle : VarHandles) {
        if (handle.HashedName == hashed_name) {
            return &handle;
        }
    }
    return nullptr;
}

FoxBytecodeFunctionHandle* FoxIREmitter::FindFunctionHandle(FoxHash hashed_name)
{
    for (FoxBytecodeFunctionHandle& handle : FunctionHandles) {
        if (handle.HashedName == hashed_name) {
            return &handle;
        }
    }
    return nullptr;
}


FoxIRRegister FoxIREmitter::FindFreeReg32()
{
    for (int register_index = FX_IR_GW0; register_index <= FX_IR_GW7; register_index++) {
        uint32 gp_r = (1 << register_index);

        if (!(mRegsInUse & gp_r)) {
            return static_cast<FoxIRRegister>(register_index);
        }
    }

    return FX_IR_GW6;
}

FoxIRRegister FoxIREmitter::FindFreeReg64()
{
    for (int register_index = FX_IR_GX0; register_index <= FX_IR_GX3; register_index++) {
        uint32 gp_r = (1 << register_index);

        if (!(mRegsInUse & gp_r)) {
            return static_cast<FoxIRRegister>(register_index);
        }
    }

    return FX_IR_GX3;
}

const char* FoxIREmitter::GetRegisterName(FoxIRRegister reg)
{
    switch (reg) {
    case FX_IR_PARAMREG0:
        return "PARAMREG0";
    case FX_IR_PARAMREG1:
        return "PARAMREG1";
    case FX_IR_PARAMREG2:
        return "PARAMREG2";
    case FX_IR_PARAMREG3:
        return "PARAMREG3";
    case FX_IR_GW0:
        return "GW0";
    case FX_IR_GW1:
        return "GW1";
    case FX_IR_GW2:
        return "GW2";
    case FX_IR_GW3:
        return "GW3";
    case FX_IR_GW4:
        return "GW4";
    case FX_IR_GW5:
        return "GW5";
    case FX_IR_GW6:
        return "GW6";
    case FX_IR_GW7:
        return "GW7";
    case FX_IR_GX0:
        return "GX0";
    case FX_IR_GX1:
        return "GX1";
    case FX_IR_GX2:
        return "GX2";
    case FX_IR_GX3:
        return "GX3";
    case FX_IR_SP:
        return "SP";
    case FX_IR_REG_RETURN_VALUE:
        return "RETVAL";
    default:;
    };

    return "NONE";
}

void FoxIREmitter::Write16(uint16 value)
{
    mBytecode.Insert(static_cast<uint8>(value >> 8));
    mBytecode.Insert(static_cast<uint8>(value));
}

void FoxIREmitter::Write32(uint32 value)
{
    Write16(static_cast<uint16>(value >> 16));
    Write16(static_cast<uint16>(value));
}

void FoxIREmitter::WriteOp(uint8 op_base, uint8 op_spec)
{
    mBytecode.Insert(op_base);
    mBytecode.Insert(op_spec);
}

using IRRhsMode = FoxIREmitter::RhsMode;

#define MARK_REGISTER_USED(regn_)                                                                                                                    \
    {                                                                                                                                                \
        MarkRegisterUsed(regn_);                                                                                                                     \
    }
#define MARK_REGISTER_FREE(regn_)                                                                                                                    \
    {                                                                                                                                                \
        MarkRegisterFree(regn_);                                                                                                                     \
    }

void FoxIREmitter::MarkRegisterUsed(FoxIRRegister reg)
{
    uint16 register_flag = (1 << reg);
    mRegsInUse = static_cast<uint32>(uint16(mRegsInUse) | register_flag);
}

void FoxIREmitter::MarkRegisterFree(FoxIRRegister reg)
{
    uint16 register_flag = (1 << reg);
    mRegsInUse = static_cast<uint32>(uint16(mRegsInUse) & (~register_flag));
}

void FoxIREmitter::EmitSave32(int16 offset, uint32 value)
{
    // SAVE32 [i16 offset] [i32]
    WriteOp(IrBase_Save, IrSpecSave_Int32);

    Write16(offset);
    Write32(value);
}

void FoxIREmitter::EmitSaveReg32(int16 offset, FoxIRRegister reg)
{
    // SAVE32r [i16 offset] [%r32]
    WriteOp(IrBase_Save, IrSpecSave_Reg32);

    Write16(offset);
    Write16(reg);
}


void FoxIREmitter::EmitSaveAbsolute32(uint32 position, uint32 value)
{
    // SAVE32a [i32 offset] [i32]
    WriteOp(IrBase_Save, IrSpecSave_AbsoluteInt32);

    Write32(position);
    Write32(value);
}

void FoxIREmitter::EmitSaveAbsoluteReg32(uint32 position, FoxIRRegister reg)
{
    // SAVE32r [i32 offset] [%r32]
    WriteOp(IrBase_Save, IrSpecSave_AbsoluteReg32);

    Write32(position);
    Write16(reg);
}

void FoxIREmitter::EmitPush32(uint32 value)
{
    // PUSH32 [i32]
    WriteOp(IrBase_Push, IrSpecPush_Int32);
    Write32(value);

    mStackOffset += 4;
}

void FoxIREmitter::EmitPush32r(FoxIRRegister reg)
{
    // PUSH32r [%r32]
    WriteOp(IrBase_Push, IrSpecPush_Reg32);
    Write16(reg);

    mStackOffset += 4;
}

void FoxIREmitter::EmitStackAlloc(uint16 size)
{
    // SALLOC [u16]

    WriteOp(IrBase_Push, IrSpecPush_StackAlloc);
    Write16(size);

    mStackOffset += size;
}


void FoxIREmitter::EmitPop32(FoxIRRegister output_reg)
{
    // POP32 [%r32]
    WriteOp(IrBase_Pop, (IrSpecPop_Int32 << 4) | (output_reg & 0x0F));

    mStackOffset -= 4;
}

void FoxIREmitter::EmitLoad32(int offset, FoxIRRegister output_reg)
{
    // LOAD [i16] [%r32]
    WriteOp(IrBase_Load, (IrSpecLoad_Int32 << 4) | (output_reg & 0x0F));
    Write16(static_cast<uint16>(offset));
}

void FoxIREmitter::EmitLoadAbsolute32(uint32 position, FoxIRRegister output_reg)
{
    // LOADA [i32] [%r32]
    WriteOp(IrBase_Load, (IrSpecLoad_AbsoluteInt32 << 4) | (output_reg & 0x0F));
    Write32(position);
}

void FoxIREmitter::EmitJumpRelative(uint16 offset)
{
    WriteOp(IrBase_Jump, IrSpecJump_Relative);
    Write16(offset);
}

void FoxIREmitter::EmitJumpAbsolute(uint32 position)
{
    WriteOp(IrBase_Jump, IrSpecJump_Absolute);
    Write32(position);
}


void FoxIREmitter::EmitJumpAbsoluteReg32(FoxIRRegister reg)
{
    WriteOp(IrBase_Jump, IrSpecJump_AbsoluteReg32);
    Write16(reg);
}

void FoxIREmitter::EmitJumpCallAbsolute(uint32 position)
{
    WriteOp(IrBase_Jump, IrSpecJump_CallAbsolute);
    Write32(position);
}


void FoxIREmitter::EmitJumpCallExternal(FoxHash hashed_name)
{
    WriteOp(IrBase_Jump, IrSpecJump_CallExternal);
    Write32(hashed_name);
}

void FoxIREmitter::EmitJumpReturnToCaller()
{
    WriteOp(IrBase_Jump, IrSpecJump_ReturnToCaller);
}

void FoxIREmitter::EmitJumpReturnToCallerReg32(FoxIRRegister reg)
{
    WriteOp(IrBase_Jump, IrSpecJump_ReturnToCaller_Reg32);
    Write16(reg);
}

void FoxIREmitter::EmitJumpReturnToCallerInt32(int32 value)
{
    WriteOp(IrBase_Jump, IrSpecJump_ReturnToCaller_Int32);
    Write32(value);
}

void FoxIREmitter::EmitMoveInt32(FoxIRRegister reg, uint32 value)
{
    WriteOp(IrBase_Move, (IrSpecMove_Int32 << 4) | (reg & 0x0F));
    Write32(value);
}

void FoxIREmitter::EmitMoveReg32(FoxIRRegister dest_reg, FoxIRRegister src_reg)
{
    // Ignore if there is no work to do
    if (dest_reg == src_reg) {
        return;
    }

    WriteOp(IrBase_Move, (IrSpecMove_Reg32 << 4) | (dest_reg & 0x0F));
    Write16(src_reg);
}

void FoxIREmitter::EmitVariableSetInt32(uint16 var_index, int32 value)
{
    WriteOp(IrBase_Variable, IrSpecVariable_Set_Int32);
    Write16(var_index);
    Write32(value);
}

void FoxIREmitter::EmitVariableSetReg32(uint16 var_index, FoxIRRegister reg)
{
    WriteOp(IrBase_Variable, IrSpecVariable_Set_Reg32);
    Write16(var_index);
    Write16(reg);
}

void FoxIREmitter::EmitVariableGetInt32(uint16 var_index, FoxIRRegister dest_reg)
{
    WriteOp(IrBase_Variable, IrSpecVariable_Get_Int32);
    Write16(var_index);
    Write16(dest_reg);
}

void FoxIREmitter::EmitParamsStart()
{
    WriteOp(IrBase_Marker, IrSpecMarker_ParamPushBlockBegin);
}

void FoxIREmitter::EmitType(FoxValue::ValueType type)
{
    IrSpecType op_type = IrSpecType_Int;

    if (type == FoxValue::STRING) {
        op_type = IrSpecType_String;
    }

    WriteOp(IrBase_Type, op_type);
}

uint32 FoxIREmitter::EmitDataString(char* str, uint16 length)
{
    // WriteOp(IrBase_Data, IrSpecData_String);

    uint32 start_index = mBytecode.Size();
    uint16 final_length = length;

    // If the length is not a factor of 2 (sizeof uint16) then add a byte of padding
    if ((final_length & 0x01) != 0) {
        ++final_length;
    }
    // The string is a factor of two, but there isn't going to be a null byte included. Pad with a whole uint16 to preserve alignment.
    else {
        final_length += sizeof(uint16);
    }

    Write16(final_length);


    for (int i = 0; i < final_length; i++) {
        if (i >= length) {
            mBytecode.Insert(0);
            continue;
        }

        mBytecode.Insert(str[i]);
    }


    return start_index;
}


FoxIRRegister FoxIREmitter::EmitBinop(FoxAstBinop* binop, FoxBytecodeVarHandle* handle)
{
    bool rhs_is_binop = false;

    FoxIRRegister lhs_register = EmitRhsToRegister(binop->pLeft, FX_IR_NONE, true);
    FoxIRRegister rhs_register = FX_IR_NONE;

    if (binop->pRight->NodeType == FX_AST_BINOP) {
        rhs_is_binop = true;

        FoxAstBinop* binop_node = reinterpret_cast<FoxAstBinop*>(binop->pRight);
        MarkRegisterFree(rhs_register);
        rhs_register = EmitRhsToRegister(binop_node->pLeft, FX_IR_NONE, true);
    }
    else {
        rhs_register = EmitRhsToRegister(binop->pRight, FX_IR_NONE, true);
    }

    if (binop->OpToken->Type == TT::Plus) {
        WriteOp(IrBase_Arith, IrSpecArith_Add_Reg32);

        mBytecode.Insert(lhs_register);
        mBytecode.Insert(rhs_register);
    }

    if (rhs_is_binop) {
        // MarkRegisterFree(lhs_register);

        FoxAstBinop second_binop;
        second_binop.OpToken = binop->OpToken;
        second_binop.pLeft = binop->pLeft;
        second_binop.pRight = reinterpret_cast<FoxAstBinop*>(binop->pRight)->pRight;

        lhs_register = EmitBinop(&second_binop, handle);
    }

    // We no longer need the lhs or rhs registers, free em
    // MARK_REGISTER_FREE(a_reg);
    MARK_REGISTER_FREE(rhs_register);

    if (handle != nullptr) {
        handle->Register = lhs_register;
    }


    return lhs_register;
}

FoxIRRegister FoxIREmitter::EmitVarFetch(FoxAstVarRef* ref, RhsMode mode)
{
    FoxBytecodeVarHandle* var_handle = FindVarHandle(ref->pName->GetHash());

    // bool force_absolute_load = false;

    // If the variable is from a previous scope, load it from an absolute address. local offsets
    // will change depending on where they are called.
    // if (var_handle->ScopeIndex < mScopeIndex) {
    //     force_absolute_load = true;
    // }

    if (!var_handle) {
        FoxLogError("Could not find var handle!");
        return FX_IR_GW0;
    }

    if (var_handle->Register != FX_IR_NONE) {
        return var_handle->Register;
    }

    FoxIRRegister reg = FindFreeReg32();

    MARK_REGISTER_USED(reg);

    var_handle->Register = reg;

    // DoLoad(var_handle->Offset, reg, force_absolute_load);
    EmitVariableGetInt32(var_handle->VarIndexInScope, reg);

    if (mode == RhsMode::RHS_FETCH_TO_REGISTER) {
        return reg;
    }

    // If we are just copying the variable to this new variable, we can free the register after
    // we push to the stack.
    if (mode == RhsMode::RHS_DEFINE_IN_MEMORY) {
        if (var_handle->Type == FoxValue::STRING) {
            EmitType(var_handle->Type);
        }

        // Save the value register to the new variable

        EmitVariableSetReg32(var_handle->VarIndexInScope, reg);
        // EmitPush32r(reg);
        MARK_REGISTER_FREE(reg);

        return FX_IR_GW0;
    }

    return reg;
}


uint16 FoxIREmitter::GetSizeOfType(FoxTokenizer::Token* token)
{
    const FoxHash type_hash = token->GetHash();

    constexpr FoxHash type_int_hash = FoxHashStr("int");
    constexpr FoxHash type_float_hash = FoxHashStr("float");

    if (type_hash == type_int_hash) {
        return sizeof(int32);
    }
    else if (type_hash == type_float_hash) {
        return sizeof(float32);
    }
    else {
        FoxLogError("UNKNOWN TYPE");
    }

    return 0;
}


void FoxIREmitter::DoLoad(uint32 stack_offset, FoxIRRegister output_reg, bool force_absolute)
{
    if (stack_offset < 0xFFFE && !force_absolute) {
        // Relative load

        // Calculate the relative index to the current stack offset
        int input_offset = -(static_cast<int>(mStackOffset) - static_cast<int>(stack_offset));

        EmitLoad32(input_offset, output_reg);
    }
    else {
        // Absolute load
        EmitLoadAbsolute32(stack_offset, output_reg);
    }
}

void FoxIREmitter::DoSaveInt32(uint32 stack_offset, uint32 value, bool force_absolute)
{
    if (stack_offset < 0xFFFE && !force_absolute) {
        // Relative save

        // Calculate the relative index to the current stack offset
        int input_offset = -(static_cast<int>(mStackOffset) - static_cast<int>(stack_offset));

        EmitSave32(input_offset, value);
    }
    else {
        // Absolute save
        EmitSaveAbsolute32(stack_offset, value);
    }
}

void FoxIREmitter::DoSaveReg32(uint32 stack_offset, FoxIRRegister reg, bool force_absolute)
{
    if (stack_offset < 0xFFFE && !force_absolute) {
        // Relative save

        // Calculate the relative index to the current stack offset
        int input_offset = -(static_cast<int>(mStackOffset) - static_cast<int>(stack_offset));

        EmitSaveReg32(input_offset, reg);
    }
    else {
        // Absolute save
        EmitSaveAbsoluteReg32(stack_offset, reg);
    }
}

void FoxIREmitter::EmitAssign(FoxAstAssign* assign)
{
    FoxBytecodeVarHandle* var_handle = FindVarHandle(assign->Var->pName->GetHash());
    if (var_handle == nullptr) {
        FoxLogError("Var '{:.{}}' does not exist!", assign->Var->pName->Start, assign->Var->pName->Length);
        return;
    }

    // bool force_absolute_save = false;

    // if (var_handle->ScopeIndex < mScopeIndex) {
    //     force_absolute_save = true;
    // }

    // int output_offset = -(static_cast<int>(mStackOffset) - static_cast<int>(var_handle->Offset));

    if (!var_handle) {
        FoxLogError("Could not find var handle to assign to!");
        return;
    }

    EmitRhs(assign->Rhs, RhsMode::RHS_ASSIGN_TO_HANDLE, var_handle);
}

FoxIRRegister FoxIREmitter::EmitLiteralInt(FoxAstLiteral* literal, RhsMode mode, FoxBytecodeVarHandle* handle)
{
    // If this is on variable definition, push the value to the stack.
    if (mode == RhsMode::RHS_DEFINE_IN_MEMORY) {
        EmitPush32(literal->Value.ValueInt);

        return FX_IR_GW3;
    }

    // If this is as a literal, push the value to the stack and pop onto the target register.
    else if (mode == RhsMode::RHS_FETCH_TO_REGISTER) {
        // EmitPush32(literal->Value.ValueInt);

        FoxIRRegister output_reg = FindFreeReg32();
        // EmitPop32(output_reg);

        EmitMoveInt32(output_reg, literal->Value.ValueInt);

        // Mark the output register as used to store it
        MARK_REGISTER_USED(output_reg);

        return output_reg;
    }

    else if (mode == RhsMode::RHS_ASSIGN_TO_HANDLE) {
        // const bool force_absolute_save = (handle->ScopeIndex < mScopeIndex);
        // DoSaveInt32(handle->Offset, literal->Value.ValueInt, force_absolute_save);
        EmitVariableSetInt32(handle->VarIndexInScope, literal->Value.ValueInt);

        return FX_IR_GW3;
    }

    return FX_IR_GW3;
}


FoxIRRegister FoxIREmitter::EmitLiteralString(FoxAstLiteral* literal, RhsMode mode, FoxBytecodeVarHandle* handle)
{
    const uint32 string_length = strlen(literal->Value.ValueString);

    // Emit the length and string data
    const uint32 string_position = EmitDataString(literal->Value.ValueString, string_length);

    // local string some_value = "Some String";
    if (mode == RhsMode::RHS_DEFINE_IN_MEMORY) {
        // Push the location and mark it as a pointer to a string
        EmitType(FoxValue::STRING);
        EmitPush32(string_position);

        return FX_IR_GW3;
    }

    // some_function("Some String")
    else if (mode == RhsMode::RHS_FETCH_TO_REGISTER) {
        // Push the location for the string and pop it back to a register.
        EmitType(FoxValue::STRING);

        // Push the string position
        // EmitPush32(string_position);

        // Find a register to output to and write the index
        FoxIRRegister output_reg = FindFreeReg32();
        // EmitPop32(output_reg);

        EmitMoveInt32(output_reg, string_position);

        // Mark the output register as used to store it
        MARK_REGISTER_USED(output_reg);

        return output_reg;
    }

    // some_previous_value = "Some String";
    else if (mode == RhsMode::RHS_ASSIGN_TO_HANDLE) {
        const bool force_absolute_save = (handle->ScopeIndex < mScopeIndex);

        DoSaveInt32(handle->Offset, string_position, force_absolute_save);
        handle->Type = FoxValue::STRING;

        return FX_IR_GW3;
    }

    return FX_IR_GW3;
}

void FoxIREmitter::EmitMarker(IrSpecMarker spec)
{
    WriteOp(IrBase_Marker, spec);
}

FoxIRRegister FoxIREmitter::EmitRhsToRegister(FoxAstNode* rhs, FoxIRRegister dest_register, bool auto_register)
{
    if (auto_register) {
        dest_register = FindFreeReg32();
    }

    MarkRegisterUsed(dest_register);

    if (rhs->NodeType == FX_AST_LITERAL) {
        FoxAstLiteral* literal = reinterpret_cast<FoxAstLiteral*>(rhs);

        // Move the integer directly into the register
        if (literal->Value.Type == FoxValue::INT) {
            EmitMoveInt32(dest_register, literal->Value.ValueInt);
            return dest_register;
        }

        else if (literal->Value.Type == FoxValue::REF) {
            FoxHash var_name_hash = literal->Value.pValueRef->pName->GetHash();
            FoxBytecodeVarHandle* var_handle = FindVarHandle(var_name_hash);

            if (!var_handle) {
                FoxLogError("Could not find variable handle with hash {}!", var_name_hash);
                return dest_register;
            }

            // If the variable is already loaded into a register and we automatically select the register,
            // return the register that the value is currently loaded into.
            if (var_handle->Register != FX_IR_NONE && auto_register) {
                MarkRegisterFree(dest_register);

                return var_handle->Register;
            }

            // If the variable is already loaded into a register and we want it in `dest_register`, move it into
            // the destination register.
            if (var_handle->Register != FX_IR_NONE) {
                EmitMoveReg32(dest_register, var_handle->Register);
                return dest_register;
            }

            // The variable is not already loaded, so we can load it from the stack into our destination.
            EmitVariableGetInt32(var_handle->VarIndexInScope, dest_register);

            var_handle->Register = dest_register;

            return dest_register;
        }
    }

    else if (rhs->NodeType == FX_AST_BINOP) {
        FoxIRRegister result_register = EmitBinop(reinterpret_cast<FoxAstBinop*>(rhs), nullptr);

        EmitMoveReg32(dest_register, dest_register);

        return dest_register;
    }
    else if (rhs->NodeType == FX_AST_PROCCALL) {
        DoFunctionCall(reinterpret_cast<FoxAstFunctionCall*>(rhs));

        // Move return value into our destination register
        EmitMoveReg32(dest_register, FX_IR_REG_RETURN_VALUE);

        return dest_register;
    }

    FoxLogWarning("No instructions emitted for RHS");

    MarkRegisterFree(dest_register);

    return FX_IR_NONE;
}

FoxIRRegister FoxIREmitter::EmitRhs(FoxAstNode* rhs, FoxIREmitter::RhsMode mode, FoxBytecodeVarHandle* handle)
{
    if (rhs->NodeType == FX_AST_LITERAL) {
        FoxAstLiteral* literal = reinterpret_cast<FoxAstLiteral*>(rhs);

        if (literal->Value.Type == FoxValue::INT) {
            return EmitLiteralInt(literal, mode, handle);
        }
        else if (literal->Value.Type == FoxValue::STRING) {
            return EmitLiteralString(literal, mode, handle);
        }
        else if (literal->Value.Type == FoxValue::REF) {
            // Reference another value, load from memory into register
            FoxIRRegister output_register = EmitVarFetch(literal->Value.pValueRef, mode);
            if (mode == IRRhsMode::RHS_ASSIGN_TO_HANDLE) {
                // DoSaveReg32(handle->Offset, output_register);
                EmitVariableSetReg32(handle->VarIndexInScope, output_register);
            }

            return output_register;
        }

        return FX_IR_GW3;
    }

    else if (rhs->NodeType == FX_AST_PROCCALL || rhs->NodeType == FX_AST_BINOP) {
        FoxIRRegister result_register = FX_IR_GW3;

        if (rhs->NodeType == FX_AST_BINOP) {
            result_register = EmitBinop(reinterpret_cast<FoxAstBinop*>(rhs), handle);
        }

        else if (rhs->NodeType == FX_AST_PROCCALL) {
            DoFunctionCall(reinterpret_cast<FoxAstFunctionCall*>(rhs));
            // Function results are stored in XR
            result_register = FX_IR_REG_RETURN_VALUE;
        }


        if (mode == RhsMode::RHS_DEFINE_IN_MEMORY) {
            uint32 offset = mStackOffset;
            EmitPush32r(result_register);

            MARK_REGISTER_FREE(result_register);

            if (handle) {
                handle->Offset = offset;
            }

            return FX_IR_GW3;
        }

        else if (mode == RhsMode::RHS_FETCH_TO_REGISTER) {
            // Push the result to a register
            // EmitPush32r(result_register);


            // Find a register to output to, and pop the value to there.
            FoxIRRegister output_reg = FindFreeReg32();
            // EmitPop32(output_reg);

            EmitMoveReg32(output_reg, result_register);

            MARK_REGISTER_FREE(result_register);
            // Mark the output register as used to store it
            MARK_REGISTER_USED(output_reg);

            return output_reg;
        }
        else if (mode == IRRhsMode::RHS_ASSIGN_TO_HANDLE) {
            // const bool force_absolute_save = (handle->ScopeIndex < mScopeIndex);

            // Save the value back to the variable
            // DoSaveReg32(handle->Offset, result_register, force_absolute_save);

            EmitVariableSetReg32(handle->VarIndexInScope, result_register);
            handle->Register = result_register;

            MARK_REGISTER_FREE(result_register);

            return FX_IR_GW3;
        }
    }

    return FX_IR_GW3;
}

FoxBytecodeVarHandle* FoxIREmitter::DoVarDeclare(FoxAstVarDecl* decl, VarDeclareMode mode)
{
    RETURN_VALUE_IF_NO_NODE(decl, nullptr);

    // const uint16 size_of_type = static_cast<uint16>(sizeof(int32));

    const FoxHash type_int = FoxHashStr("int");
    const FoxHash type_string = FoxHashStr("string");

    FoxHash decl_hash = decl->Name->GetHash();
    FoxHash type_hash = decl->Type->GetHash();

    FoxValue::ValueType value_type = FoxValue::INT;

    switch (type_hash) {
    case type_int:
        value_type = FoxValue::INT;
        break;
    case type_string:
        value_type = FoxValue::STRING;
        break;
    };

    const uint16 size_of_type = GetSizeOfType(decl->Type);

    FoxBytecodeVarHandle handle {
        .HashedName = decl_hash,
        .Type = value_type, // Just int for now
        .Offset = (mStackOffset),
        .SizeOnStack = size_of_type,
        .ScopeIndex = mScopeIndex,
        .VarIndexInScope = mVarsInScope,
    };

    FoxLogDebug("DEFINING PARAM");

    mVarsInScope++;

    VarHandles.Insert(handle);

    // FoxBytecodeVarHandle* inserted_handle = &VarHandles[VarHandles.Size() - 1];

    FoxBytecodeVarHandle* var_handle = FindVarHandle(decl_hash);

    if (var_handle == nullptr) {
        // printf("!!! Could not find var handle!\n");
        FoxLogError("Could not find var handle!");
        return nullptr;
    }

    if (mode == DECLARE_NO_EMIT) {
        // Do not emit any values
        return var_handle;
    }


    if (decl->Assignment) {
        FoxAstNode* rhs = decl->Assignment->Rhs;

        EmitRhs(rhs, RhsMode::RHS_ASSIGN_TO_HANDLE, var_handle);


        // EmitPush32(0);

        // EmitRhs(rhs, RhsMode::RHS_ASSIGN_TO_HANDLE, inserted_handle);
    }
    else {
        // There is no assignment, push zero as the value for now and
        // a later assignment can set it using save32.
        EmitPush32(0);
    }

    return var_handle;
}

void FoxIREmitter::DoFunctionCall(FoxAstFunctionCall* call)
{
    RETURN_IF_NO_NODE(call);

    FoxBytecodeFunctionHandle* handle = FindFunctionHandle(call->HashedName);

    std::vector<uint32> call_locations;
    call_locations.reserve(8);

    EmitParamsStart();

    int call_location_index = 0;

    uint32 precall_regs_in_use = mRegsInUse;

    int parameter_index = 0;

    // Fetch all parameters into registers
    for (FoxAstNode* param : call->Params) {
        EmitRhsToRegister(param, static_cast<FoxIRRegister>(FX_IR_PARAMREG0 + parameter_index));

        parameter_index++;
    }

    // The handle could not be found, write it as a possible external symbol.
    // if (!handle) {
    //     printf("Call name-> %u\n", call->HashedName);

    //     // Since popping the parameters are handled internally in the VM,
    //     // we need to decrement the stack offset here.
    //     for (int i = 0; i < call->Params.size(); i++) {
    //         mStackOffset -= 4;
    //     }

    //     EmitJumpCallExternal(call->HashedName);

    //     // EmitPop32(FX_REG_RA);
    //     return;
    // }

    // For aarch64 W8-W15 are assummed to be clobbered after a subroutine call

    auto& clobber_list = call->Function->Declaration->ClobberList;

    if (clobber_list.empty()) {
        MarkVariablesAsClobbered(FX_IR_GW0, FX_IR_GW7);
        MarkVariablesAsClobbered(FX_IR_PARAMREG0, FX_IR_PARAMREG3);
    }
    else {
        for (FoxIRRegister clobbered_reg : clobber_list) {
            MarkVariablesAsClobbered(clobbered_reg, clobbered_reg);
        }
    }


    EmitJumpCallAbsolute(handle->HashedName);

    // Free all of the parameter used registers
    mRegsInUse = precall_regs_in_use;
}

void FoxIREmitter::MarkVariablesAsClobbered(FoxIRRegister start_reg, FoxIRRegister end_reg)
{
    for (FoxBytecodeVarHandle& var_handle : VarHandles) {
        if (var_handle.Register >= start_reg && var_handle.Register <= end_reg) {
            var_handle.Register = FX_IR_NONE;
        }
    }
}

FoxBytecodeVarHandle* FoxIREmitter::DefineAndFetchParam(FoxAstNode* param_decl_node, uint16 index, bool alloc_stack_space)
{
    if (param_decl_node->NodeType != FX_AST_VARDECL) {
        FoxLogError("Param node type is not vardecl!");
        return nullptr;
    }

    FoxLogDebug("DEFINING PARAMETER");

    // Emit variable without emitting pushes or pops
    FoxBytecodeVarHandle* handle = DoVarDeclare(reinterpret_cast<FoxAstVarDecl*>(param_decl_node), DECLARE_NO_EMIT);

    if (alloc_stack_space) {
        EmitStackAlloc(4);
    }

    if (!handle) {
        FoxLogError("Could not define and fetch param!");
        return nullptr;
    }

    FoxIRRegister reg = static_cast<FoxIRRegister>(FX_IR_PARAMREG0 + index);

    if ((mRegsInUse & (1u << reg))) {
        FoxLogWarning("Clobbering register {} for function parameter", GetRegisterName(reg));
    }

    MarkRegisterUsed(reg);

    // handle->Register = reg;

    // assert(handle->SizeOnStack == 4);

    // mStackOffset += handle->SizeOnStack;

    return handle;
}

FoxBytecodeVarHandle* FoxIREmitter::DefineReturnVar(FoxAstVarDecl* decl)
{
    RETURN_VALUE_IF_NO_NODE(decl, nullptr);

    return DoVarDeclare(decl);
}

void FoxIREmitter::EmitFunctionDefinitionsInBlock(FoxAstBlock* block)
{
    if (!block) {
        return;
    }

    for (FoxAstNode* stmt : block->Statements) {
        if (stmt->NodeType == FX_AST_PROCDECL) {
            EmitFunction(reinterpret_cast<FoxAstFunctionDecl*>(stmt));
        }
    }
}

void FoxIREmitter::EmitFunction(FoxAstFunctionDecl* function)
{
    RETURN_IF_NO_NODE(function);

    EmitFunctionDefinitionsInBlock(function->Block);

    ++mScopeIndex;

    // Store the bytecode offset before the function is emitted

    const size_t start_of_function = mBytecode.Size();
    printf("Start of function %zu\n", start_of_function);

    // Emit the jump instruction, we will update the jump position after emitting all of the code inside the block
    // EmitJumpRelative(0);

    // const uint32 initial_stack_offset = mStackOffset;

    // const size_t header_jump_start_index = start_of_function + sizeof(uint16);

    size_t start_var_handle_count = VarHandles.Size();

    // Offset for the pushed return address
    mStackOffset += 4;

    // Emit the body of the function
    {
        int parameter_index = 0;

        for (FoxAstNode* param_decl_node : function->Params->Statements) {
            DefineAndFetchParam(param_decl_node, parameter_index, (function->Block != nullptr));

            parameter_index++;
        }

        if (function->Name) {
            EmitMarker(IrSpecMarker_FunctionName);
            FoxLogDebug("Data Name: {:.{}}\n", function->Name->Start, function->Name->Length);
            EmitDataString(function->Name->Start, function->Name->Length);
        }

        if (!function->Block) {
            EmitMarker(IrSpecMarker_ExtFn);
        }

        // Do not check if there are function definitions to be declared when emitting the block here as they are checked above, before any parameters
        // or stack allocations are output.
        EmitBlock(function->Block, parameter_index, true);

        // Check to see if there has been a return statement in the function

        if (function->Block) {
            bool block_has_return = false;

            for (FoxAstNode* statement : function->Block->Statements) {
                if (statement->NodeType == FX_AST_RETURN) {
                    block_has_return = true;
                    break;
                }
            }

            // There is no return statement in the function's block, add a return statement
            if (!block_has_return) {
                EmitJumpReturnToCaller();

                // FoxIRRegister result_register = FindFreeReg32();

                // MARK_REGISTER_USED(result_register);

                // if (return_var != nullptr) {
                //     DoLoad(return_var->Offset, result_register);
                // }
            }
        }
    }

    // Return offset back to pre-call
    mStackOffset -= 4;

    // const size_t end_of_function = mBytecode.Size();
    // const uint16 distance_to_function = static_cast<uint16>(end_of_function - (start_of_function)-4);

    // Update the jump to the end of the function
    // mBytecode[header_jump_start_index] = static_cast<uint8>(distance_to_function >> 8);
    // mBytecode[header_jump_start_index + 1] = static_cast<uint8>((distance_to_function & 0xFF));

    FoxBytecodeFunctionHandle function_handle {.HashedName = function->Name->GetHash(), .BytecodeIndex = static_cast<uint32>(start_of_function)};

    const size_t number_of_scope_var_handles = VarHandles.Size() - start_var_handle_count;
    printf("Number of var handles to remove: %zu\n", number_of_scope_var_handles);

    FunctionHandles.push_back(function_handle);

    --mScopeIndex;

    // Delete the variables on the stack
    for (int i = 0; i < number_of_scope_var_handles; i++) {
        FoxBytecodeVarHandle* var = VarHandles.RemoveLast();
        assert(var->SizeOnStack == 4);
        mStackOffset -= var->SizeOnStack;
    }

    mVarsInScope = 0;
}

void FoxIREmitter::EmitBlock(FoxAstBlock* block, int params_to_save, bool ignore_function_definitions)
{
    RETURN_IF_NO_NODE(block);

    mVarsInScope = params_to_save;

    bool will_emit_entrypoint = false;

    if (!mEntryPointEmitted) {
        will_emit_entrypoint = true;

        // We are going to emit the entry point at the end when we emit this block, but we dont want the function definitions in it to leech and steal
        // our entry marker.
        mEntryPointEmitted = true;
    }

    bool does_block_branch = false;

    if (!ignore_function_definitions) {
        // Before outputting any statements output any function definitions in the block.
        EmitFunctionDefinitionsInBlock(block);
    }

    // For each var declared in the block, write a stack allocation in the frame header
    for (FoxAstNode* node : block->Statements) {
        if (!does_block_branch && DoesNodeBranch(node)) {
            does_block_branch = true;
        }

        // Ignore function definitions when emitting the block statements as they are handled elsewhere!
        if (node->NodeType == FX_AST_PROCDECL) {
            continue;
        }
        else if (node->NodeType == FX_AST_VARDECL) {
            FoxAstVarDecl* var_decl = reinterpret_cast<FoxAstVarDecl*>(node);

            uint32 stack_index = mStackOffset;

            EmitStackAlloc(GetSizeOfType(var_decl->Type));

            FoxBytecodeVarHandle var_handle {
                .HashedName = var_decl->Name->GetHash(),
                .Offset = stack_index,
                .ScopeIndex = mScopeIndex,

                .VarIndexInScope = mVarsInScope,
                .Type = FoxValue::INT,
            };

            VarHandles.Insert(var_handle);

            ++mVarsInScope;
        }
    }

    if (will_emit_entrypoint) {
        EmitMarker(IrSpecMarker_EntryPoint);
    }

    if (does_block_branch) {
        EmitMarker(IrSpecMarker_FunctionBranches);
    }

    // After the stack allocations, mark the start of the frame.
    EmitMarker(IrSpecMarker_FrameBegin);

    if (params_to_save != 0) {
        EmitMarker(IrSpecMarker_ParamsBegin);

        for (int i = 0; i < params_to_save; i++) {
            uint32 base_var_index = (mVarsInScope - params_to_save);
            // uint32 base_var_index = 0;
            EmitVariableSetReg32(base_var_index + i, static_cast<FoxIRRegister>(FX_IR_PARAMREG0 + i));
            MarkRegisterFree(static_cast<FoxIRRegister>(FX_IR_PARAMREG0 + i));
        }

        EmitMarker(IrSpecMarker_ParamsEnd);
    }


    for (FoxAstNode* node : block->Statements) {
        Emit(node);
    }

    mVarsInScope = 0;
    EmitMarker(IrSpecMarker_FrameEnd);
}

void FoxIREmitter::PrintBytecode()
{
    const size_t size = mBytecode.Size();
    for (int i = 0; i < 25; i++) {
        printf("%02d ", i);
    }
    printf("\n");
    for (int i = 0; i < 25; i++) {
        printf("---");
    }
    printf("\n");

    for (size_t i = 0; i < size; i++) {
        printf("%02X ", mBytecode[i]);

        if (i > 0 && ((i + 1) % 25) == 0) {
            printf("\n");
        }
    }
    printf("\n");
}

bool FoxIREmitter::DoesNodeBranch(FoxAstNode* node)
{
    if (node == nullptr) {
        return false;
    }

    if (node->NodeType == FX_AST_PROCCALL) {
        return true;
    }

    else if (node->NodeType == FX_AST_BLOCK) {
        FoxAstBlock* block = reinterpret_cast<FoxAstBlock*>(node);
        for (FoxAstNode* stmt : block->Statements) {
            if (DoesNodeBranch(stmt)) {
                return true;
            }
        }

        return false;
    }

    else if (node->NodeType == FX_AST_VARDECL) {
        FoxAstVarDecl* vardecl = reinterpret_cast<FoxAstVarDecl*>(node);

        return DoesNodeBranch(vardecl->Assignment);
    }
    else if (node->NodeType == FX_AST_BINOP) {
        FoxAstBinop* binop = reinterpret_cast<FoxAstBinop*>(node);
        return (DoesNodeBranch(binop->pLeft) || DoesNodeBranch(binop->pRight));
    }

    else if (node->NodeType == FX_AST_ASSIGN) {
        FoxAstAssign* assign = reinterpret_cast<FoxAstAssign*>(node);

        return DoesNodeBranch(assign->Rhs);
    }

    return false;
}

#pragma endregion IrEmitter


/////////////////////////////////////
// IR Printer
/////////////////////////////////////

uint16 FoxIRPrinter::Read16()
{
    uint8 lo = mBytecode[mBytecodeIndex++];
    uint8 hi = mBytecode[mBytecodeIndex++];

    return ((static_cast<uint16>(lo) << 8) | hi);
}

uint32 FoxIRPrinter::Read32()
{
    uint16 lo = Read16();
    uint16 hi = Read16();

    return ((static_cast<uint32>(lo) << 16) | hi);
}

#define BC_PRINT_OP(fmt_, ...) FoxLog<FoxLogChannel::None>(fmt_, ##__VA_ARGS__)

void FoxIRPrinter::DoLoad(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == IrSpecLoad_Int32) {
        int16 offset = Read16();
        BC_PRINT_OP("load [i32] {}, {}", offset, FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg)));
    }
    else if (op_spec == IrSpecLoad_AbsoluteInt32) {
        uint32 offset = Read32();
        BC_PRINT_OP("loada [i32] {}, {}", offset, FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg)));
    }
}

void FoxIRPrinter::DoPush(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == IrSpecPush_Int32) {
        uint32 value = Read32();
        BC_PRINT_OP("push [i32] {}", value);
    }
    else if (op_spec == IrSpecPush_Reg32) {
        uint16 reg = Read16();
        BC_PRINT_OP("push [r32] {}", FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(reg)));
    }
    else if (op_spec == IrSpecPush_StackAlloc) {
        uint16 size = Read16();
        BC_PRINT_OP("salloc {}", size);
    }
}

void FoxIRPrinter::DoPop(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == IrSpecPop_Int32) {
        BC_PRINT_OP("pop [i32] {}", FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg)));
    }
}

void FoxIRPrinter::DoArith(char* s, uint8 op_base, uint8 op_spec)
{
    uint8 a_reg = mBytecode[mBytecodeIndex++];
    uint8 b_reg = mBytecode[mBytecodeIndex++];

    if (op_spec == IrSpecArith_Add_Reg32) {
        BC_PRINT_OP("add [i32] {}, {}", FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(a_reg)),
                    FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(b_reg)));
    }
}

void FoxIRPrinter::DoSave(char* s, uint8 op_base, uint8 op_spec)
{
    // Save a imm32 into an offset in the stack
    if (op_spec == IrSpecSave_Int32) {
        const int16 offset = Read16();
        const uint32 value = Read32();

        BC_PRINT_OP("save [i32] {}, {}", offset, value);
    }

    // Save a register into an offset in the stack
    else if (op_spec == IrSpecSave_Reg32) {
        const int16 offset = Read16();
        uint16 reg = Read16();

        BC_PRINT_OP("save [r32] {}, {}", offset, FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(reg)));
    }
    else if (op_spec == IrSpecSave_AbsoluteInt32) {
        const uint32 offset = Read32();
        const uint32 value = Read32();

        BC_PRINT_OP("savea [i32] {}, {}", offset, value);
    }
    else if (op_spec == IrSpecSave_AbsoluteReg32) {
        const uint32 offset = Read32();
        uint16 reg = Read16();

        BC_PRINT_OP("savea [r32] {}, {}", offset, FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(reg)));
    }
}

void FoxIRPrinter::DoJump(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == IrSpecJump_Relative) {
        uint16 offset = Read16();
        BC_PRINT_OP("jmpr {}", offset);
    }
    else if (op_spec == IrSpecJump_Absolute) {
        uint32 position = Read32();
        BC_PRINT_OP("jmpa {}", position);
    }
    else if (op_spec == IrSpecJump_AbsoluteReg32) {
        uint16 reg = Read16();
        BC_PRINT_OP("jmpar {}", FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(reg)));
    }
    else if (op_spec == IrSpecJump_CallAbsolute) {
        uint32 position = Read32();
        BC_PRINT_OP("calla {}", position);
    }
    else if (op_spec == IrSpecJump_ReturnToCaller) {
        BC_PRINT_OP("ret");
    }
    else if (op_spec == IrSpecJump_ReturnToCaller_Reg32) {
        const char* reg_name = FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(Read16()));

        BC_PRINT_OP("ret [r32] {}", reg_name);
    }
    else if (op_spec == IrSpecJump_ReturnToCaller_Int32) {
        int32 value = Read32();
        BC_PRINT_OP("ret [i32] {}", value);
    }
    else if (op_spec == IrSpecJump_CallExternal) {
        uint32 hashed_name = Read32();
        BC_PRINT_OP("callext {}", hashed_name);
    }
}


void FoxIRPrinter::DoData(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == IrSpecData_String) {
        uint16 length = Read16();
        char* data_str = FX_SCRIPT_ALLOC_MEMORY(char, length);
        uint16* data_str16 = reinterpret_cast<uint16*>(data_str);

        uint32 bytecode_end = mBytecodeIndex + length;
        int data_index = 0;
        while (mBytecodeIndex < bytecode_end) {
            uint16 value16 = Read16();
            data_str16[data_index++] = ((value16 << 8) | (value16 >> 8));
        }

        BC_PRINT_OP("datastr {}, {:.{}}", length, data_str, length);

        FX_SCRIPT_FREE(char, data_str);
    }
}

void FoxIRPrinter::DoType(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == IrSpecType_Int) {
        BC_PRINT_OP("type int");
    }
    else if (op_spec == IrSpecType_String) {
        BC_PRINT_OP("type str");
    }
}

void FoxIRPrinter::DoMove(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == IrSpecMove_Int32) {
        uint32 value = Read32();
        BC_PRINT_OP("move [i32] {}, {}", FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg)), value);
    }
    else if (op_spec == IrSpecMove_Reg32) {
        const char* dest_reg = FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg));
        const char* src_reg = FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(Read16()));

        BC_PRINT_OP("move [r32] {}, {}", dest_reg, src_reg);
    }
}

void FoxIRPrinter::DoMarker(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == IrSpecMarker_FrameBegin) {
        BC_PRINT_OP("@FrameBegin");
    }
    else if (op_spec == IrSpecMarker_FrameEnd) {
        BC_PRINT_OP("@FrameEnd");
    }
    else if (op_spec == IrSpecMarker_ParamPushBlockBegin) {
        BC_PRINT_OP("@Params");
    }
    else if (op_spec == IrSpecMarker_ParamsBegin) {
        BC_PRINT_OP("PARAMS BEGIN");
    }
    else if (op_spec == IrSpecMarker_ParamsEnd) {
        BC_PRINT_OP("PARAMS END");
    }
    else if (op_spec == IrSpecMarker_EntryPoint) {
        BC_PRINT_OP("@Entry");
    }
    else if (op_spec == IrSpecMarker_FunctionBranches) {
        BC_PRINT_OP("@Branches");
    }
    else if (op_spec == IrSpecMarker_FunctionName) {
        char name_buffer[256];
        uint32 name_length = Read16();

        int name_index = 0;
        for (name_index = 0; name_index < name_length; name_index += 2) {
            *(reinterpret_cast<uint16*>(&name_buffer[name_index])) = ReverseInt16(Read16());
        }

        BC_PRINT_OP("@FunctionName {:.{}}", name_buffer, name_length);
    }
    else if (op_spec == IrSpecMarker_ExtFn) {
        BC_PRINT_OP("@ExtFn");
    }
}


void FoxIRPrinter::DoVariable(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == IrSpecVariable_Get_Int32) {
        uint16 var_index = Read16();
        FoxIRRegister dest_reg = static_cast<FoxIRRegister>(Read16());
        BC_PRINT_OP("vget [i32] ${}, {}", var_index, FoxIREmitter::GetRegisterName(dest_reg));
    }
    else if (op_spec == IrSpecVariable_Set_Int32) {
        uint16 var_index = Read16();
        uint32 value = Read32();
        BC_PRINT_OP("vset [i32] ${}, {}", var_index, value);
    }
    else if (op_spec == IrSpecVariable_Set_Reg32) {
        uint16 var_index = Read16();
        FoxIRRegister reg = static_cast<FoxIRRegister>(Read16());
        BC_PRINT_OP("vset [r32] ${}, {}", var_index, FoxIREmitter::GetRegisterName(reg));
    }
}


void FoxIRPrinter::Print()
{
    while (mBytecodeIndex < mBytecode.Size()) {
        PrintOp();
    }
}

void FoxIRPrinter::PrintOp()
{
    // uint32 bc_index = mBytecodeIndex;

    uint16 op_full = Read16();

    const uint8 op_base = static_cast<uint8>(op_full >> 8);
    const uint8 op_spec = static_cast<uint8>(op_full & 0xFF);

    char s[128];

    switch (op_base) {
    case IrBase_Push:
        DoPush(s, op_base, op_spec);
        break;
    case IrBase_Pop:
        DoPop(s, op_base, op_spec);
        break;
    case IrBase_Load:
        DoLoad(s, op_base, op_spec);
        break;
    case IrBase_Arith:
        DoArith(s, op_base, op_spec);
        break;
    case IrBase_Jump:
        DoJump(s, op_base, op_spec);
        break;
    case IrBase_Save:
        DoSave(s, op_base, op_spec);
        break;
    case IrBase_Data:
        DoData(s, op_base, op_spec);
        break;
    case IrBase_Type:
        DoType(s, op_base, op_spec);
        break;
    case IrBase_Move:
        DoMove(s, op_base, op_spec);
        break;
    case IrBase_Marker:
        DoMarker(s, op_base, op_spec);
        break;
    case IrBase_Variable:
        DoVariable(s, op_base, op_spec);
        break;
    }
}
