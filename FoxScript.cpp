#include "FoxScript.hpp"

#include "FoxLog.hpp"
#include "FoxScriptUtil.hpp"

#include <stdio.h>

#include <vector>

#define FX_SCRIPT_SCOPE_GLOBAL_VARS_START_SIZE 32
#define FX_SCRIPT_SCOPE_LOCAL_VARS_START_SIZE 16

#define FX_SCRIPT_SCOPE_GLOBAL_ACTIONS_START_SIZE 32
#define FX_SCRIPT_SCOPE_LOCAL_ACTIONS_START_SIZE 32

#define MARK_REGISTER_USED(regn_)                                                                                                                    \
    {                                                                                                                                                \
        MarkRegisterUsed(regn_);                                                                                                                     \
    }
#define MARK_REGISTER_FREE(regn_)                                                                                                                    \
    {                                                                                                                                                \
        MarkRegisterFree(regn_);                                                                                                                     \
    }

using Token = FoxTokenizer::Token;
using TT = FoxTokenizer::TokenType;

static uint16 ReverseInt16(uint16 value)
{
    return (value >> 8) | (value << 8);
}

FoxValue FoxValue::None {};

void FoxConfigScript::LoadFile(const char* path)
{
    FILE* fp = FoxUtil::FileOpen(path, "rb");
    if (fp == nullptr) {
        // printf("[ERROR] Could not open config file at '%s'\n", path);
        FoxLogError("Could not open config file at '{}'", path);
        return;
    }

    std::fseek(fp, 0, SEEK_END);
    size_t file_size = std::ftell(fp);
    std::rewind(fp);

    mFileData = FX_SCRIPT_ALLOC_MEMORY(char, file_size);

    size_t read_size = std::fread(mFileData, 1, file_size, fp);
    if (read_size != file_size) {
        FoxLogWarning("Error reading all data from config file at '{}' (read={}, size={})", path, read_size, file_size);
    }

    FoxTokenizer tokenizer(mFileData, read_size);
    tokenizer.Tokenize();


    fclose(fp);

    mTokens = std::move(tokenizer.GetTokens());

    /*for (const auto& token : mTokens) {
        token.Print();
    }*/

    mScopes.Create(8);
    mCurrentScope = mScopes.Insert();
    mCurrentScope->Vars.Create(FX_SCRIPT_SCOPE_GLOBAL_VARS_START_SIZE);
    mCurrentScope->Functions.Create(FX_SCRIPT_SCOPE_GLOBAL_ACTIONS_START_SIZE);

    CreateInternalVariableTokens();
}

Token& FoxConfigScript::GetToken(int offset)
{
    const uint32 idx = mTokenIndex + offset;
    if (idx < 0 || idx >= mTokens.Size()) {
        printf("SOMETHING IS MISSING\n");
    }
    assert(idx >= 0 && idx <= mTokens.Size());
    return mTokens[idx];
}

Token& FoxConfigScript::EatToken(TT token_type)
{
    Token& token = GetToken();
    if (token.Type != token_type) {
        FoxLogError("{}:{}: Unexpected token type {} when expecting {}!", token.FileLine, token.FileColumn, FoxTokenizer::GetTypeName(token.Type),
                    FoxTokenizer::GetTypeName(token_type));
        mHasErrors = true;
    }
    ++mTokenIndex;
    return token;
}

Token* FoxConfigScript::CreateTokenFromString(FoxTokenizer::TokenType type, const char* text)
{
    const uint32 name_len = strlen(text);

    // Create (fabricate..) the name token
    Token* token = FX_SCRIPT_ALLOC_NODE(Token);
    token->Start = FX_SCRIPT_ALLOC_MEMORY(char, name_len);
    token->End = token->Start + name_len;
    token->Length = name_len;
    token->Type = type;

    // Copy the name to the buffer in the name token
    memcpy(token->Start, text, name_len);

    return token;
}

void FoxConfigScript::CreateInternalVariableTokens()
{
    mTokenReturnVar = CreateTokenFromString(TT::Identifier, FX_SCRIPT_VAR_RETURN_VAL);
}

void PrintDocCommentExample(FoxTokenizer::Token* comment, int example_tag_length, bool is_command_mode)
{
    char* start = comment->Start + example_tag_length;

    for (int i = 0; i < comment->Length - example_tag_length; i++) {
        char ch = start[i];

        if (!is_command_mode) {
            putchar(ch);
            continue;
        }

        // If we are in command mode, print the example using the command syntax
        if (ch == '(' || ch == ')' || ch == ',') {
            putchar(' ');
            continue;
        }

        if (ch == ' ' || ch == '\t' || ch == ';') {
            continue;
        }

        putchar(ch);
    }

    puts("");
}

