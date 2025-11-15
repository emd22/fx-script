#include "FoxCGArm64.hpp"

#include "FoxLog.hpp"

/////////////////////////////////////
// FxIRToArm64
/////////////////////////////////////

uint16 FoxIRToArm64::Read16()
{
    uint8 lo = mBytecode[mBytecodeIndex++];
    uint8 hi = mBytecode[mBytecodeIndex++];

    return ((static_cast<uint16>(lo) << 8) | hi);
}

uint32 FoxIRToArm64::Read32()
{
    uint16 lo = Read16();
    uint16 hi = Read16();

    return ((static_cast<uint32>(lo) << 16) | hi);
}

void FoxIRToArm64::DoLoad(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == IrSpecLoad_Int32) {
        int16 offset = Read16();
        FoxAsm("load [i32] {}, {}", offset, FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg)));
    }
    else if (op_spec == IrSpecLoad_AbsoluteInt32) {
        uint32 offset = Read32();
        FoxAsm("loada [i32] {}, {}", offset, FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg)));
    }
}

void FoxIRToArm64::DoPush(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == IrSpecPush_Int32) {
        uint32 value = Read32();
        FoxAsm("push [i32] {}", value);
    }
    else if (op_spec == IrSpecPush_Reg32) {
        uint16 reg = Read16();
        FoxAsm("push [r32] {}", FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(reg)));
    }
    else if (op_spec == IrSpecPush_StackAlloc) {
        uint16 size = Read16();

        PreFrameStackAllocation += size;

        FoxAsm("// SAlloc {}", size);
    }
}

void FoxIRToArm64::DoPop(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == IrSpecPop_Int32) {
        FoxAsm("pop [i32] {}", FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg)));
    }
}

void FoxIRToArm64::DoArith(char* s, uint8 op_base, uint8 op_spec)
{
    FoxIRRegister a_reg = static_cast<FoxIRRegister>(mBytecode[mBytecodeIndex++]);
    FoxIRRegister b_reg = static_cast<FoxIRRegister>(mBytecode[mBytecodeIndex++]);

    if (op_spec == IrSpecArith_Add_Reg32) {
        // BC_PRINT_OP("add [i32] %s, %s", FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(a_reg)),
        //             FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(b_reg)));

        const char* lhs_reg = GetRegisterName(GetArmRegFromIRReg(a_reg));
        const char* rhs_reg = GetRegisterName(GetArmRegFromIRReg(b_reg));

        FoxAsm("add {}, {}, {}", lhs_reg, lhs_reg, rhs_reg);
    }
}

void FoxIRToArm64::DoSave(char* s, uint8 op_base, uint8 op_spec)
{
    // Save a imm32 into an offset in the stack
    if (op_spec == IrSpecSave_Int32) {
        const int16 offset = Read16();
        const uint32 value = Read32();

        FoxAsm("save [i32] {}, {}", offset, value);
    }

    // Save a register into an offset in the stack
    else if (op_spec == IrSpecSave_Reg32) {
        const int16 offset = Read16();
        uint16 reg = Read16();

        FoxAsm("save [r32] %d, %s", offset, FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(reg)));
    }
    else if (op_spec == IrSpecSave_AbsoluteInt32) {
        const uint32 offset = Read32();
        const uint32 value = Read32();

        FoxAsm("savea [i32] {}, {}", offset, value);
    }
    else if (op_spec == IrSpecSave_AbsoluteReg32) {
        const uint32 offset = Read32();
        uint16 reg = Read16();

        FoxAsm("savea [r32] {}, {}", offset, FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(reg)));
    }
}

void FoxIRToArm64::EmitFrameRestore()
{
    FoxIRArm64Frame* current_frame = GetCurrentFrame();

    if (mFunctionContainsBranches) {
        FoxAsm("ldp x29, x30, [sp, #{}]", GetCurrentFrame()->StackAllocated);
    }

    if (current_frame->StackAllocated != 0 || mFunctionContainsBranches) {
        int total_allocated = current_frame->StackAllocated;

        // If we do branch, we have saved the FP and LR registers to the stack. Each one is 8 bytes long (uint64), so we will need to keep that in
        // mind when restoring the stack frame.
        if (mFunctionContainsBranches) {
            total_allocated += sizeof(uint64) * 2;
        }

        FoxAsm("add sp, sp, #{}", total_allocated);
    }

    mFunctionContainsBranches = false;
}

