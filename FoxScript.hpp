#pragma once

#include "FoxMPPagedArray.hpp"
#include "FoxTokenizer.hpp"

#include <vector>

#define FX_SCRIPT_VERSION_MAJOR 0
#define FX_SCRIPT_VERSION_MINOR 3
#define FX_SCRIPT_VERSION_PATCH 2

#define FX_SCRIPT_VAR_RETURN_VAL "__ReturnVal__"


struct FoxAstVarRef;
struct FoxScope;
struct FoxFunction;

struct FoxValue
{
    static FoxValue None;

    enum ValueType : uint16
    {
        NONETYPE = 0x00,
        INT = 0x01,
        FLOAT = 0x02,
        STRING = 0x04,
        VEC3 = 0x08,
        REF = 0x10
    };

    ValueType Type = NONETYPE;

    union
    {
        int ValueInt = 0;
        float ValueFloat;
        float ValueVec3[3];
        char* ValueString;

        FoxAstVarRef* ValueRef;
    };

    FoxValue()
    {
    }

    explicit FoxValue(ValueType type, int value) : Type(type), ValueInt(value)
    {
    }

    explicit FoxValue(ValueType type, float value) : Type(type), ValueFloat(value)
    {
    }

    FoxValue(const FoxValue& other)
    {
        Type = other.Type;
        if (other.Type == INT) {
            ValueInt = other.ValueInt;
        }
        else if (other.Type == FLOAT) {
            ValueFloat = other.ValueFloat;
        }
        else if (other.Type == STRING) {
            ValueString = other.ValueString;
        }
        else if (other.Type == REF) {
            ValueRef = other.ValueRef;
        }
    }

    void Print() const
    {
        printf("[Value: ");
        if (Type == NONETYPE) {
            printf("Null]\n");
        }
        else if (Type == INT) {
            printf("Int, %d]\n", ValueInt);
        }
        else if (Type == FLOAT) {
            printf("Float, %f]\n", ValueFloat);
        }
        else if (Type == STRING) {
            printf("String, %s]\n", ValueString);
        }
        else if (Type == REF) {
            printf("Ref, %p]\n", ValueRef);
        }
    }

    inline bool IsNumber()
    {
        return (Type == INT || Type == FLOAT);
    }

    inline bool IsRef()
    {
        return (Type == REF);
    }
};

enum FoxAstType
{
    FX_AST_LITERAL,
    // FX_AST_NAME,

    FX_AST_BINOP,
    FX_AST_UNARYOP,
    FX_AST_BLOCK,

    // Variables
    FX_AST_VARREF,
    FX_AST_VARDECL,
    FX_AST_ASSIGN,

    // Functions
    FX_AST_ACTIONDECL,
    FX_AST_ACTIONCALL,
    FX_AST_RETURN,

    FX_AST_DOCCOMMENT,

    FX_AST_COMMANDMODE,
};

struct FoxAstNode
{
    FoxAstType NodeType;
};


struct FoxAstLiteral : public FoxAstNode
{
    FoxAstLiteral()
    {
        this->NodeType = FX_AST_LITERAL;
    }

    // FoxTokenizer::Token* Token = nullptr;
    FoxValue Value;
};

struct FoxAstBinop : public FoxAstNode
{
    FoxAstBinop()
    {
        this->NodeType = FX_AST_BINOP;
    }

    FoxTokenizer::Token* OpToken = nullptr;
    FoxAstNode* Left = nullptr;
    FoxAstNode* Right = nullptr;
};

struct FoxAstBlock : public FoxAstNode
{
    FoxAstBlock()
    {
        this->NodeType = FX_AST_BLOCK;
    }

    std::vector<FoxAstNode*> Statements;
};

struct FoxAstVarRef : public FoxAstNode
{
    FoxAstVarRef()
    {
        this->NodeType = FX_AST_VARREF;
    }

    FoxTokenizer::Token* Name = nullptr;
    FoxScope* Scope = nullptr;
};

struct FoxAstAssign : public FoxAstNode
{
    FoxAstAssign()
    {
        this->NodeType = FX_AST_ASSIGN;
    }