void PrintDocComment(FoxTokenizer::Token* comment, bool is_command_mode)
{
    const char* example_tag = "EX: ";
    const int example_tag_length = 4;

    char* start = comment->Start;

    if (comment->Length > example_tag_length && !strncmp(start, example_tag, example_tag_length)) {
        printf("\n\t-> Script : ");
        PrintDocCommentExample(comment, example_tag_length, false);

        printf("\t-> Command: ");
        PrintDocCommentExample(comment, example_tag_length, true);

        return;
    }

    printf("%.*s\n", comment->Length, start);
}

FoxAstNode* FoxConfigScript::TryParseKeyword(FoxAstBlock* parent_block)
{
    if (mTokenIndex >= mTokens.Size()) {
        return nullptr;
    }

    Token& tk = GetToken();
    FoxHash hash = tk.GetHash();

    // function [name] ( < [arg type] [arg name] ...> ) { <statements...> }
    constexpr FoxHash kw_function = FoxHashStr("fn");

    // local [type] [name] <?assignment> ;
    constexpr FoxHash kw_local = FoxHashStr("local");

    // global [type] [name] <?assignment> ;
    constexpr FoxHash kw_global = FoxHashStr("global");

    // return ;
    constexpr FoxHash kw_return = FoxHashStr("return");

    // help [name of function] ;
    constexpr FoxHash kw_help = FoxHashStr("help");

    if (hash == kw_function) {
        EatToken(TT::Identifier);
        // ParseFunctionDeclare();
        return ParseFunctionDeclare();
    }
    if (hash == kw_local) {
        EatToken(TT::Identifier);
        return ParseVarDeclare();
    }
    if (hash == kw_global) {
        EatToken(TT::Identifier);
        return ParseVarDeclare(&mScopes[0]);
    }
    if (hash == kw_return) {
        EatToken(TT::Identifier);

        FoxAstNode* return_rhs = nullptr;

        if (GetToken().Type != TT::Semicolon) {
            // There is a value that follows, get the value
            // FoxAstNode* rhs = ParseRhs();
            return_rhs = ParseRhs();


            // Assign the return value to __ReturnVal__

            // FoxAstVarRef* var_ref = FX_SCRIPT_ALLOC_NODE(FoxAstVarRef);
            // var_ref->Name = mTokenReturnVar;

            // FoxAstAssign* assign = FX_SCRIPT_ALLOC_NODE(FoxAstAssign);
            // assign->Var = var_ref;
            // assign->Rhs = rhs;

            // parent_block->Statements.push_back(assign);
        }

        FoxAstReturn* ret = FX_SCRIPT_ALLOC_NODE(FoxAstReturn);
        ret->Rhs = return_rhs;

        return ret;
    }
    if (hash == kw_help) {
        EatToken(TT::Identifier);

        FoxTokenizer::Token& func_ref = EatToken(TT::Identifier);

        FoxFunction* function = FindFunction(func_ref.GetHash());

        if (function) {
            for (FoxAstDocComment* comment : function->Declaration->DocComments) {
                printf("[DOC] %.*s: ", function->Name->Length, function->Name->Start);
                PrintDocComment(comment->Comment, mInCommandMode);
            }
        }

        return nullptr;
    }

    return nullptr;
}

void FoxConfigScript::PushScope()
{
    FoxScope* current = mCurrentScope;

    FoxScope* new_scope = mScopes.Insert();
    new_scope->Parent = current;
    new_scope->Vars.Create(FX_SCRIPT_SCOPE_LOCAL_VARS_START_SIZE);
    new_scope->Functions.Create(FX_SCRIPT_SCOPE_LOCAL_ACTIONS_START_SIZE);

    mCurrentScope = new_scope;
}

void FoxConfigScript::PopScope()
{
    FoxScope* new_scope = mCurrentScope->Parent;
    mScopes.RemoveLast();

    assert(new_scope == &mScopes.GetLast());

    mCurrentScope = new_scope;
}

FoxAstVarDecl* FoxConfigScript::InternalVarDeclare(FoxTokenizer::Token* name_token, FoxTokenizer::Token* type_token, FoxScope* scope)
{
    if (scope == nullptr) {
        scope = mCurrentScope;
    }

    // Allocate the declaration node
    FoxAstVarDecl* node = FX_SCRIPT_ALLOC_NODE(FoxAstVarDecl);

    node->Name = name_token;
    node->Type = type_token;
    node->DefineAsGlobal = (scope == &mScopes[0]);

    // Push the variable to the scope
    FoxVar var {name_token, type_token, scope};
    scope->Vars.Insert(var);

    return node;
}

