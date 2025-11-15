#pragma once

#include "FoxIR.hpp"

struct FoxIRArm64Frame
{
    uint32 StackAllocated = 0;
    uint32 UnAlignedStackAllocated = 0;
    uint32 RegistersInUse = 0;

    bool HasBaselevelReturnStmt = false;
};


enum FoxArm64Register
{
    Fox_Arm64_W0,
    Fox_Arm64_W1,
    Fox_Arm64_W2,
    Fox_Arm64_W3,
    Fox_Arm64_W4,
    Fox_Arm64_W5,
    Fox_Arm64_W6,
    Fox_Arm64_W7,
    Fox_Arm64_W8,
    Fox_Arm64_W9,
    Fox_Arm64_W10,
    Fox_Arm64_W11,
    Fox_Arm64_W12,
    Fox_Arm64_W13,
    Fox_Arm64_W14,
    Fox_Arm64_W15,

    Fox_Arm64_None,
};

class FoxIRToArm64
{
public:
    enum RegisterUsage
    {
        Usage_Parameters,
        Usage_General,
        Usage_Return,
    };

public:
    FoxIRToArm64(FoxMPPagedArray<uint8>& bytecode)
    {
        mBytecode = bytecode;
        mBytecode.DoNotDestroy = true;

        mFunctionRefs.Create(32);
    }

    void Print();
    void PrintOp();


private:
    uint16 Read16();
    uint32 Read32();

    void DoPush(char* s, uint8 op_base, uint8 op_spec);
    void DoPop(char* s, uint8 op_base, uint8 op_spec);
    void DoLoad(char* s, uint8 op_base, uint8 op_spec);
    void DoArith(char* s, uint8 op_base, uint8 op_spec);
    void DoSave(char* s, uint8 op_base, uint8 op_spec);
    void DoJump(char* s, uint8 op_base, uint8 op_spec);
    void DoData(char* s, uint8 op_base, uint8 op_spec);
    void DoType(char* s, uint8 op_base, uint8 op_spec);
    void DoMove(char* s, uint8 op_base, uint8 op_spec);
    void DoMarker(char* s, uint8 op_base, uint8 op_spec);
    void DoVariable(char* s, uint8 op_base, uint8 op_spec);

    void EmitFrameRestore();

    FoxIRFunctionRef* GetFunctionRefFromHash(uint32 position);

private:
    // void ResetFrame();

    uint32 MakeValueFactorOf16(uint32 value);

    FoxArm64Register RegisterRequest(RegisterUsage usage);
    bool IsRegisterInUse(FoxArm64Register reg);
    void RegisterRelease(FoxArm64Register reg);

    FoxArm64Register GetArmRegFromIRReg(FoxIRRegister ir_reg);

    const char* GetRegisterName(FoxArm64Register reg);

    FoxIRArm64Frame* GetCurrentFrame();

    FoxIRArm64Frame* FramePush();
    void FramePop();


private:
    uint32 mBytecodeIndex = 0;
    FoxMPPagedArray<uint8> mBytecode;

    uint32 PreFrameStackAllocation = 0;
    FoxMPPagedArray<FoxIRArm64Frame> mStackFrames;

    bool mInParamsBlock = false;
    bool mEmitDefinitionAsEntryPoint = false;
    bool mFunctionContainsBranches = false;

    char mCurrentLabelName[256];
    uint16 mCurrentLabelNameLength = 0;

    FoxMPPagedArray<FoxIRFunctionRef> mFunctionRefs;
};