    FoxAstVarRef* Var = nullptr;
    // FoxValue Value;
    FoxAstNode* Rhs = nullptr;
};

struct FoxAstVarDecl : public FoxAstNode
{
    FoxAstVarDecl()
    {
        this->NodeType = FX_AST_VARDECL;
    }

    FoxTokenizer::Token* Name = nullptr;
    FoxTokenizer::Token* Type = nullptr;
    FoxAstAssign* Assignment = nullptr;

    /// Ignore the scope that the variable is declared in, force it to be global.
    bool DefineAsGlobal = false;
};

struct FoxAstDocComment : public FoxAstNode
{
    FoxAstDocComment()
    {
        this->NodeType = FX_AST_DOCCOMMENT;
    }

    FoxTokenizer::Token* Comment;
};

struct FoxAstFunctionDecl : public FoxAstNode
{
    FoxAstFunctionDecl()
    {
        this->NodeType = FX_AST_ACTIONDECL;
    }

    FoxTokenizer::Token* Name = nullptr;
    FoxAstVarDecl* ReturnVar = nullptr;
    FoxAstBlock* Params = nullptr;
    FoxAstBlock* Block = nullptr;

    std::vector<FoxAstDocComment*> DocComments;
};

struct FoxAstCommandMode : public FoxAstNode
{
    FoxAstCommandMode()
    {
        this->NodeType = FX_AST_COMMANDMODE;
    }

    FoxAstNode* Node = nullptr;
};

struct FoxAstFunctionCall : public FoxAstNode
{
    FoxAstFunctionCall()
    {
        this->NodeType = FX_AST_ACTIONCALL;
    }

    FoxFunction* Function = nullptr;
    FoxHash HashedName = 0;
    std::vector<FoxAstNode*> Params {}; // FoxAstLiteral or FoxAstVarRef
};

struct FoxAstReturn : public FoxAstNode
{
    FoxAstReturn()
    {
        this->NodeType = FX_AST_RETURN;
    }

    FoxAstNode* Rhs = nullptr;
};

/**
 * @brief Data is accessible from a label, such as a variable or an function.
 */
struct FoxLabelledData
{
    FoxHash HashedName = 0;
    FoxTokenizer::Token* Name = nullptr;

    FoxScope* Scope = nullptr;
};

struct FoxFunction : public FoxLabelledData
{
    FoxFunction(FoxTokenizer::Token* name, FoxScope* scope, FoxAstBlock* block, FoxAstFunctionDecl* declaration)
    {
        HashedName = name->GetHash();
        Name = name;
        Scope = scope;
        Block = block;
        Declaration = declaration;
    }

    FoxAstFunctionDecl* Declaration = nullptr;
    FoxAstBlock* Block = nullptr;
};

struct FoxVar : public FoxLabelledData
{
    FoxTokenizer::Token* Type = nullptr;
    FoxValue Value;

    bool IsExternal = false;

    void Print() const
    {
        printf("[Var] Type: %.*s, Name: %.*s (Hash:%u)", Type->Length, Type->Start, Name->Length, Name->Start, Name->GetHash());
        Value.Print();
    }

    FoxVar()
    {
    }

    FoxVar(FoxTokenizer::Token* type, FoxTokenizer::Token* name, FoxScope* scope, bool is_external = false) : Type(type)
    {
        this->HashedName = name->GetHash();
        this->Name = name;
        this->Scope = scope;
        IsExternal = is_external;
    }

    FoxVar(const FoxVar& other)
    {
        HashedName = other.HashedName;
        Type = other.Type;
        Name = other.Name;
        Value = other.Value;
        IsExternal = other.IsExternal;
    }

    FoxVar& operator=(FoxVar&& other) noexcept
    {
        HashedName = other.HashedName;
        Type = other.Type;
        Name = other.Name;
        Value = other.Value;
        IsExternal = other.IsExternal;

        Name = nullptr;
        Type = nullptr;
        HashedName = 0;

        return *this;
    }