FoxAstVarDecl* FoxConfigScript::ParseVarDeclare(FoxScope* scope)
{
    if (scope == nullptr) {
        scope = mCurrentScope;
    }

    Token& type = EatToken(TT::Identifier);
    Token& name = EatToken(TT::Identifier);

    FoxAstVarDecl* node = FX_SCRIPT_ALLOC_NODE(FoxAstVarDecl);

    node->Name = &name;
    node->Type = &type;
    node->DefineAsGlobal = (scope == &mScopes[0]);

    FoxVar var {&type, &name, scope};

    node->Assignment = TryParseAssignment(node->Name);
    /*if (node->Assignment) {
        var.Value = node->Assignment->Value;
    }*/
    scope->Vars.Insert(var);

    return node;
}

// FoxVar& FoxConfigScript::ParseVarDeclare()
//{
//     Token& type = EatToken(TT::Identifier);
//     Token& name = EatToken(TT::Identifier);
//
//     FoxVar var{ name.GetHash(), &type, &name };
//
//     TryParseAssignment(var);
//
//     mCurrentScope->Vars.Insert(var);
//
//     return mCurrentScope->Vars.GetLast();
// }

FoxVar* FoxConfigScript::FindVar(FoxHash hashed_name)
{
    FoxScope* scope = mCurrentScope;

    while (scope) {
        FoxVar* var = scope->FindVarInScope(hashed_name);
        if (var) {
            return var;
        }

        scope = scope->Parent;
    }

    return nullptr;
}

// FoxExternalFunc* FoxConfigScript::FindExternalFunction(FoxHash hashed_name)
// {
//     for (FoxExternalFunc& func : mExternalFuncs) {
//         if (func.HashedName == hashed_name) {
//             return &func;
//         }
//     }

//     return nullptr;
// }

FoxFunction* FoxConfigScript::FindFunction(FoxHash hashed_name)
{
    FoxScope* scope = mCurrentScope;

    while (scope) {
        FoxFunction* var = scope->FindFunctionInScope(hashed_name);
        if (var) {
            return var;
        }

        scope = scope->Parent;
    }

    return nullptr;
}

void FoxConfigScript::Execute()
{
    mRootBlock = Parse();

    // If there are errors, exit early
    if (mHasErrors || mRootBlock == nullptr) {
        return;
    }
    printf("\n=====\n");


    FoxIREmitter ir_emitter;
    ir_emitter.BeginEmitting(mRootBlock);

    printf("\n=====\n");

    FoxIRPrinter ir_printer(ir_emitter.mBytecode);

    ir_printer.Print();

    printf("\n=====\n");

    FoxIRToArm64 ir_to_arm64(ir_emitter.mBytecode);

    ir_to_arm64.Print();

    FoxAstDestroyer destroyer(mRootBlock);
}

FoxValue FoxConfigScript::ParseValue()
{
    Token& token = GetToken();
    TT token_type = token.Type;
    FoxValue value;

    if (token_type == TT::Identifier) {
        FoxVar* var = FindVar(token.GetHash());

        if (var) {
            value.Type = FoxValue::REF;

            FoxAstVarRef* var_ref = FX_SCRIPT_ALLOC_NODE(FoxAstVarRef);
            var_ref->Name = var->Name;
            var_ref->Scope = var->Scope;

            value.ValueRef = var_ref;

            EatToken(TT::Identifier);

            return value;
        }
        else {
            // We cannot find the definition for the variable, assume that it is an external variable that will be defined
            // during the interpret stage.

            FoxLogError("ERROR: Undefined reference to variable {}", token);

            // printf("Undefined reference to variable \"%.*s\"! (Hash:%u)\n", token.Length, token.Start, token.GetHash());
            EatToken(TT::Identifier);
        }
    }

    switch (token_type) {
    case TT::Integer:
        EatToken(TT::Integer);
        value.Type = FoxValue::INT;
        value.ValueInt = token.ToInt();
        break;
    case TT::Float:
        EatToken(TT::Float);
        value.Type = FoxValue::FLOAT;
        value.ValueFloat = token.ToFloat();
        break;
    case TT::String:
        EatToken(TT::String);
        value.Type = FoxValue::STRING;
        value.ValueString = token.GetHeapStr();
        break;
    default:;
    }

    return value;
}

#define RETURN_IF_NO_TOKENS(rval_)                                                                                                                   \
    {                                                                                                                                                \
        if (mTokenIndex >= mTokens.Size())                                                                                                           \
            return (rval_);                                                                                                                          \
    }