void FoxIRToArm64::DoJump(char* s, uint8 op_base, uint8 op_spec)
{
    FoxIRArm64Frame* current_frame = GetCurrentFrame();

    if (op_spec == IrSpecJump_Relative) {
        uint16 offset = Read16();
        FoxAsm("jmpr {}", offset);
    }
    else if (op_spec == IrSpecJump_Absolute) {
        uint32 position = Read32();
        FoxAsm("jmpa {}", position);
    }
    else if (op_spec == IrSpecJump_AbsoluteReg32) {
        uint16 reg = Read16();
        FoxAsm("jmpar {}", FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(reg)));
    }
    else if (op_spec == IrSpecJump_CallAbsolute) {
        mInParamsBlock = false;

        uint32 hashed_name = Read32();
        FoxIRFunctionRef* ref = GetFunctionRefFromHash(hashed_name);
        if (ref) {
            FoxAsm("bl {}", ref->Name);
        }
        else {
            FoxLogError("Could not find label with hash {}!", hashed_name);
        }
    }
    else if (op_spec == IrSpecJump_CallExternal) {
        uint32 hashed_name = Read32();

        FoxAsm("callext {}", hashed_name);
    }

    else if (op_spec == IrSpecJump_ReturnToCaller) {
        current_frame->HasBaselevelReturnStmt = true;
        EmitFrameRestore();
        FoxAsm("ret");
    }
    else if (op_spec == IrSpecJump_ReturnToCaller_Reg32) {
        current_frame->HasBaselevelReturnStmt = true;

        const FoxArm64Register value_reg = GetArmRegFromIRReg(static_cast<FoxIRRegister>(Read16()));

        if (value_reg != Fox_Arm64_W0) {
            FoxAsm("mov w0, {}", GetRegisterName(value_reg));
        }

        EmitFrameRestore();
        FoxAsm("ret");
    }
    else if (op_spec == IrSpecJump_ReturnToCaller_Int32) {
        current_frame->HasBaselevelReturnStmt = true;

        FoxAsm("mov w0, #{}", Read32());
        EmitFrameRestore();
        FoxAsm("ret");
    }
}


void FoxIRToArm64::DoData(char* s, uint8 op_base, uint8 op_spec)
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

        FX_SCRIPT_FREE(char, data_str);
    }
}

void FoxIRToArm64::DoType(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == IrSpecType_Int) {
        FoxAsm("// type int");
    }
    else if (op_spec == IrSpecType_String) {
        FoxAsm("// type str");
    }
}

void FoxIRToArm64::DoMove(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == IrSpecMove_Int32) {
        int value = static_cast<int32>(Read32());
        const FoxArm64Register dest_reg = GetArmRegFromIRReg(static_cast<FoxIRRegister>(op_reg));

        // BC_PRINT_OP("move [i32] %s, %u\t", FoxIREmitter::GetRegisterName(), value);

        FoxAsm("mov {}, #{}", GetRegisterName(dest_reg), value);
    }
    else if (op_spec == IrSpecMove_Reg32) {
        const FoxArm64Register dest_reg = GetArmRegFromIRReg(static_cast<FoxIRRegister>(op_reg));
        const FoxArm64Register src_reg = GetArmRegFromIRReg(static_cast<FoxIRRegister>(Read16()));

        FoxAsm("mov {}, {}", GetRegisterName(dest_reg), GetRegisterName(src_reg));
    }
}