    ~FoxVar()
    {
        if (!IsExternal) {
            return;
        }

        // Free tokens allocated by external variables
        if (Type && Type->Start) {
            FX_SCRIPT_FREE(char, Type->Start);
        }

        if (this->Name && this->Name->Start) {
            FX_SCRIPT_FREE(char, this->Name->Start);
        }
    }
};

// class FoxVM;


// struct FoxExternalFunc
// {
//     // using FuncType = void (*)(FoxInterpreter& interpreter, std::vector<FoxValue>& params, FoxValue* return_value);

//     // using FuncType = void (*)(FoxVM* vm, std::vector<FoxValue>& params, FoxValue* return_value);

//     FoxHash HashedName = 0;
//     // FuncType Function = nullptr;

//     std::vector<FoxValue::ValueType> ParameterTypes;
//     bool IsVariadic = false;
// };

struct FoxScope
{
    FoxMPPagedArray<FoxVar> Vars;
    FoxMPPagedArray<FoxFunction> Functions;

    FoxScope* Parent = nullptr;

    // This points to the return value for the current scope. If an function returns a value,
    // this will be set to the variable that holds its value. This is interpreter only.
    FoxVar* ReturnVar = nullptr;

    void PrintAllVarsInScope()
    {
        puts("\n=== SCOPE ===");
        for (FoxVar& var : Vars) {
            var.Print();
        }
    }

    FoxVar* FindVarInScope(FoxHash hashed_name)
    {
        return FindInScope<FoxVar>(hashed_name, Vars);
    }

    FoxFunction* FindFunctionInScope(FoxHash hashed_name)
    {
        return FindInScope<FoxFunction>(hashed_name, Functions);
    }

    template <typename T>
        requires std::is_base_of_v<FoxLabelledData, T>
    T* FindInScope(FoxHash hashed_name, const FoxMPPagedArray<T>& buffer)
    {
        for (T& var : buffer) {
            if (var.HashedName == hashed_name) {
                return &var;
            }
        }

        return nullptr;
    }
};

// class FoxInterpreter;

class FoxConfigScript
{
    using Token = FoxTokenizer::Token;
    using TT = FoxTokenizer::TokenType;

public:
    FoxConfigScript() = default;

    void LoadFile(const char* path);

    void PushScope();
    void PopScope();

    FoxVar* FindVar(FoxHash hashed_name);

    FoxFunction* FindFunction(FoxHash hashed_name);
    // FoxExternalFunc* FindExternalFunction(FoxHash hashed_name);

    FoxAstNode* TryParseKeyword(FoxAstBlock* parent_block);

    FoxAstAssign* TryParseAssignment(FoxTokenizer::Token* var_name);

    FoxValue ParseValue();

    FoxAstFunctionDecl* ParseFunctionDeclare();

    FoxAstNode* ParseRhs();
    FoxAstFunctionCall* ParseFunctionCall();

    /**
     * @brief Declares a variable for internal uses as if it was declared in the script.
     * @param name Name of the variable
     * @param type The name of the type
     * @param scope The scope the variable will be declared in
     * @return
     */
    FoxAstVarDecl* InternalVarDeclare(FoxTokenizer::Token* name_token, FoxTokenizer::Token* type_token, FoxScope* scope = nullptr);
    FoxAstVarDecl* ParseVarDeclare(FoxScope* scope = nullptr);

    FoxAstBlock* ParseBlock();

    FoxAstNode* ParseStatement(FoxAstBlock* parent_block);
    FoxAstNode* ParseStatementAsCommand(FoxAstBlock* parent_block);

    FoxAstBlock* Parse();

    /**
     * @brief Parses and executes a script.
     * @param interpreter The interpreter to execute with
     */
    void Execute();

    /**
     * @brief Executes a command on a script. Defaults to parsing with command style syntax.
     * @param command The command to execute on the script.
     * @return If the command has been executed
     */
    // bool ExecuteUserCommand(const char* command, FoxInterpreter& interpreter);

    Token& GetToken(int offset = 0);
    Token& EatToken(TT token_type);

    // void RegisterExternalFunc(FoxHash func_name, std::vector<FoxValue::ValueType> param_types, FoxExternalFunc::FuncType func, bool is_variadic);