static bool IsTokenTypeLiteral(FoxTokenizer::TokenType type)
{
    return (type == TT::Integer || type == TT::Float || type == TT::String);
}

FoxAstNode* FoxConfigScript::ParseRhs()
{
    RETURN_IF_NO_TOKENS(nullptr);

    bool has_parameters = false;

    if (mTokenIndex + 1 < mTokens.Size()) {
        TT next_token_type = GetToken(1).Type;
        has_parameters = next_token_type == TT::LParen;

        if (mInCommandMode) {
            has_parameters =
                next_token_type == TT::Identifier || next_token_type == TT::Integer || next_token_type == TT::Float || next_token_type == TT::String;
        }
    }

    FoxTokenizer::Token& token = GetToken();

    FoxAstNode* lhs = nullptr;

    if (token.Type == TT::Identifier) {
        if (has_parameters) {
            lhs = ParseFunctionCall();
        }
        else {
            // FoxExternalFunc* external_function = FindExternalFunction(token.GetHash());
            // if (external_function != nullptr) {
            //     lhs = ParseFunctionCall();
            // }

            FoxFunction* function = FindFunction(token.GetHash());
            if (function != nullptr) {
                lhs = ParseFunctionCall();
            }
        }
    }
    if (!lhs) {
        if (IsTokenTypeLiteral(token.Type) || token.Type == TT::Identifier) {
            FoxAstLiteral* literal = FX_SCRIPT_ALLOC_NODE(FoxAstLiteral);

            FoxValue value = ParseValue();
            literal->Value = value;

            lhs = literal;
        }
        else {
            lhs = ParseRhs();
        }
    }


    // FoxAstLiteral* literal = FX_SCRIPT_ALLOC_NODE(FoxAstLiteral);
    // literal->Value = value;

    TT op_type = GetToken(0).Type;
    if (op_type == TT::Plus || op_type == TT::Minus) {
        FoxAstBinop* binop = FX_SCRIPT_ALLOC_NODE(FoxAstBinop);

        binop->Left = lhs;
        binop->OpToken = &EatToken(op_type);
        binop->Right = ParseRhs();

        return binop;
    }

    return lhs;
}

FoxAstAssign* FoxConfigScript::TryParseAssignment(FoxTokenizer::Token* var_name)
{
    if (GetToken().Type != TT::Equals) {
        return nullptr;
    }

    EatToken(TT::Equals);

    FoxAstAssign* node = FX_SCRIPT_ALLOC_NODE(FoxAstAssign);

    FoxAstVarRef* var_ref = FX_SCRIPT_ALLOC_NODE(FoxAstVarRef);
    var_ref->Name = var_name;
    var_ref->Scope = mCurrentScope;
    node->Var = var_ref;

    // node->Value = ParseValue();
    node->Rhs = ParseRhs();

    return node;
}

void FoxConfigScript::DefineExternalVar(const char* type, const char* name, const FoxValue& value)
{
    Token* name_token = FX_SCRIPT_ALLOC_MEMORY(Token, sizeof(Token));
    Token* type_token = FX_SCRIPT_ALLOC_MEMORY(Token, sizeof(Token));

    {
        const uint32 type_len = strlen(type);

        char* type_buffer = FX_SCRIPT_ALLOC_MEMORY(char, (type_len + 1));
        std::strcpy(type_buffer, type);

        type_token->Start = type_buffer;
        type_token->Type = TT::Identifier;
        type_token->Start[type_len] = 0;
        type_token->Length = type_len + 1;
    }

    {
        const uint32 name_len = strlen(name);

        char* name_buffer = FX_SCRIPT_ALLOC_MEMORY(char, (name_len + 1));
        std::strcpy(name_buffer, name);

        name_token->Start = name_buffer;
        name_token->Type = TT::Identifier;
        name_token->Start[name_len] = 0;
        name_token->Length = name_len + 1;
    }

    FoxScope* definition_scope = &mScopes[0];

    FoxVar var(type_token, name_token, definition_scope, true);
    var.Value = value;

    definition_scope->Vars.Insert(var);

    // To prevent the variable data from being deleted here.
    var.Name = nullptr;
    var.Type = nullptr;
}