void FoxIRToArm64::DoMarker(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == IrSpecMarker_FrameBegin) {
        uint32 stack_allocation = MakeValueFactorOf16(PreFrameStackAllocation);

        // Storage for the frame pointer and link register (x29 & x30)

        FoxIRArm64Frame* current_frame = FramePush();
        current_frame->StackAllocated = stack_allocation;

        constexpr uint32 size_of_opcode = sizeof(uint16);

        // The function definition index is the current bytecode index minus the size of an opcode,
        // as the current opcode is loaded into memory(thus offsetting the bytecode index)
        const uint32 function_bytecode_index = mBytecodeIndex - size_of_opcode;

        FoxIRFunctionRef function_ref {
            .Name = FX_SCRIPT_ALLOC_MEMORY(char, mCurrentLabelNameLength + 1),
            .Position = function_bytecode_index,
        };

        memcpy(function_ref.Name, mCurrentLabelName, mCurrentLabelNameLength);
        function_ref.Name[mCurrentLabelNameLength] = 0;
        function_ref.HashedName = FoxHashStr(function_ref.Name);

        mFunctionRefs.Insert(function_ref);

        if (mEmitDefinitionAsEntryPoint) {
            FoxAsm("_main:");
        }
        else {
            FoxAsm("{}:", function_ref.Name);
        }

        FoxAsmIncreaseIndent();

        // If there are no stack allocations and the current frame does not branch, do not create a new stack frame.
        if (current_frame->StackAllocated != 0 || mFunctionContainsBranches) {
            int total_allocated = current_frame->StackAllocated;

            // If we do branch, we need to save the FP and LR registers to the stack. Each one is 8 bytes long (uint64).
            if (mFunctionContainsBranches) {
                total_allocated += sizeof(uint64) * 2;
            }

            // Allocate the stack frame
            FoxAsm("sub sp, sp, #{}", total_allocated);
        }

        if (mFunctionContainsBranches) {
            FoxAsm("stp x29, x30, [sp, #{}]", current_frame->StackAllocated);
            // Move the FP back to ignore the storage for the above
            FoxAsm("add x29, sp, #16");
        }
    }
    else if (op_spec == IrSpecMarker_FrameEnd) {
        FoxIRArm64Frame* current_frame = GetCurrentFrame();
        // If there is a return statement on base level (without branching, conditions, etc.) then we can
        // omit the frame restore logic here as it will already be covered by the return statement.
        if (current_frame && !current_frame->HasBaselevelReturnStmt) {
            EmitFrameRestore();
        }

        FoxAsmDecreaseIndent();

        FramePop();

        // Reset the current stack frame
    }
    else if (op_spec == IrSpecMarker_ParamsBegin) {
        mInParamsBlock = true;
    }

    else if (op_spec == IrSpecMarker_ParamRegBlockBegin) {
        mInParamsBlock = true;
    }
    else if (op_spec == IrSpecMarker_ParamRegBlockEnd) {
        mInParamsBlock = false;
    }

    else if (op_spec == IrSpecMarker_EntryPoint) {
        mEmitDefinitionAsEntryPoint = true;
    }
    else if (op_spec == IrSpecMarker_FunctionBranches) {
        mFunctionContainsBranches = true;
    }
    else if (op_spec == IrSpecMarker_FunctionName) {
        uint32 name_length = Read16();

        int name_index = 0;
        for (name_index = 0; name_index < name_length; name_index += 2) {
            *(reinterpret_cast<uint16*>(&mCurrentLabelName[name_index])) = ReverseInt16(Read16());
        }

        mCurrentLabelNameLength = name_index;
    }
    else if (op_spec == IrSpecMarker_ExtFn) {
        FoxIRFunctionRef function_ref {
            .Name = FX_SCRIPT_ALLOC_MEMORY(char, mCurrentLabelNameLength + 1),
            .Position = mBytecodeIndex,
        };

        memcpy(function_ref.Name, mCurrentLabelName, mCurrentLabelNameLength);
        function_ref.Name[mCurrentLabelNameLength] = 0;
        function_ref.HashedName = FoxHashStr(function_ref.Name);

        mFunctionRefs.Insert(function_ref);
    }
}


void FoxIRToArm64::DoVariable(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == IrSpecVariable_Get_Int32) {
        uint16 var_index = Read16();

        const FoxIRRegister dest_ir_reg = static_cast<FoxIRRegister>(Read16());
        const FoxArm64Register dest_reg = GetArmRegFromIRReg(dest_ir_reg);

        const uint32 var_stack_offset = GetCurrentFrame()->StackAllocated - ((var_index + 1) * 4);

        FoxAsm("ldr {}, [sp, #{}]", GetRegisterName(dest_reg), var_stack_offset);
    }
    else if (op_spec == IrSpecVariable_Set_Int32) {
        uint16 var_index = Read16();
        uint32 value = Read32();

        FoxArm64Register reg = RegisterRequest(Usage_General);

        const char* reg_name = GetRegisterName(reg);

        FoxAsm("mov {}, #{}", reg_name, static_cast<int32>(value));

        const uint32 var_stack_offset = GetCurrentFrame()->StackAllocated - ((var_index + 1) * 4);

        FoxAsm("str {}, [sp, #{}]", reg_name, var_stack_offset);

        RegisterRelease(reg);
    }
    else if (op_spec == IrSpecVariable_Set_Reg32) {
        uint16 var_index = Read16();
        FoxIRRegister reg = static_cast<FoxIRRegister>(Read16());

        const uint32 var_stack_offset = GetCurrentFrame()->StackAllocated - ((var_index + 1) * 4);

        FoxAsm("str {}, [sp, #{}]", GetRegisterName(GetArmRegFromIRReg(reg)), var_stack_offset);
    }
}


void FoxIRToArm64::Print()
{
    FoxAsm(".global _main\n");
    while (mBytecodeIndex < mBytecode.Size()) {
        PrintOp();
    }
}

void FoxIRToArm64::PrintOp()
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

    // printf("%-25s", s);

    // printf("\t// Offset: %u", bc_index);
}

uint32 FoxIRToArm64::MakeValueFactorOf16(uint32 value)
{
    // Get the bottom four bits (value % 16)
    const uint8 remainder = (value & 0x0F);

    if (remainder != 0) {
        value += (16 - remainder);
    }

    return value;
}