    void DefineExternalVar(const char* type, const char* name, const FoxValue& value);

private:
    template <typename T>
        requires std::is_base_of_v<FoxLabelledData, T>
    T* FindLabelledData(FoxHash hashed_name, FoxMPPagedArray<T>& buffer)
    {
        FoxScope* scope = mCurrentScope;

        while (scope) {
            T* var = scope->FindInScope<T>(hashed_name, buffer);
            if (var) {
                return var;
            }

            scope = scope->Parent;
        }

        return nullptr;
    }

    // void DefineDefaultExternalFunctions();

    Token* CreateTokenFromString(FoxTokenizer::TokenType type, const char* text);
    void CreateInternalVariableTokens();

private:
    FoxMPPagedArray<FoxScope> mScopes;
    FoxScope* mCurrentScope;

    // std::vector<FoxExternalFunc> mExternalFuncs;

    std::vector<FoxAstDocComment*> CurrentDocComments;

    FoxAstBlock* mRootBlock = nullptr;

    bool mHasErrors = false;
    bool mInCommandMode = false;

    char* mFileData;
    FoxMPPagedArray<Token> mTokens = {};
    uint32 mTokenIndex = 0;

    // Name tokens for internal variables
    Token* mTokenReturnVar = nullptr;
};

//////////////////////////////////
// Script AST Printer
//////////////////////////////////

class FoxAstPrinter
{
public:
    FoxAstPrinter(FoxAstBlock* root_block)
    //: mRootBlock(root_block)
    {
    }

    void Print(FoxAstNode* node, int depth = 0)
    {
        if (node == nullptr) {
            return;
        }

        for (int i = 0; i < depth; i++) {
            putchar(' ');
            putchar(' ');
        }

        if (node->NodeType == FX_AST_BLOCK) {
            puts("[BLOCK]");

            FoxAstBlock* block = reinterpret_cast<FoxAstBlock*>(node);
            for (FoxAstNode* child : block->Statements) {
                Print(child, depth + 1);
            }
            return;
        }
        else if (node->NodeType == FX_AST_ACTIONDECL) {
            FoxAstFunctionDecl* functiondecl = reinterpret_cast<FoxAstFunctionDecl*>(node);
            printf("[ACTIONDECL] ");
            functiondecl->Name->Print();

            for (FoxAstNode* param : functiondecl->Params->Statements) {
                Print(param, depth + 1);
            }

            Print(functiondecl->Block, depth + 1);
        }
        else if (node->NodeType == FX_AST_VARDECL) {
            FoxAstVarDecl* vardecl = reinterpret_cast<FoxAstVarDecl*>(node);

            printf("[VARDECL] ");
            vardecl->Name->Print();

            Print(vardecl->Assignment, depth + 1);
        }
        else if (node->NodeType == FX_AST_ASSIGN) {
            FoxAstAssign* assign = reinterpret_cast<FoxAstAssign*>(node);

            printf("[ASSIGN] ");

            assign->Var->Name->Print();
            Print(assign->Rhs, depth + 1);
        }
        else if (node->NodeType == FX_AST_ACTIONCALL) {
            FoxAstFunctionCall* functioncall = reinterpret_cast<FoxAstFunctionCall*>(node);

            printf("[ACTIONCALL] ");
            if (functioncall->Function == nullptr) {
                printf("{defined externally}");
            }
            else {
                functioncall->Function->Name->Print(true);
            }

            printf(" (%zu params)\n", functioncall->Params.size());
        }
        else if (node->NodeType == FX_AST_LITERAL) {
            FoxAstLiteral* literal = reinterpret_cast<FoxAstLiteral*>(node);

            printf("[LITERAL] ");
            literal->Value.Print();
        }
        else if (node->NodeType == FX_AST_BINOP) {
            FoxAstBinop* binop = reinterpret_cast<FoxAstBinop*>(node);

            printf("[BINOP] ");
            binop->OpToken->Print();

            Print(binop->Left, depth + 1);
            Print(binop->Right, depth + 1);
        }
        else if (node->NodeType == FX_AST_COMMANDMODE) {
            FoxAstCommandMode* command_mode = reinterpret_cast<FoxAstCommandMode*>(node);
            printf("[COMMANDMODE]\n");

            Print(command_mode->Node, depth + 1);
        }
        else if (node->NodeType == FX_AST_RETURN) {
            FoxAstReturn* return_node = reinterpret_cast<FoxAstReturn*>(node);

            puts("[RETURN]");

            if (return_node->Rhs) {
                Print(return_node->Rhs, depth + 1);
            }
        }
        else {
            puts("[UNKNOWN]");
        }
        // else if (node->NodeType == FX_AST_)
    }

public:
    // FoxAstBlock* mRootBlock = nullptr;
};


