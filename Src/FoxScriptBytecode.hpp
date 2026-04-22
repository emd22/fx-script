#pragma once

#include "FoxScriptUtil.hpp"


/////////////////////////////////////////////
// NEW IR Bytecode
/////////////////////////////////////////////


enum FoxBytecodeBase : uint8
{
    BcBase_Push = 1,
    BcBase_Pop,
    BcBase_Load,
    BcBase_Arith,
    BcBase_Save,
    BcBase_Jump,
    BcBase_Data,
    BcBase_Type,
    BcBase_Move,
    BcBase_Marker,

    BcBase_Variable,
};

enum BcSpecPush : uint8
{
    BcSpecPush_Int32 = 1, // PUSH32  [imm]
    BcSpecPush_Reg32,     // PUSH32r [%r32]
    BcSpecPush_Var,       // VPUSH [%var]

    BcSpecPush_StackAlloc,
};

enum BrSpecPop : uint8
{
    BcSpecPop_Int32 = 1, // PIr32 [%r32]
    BcSpecPop_Variable_Int32,
};

enum BrSpecLoad : uint8
{
    BcSpecLoad_Int32 = 1, // LOAD32 [offset] [%r32]
    BcSpecLoad_AbsoluteInt32,
};

enum BcSpecArith : uint8
{
    BcSpecArith_Add = 1 // ADD [%var] [%var]
};

enum BcSpecSave : uint8
{
    BcSpecSave_Int32 = 1,
    BcSpecSave_Reg32,
    BcSpecSave_AbsoluteInt32,
    BcSpecSave_AbsoluteReg32
};

enum BcSpecJump : uint8
{
    BcSpecJump_Relative = 1,
    BcSpecJump_Absolute,
    BcSpecJump_AbsoluteReg32,

    BcSpecJump_CallAbsolute,

    BcSpecJump_ReturnToCaller,
    BcSpecJump_ReturnToCaller_Int32,
    BcSpecJump_ReturnToCaller_Reg32,

    BcSpecJump_CallExternal,
};

enum BcSpecData : uint8
{
    BcSpecData_String = 1,
};

enum BcSpecType : uint8
{
    BcSpecType_Int = 1,
    BcSpecType_String,
};

enum BcSpecMove : uint8
{
    BcSpecMove_Int32 = 1,
    BcSpecMove_Reg32,
};

enum BcSpecMarker : uint8
{
    // Function frame ops
    BcSpecMarker_FrameBegin = 1,
    BcSpecMarker_FrameEnd,

    BcSpecMarker_ParamsBegin,
    BcSpecMarker_ParamRegBlockBegin,
    BcSpecMarker_ParamRegBlockEnd,

    BcSpecMarker_EntryPoint,
    BcSpecMarker_FunctionBranches,

    BcSpecMarker_FunctionName,

    BcSpecMarker_ExtFn,

    BcSpecMarker_Proc,
    BcSpecMarker_ExternalProc,

    BcSpecMarker_ProcEnd,

};

enum BcSpecVariable : uint8
{
    BcSpecVariable_Set_Int32 = 1,
    BcSpecVariable_Set_Reg32,
    BcSpecVariable_Set_Var,
    BcSpecVariable_Get_Int32,

    BcSpecVariable_Define_Int32,
};


/////////////////////////////////////////////
// IR Bytecode
/////////////////////////////////////////////


enum IrBase : uint8
{
    IrBase_Push = 1,
    IrBase_Pop,
    IrBase_Load,
    IrBase_Arith,
    IrBase_Save,
    IrBase_Jump,
    IrBase_Data,
    IrBase_Type,
    IrBase_Move,
    IrBase_Marker,

    IrBase_Variable,
};

enum IrSpecPush : uint8
{
    IrSpecPush_Int32 = 1, // PUSH32  [imm]
    IrSpecPush_Reg32,     // PUSH32r [%r32]

    IrSpecPush_StackAlloc,
};

enum IrSpecPop : uint8
{
    IrSpecPop_Int32 = 1, // PIr32 [%r32]
};

enum IrSpecLoad : uint8
{
    IrSpecLoad_Int32 = 1, // LOAD32 [offset] [%r32]
    IrSpecLoad_AbsoluteInt32,
};

enum IrSpecArith : uint8
{
    IrSpecArith_Add_Reg32 = 1 // ADD [%r32] [%r32]
};

enum IrSpecSave : uint8
{
    IrSpecSave_Int32 = 1,
    IrSpecSave_Reg32,
    IrSpecSave_AbsoluteInt32,
    IrSpecSave_AbsoluteReg32
};

enum IrSpecJump : uint8
{
    IrSpecJump_Relative = 1,
    IrSpecJump_Absolute,
    IrSpecJump_AbsoluteReg32,

    IrSpecJump_CallAbsolute,

    IrSpecJump_ReturnToCaller,
    IrSpecJump_ReturnToCaller_Int32,
    IrSpecJump_ReturnToCaller_Reg32,

    IrSpecJump_CallExternal,
};

enum IrSpecData : uint8
{
    IrSpecData_String = 1,
};

enum IrSpecType : uint8
{
    IrSpecType_Int = 1,
    IrSpecType_String,
};

enum IrSpecMove : uint8
{
    IrSpecMove_Int32 = 1,
    IrSpecMove_Reg32,
};

enum IrSpecMarker : uint8
{
    // Function frame ops
    IrSpecMarker_FrameBegin = 1,
    IrSpecMarker_FrameEnd,

    IrSpecMarker_ParamPushBlockBegin,
    IrSpecMarker_ParamsBegin,
    IrSpecMarker_ParamsEnd,

    IrSpecMarker_EntryPoint,
    IrSpecMarker_FunctionBranches,

    IrSpecMarker_FunctionName,

    IrSpecMarker_ExtFn,
};

enum IrSpecVariable : uint8
{
    IrSpecVariable_Set_Int32 = 1,
    IrSpecVariable_Set_Reg32,
    IrSpecVariable_Get_Int32,
};