FoxArm64Register FoxIRToArm64::RegisterRequest(FoxIRToArm64::RegisterUsage usage)
{
    FoxArm64Register min, max;

    switch (usage) {
    case Usage_Parameters:
        min = Fox_Arm64_W0;
        max = Fox_Arm64_W7;
        break;
    case Usage_General:
        min = Fox_Arm64_W8;
        max = Fox_Arm64_W15;
        break;
    case Usage_Return:
        RegisterRelease(Fox_Arm64_W0);

        min = Fox_Arm64_W0;
        max = Fox_Arm64_W0;
        break;
    }

    while (min <= max) {
        const uint32 reg_flag = (1 << min);

        FoxIRArm64Frame* current_frame = &mStackFrames.GetLast();

        // If register is not in use, return it
        if (!(current_frame->RegistersInUse & reg_flag)) {
            // Mark the register as in use
            current_frame->RegistersInUse |= reg_flag;

            return min;
        }

        // Increment the register
        min = static_cast<FoxArm64Register>(static_cast<uint32>(min) + 1);
    }


    return Fox_Arm64_None;
}


void FoxIRToArm64::RegisterRelease(FoxArm64Register reg)
{
    GetCurrentFrame()->RegistersInUse &= ~(1 << reg);
}

bool FoxIRToArm64::IsRegisterInUse(FoxArm64Register reg)
{
    return (GetCurrentFrame()->RegistersInUse & (1 << static_cast<uint32>(reg)));
}


FoxArm64Register FoxIRToArm64::GetArmRegFromIRReg(FoxIRRegister ir_reg)
{
    switch (ir_reg) {
    /* Parameter registers */
    case FX_IR_REG_RETURN_VALUE:
        [[fallthrough]];
    case FX_IR_PARAMREG0:
        return Fox_Arm64_W0;
    case FX_IR_PARAMREG1:
        return Fox_Arm64_W1;
    case FX_IR_PARAMREG2:
        return Fox_Arm64_W2;
    case FX_IR_PARAMREG3:
        return Fox_Arm64_W3;
    /* General purpose registers */
    case FX_IR_GW0:
        return Fox_Arm64_W8;
    case FX_IR_GW1:
        return Fox_Arm64_W9;
    case FX_IR_GW2:
        return Fox_Arm64_W10;
    case FX_IR_GW3:
        return Fox_Arm64_W11;

    // case FX_IR_REG_RETURN_VALUE:
    //     return Fox_Arm64_W0;
    default:
        break;
    }

    uint32 register_offset = static_cast<uint32>(Fox_Arm64_W8);

    if (mInParamsBlock) {
        register_offset = static_cast<uint32>(Fox_Arm64_W0);
    }

    return static_cast<FoxArm64Register>(register_offset + static_cast<uint32>(ir_reg));
}

FoxIRArm64Frame* FoxIRToArm64::GetCurrentFrame()
{
    if (mStackFrames.IsEmpty()) {
        return nullptr;
    }

    return &mStackFrames.GetLast();
}

FoxIRArm64Frame* FoxIRToArm64::FramePush()
{
    if (!mStackFrames.IsInited()) {
        mStackFrames.Create(16);
    }

    PreFrameStackAllocation = 0;

    return mStackFrames.Insert();
}

void FoxIRToArm64::FramePop()
{
    mStackFrames.RemoveLast();
}


const char* FoxIRToArm64::GetRegisterName(FoxArm64Register reg)
{
    switch (reg) {
    case Fox_Arm64_W0:
        return "w0";
    case Fox_Arm64_W1:
        return "w1";
    case Fox_Arm64_W2:
        return "w2";
    case Fox_Arm64_W3:
        return "w3";
    case Fox_Arm64_W4:
        return "w4";
    case Fox_Arm64_W5:
        return "w5";
    case Fox_Arm64_W6:
        return "w6";
    case Fox_Arm64_W7:
        return "w7";
    case Fox_Arm64_W8:
        return "w8";
    case Fox_Arm64_W9:
        return "w9";
    case Fox_Arm64_W10:
        return "w10";
    case Fox_Arm64_W11:
        return "w11";
    case Fox_Arm64_W12:
        return "w12";
    case Fox_Arm64_W13:
        return "w13";
    case Fox_Arm64_W14:
        return "w14";
    case Fox_Arm64_W15:
        return "w15";
    case Fox_Arm64_None:
        return "(none)";
    }

    return "(unknown)";
}

FoxIRFunctionRef* FoxIRToArm64::GetFunctionRefFromHash(uint32 hashed_name)
{
    for (FoxIRFunctionRef& ref : mFunctionRefs) {
        if (ref.HashedName == hashed_name) {
            return &ref;
        }
    }
    return nullptr;
}