FoxAstNode* FoxConfigScript::ParseStatementAsCommand(FoxAstBlock* parent_block)
{
    if (mHasErrors) {
        return nullptr;
    }

    RETURN_IF_NO_TOKENS(nullptr);

    // Eat any extraneous semicolons
    while (GetToken().Type == TT::Semicolon) {
        EatToken(TT::Semicolon);
        if (mTokenIndex >= mTokens.Size()) {
            return nullptr;
        }
    }

    // Try to parse as a keyword first
    FoxAstNode* node = TryParseKeyword(parent_block);

    if (node) {
        RETURN_IF_NO_TOKENS(node);

        EatToken(TT::Semicolon);
        return node;
    }

    // Check identifier
    if (mTokenIndex < mTokens.Size() && GetToken().Type == TT::Identifier) {
        TT next_token_type = TT::Unknown;

        if (mTokenIndex + 1 < mTokens.Size()) {
            next_token_type = GetToken(1).Type;
        }

        if (mTokenIndex + 1 < mTokens.Size() && GetToken(1).Type == TT::Equals) {
            Token& assign_name = EatToken(TT::Identifier);
            node = TryParseAssignment(&assign_name);
        }
        // If there is what looks to be a function call, try it
        else {
            // FoxExternalFunc* external_function = FindExternalFunction(GetToken().GetHash());
            // if (external_function != nullptr) {
            //     node = ParseFunctionCall();
            // }
            // else {
            FoxFunction* function = FindFunction(GetToken().GetHash());
            if (function != nullptr) {
                node = ParseFunctionCall();
            }
            // }
        }
        // If there is just a semicolon after an identifier, parse as an rhs to print out later
        if (!node && next_token_type == TT::Semicolon) {
            node = ParseRhs();
        }
    }

    RETURN_IF_NO_TOKENS(node);

    EatToken(TT::Semicolon);

    return node;
}

FoxAstNode* FoxConfigScript::ParseStatement(FoxAstBlock* parent_block)
{
    if (mHasErrors) {
        return nullptr;
    }

    RETURN_IF_NO_TOKENS(nullptr);

    if (GetToken().Type == TT::Dollar) {
        EatToken(TT::Dollar);

        mInCommandMode = true;

        FoxAstCommandMode* cmd_mode = FX_SCRIPT_ALLOC_NODE(FoxAstCommandMode);
        cmd_mode->Node = ParseStatementAsCommand(parent_block);

        mInCommandMode = false;

        return cmd_mode;
    }

    while (GetToken().Type == TT::DocComment) {
        FoxAstDocComment* comment = FX_SCRIPT_ALLOC_NODE(FoxAstDocComment);
        comment->Comment = &EatToken(TT::DocComment);
        CurrentDocComments.push_back(comment);

        if (mTokenIndex >= mTokens.Size()) {
            return nullptr;
        }
    }

    // Eat any extraneous semicolons
    while (GetToken().Type == TT::Semicolon) {
        EatToken(TT::Semicolon);

        if (mTokenIndex >= mTokens.Size()) {
            return nullptr;
        }
    }

    FoxAstNode* node = TryParseKeyword(parent_block);

    if (!node && (mTokenIndex < mTokens.Size() && GetToken().Type == TT::Identifier)) {
        if (mTokenIndex + 1 < mTokens.Size() && GetToken(1).Type == TT::LParen) {
            node = ParseFunctionCall();
        }
        else if (mTokenIndex + 1 < mTokens.Size() && GetToken(1).Type == TT::Equals) {
            Token& assign_name = EatToken(TT::Identifier);
            node = TryParseAssignment(&assign_name);
        }
        else {
            GetToken().Print();
        }
    }


    if (!node) {
        return nullptr;
    }

    // Blocks do not require semicolons
    if (node->NodeType == FX_AST_BLOCK || node->NodeType == FX_AST_ACTIONDECL) {
        return node;
    }

    EatToken(TT::Semicolon);

    return node;
}

FoxAstBlock* FoxConfigScript::ParseBlock()
{
    bool in_command = mInCommandMode;
    mInCommandMode = false;

    FoxAstBlock* block = FX_SCRIPT_ALLOC_NODE(FoxAstBlock);

    EatToken(TT::LBrace);

    while (GetToken().Type != TT::RBrace) {
        FoxAstNode* command = ParseStatement(block);
        if (command == nullptr) {
            break;
        }
        block->Statements.push_back(command);
    }

    EatToken(TT::RBrace);

    mInCommandMode = in_command;
    return block;
}