//////////////////////////////////
// Script AST Printer
//////////////////////////////////

class FoxAstDestroyer
{
public:
    FoxAstDestroyer(FoxAstBlock* root_block)
    //: mRootBlock(root_block)
    {
        Do(root_block);
    }

    void Do(FoxAstNode* node);

public:
    // FoxAstBlock* mRootBlock = nullptr;
};

enum FoxIRRegister : uint8
{
    /* General Purpose (32 bit) registers */
    FX_IR_GW0,
    FX_IR_GW1,
    FX_IR_GW2,
    FX_IR_GW3,

    /* General Purpose (64 bit) registers */
    FX_IR_GX0,
    FX_IR_GX1,
    FX_IR_GX2,
    FX_IR_GX3,

    FX_IR_REG_RETURN_VALUE,

    /* Stack pointer */
    FX_IR_SP,
};

struct FoxBytecodeVarHandle
{
    FoxHash HashedName = 0;
    FoxValue::ValueType Type = FoxValue::INT;
    int64 Offset = 0;

    uint16 VarIndexInScope = 0;

    uint16 SizeOnStack = 4;
    uint32 ScopeIndex = 0;
};

struct FoxBytecodeFunctionHandle
{
    FoxHash HashedName = 0;
    uint32 BytecodeIndex = 0;
};

////////////////////////////////////////////
// IR Emitter
////////////////////////////////////////////


struct FoxIRFunctionRef
{
    char* Name = nullptr;
    uint32 HashedName = 0;
    uint32 Position = 0;
};

#include "FoxScriptBytecode.hpp"

class FoxIREmitter
{
public:
    FoxIREmitter() = default;

    void BeginEmitting(FoxAstNode* node);
    void Emit(FoxAstNode* node);

    enum RhsMode
    {
        RHS_FETCH_TO_REGISTER,

        /**
         * @brief Pushes the value to the stack, assuming that the value does not exist yet.
         */
        RHS_DEFINE_IN_MEMORY,

        RHS_ASSIGN_TO_HANDLE,
    };

    static const char* GetRegisterName(FoxIRRegister reg);

    FoxMPPagedArray<uint8> mBytecode {};

    enum VarDeclareMode
    {
        DECLARE_DEFAULT,
        DECLARE_NO_EMIT,
    };


private:
    void EmitBlock(FoxAstBlock* block, bool ignore_function_definitions = false);
    void EmitFunction(FoxAstFunctionDecl* function);
    void EmitFunctionDefinitionsInBlock(FoxAstBlock* block);
    void DoFunctionCall(FoxAstFunctionCall* call);
    FoxBytecodeVarHandle* DoVarDeclare(FoxAstVarDecl* decl, VarDeclareMode mode = DECLARE_DEFAULT);
    void EmitAssign(FoxAstAssign* assign);
    FoxBytecodeVarHandle* DefineAndFetchParam(FoxAstNode* param_decl_node);
    FoxBytecodeVarHandle* DefineReturnVar(FoxAstVarDecl* decl);

    FoxIRRegister EmitVarFetch(FoxAstVarRef* ref, RhsMode mode);

    uint16 GetSizeOfType(FoxTokenizer::Token* type);

    void DoLoad(uint32 stack_offset, FoxIRRegister output_reg, bool force_absolute = false);
    void DoSaveInt32(uint32 stack_offset, uint32 value, bool force_absolute = false);
    void DoSaveReg32(uint32 stack_offset, FoxIRRegister reg, bool force_absolute = false);

