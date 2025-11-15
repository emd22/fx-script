#pragma once


#include "FoxAst.hpp"

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