FoxAstFunctionDecl* FoxConfigScript::ParseFunctionDeclare()
{
    FoxAstFunctionDecl* node = FX_SCRIPT_ALLOC_NODE(FoxAstFunctionDecl);

    if (!CurrentDocComments.empty()) {
        node->DocComments = CurrentDocComments;
        CurrentDocComments.clear();
    }

    // Name of the function
    Token& name = EatToken(TT::Identifier);

    node->Name = &name;

    PushScope();
    EatToken(TT::LParen);

    FoxAstBlock* params = FX_SCRIPT_ALLOC_NODE(FoxAstBlock);

    // Parse the parameter list
    while (GetToken().Type != TT::RParen) {
        params->Statements.push_back(ParseVarDeclare());

        if (GetToken().Type == TT::Comma) {
            EatToken(TT::Comma);
            continue;
        }

        break;
    }

    EatToken(TT::RParen);

    // Parse the return type
    /*if (GetToken().Type != TT::LBrace) {
        FoxAstVarDecl* return_decl = ParseVarDeclare();
        node->ReturnVar = return_decl;
    }*/

    // Check to see if there is a return type provided
    if (GetToken().Type != TT::LBrace) {
        // There is a return type, declare the __ReturnVal__ variable

        // Get the token for the type
        Token& type_token = EatToken(TT::Identifier);

        FoxAstVarDecl* return_decl = InternalVarDeclare(mTokenReturnVar, &type_token);
        node->ReturnVar = return_decl;
    }


    node->Block = ParseBlock();
    PopScope();

    node->Params = params;

    FoxFunction function(&name, mCurrentScope, node->Block, node);
    mCurrentScope->Functions.Insert(function);

    return node;
}

// FoxValue FoxConfigScript::TryCallInternalFunc(FoxHash func_name, std::vector<FoxValue>& params)
//{
//     FoxValue return_value;
//
//     for (const FoxInternalFunc& func : mInternalFuncs) {
//         if (func.HashedName == func_name) {
//             func.Func(params, &return_value);
//             return return_value;
//         }
//     }
//
//     return return_value;
// }

FoxAstFunctionCall* FoxConfigScript::ParseFunctionCall()
{
    FoxAstFunctionCall* node = FX_SCRIPT_ALLOC_NODE(FoxAstFunctionCall);

    Token& name = EatToken(TT::Identifier);

    node->HashedName = name.GetHash();
    node->Function = FindFunction(node->HashedName);

    TT end_token_type = TT::Semicolon;

    if (!mInCommandMode) {
        end_token_type = TT::RParen;
    }

    if (!mInCommandMode || GetToken().Type == TT::LParen) {
        EatToken(TT::LParen);
    }

    while (GetToken().Type != end_token_type) {
        FoxAstNode* param = ParseRhs();

        if (param == nullptr) {
            break;
        }

        node->Params.push_back(param);

        TT next_tt = GetToken().Type;

        if (GetToken().Type == TT::Comma) {
            EatToken(TT::Comma);
            continue;
        }

        if (mInCommandMode && (next_tt != TT::RParen && next_tt != TT::Semicolon)) {
            continue;
        }


        break;
    }

    if (!mInCommandMode || GetToken().Type == TT::RParen) {
        EatToken(TT::RParen);
    }

    return node;
}

FoxAstBlock* FoxConfigScript::Parse()
{
    FoxAstBlock* root_block = FX_SCRIPT_ALLOC_NODE(FoxAstBlock);

    FoxAstNode* keyword;
    while ((keyword = ParseStatement(root_block))) {
        root_block->Statements.push_back(keyword);
    }

    /*for (const auto& var : mCurrentScope->Vars) {
        var.Print();
    }*/

    if (mHasErrors) {
        return nullptr;
    }

    FoxAstPrinter printer(root_block);
    printer.Print(root_block);

    return root_block;
}