    void EmitPush32(uint32 value);
    void EmitPush32r(FoxIRRegister reg);

    void EmitStackAlloc(uint16 size);

    void EmitPop32(FoxIRRegister output_reg);

    void EmitLoad32(int offset, FoxIRRegister output_reg);
    void EmitLoadAbsolute32(uint32 position, FoxIRRegister output_reg);

    void EmitSave32(int16 offset, uint32 value);
    void EmitSaveReg32(int16 offset, FoxIRRegister reg);

    void EmitSaveAbsolute32(uint32 offset, uint32 value);
    void EmitSaveAbsoluteReg32(uint32 offset, FoxIRRegister reg);

    void EmitJumpRelative(uint16 offset);
    void EmitJumpAbsolute(uint32 position);
    void EmitJumpAbsoluteReg32(FoxIRRegister reg);
    void EmitJumpCallAbsolute(uint32 position);

    void EmitJumpReturnToCaller();
    void EmitJumpReturnToCallerReg32(FoxIRRegister reg);
    void EmitJumpReturnToCallerInt32(int32 value);

    void EmitJumpCallExternal(FoxHash hashed_name);

    void EmitVariableGetInt32(uint16 var_index, FoxIRRegister dest_reg);
    void EmitVariableSetInt32(uint16 var_index, int32 value);
    void EmitVariableSetReg32(uint16 var_index, FoxIRRegister reg);

    void EmitMoveInt32(FoxIRRegister reg, uint32 value);
    void EmitMoveReg32(FoxIRRegister dest_reg, FoxIRRegister src_reg);

    void EmitParamsStart();
    void EmitType(FoxValue::ValueType type);

    uint32 EmitDataString(char* str, uint16 length);

    FoxIRRegister EmitBinop(FoxAstBinop* binop, FoxBytecodeVarHandle* handle);

    FoxIRRegister EmitRhs(FoxAstNode* rhs, RhsMode mode, FoxBytecodeVarHandle* handle);

    FoxIRRegister EmitLiteralInt(FoxAstLiteral* literal, RhsMode mode, FoxBytecodeVarHandle* handle);
    FoxIRRegister EmitLiteralString(FoxAstLiteral* literal, RhsMode mode, FoxBytecodeVarHandle* handle);

    void EmitMarker(IrSpecMarker spec);

    void WriteOp(uint8 base_op, uint8 spec_op);
    void Write16(uint16 value);
    void Write32(uint32 value);

    FoxIRRegister FindFreeReg32();
    FoxIRRegister FindFreeReg64();

    FoxBytecodeVarHandle* FindVarHandle(FoxHash hashed_name);
    FoxBytecodeFunctionHandle* FindFunctionHandle(FoxHash hashed_name);

    void PrintBytecode();

    void MarkRegisterUsed(FoxIRRegister reg);
    void MarkRegisterFree(FoxIRRegister reg);

    bool DoesNodeBranch(FoxAstNode* node);

public:
    FoxMPPagedArray<FoxBytecodeVarHandle> VarHandles;
    std::vector<FoxBytecodeFunctionHandle> FunctionHandles;

private:
    uint32 mRegsInUse = 0;

    int64 mStackOffset = 0;
    uint32 mStackSize = 0;

    uint16 mVarsInScope = 0;
    uint32 mLabelId = 0;

    uint16 mScopeIndex = 0;

    bool mEntryPointEmitted = false;
};


class FoxIRPrinter
{
public:
    FoxIRPrinter(FoxMPPagedArray<uint8>& bytecode)
    {
        mBytecode = bytecode;
        mBytecode.DoNotDestroy = true;
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

private:
    uint32 mBytecodeIndex = 0;
    FoxMPPagedArray<uint8> mBytecode;
};


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

    bool mEmitDefinitionAsEntryPoint = false;
    bool mFunctionContainsBranches = false;

    char mCurrentLabelName[256];
    uint16 mCurrentLabelNameLength = 0;

    FoxMPPagedArray<FoxIRFunctionRef> mFunctionRefs;
};
