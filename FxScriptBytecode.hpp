#pragma once

#include "FxScriptUtil.hpp"


/*
[BASE] [SPEC]
*/

enum OpBase : uint8
{
    OpBase_Push = 1,
    OpBase_Pop,
    OpBase_Load,
    OpBase_Arith,
    OpBase_Save,
    OpBase_Jump,
    OpBase_Data,
    OpBase_Type,
    OpBase_Move,
};

enum OpSpecPush : uint8
{
    OpSpecPush_Int32 = 1, // PUSH32  [imm]
    OpSpecPush_Reg32,     // PUSH32r [%r32]
};

enum OpSpecPop : uint8
{
    OpSpecPop_Int32 = 1, // POP32 [%r32]
};

enum OpSpecLoad : uint8
{
    OpSpecLoad_Int32 = 1, // LOAD32 [offset] [%r32]
    OpSpecLoad_AbsoluteInt32,
};

enum OpSpecArith : uint8
{
    OpSpecArith_Add = 1 // ADD [%r32] [%r32]
};

enum OpSpecSave : uint8
{
    OpSpecSave_Int32 = 1,
    OpSpecSave_Reg32,
    OpSpecSave_AbsoluteInt32,
    OpSpecSave_AbsoluteReg32
};

enum OpSpecJump : uint8
{
    OpSpecJump_Relative = 1,
    OpSpecJump_Absolute,
    OpSpecJump_AbsoluteReg32,

    OpSpecJump_CallAbsolute,
    OpSpecJump_ReturnToCaller,

    OpSpecJump_CallExternal,
};

enum OpSpecData : uint8
{
    OpSpecData_String = 1,
    OpSpecData_ParamsStart,
};

enum OpSpecType : uint8
{
    OpSpecType_Int = 1,
    OpSpecType_String,
};

enum OpSpecMove : uint8
{
    OpSpecMove_Int32 = 1,
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
};

enum IrSpecPush : uint8
{
    IrSpecPush_Int32 = 1, // PUSH32  [imm]
    IrSpecPush_Reg32,     // PUSH32r [%r32]
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
    IrSpecArith_Add = 1 // ADD [%r32] [%r32]
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

    IrSpecJump_CallExternal,
};

enum IrSpecData : uint8
{
    IrSpecData_String = 1,
    IrSpecData_ParamsStart,
};

enum IrSpecType : uint8
{
    IrSpecType_Int = 1,
    IrSpecType_String,
};

enum IrSpecMove : uint8
{
    IrSpecMove_Int32 = 1,
};