void FoxAstDestroyer::Do(FoxAstNode* node)
{
    if (node == nullptr) {
        return;
    }

    if (node->NodeType == FX_AST_BLOCK) {
        FoxAstBlock* block = reinterpret_cast<FoxAstBlock*>(node);
        for (FoxAstNode* child : block->Statements) {
            Do(child);
        }

        FX_SCRIPT_FREE(FoxAstBlock, block);
    }
    else if (node->NodeType == FX_AST_ACTIONDECL) {
        FoxAstFunctionDecl* functiondecl = reinterpret_cast<FoxAstFunctionDecl*>(node);

        for (FoxAstNode* param : functiondecl->Params->Statements) {
            Do(param);
        }

        Do(functiondecl->Block);

        FX_SCRIPT_FREE(FoxAstFunctionDecl, functiondecl);
    }
    else if (node->NodeType == FX_AST_VARDECL) {
        FoxAstVarDecl* vardecl = reinterpret_cast<FoxAstVarDecl*>(node);

        Do(vardecl->Assignment);

        FX_SCRIPT_FREE(FoxAstVarDecl, vardecl);
    }
    else if (node->NodeType == FX_AST_ASSIGN) {
        FoxAstAssign* assign = reinterpret_cast<FoxAstAssign*>(node);

        Do(assign->Rhs);

        FX_SCRIPT_FREE(FoxAstAssign, assign);
    }
    else if (node->NodeType == FX_AST_ACTIONCALL) {
        FoxAstFunctionCall* functioncall = reinterpret_cast<FoxAstFunctionCall*>(node);

        FX_SCRIPT_FREE(FoxAstFunctionCall, functioncall);
    }
    else if (node->NodeType == FX_AST_LITERAL) {
        FoxAstLiteral* literal = reinterpret_cast<FoxAstLiteral*>(node);

        FX_SCRIPT_FREE(FoxAstLiteral, literal);
    }
    else if (node->NodeType == FX_AST_BINOP) {
        FoxAstBinop* binop = reinterpret_cast<FoxAstBinop*>(node);

        Do(binop->Left);
        Do(binop->Right);

        FX_SCRIPT_FREE(FoxAstBinop, binop);
    }
    else if (node->NodeType == FX_AST_COMMANDMODE) {
        FoxAstCommandMode* command_mode = reinterpret_cast<FoxAstCommandMode*>(node);

        Do(command_mode->Node);

        FX_SCRIPT_FREE(FoxAstCommandMode, command_mode);
    }
    else if (node->NodeType == FX_AST_RETURN) {
        FoxAstReturn* return_node = reinterpret_cast<FoxAstReturn*>(node);

        if (return_node->Rhs) {
            Do(return_node->Rhs);
        }

        FX_SCRIPT_FREE(FoxAstReturn, return_node);
    }
    else {
        FoxLogError("Cannot free unknown node!");
    }
}


///////////////////////////////////////////////
// IR Asm Emitter
///////////////////////////////////////////////

#pragma region IrEmitter

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
        return EmitBlock(reinterpret_cast<FoxAstBlock*>(node));
    }
    // else if (node->NodeType == FX_AST_ACTIONDECL) {
    //     return EmitFunction(reinterpret_cast<FoxAstFunctionDecl*>(node));
    // }
    else if (node->NodeType == FX_AST_ACTIONCALL) {
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
                    const FoxAstVarRef* var_ref = literal->Value.ValueRef;

                    // Get the variable and load it into a register.
                    FoxBytecodeVarHandle* var_to_return = FindVarHandle(var_ref->Name->GetHash());

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
    WriteOp(IrBase_Marker, IrSpecMarker_ParamsBegin);
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

    FoxIRRegister lhs_register = EmitRhsToRegister(binop->Left, FX_IR_NONE, true);
    FoxIRRegister rhs_register = FX_IR_NONE;

    if (binop->Right->NodeType == FX_AST_BINOP) {
        rhs_is_binop = true;

        FoxAstBinop* binop_node = reinterpret_cast<FoxAstBinop*>(binop->Right);
        MarkRegisterFree(rhs_register);
        rhs_register = EmitRhsToRegister(binop_node->Left, FX_IR_NONE, true);
    }
    else {
        rhs_register = EmitRhsToRegister(binop->Right, FX_IR_NONE, true);
    }

    if (binop->OpToken->Type == TT::Plus) {
        WriteOp(IrBase_Arith, IrSpecArith_Add_Reg32);

        mBytecode.Insert(lhs_register);
        mBytecode.Insert(rhs_register);
    }

    if (rhs_is_binop) {
        MarkRegisterFree(lhs_register);

        FoxAstBinop second_binop;
        second_binop.OpToken = binop->OpToken;
        second_binop.Left = binop->Left;
        second_binop.Right = reinterpret_cast<FoxAstBinop*>(binop->Right)->Right;

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
    FoxBytecodeVarHandle* var_handle = FindVarHandle(ref->Name->GetHash());

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
    FoxBytecodeVarHandle* var_handle = FindVarHandle(assign->Var->Name->GetHash());
    if (var_handle == nullptr) {
        FoxLogError("Var '{:.{}}' does not exist!", assign->Var->Name->Start, assign->Var->Name->Length);
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
            FoxHash var_name_hash = literal->Value.ValueRef->Name->GetHash();
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

            return dest_register;
        }
    }

    else if (rhs->NodeType == FX_AST_BINOP) {
        FoxIRRegister result_register = EmitBinop(reinterpret_cast<FoxAstBinop*>(rhs), nullptr);

        EmitMoveReg32(dest_register, dest_register);

        return dest_register;
    }
    else if (rhs->NodeType == FX_AST_ACTIONCALL) {
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
            FoxIRRegister output_register = EmitVarFetch(literal->Value.ValueRef, mode);
            if (mode == IRRhsMode::RHS_ASSIGN_TO_HANDLE) {
                // DoSaveReg32(handle->Offset, output_register);
                EmitVariableSetReg32(handle->VarIndexInScope, output_register);
            }

            return output_register;
        }

        return FX_IR_GW3;
    }

    else if (rhs->NodeType == FX_AST_ACTIONCALL || rhs->NodeType == FX_AST_BINOP) {
        FoxIRRegister result_register = FX_IR_GW3;

        if (rhs->NodeType == FX_AST_BINOP) {
            result_register = EmitBinop(reinterpret_cast<FoxAstBinop*>(rhs), handle);
        }

        else if (rhs->NodeType == FX_AST_ACTIONCALL) {
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
    };

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
        EmitRhsToRegister(param, static_cast<FoxIRRegister>(FX_IR_GW0 + parameter_index));

        parameter_index++;
    }

    // The handle could not be found, write it as a possible external symbol.
    if (!handle) {
        printf("Call name-> %u\n", call->HashedName);

        // Since popping the parameters are handled internally in the VM,
        // we need to decrement the stack offset here.
        for (int i = 0; i < call->Params.size(); i++) {
            mStackOffset -= 4;
        }

        EmitJumpCallExternal(call->HashedName);

        // EmitPop32(FX_REG_RA);
        return;
    }

    EmitJumpCallAbsolute(handle->HashedName);

    // Free all of the parameter used registers
    mRegsInUse = precall_regs_in_use;
}

FoxBytecodeVarHandle* FoxIREmitter::DefineAndFetchParam(FoxAstNode* param_decl_node, uint16 index)
{
    if (param_decl_node->NodeType != FX_AST_VARDECL) {
        FoxLogError("Param node type is not vardecl!");
        return nullptr;
    }

    // Emit variable without emitting pushes or pops
    FoxBytecodeVarHandle* handle = DoVarDeclare(reinterpret_cast<FoxAstVarDecl*>(param_decl_node), DECLARE_NO_EMIT);

    if (!handle) {
        FoxLogError("Could not define and fetch param!");
        return nullptr;
    }

    FoxIRRegister reg = static_cast<FoxIRRegister>(FX_IR_GW0 + index);

    if ((mRegsInUse & (1u << reg))) {
        FoxLogWarning("Clobbering register {} for function parameter", GetRegisterName(reg));
    }

    MarkRegisterUsed(reg);

    handle->Register = reg;

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
    for (FoxAstNode* stmt : block->Statements) {
        if (stmt->NodeType == FX_AST_ACTIONDECL) {
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
            DefineAndFetchParam(param_decl_node, parameter_index);

            parameter_index++;
        }

        if (function->Name) {
            EmitMarker(IrSpecMarker_FunctionName);
            FoxLogDebug("Data Name: {:.{}}\n", function->Name->Start, function->Name->Length);
            EmitDataString(function->Name->Start, function->Name->Length);
        }

        // FoxBytecodeVarHandle* return_var = DefineReturnVar(function->ReturnVar);

        // Do not check if there are function definitions to be declared when emitting the block here as they are checked above, before any parameters
        // or stack allocations are output.
        EmitBlock(function->Block, true);

        // Check to see if there has been a return statement in the function
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
}

void FoxIREmitter::EmitBlock(FoxAstBlock* block, bool ignore_function_definitions)
{
    RETURN_IF_NO_NODE(block);

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
        if (node->NodeType == FX_AST_ACTIONDECL) {
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

    if (node->NodeType == FX_AST_ACTIONCALL) {
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
        return (DoesNodeBranch(binop->Left) || DoesNodeBranch(binop->Right));
    }

    else if (node->NodeType == FX_AST_ASSIGN) {
        FoxAstAssign* assign = reinterpret_cast<FoxAstAssign*>(node);

        return DoesNodeBranch(assign->Rhs);
    }

    return false;
}

#pragma endregion IrEmitter


/////////////////////////////////////
// Bytecode Printer
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
    else if (op_spec == IrSpecMarker_ParamsBegin) {
        BC_PRINT_OP("@Params");
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
        // If there is a return statement on base level (without branching, conditions, etc.) then we can
        // omit the frame restore logic here as it will already be covered by the return statement.
        if (!GetCurrentFrame()->HasBaselevelReturnStmt) {
            EmitFrameRestore();
        }

        FoxAsmDecreaseIndent();

        FramePop();

        // Reset the current stack frame
    }
    else if (op_spec == IrSpecMarker_ParamsBegin) {
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
    case FX_IR_REG_RETURN_VALUE:
        return Fox_Arm64_W0;
    default:
        break;
    }

    return static_cast<FoxArm64Register>(static_cast<uint32>(Fox_Arm64_W8) + static_cast<uint32>(ir_reg));
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
