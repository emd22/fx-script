#include "FoxScript.hpp"

#include "FoxScriptUtil.hpp"

#include <stdio.h>

#include <vector>

#define FX_SCRIPT_SCOPE_GLOBAL_VARS_START_SIZE 32
#define FX_SCRIPT_SCOPE_LOCAL_VARS_START_SIZE 16

#define FX_SCRIPT_SCOPE_GLOBAL_ACTIONS_START_SIZE 32
#define FX_SCRIPT_SCOPE_LOCAL_ACTIONS_START_SIZE 32

using Token = FoxTokenizer::Token;
using TT = FoxTokenizer::TokenType;

FoxValue FoxValue::None {};

void FoxConfigScript::LoadFile(const char* path)
{
    FILE* fp = FoxUtil::FileOpen(path, "rb");
    if (fp == nullptr) {
        printf("[ERROR] Could not open config file at '%s'\n", path);
        return;
    }

    std::fseek(fp, 0, SEEK_END);
    size_t file_size = std::ftell(fp);
    std::rewind(fp);

    mFileData = FX_SCRIPT_ALLOC_MEMORY(char, file_size);

    size_t read_size = std::fread(mFileData, 1, file_size, fp);
    if (read_size != file_size) {
        printf("[WARNING] Error reading all data from config file at '%s' (read=%zu, size=%zu)\n", path, read_size, file_size);
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
        printf("[ERROR] %u:%u: Unexpected token type %s when expecting %s!\n", token.FileLine, token.FileColumn,
               FoxTokenizer::GetTypeName(token.Type), FoxTokenizer::GetTypeName(token_type));
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

        if (GetToken().Type != TT::Semicolon) {
            // There is a value that follows, get the value
            FoxAstNode* rhs = ParseRhs();

            // Assign the return value to __ReturnVal__

            FoxAstVarRef* var_ref = FX_SCRIPT_ALLOC_NODE(FoxAstVarRef);
            var_ref->Name = mTokenReturnVar;

            FoxAstAssign* assign = FX_SCRIPT_ALLOC_NODE(FoxAstAssign);
            assign->Var = var_ref;
            assign->Rhs = rhs;

            parent_block->Statements.push_back(assign);
        }

        FoxAstReturn* ret = FX_SCRIPT_ALLOC_NODE(FoxAstReturn);
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

FoxExternalFunc* FoxConfigScript::FindExternalFunction(FoxHash hashed_name)
{
    for (FoxExternalFunc& func : mExternalFuncs) {
        if (func.HashedName == hashed_name) {
            return &func;
        }
    }

    return nullptr;
}

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

void FoxConfigScript::Execute(FoxVM& vm)
{
    DefineDefaultExternalFunctions();

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


#if 0
    FoxBCEmitter emitter;
    emitter.BeginEmitting(mRootBlock);

    printf("\n=====\n");

    FoxBCPrinter printer(emitter.mBytecode);
    printer.Print();

    printf("\n=====\n");

    FoxTranspilerX86 transpiler(emitter.mBytecode);
    transpiler.Print();

    printf("\n=====\n");

    for (FoxBytecodeVarHandle& handle : emitter.VarHandles) {
        printf("Var(%u) AT %lld\n", handle.HashedName, handle.Offset);
    }

    // return;

    vm.mExternalFuncs = mExternalFuncs;
    vm.Start(std::move(emitter.mBytecode));

    for (FoxBytecodeVarHandle& handle : emitter.VarHandles) {
        printf("Var(%u) AT %lld -> %u\n", handle.HashedName, handle.Offset, vm.Stack[handle.Offset]);
    }

    return;
#endif
}

#if 0
bool FoxConfigScript::ExecuteUserCommand(const char* command, FoxInterpreter& interpreter)
{
    mHasErrors = false;

    // If there are errors, exit early
    if (mRootBlock == nullptr) {
        return false;
    }

    uint32 new_token_index = mTokens.Size();

    uint32 old_token_index = mTokenIndex;

    {
        uint32 length_of_command = strlen(command);

        char* data_buffer = FX_SCRIPT_ALLOC_MEMORY(char, length_of_command + 1);
        memcpy(data_buffer, command, length_of_command);

        FoxTokenizer tokenizer(data_buffer, length_of_command);
        tokenizer.Tokenize();

        for (FoxTokenizer::Token& token : tokenizer.GetTokens()) {
            // token.Print();
            mTokens.Insert(token);
        }

        /*for (FoxTokenizer::Token& token : mTokens) {
            token.Print();
        }*/
    }

    mTokenIndex = new_token_index;
    mInCommandMode = true;

    FoxAstBlock* root_block = FX_SCRIPT_ALLOC_NODE(FoxAstBlock);

    FoxAstNode* statement;
    while ((statement = ParseStatementAsCommand(root_block))) {
        root_block->Statements.push_back(statement);
    }

    // FoxAstNode* node = ParseStatementAsCommand();

    // Revert the current state if there have been errors
    if (root_block == nullptr) {
        printf("Ignoring state...\n");
        // Get the size of the tokens
        // int current_token_index = mTokens.Size();
        for (int i = new_token_index; i < new_token_index; i++) {
            mTokens.RemoveLast();
        }

        mTokenIndex = old_token_index;

        mInCommandMode = false;

        // Since we have reverted state, clear errors flag
        mHasErrors = false;

        return false;
    }

#if 0

    // Run the new block
    {
        interpreter.mInCommandMode = true;
        interpreter.Visit(root_block);
        interpreter.mInCommandMode = false;
    }
#endif
    mInCommandMode = false;

    return true;
}

#endif

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

            printf("ERROR: Undefined reference to variable ");
            token.Print();
            printf("\n");

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
            FoxExternalFunc* external_function = FindExternalFunction(token.GetHash());
            if (external_function != nullptr) {
                lhs = ParseFunctionCall();
            }

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

    /*
    RETURN_IF_NO_TOKENS(nullptr);

    bool has_parameters = false;

    if (mTokenIndex + 1 < mTokens.Size()) {
        TT next_token_type = GetToken(1).Type;
        has_parameters = next_token_type == TT::LParen;

        if (mInCommandMode) {
            has_parameters = next_token_type == TT::Identifier || next_token_type == TT::Integer || next_token_type == TT::Float || next_token_type ==
    TT::String;
        }
    }

    FoxTokenizer::Token& token = GetToken();

    FoxAstNode* lhs = nullptr;

    if (token.Type == TT::Identifier) {
        if (has_parameters) {
            lhs = ParseFunctionCall();
        }
        else {
            FoxExternalFunc* external_function = FindExternalFunction(token.GetHash());
            if (external_function != nullptr) {
                return ParseFunctionCall();
            }

            FoxFunction* function = FindFunction(token.GetHash());
            if (function != nullptr) {
                return ParseFunctionCall();
            }

        }
    }

    else if (IsTokenTypeLiteral(token.Type) || token.Type == TT::Identifier) {
        FoxAstLiteral* literal = FX_SCRIPT_ALLOC_NODE(FoxAstLiteral);
        literal->Value = ParseValue();
        lhs = literal;
    }

    TT op_type = GetToken(0).Type;
    if (op_type == TT::Plus || op_type == TT::Minus) {
        FoxAstBinop* binop = FX_SCRIPT_ALLOC_NODE(FoxAstBinop);

        binop->Left = lhs;
        binop->OpToken = &EatToken(op_type);
        binop->Right = ParseRhs();

        return binop;
    }

    return lhs;
    */
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
            FoxExternalFunc* external_function = FindExternalFunction(GetToken().GetHash());
            if (external_function != nullptr) {
                node = ParseFunctionCall();
            }
            else {
                FoxFunction* function = FindFunction(GetToken().GetHash());
                if (function != nullptr) {
                    node = ParseFunctionCall();
                }
            }
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


// void FoxConfigScript::ParseDoCall()
//{
//     Token& call_name = EatToken(TT::Identifier);
//
//     EatToken(TT::LParen);
//
//     std::vector<FoxValue> params;
//
//     while (true) {
//         params.push_back(ParseValue());
//
//         if (GetToken().Type == TT::Comma) {
//             EatToken(TT::Comma);
//             continue;
//         }
//
//         break;
//     }
//
//     EatToken(TT::RParen);
//
//     TryCallInternalFunc(call_name.GetHash(), params);
//
//     printf("Calling ");
//     call_name.Print();
//
//     for (const auto& param : params) {
//         printf("param : ");
//         param.Print();
//     }
// }


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


void FoxConfigScript::DefineDefaultExternalFunctions()
{
    // log([int | float | string | ref] args...)
    RegisterExternalFunc(
        FoxHashStr("log"), {}, // Do not check argument types as we handle it here
        [](FoxVM* vm, std::vector<FoxValue>& args, FoxValue* return_value)
        {
            printf("[SCRIPT]: ");

            for (int i = args.size() - 1; i >= 0; i--) {
                FoxValue& arg = args[i];

                // const FoxValue& value = interpreter.GetImmediateValue(arg);
                const FoxValue& value = arg;

                switch (value.Type) {
                case FoxValue::NONETYPE:
                    printf("[none]");
                    break;
                case FoxValue::INT:
                    printf("%d", value.ValueInt);
                    break;
                case FoxValue::FLOAT:
                    printf("%f", value.ValueFloat);
                    break;
                case FoxValue::STRING:
                    printf("%s", value.ValueString);
                    break;
                default:
                    printf("Unknown type\n");
                    break;
                }

                putchar(' ');
            }

            putchar('\n');
        },
        true // Is variadic?
    );
}


void FoxConfigScript::RegisterExternalFunc(FoxHash func_name, std::vector<FoxValue::ValueType> param_types, FoxExternalFunc::FuncType callback,
                                           bool is_variadic)
{
    FoxExternalFunc func {
        .HashedName = func_name,
        .Function = callback,
        .ParameterTypes = param_types,
        .IsVariadic = is_variadic,
    };

    mExternalFuncs.push_back(func);
}


/////////////////////////////////////////
// Script Bytecode Emitter
/////////////////////////////////////////

#pragma region BytecodeEmitter

#include "FoxScriptBytecode.hpp"

void FoxBCEmitter::BeginEmitting(FoxAstNode* node)
{
    mStackSize = 1024;

    /*mStack = new uint8[1024];
    mStackStart = mStack;*/

    mBytecode.Create(4096);
    VarHandles.Create(64);

    Emit(node);

    printf("\n");

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

void FoxBCEmitter::Emit(FoxAstNode* node)
{
    RETURN_IF_NO_NODE(node);

    if (node->NodeType == FX_AST_BLOCK) {
        return EmitBlock(reinterpret_cast<FoxAstBlock*>(node));
    }
    else if (node->NodeType == FX_AST_ACTIONDECL) {
        return EmitFunction(reinterpret_cast<FoxAstFunctionDecl*>(node));
    }
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
        constexpr FoxHash return_val_hash = FoxHashStr(FX_SCRIPT_VAR_RETURN_VAL);

        FoxBytecodeVarHandle* return_var = FindVarHandle(return_val_hash);

        if (return_var) {
            DoLoad(return_var->Offset, FX_REG_XR);
        }

        EmitJumpReturnToCaller();

        return;
    }
}

FoxBytecodeVarHandle* FoxBCEmitter::FindVarHandle(FoxHash hashed_name)
{
    for (FoxBytecodeVarHandle& handle : VarHandles) {
        if (handle.HashedName == hashed_name) {
            return &handle;
        }
    }
    return nullptr;
}

FoxBytecodeFunctionHandle* FoxBCEmitter::FindFunctionHandle(FoxHash hashed_name)
{
    for (FoxBytecodeFunctionHandle& handle : FunctionHandles) {
        if (handle.HashedName == hashed_name) {
            return &handle;
        }
    }
    return nullptr;
}


FoxBCRegister FoxBCEmitter::FindFreeRegister()
{
    uint16 gp_r = 0x01;

    const uint16 num_gp_regs = FX_REG_X3;

    for (int i = 0; i < num_gp_regs; i++) {
        if (!(mRegsInUse & gp_r)) {
            // We are starting on 0x01, so register index should be N + 1
            const int register_index = i + 1;

            return static_cast<FoxBCRegister>(register_index);
        }

        gp_r <<= 1;
    }

    return FoxBCRegister::FX_REG_NONE;
}

const char* FoxBCEmitter::GetRegisterName(FoxBCRegister reg)
{
    switch (reg) {
    case FX_REG_NONE:
        return "NONE";
    case FX_REG_X0:
        return "X0";
    case FX_REG_X1:
        return "X1";
    case FX_REG_X2:
        return "X2";
    case FX_REG_X3:
        return "X3";
    case FX_REG_RA:
        return "RA";
    case FX_REG_XR:
        return "XR";
    case FX_REG_SP:
        return "SP";
    default:;
    };

    return "NONE";
}

FoxBCRegister FoxBCEmitter::RegFlagToReg(FoxRegisterFlag reg_flag)
{
    switch (reg_flag) {
    case FX_REGFLAG_NONE:
        return FX_REG_NONE;
    case FX_REGFLAG_X0:
        return FX_REG_X0;
    case FX_REGFLAG_X1:
        return FX_REG_X1;
    case FX_REGFLAG_X2:
        return FX_REG_X2;
    case FX_REGFLAG_X3:
        return FX_REG_X3;
    case FX_REGFLAG_RA:
        return FX_REG_RA;
    case FX_REGFLAG_XR:
        return FX_REG_XR;
    }

    return FX_REG_NONE;
}

FoxRegisterFlag FoxBCEmitter::RegToRegFlag(FoxBCRegister reg)
{
    switch (reg) {
    case FX_REG_NONE:
        return FX_REGFLAG_NONE;
    case FX_REG_X0:
        return FX_REGFLAG_X0;
    case FX_REG_X1:
        return FX_REGFLAG_X1;
    case FX_REG_X2:
        return FX_REGFLAG_X2;
    case FX_REG_X3:
        return FX_REGFLAG_X3;
    case FX_REG_RA:
        return FX_REGFLAG_RA;
    case FX_REG_XR:
        return FX_REGFLAG_XR;
    case FX_REG_SP:
        return FX_REGFLAG_NONE;
    default:;
    }

    return FX_REGFLAG_NONE;
}


void FoxBCEmitter::Write16(uint16 value)
{
    mBytecode.Insert(static_cast<uint8>(value >> 8));
    mBytecode.Insert(static_cast<uint8>(value));
}

void FoxBCEmitter::Write32(uint32 value)
{
    Write16(static_cast<uint16>(value >> 16));
    Write16(static_cast<uint16>(value));
}

void FoxBCEmitter::WriteOp(uint8 op_base, uint8 op_spec)
{
    mBytecode.Insert(op_base);
    mBytecode.Insert(op_spec);
}

using RhsMode = FoxBCEmitter::RhsMode;

#define MARK_REGISTER_USED(regn_)                                                                                                                    \
    {                                                                                                                                                \
        MarkRegisterUsed(regn_);                                                                                                                     \
    }
#define MARK_REGISTER_FREE(regn_)                                                                                                                    \
    {                                                                                                                                                \
        MarkRegisterFree(regn_);                                                                                                                     \
    }

void FoxBCEmitter::MarkRegisterUsed(FoxBCRegister reg)
{
    FoxRegisterFlag rflag = RegToRegFlag(reg);
    mRegsInUse = static_cast<FoxRegisterFlag>(uint16(mRegsInUse) | uint16(rflag));
}

void FoxBCEmitter::MarkRegisterFree(FoxBCRegister reg)
{
    FoxRegisterFlag rflag = RegToRegFlag(reg);

    mRegsInUse = static_cast<FoxRegisterFlag>(uint16(mRegsInUse) & (~uint16(rflag)));
}

void FoxBCEmitter::EmitSave32(int16 offset, uint32 value)
{
    // SAVE32 [i16 offset] [i32]
    WriteOp(OpBase_Save, OpSpecSave_Int32);

    Write16(offset);
    Write32(value);
}

void FoxBCEmitter::EmitSaveReg32(int16 offset, FoxBCRegister reg)
{
    // SAVE32r [i16 offset] [%r32]
    WriteOp(OpBase_Save, OpSpecSave_Reg32);

    Write16(offset);
    Write16(reg);
}


void FoxBCEmitter::EmitSaveAbsolute32(uint32 position, uint32 value)
{
    // SAVE32a [i32 offset] [i32]
    WriteOp(OpBase_Save, OpSpecSave_AbsoluteInt32);

    Write32(position);
    Write32(value);
}

void FoxBCEmitter::EmitSaveAbsoluteReg32(uint32 position, FoxBCRegister reg)
{
    // SAVE32r [i32 offset] [%r32]
    WriteOp(OpBase_Save, OpSpecSave_AbsoluteReg32);

    Write32(position);
    Write16(reg);
}

void FoxBCEmitter::EmitPush32(uint32 value)
{
    // PUSH32 [i32]
    WriteOp(OpBase_Push, OpSpecPush_Int32);
    Write32(value);

    mStackOffset += 4;
}

void FoxBCEmitter::EmitPush32r(FoxBCRegister reg)
{
    // PUSH32r [%r32]
    WriteOp(OpBase_Push, OpSpecPush_Reg32);
    Write16(reg);

    mStackOffset += 4;
}


void FoxBCEmitter::EmitPop32(FoxBCRegister output_reg)
{
    // POP32 [%r32]
    WriteOp(OpBase_Pop, (OpSpecPop_Int32 << 4) | (output_reg & 0x0F));

    mStackOffset -= 4;
}

void FoxBCEmitter::EmitLoad32(int offset, FoxBCRegister output_reg)
{
    // LOAD [i16] [%r32]
    WriteOp(OpBase_Load, (OpSpecLoad_Int32 << 4) | (output_reg & 0x0F));
    Write16(static_cast<uint16>(offset));
}

void FoxBCEmitter::EmitLoadAbsolute32(uint32 position, FoxBCRegister output_reg)
{
    // LOADA [i32] [%r32]
    WriteOp(OpBase_Load, (OpSpecLoad_AbsoluteInt32 << 4) | (output_reg & 0x0F));
    Write32(position);
}

void FoxBCEmitter::EmitJumpRelative(uint16 offset)
{
    WriteOp(OpBase_Jump, OpSpecJump_Relative);
    Write16(offset);
}

void FoxBCEmitter::EmitJumpAbsolute(uint32 position)
{
    WriteOp(OpBase_Jump, OpSpecJump_Absolute);
    Write32(position);
}


void FoxBCEmitter::EmitJumpAbsoluteReg32(FoxBCRegister reg)
{
    WriteOp(OpBase_Jump, OpSpecJump_AbsoluteReg32);
    Write16(reg);
}

void FoxBCEmitter::EmitJumpCallAbsolute(uint32 position)
{
    WriteOp(OpBase_Jump, OpSpecJump_CallAbsolute);
    Write32(position);
}


void FoxBCEmitter::EmitJumpCallExternal(FoxHash hashed_name)
{
    WriteOp(OpBase_Jump, OpSpecJump_CallExternal);
    Write32(hashed_name);
}

void FoxBCEmitter::EmitJumpReturnToCaller()
{
    WriteOp(OpBase_Jump, OpSpecJump_ReturnToCaller);
}

void FoxBCEmitter::EmitMoveInt32(FoxBCRegister reg, uint32 value)
{
    WriteOp(OpBase_Move, (OpSpecMove_Int32 << 4) | (reg & 0x0F));
    Write32(value);
}

void FoxBCEmitter::EmitParamsStart()
{
    WriteOp(OpBase_Data, OpSpecData_ParamsStart);
}

void FoxBCEmitter::EmitType(FoxValue::ValueType type)
{
    OpSpecType op_type = OpSpecType_Int;

    if (type == FoxValue::STRING) {
        op_type = OpSpecType_String;
    }

    WriteOp(OpBase_Type, op_type);
}

uint32 FoxBCEmitter::EmitDataString(char* str, uint16 length)
{
    WriteOp(OpBase_Data, OpSpecData_String);

    uint32 start_index = mBytecode.Size();

    uint16 final_length = length + 1;

    // If the length is not a factor of 2 (sizeof uint16) then add a byte of padding
    if ((final_length & 0x01)) {
        ++final_length;
    }

    Write16(final_length);

    for (int i = 0; i < final_length; i += 2) {
        mBytecode.Insert(str[i]);

        if (i >= length) {
            mBytecode.Insert(0);
            break;
        }

        mBytecode.Insert(str[i + 1]);
    }

    return start_index;
}


FoxBCRegister FoxBCEmitter::EmitBinop(FoxAstBinop* binop, FoxBytecodeVarHandle* handle)
{
    bool will_preserve_lhs = false;
    // Load the A and B values into the registers
    FoxBCRegister a_reg = EmitRhs(binop->Left, RhsMode::RHS_FETCH_TO_REGISTER, handle);

    // Since there is a chance that this register will be clobbered (by binop, function call, etc), we will
    // push the value of the register here and return it after processing the RHS
    if (binop->Right->NodeType != FX_AST_LITERAL) {
        will_preserve_lhs = true;
        EmitPush32r(a_reg);
    }

    FoxBCRegister b_reg = EmitRhs(binop->Right, RhsMode::RHS_FETCH_TO_REGISTER, handle);

    // Retrieve the previous LHS
    if (will_preserve_lhs) {
        EmitPop32(a_reg);
    }

    if (binop->OpToken->Type == TT::Plus) {
        WriteOp(OpBase_Arith, OpSpecArith_Add);

        mBytecode.Insert(a_reg);
        mBytecode.Insert(b_reg);
    }

    // We no longer need the lhs or rhs registers, free em
    MARK_REGISTER_FREE(a_reg);
    MARK_REGISTER_FREE(b_reg);

    return FX_REG_XR;
}

FoxBCRegister FoxBCEmitter::EmitVarFetch(FoxAstVarRef* ref, RhsMode mode)
{
    FoxBytecodeVarHandle* var_handle = FindVarHandle(ref->Name->GetHash());

    bool force_absolute_load = false;

    // If the variable is from a previous scope, load it from an absolute address. local offsets
    // will change depending on where they are called.
    if (var_handle->ScopeIndex < mScopeIndex) {
        force_absolute_load = true;
    }

    if (!var_handle) {
        printf("Could not find var handle!");
        return FX_REG_NONE;
    }

    FoxBCRegister reg = FindFreeRegister();

    MARK_REGISTER_USED(reg);

    DoLoad(var_handle->Offset, reg, force_absolute_load);

    if (mode == RhsMode::RHS_FETCH_TO_REGISTER) {
        return reg;
    }

    // If we are just copying the variable to this new variable, we can free the register after
    // we push to the stack.
    if (mode == RhsMode::RHS_DEFINE_IN_MEMORY) {
        if (var_handle->Type == FoxValue::STRING) {
            EmitType(var_handle->Type);
        }

        EmitPush32r(reg);
        MARK_REGISTER_FREE(reg);

        return FX_REG_NONE;
    }

    // This is a reference to a variable, return the register we loaded it into
    return reg;
}

void FoxBCEmitter::DoLoad(uint32 stack_offset, FoxBCRegister output_reg, bool force_absolute)
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

void FoxBCEmitter::DoSaveInt32(uint32 stack_offset, uint32 value, bool force_absolute)
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

void FoxBCEmitter::DoSaveReg32(uint32 stack_offset, FoxBCRegister reg, bool force_absolute)
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

void FoxBCEmitter::EmitAssign(FoxAstAssign* assign)
{
    FoxBytecodeVarHandle* var_handle = FindVarHandle(assign->Var->Name->GetHash());
    if (var_handle == nullptr) {
        printf("!!! Var '%.*s' does not exist!\n", assign->Var->Name->Length, assign->Var->Name->Start);
        return;
    }

    // bool force_absolute_save = false;

    // if (var_handle->ScopeIndex < mScopeIndex) {
    //     force_absolute_save = true;
    // }

    // int output_offset = -(static_cast<int>(mStackOffset) - static_cast<int>(var_handle->Offset));

    if (!var_handle) {
        printf("Could not find var handle to assign to!");
        return;
    }

    EmitRhs(assign->Rhs, RhsMode::RHS_ASSIGN_TO_HANDLE, var_handle);
}

FoxBCRegister FoxBCEmitter::EmitLiteralInt(FoxAstLiteral* literal, RhsMode mode, FoxBytecodeVarHandle* handle)
{
    // If this is on variable definition, push the value to the stack.
    if (mode == RhsMode::RHS_DEFINE_IN_MEMORY) {
        EmitPush32(literal->Value.ValueInt);

        return FX_REG_NONE;
    }

    // If this is as a literal, push the value to the stack and pop onto the target register.
    else if (mode == RhsMode::RHS_FETCH_TO_REGISTER) {
        // EmitPush32(literal->Value.ValueInt);

        FoxBCRegister output_reg = FindFreeRegister();
        // EmitPop32(output_reg);

        EmitMoveInt32(output_reg, literal->Value.ValueInt);

        // Mark the output register as used to store it
        MARK_REGISTER_USED(output_reg);

        return output_reg;
    }

    else if (mode == RhsMode::RHS_ASSIGN_TO_HANDLE) {
        const bool force_absolute_save = (handle->ScopeIndex < mScopeIndex);
        DoSaveInt32(handle->Offset, literal->Value.ValueInt, force_absolute_save);

        return FX_REG_NONE;
    }

    return FX_REG_NONE;
}


FoxBCRegister FoxBCEmitter::EmitLiteralString(FoxAstLiteral* literal, RhsMode mode, FoxBytecodeVarHandle* handle)
{
    const uint32 string_length = strlen(literal->Value.ValueString);

    // Emit the length and string data
    const uint32 string_position = EmitDataString(literal->Value.ValueString, string_length);

    // local string some_value = "Some String";
    if (mode == RhsMode::RHS_DEFINE_IN_MEMORY) {
        // Push the location and mark it as a pointer to a string
        EmitType(FoxValue::STRING);
        EmitPush32(string_position);

        return FX_REG_NONE;
    }

    // some_function("Some String")
    else if (mode == RhsMode::RHS_FETCH_TO_REGISTER) {
        // Push the location for the string and pop it back to a register.
        EmitType(FoxValue::STRING);

        // Push the string position
        // EmitPush32(string_position);

        // Find a register to output to and write the index
        FoxBCRegister output_reg = FindFreeRegister();
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

        return FX_REG_NONE;
    }

    return FX_REG_NONE;
}

FoxBCRegister FoxBCEmitter::EmitRhs(FoxAstNode* rhs, FoxBCEmitter::RhsMode mode, FoxBytecodeVarHandle* handle)
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
            FoxBCRegister output_register = EmitVarFetch(literal->Value.ValueRef, mode);
            if (mode == RhsMode::RHS_ASSIGN_TO_HANDLE) {
                DoSaveReg32(handle->Offset, output_register);
            }

            return output_register;
        }

        return FX_REG_NONE;
    }

    else if (rhs->NodeType == FX_AST_ACTIONCALL || rhs->NodeType == FX_AST_BINOP) {
        FoxBCRegister result_register = FX_REG_XR;

        if (rhs->NodeType == FX_AST_BINOP) {
            result_register = EmitBinop(reinterpret_cast<FoxAstBinop*>(rhs), handle);
        }

        else if (rhs->NodeType == FX_AST_ACTIONCALL) {
            DoFunctionCall(reinterpret_cast<FoxAstFunctionCall*>(rhs));
            // Function results are stored in XR
            result_register = FX_REG_XR;
        }


        if (mode == RhsMode::RHS_DEFINE_IN_MEMORY) {
            uint32 offset = mStackOffset;
            EmitPush32r(result_register);

            if (handle) {
                handle->Offset = offset;
            }

            return FX_REG_NONE;
        }

        else if (mode == RhsMode::RHS_FETCH_TO_REGISTER) {
            // Push the result to a register
            EmitPush32r(result_register);

            // Find a register to output to, and pop the value to there.
            FoxBCRegister output_reg = FindFreeRegister();
            EmitPop32(output_reg);

            // Mark the output register as used to store it
            MARK_REGISTER_USED(output_reg);

            return output_reg;
        }
        else if (mode == RhsMode::RHS_ASSIGN_TO_HANDLE) {
            const bool force_absolute_save = (handle->ScopeIndex < mScopeIndex);

            // Save the value back to the variable
            DoSaveReg32(handle->Offset, result_register, force_absolute_save);

            return FX_REG_NONE;
        }
    }

    return FX_REG_NONE;
}

FoxBytecodeVarHandle* FoxBCEmitter::DoVarDeclare(FoxAstVarDecl* decl, VarDeclareMode mode)
{
    RETURN_VALUE_IF_NO_NODE(decl, nullptr);

    const uint16 size_of_type = static_cast<uint16>(sizeof(int32));

    const FoxHash type_int = FoxHashStr("int");
    const FoxHash type_string = FoxHashStr("string");

    FoxHash decl_hash = decl->Name->GetHash();

    FoxValue::ValueType value_type = FoxValue::INT;

    switch (decl_hash) {
    case type_int:
        value_type = FoxValue::INT;
        break;
    case type_string:
        value_type = FoxValue::STRING;
        break;
    };

    FoxBytecodeVarHandle handle {
        .HashedName = decl->Name->GetHash(),
        .Type = value_type, // Just int for now
        .Offset = (mStackOffset),
        .SizeOnStack = size_of_type,
        .ScopeIndex = mScopeIndex,
    };

    VarHandles.Insert(handle);

    FoxBytecodeVarHandle* inserted_handle = &VarHandles[VarHandles.Size() - 1];

    if (mode == DECLARE_NO_EMIT) {
        // Do not emit any values
        return inserted_handle;
    }


    if (decl->Assignment) {
        FoxAstNode* rhs = decl->Assignment->Rhs;

        EmitRhs(rhs, RhsMode::RHS_DEFINE_IN_MEMORY, inserted_handle);


        // EmitPush32(0);

        // EmitRhs(rhs, RhsMode::RHS_ASSIGN_TO_HANDLE, inserted_handle);
    }
    else {
        // There is no assignment, push zero as the value for now and
        // a later assignment can set it using save32.
        EmitPush32(0);
    }

    return inserted_handle;
}

void FoxBCEmitter::DoFunctionCall(FoxAstFunctionCall* call)
{
    RETURN_IF_NO_NODE(call);

    FoxBytecodeFunctionHandle* handle = FindFunctionHandle(call->HashedName);


    std::vector<uint32> call_locations;
    call_locations.reserve(8);

    // Push all params to stack
    for (FoxAstNode* param : call->Params) {
        // FoxBCRegister reg =
        if (param->NodeType == FX_AST_ACTIONCALL) {
            EmitRhs(param, RhsMode::RHS_DEFINE_IN_MEMORY, nullptr);
            call_locations.push_back(mStackOffset - 4);
        }
        // MARK_REGISTER_FREE(reg);
    }

    EmitPush32r(FX_REG_RA);

    EmitParamsStart();

    int call_location_index = 0;

    // Push all params to stack
    for (FoxAstNode* param : call->Params) {
        if (param->NodeType == FX_AST_ACTIONCALL) {
            FoxBCRegister temp_register = FindFreeRegister();

            DoLoad(call_locations[call_location_index], temp_register);
            call_location_index++;

            EmitPush32r(temp_register);

            continue;
        }

        EmitRhs(param, RhsMode::RHS_DEFINE_IN_MEMORY, nullptr);
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

        EmitPop32(FX_REG_RA);
        return;
    }

    EmitJumpCallAbsolute(handle->BytecodeIndex);

    EmitPop32(FX_REG_RA);
}

FoxBytecodeVarHandle* FoxBCEmitter::DefineAndFetchParam(FoxAstNode* param_decl_node)
{
    if (param_decl_node->NodeType != FX_AST_VARDECL) {
        printf("!!! param node type is not vardecl!\n");
        return nullptr;
    }

    // Emit variable without emitting pushes or pops
    FoxBytecodeVarHandle* handle = DoVarDeclare(reinterpret_cast<FoxAstVarDecl*>(param_decl_node), DECLARE_NO_EMIT);

    if (!handle) {
        printf("!!! could not define and fetch param!\n");
        return nullptr;
    }

    assert(handle->SizeOnStack == 4);

    mStackOffset += handle->SizeOnStack;

    return handle;
}

FoxBytecodeVarHandle* FoxBCEmitter::DefineReturnVar(FoxAstVarDecl* decl)
{
    RETURN_VALUE_IF_NO_NODE(decl, nullptr);

    return DoVarDeclare(decl);
}

void FoxBCEmitter::EmitFunction(FoxAstFunctionDecl* function)
{
    RETURN_IF_NO_NODE(function);

    ++mScopeIndex;

    // Store the bytecode offset before the function is emitted

    const size_t start_of_function = mBytecode.Size();
    printf("Start of function %zu\n", start_of_function);

    // Emit the jump instruction, we will update the jump position after emitting all of the code inside the block
    EmitJumpRelative(0);

    // const uint32 initial_stack_offset = mStackOffset;

    const size_t header_jump_start_index = start_of_function + sizeof(uint16);

    size_t start_var_handle_count = VarHandles.Size();

    // Offset for the pushed return address
    mStackOffset += 4;

    // Emit the body of the function
    {
        for (FoxAstNode* param_decl_node : function->Params->Statements) {
            DefineAndFetchParam(param_decl_node);
        }

        FoxBytecodeVarHandle* return_var = DefineReturnVar(function->ReturnVar);

        EmitBlock(function->Block);

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

            if (return_var != nullptr) {
                DoLoad(return_var->Offset, FX_REG_XR);
            }
        }
    }

    // Return offset back to pre-call
    mStackOffset -= 4;

    const size_t end_of_function = mBytecode.Size();
    const uint16 distance_to_function = static_cast<uint16>(end_of_function - (start_of_function)-4);

    // Update the jump to the end of the function
    mBytecode[header_jump_start_index] = static_cast<uint8>(distance_to_function >> 8);
    mBytecode[header_jump_start_index + 1] = static_cast<uint8>((distance_to_function & 0xFF));

    FoxBytecodeFunctionHandle function_handle {.HashedName = function->Name->GetHash(), .BytecodeIndex = static_cast<uint32>(start_of_function + 4)};

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

void FoxBCEmitter::EmitBlock(FoxAstBlock* block)
{
    RETURN_IF_NO_NODE(block);

    for (FoxAstNode* node : block->Statements) {
        Emit(node);
    }
}

void FoxBCEmitter::PrintBytecode()
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

#pragma endregion BytecodeEmitter


/////////////////////////////////////
// Bytecode Printer
/////////////////////////////////////

uint16 FoxBCPrinter::Read16()
{
    uint8 lo = mBytecode[mBytecodeIndex++];
    uint8 hi = mBytecode[mBytecodeIndex++];

    return ((static_cast<uint16>(lo) << 8) | hi);
}

uint32 FoxBCPrinter::Read32()
{
    uint16 lo = Read16();
    uint16 hi = Read16();

    return ((static_cast<uint32>(lo) << 16) | hi);
}

#define BC_PRINT_OP(fmt_, ...) snprintf(s, 128, fmt_, ##__VA_ARGS__)

void FoxBCPrinter::DoLoad(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == OpSpecLoad_Int32) {
        int16 offset = Read16();
        BC_PRINT_OP("load32 %d, %s", offset, FoxBCEmitter::GetRegisterName(static_cast<FoxBCRegister>(op_reg)));
    }
    else if (op_spec == OpSpecLoad_AbsoluteInt32) {
        uint32 offset = Read32();
        BC_PRINT_OP("load32a %u, %s", offset, FoxBCEmitter::GetRegisterName(static_cast<FoxBCRegister>(op_reg)));
    }
}

void FoxBCPrinter::DoPush(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == OpSpecPush_Int32) {
        uint32 value = Read32();
        BC_PRINT_OP("push32 %u", value);
    }
    else if (op_spec == OpSpecPush_Reg32) {
        uint16 reg = Read16();
        BC_PRINT_OP("push32r %s", FoxBCEmitter::GetRegisterName(static_cast<FoxBCRegister>(reg)));
    }
}

void FoxBCPrinter::DoPop(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == OpSpecPush_Int32) {
        BC_PRINT_OP("pop32 %s", FoxBCEmitter::GetRegisterName(static_cast<FoxBCRegister>(op_reg)));
    }
}

void FoxBCPrinter::DoArith(char* s, uint8 op_base, uint8 op_spec)
{
    uint8 a_reg = mBytecode[mBytecodeIndex++];
    uint8 b_reg = mBytecode[mBytecodeIndex++];

    if (op_spec == OpSpecArith_Add) {
        BC_PRINT_OP("add32 %s, %s", FoxBCEmitter::GetRegisterName(static_cast<FoxBCRegister>(a_reg)),
                    FoxBCEmitter::GetRegisterName(static_cast<FoxBCRegister>(b_reg)));
    }
}

void FoxBCPrinter::DoSave(char* s, uint8 op_base, uint8 op_spec)
{
    // Save a imm32 into an offset in the stack
    if (op_spec == OpSpecSave_Int32) {
        const int16 offset = Read16();
        const uint32 value = Read32();

        BC_PRINT_OP("save32 %d, %u", offset, value);
    }

    // Save a register into an offset in the stack
    else if (op_spec == OpSpecSave_Reg32) {
        const int16 offset = Read16();
        uint16 reg = Read16();

        BC_PRINT_OP("save32r %d, %s", offset, FoxBCEmitter::GetRegisterName(static_cast<FoxBCRegister>(reg)));
    }
    else if (op_spec == OpSpecSave_AbsoluteInt32) {
        const uint32 offset = Read32();
        const uint32 value = Read32();

        BC_PRINT_OP("save32a %u, %u", offset, value);
    }
    else if (op_spec == OpSpecSave_AbsoluteReg32) {
        const uint32 offset = Read32();
        uint16 reg = Read16();

        BC_PRINT_OP("save32ar %u, %s", offset, FoxBCEmitter::GetRegisterName(static_cast<FoxBCRegister>(reg)));
    }
}

void FoxBCPrinter::DoJump(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == OpSpecJump_Relative) {
        uint16 offset = Read16();
        printf("# jump relative to (%u)\n", mBytecodeIndex + offset);
        BC_PRINT_OP("jmpr %d", offset);
    }
    else if (op_spec == OpSpecJump_Absolute) {
        uint32 position = Read32();
        BC_PRINT_OP("jmpa %u", position);
    }
    else if (op_spec == OpSpecJump_AbsoluteReg32) {
        uint16 reg = Read16();
        BC_PRINT_OP("jmpar %s", FoxBCEmitter::GetRegisterName(static_cast<FoxBCRegister>(reg)));
    }
    else if (op_spec == OpSpecJump_CallAbsolute) {
        uint32 position = Read32();
        BC_PRINT_OP("calla %u", position);
    }
    else if (op_spec == OpSpecJump_ReturnToCaller) {
        BC_PRINT_OP("ret");
    }
    else if (op_spec == OpSpecJump_CallExternal) {
        uint32 hashed_name = Read32();
        BC_PRINT_OP("callext %u", hashed_name);
    }
}

void FoxBCPrinter::DoData(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == OpSpecData_String) {
        uint16 length = Read16();
        char* data_str = FX_SCRIPT_ALLOC_MEMORY(char, length);
        uint16* data_str16 = reinterpret_cast<uint16*>(data_str);

        uint32 bytecode_end = mBytecodeIndex + length;
        int data_index = 0;
        while (mBytecodeIndex < bytecode_end) {
            uint16 value16 = Read16();
            data_str16[data_index++] = ((value16 << 8) | (value16 >> 8));
        }

        BC_PRINT_OP("datastr %d, %.*s", length, length, data_str);

        FX_SCRIPT_FREE(char, data_str);
    }
    else if (op_spec == OpSpecData_ParamsStart) {
        BC_PRINT_OP("paramsstart");
    }
}

void FoxBCPrinter::DoType(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == OpSpecType_Int) {
        BC_PRINT_OP("typeint");
    }
    else if (op_spec == OpSpecType_String) {
        BC_PRINT_OP("typestr");
    }
}

void FoxBCPrinter::DoMove(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == OpSpecMove_Int32) {
        uint32 value = Read32();
        BC_PRINT_OP("move32 %s, %u\t", FoxBCEmitter::GetRegisterName(static_cast<FoxBCRegister>(op_reg)), value);
    }
}


void FoxBCPrinter::Print()
{
    while (mBytecodeIndex < mBytecode.Size()) {
        PrintOp();
    }
}

void FoxBCPrinter::PrintOp()
{
    uint32 bc_index = mBytecodeIndex;

    uint16 op_full = Read16();

    const uint8 op_base = static_cast<uint8>(op_full >> 8);
    const uint8 op_spec = static_cast<uint8>(op_full & 0xFF);

    char s[128];

    switch (op_base) {
    case OpBase_Push:
        DoPush(s, op_base, op_spec);
        break;
    case OpBase_Pop:
        DoPop(s, op_base, op_spec);
        break;
    case OpBase_Load:
        DoLoad(s, op_base, op_spec);
        break;
    case OpBase_Arith:
        DoArith(s, op_base, op_spec);
        break;
    case OpBase_Jump:
        DoJump(s, op_base, op_spec);
        break;
    case OpBase_Save:
        DoSave(s, op_base, op_spec);
        break;
    case OpBase_Data:
        DoData(s, op_base, op_spec);
        break;
    case OpBase_Type:
        DoType(s, op_base, op_spec);
        break;
    case OpBase_Move:
        DoMove(s, op_base, op_spec);
        break;
    }

    printf("%-25s", s);

    printf("\t# Offset: %u\n", bc_index);
}


///////////////////////////////////////////
// Bytecode VM
///////////////////////////////////////////

void FoxVM::PrintRegisters()
{
    printf("\n=== Register Dump ===\n\n");
    printf("X0=%u\tX1=%u\tX2=%u\tX3=%u\n", Registers[FX_REG_X0], Registers[FX_REG_X1], Registers[FX_REG_X2], Registers[FX_REG_X3]);

    printf("XR=%u\tRA=%u\n", Registers[FX_REG_XR], Registers[FX_REG_RA]);

    printf("\n=====================\n\n");
}

uint16 FoxVM::Read16()
{
    uint8 lo = mBytecode[mPC++];
    uint8 hi = mBytecode[mPC++];

    return ((static_cast<uint16>(lo) << 8) | hi);
}

uint32 FoxVM::Read32()
{
    uint16 lo = Read16();
    uint16 hi = Read16();

    return ((static_cast<uint32>(lo) << 16) | hi);
}

void FoxVM::Push16(uint16 value)
{
    uint16* dptr = reinterpret_cast<uint16*>(Stack + Registers[FX_REG_SP]);
    (*dptr) = value;

    Registers[FX_REG_SP] += sizeof(uint16);
}

void FoxVM::Push32(uint32 value)
{
    uint32* dptr = reinterpret_cast<uint32*>(Stack + Registers[FX_REG_SP]);
    (*dptr) = value;

    Registers[FX_REG_SP] += sizeof(uint32);
}

uint32 FoxVM::Pop32()
{
    if (Registers[FX_REG_SP] == 0) {
        printf("ERR\n");
    }

    Registers[FX_REG_SP] -= sizeof(uint32);

    uint32 value = *reinterpret_cast<uint32*>(Stack + Registers[FX_REG_SP]);
    return value;
}


FoxVMCallFrame& FoxVM::PushCallFrame()
{
    mIsInCallFrame = true;

    FoxVMCallFrame& start_frame = mCallFrames[mCallFrameIndex++];
    start_frame.StartStackIndex = Registers[FX_REG_SP];

    return start_frame;
}

void FoxVM::PopCallFrame()
{
    FoxVMCallFrame* frame = GetCurrentCallFrame();
    if (!frame) {
        FX_BREAKPOINT;
    }

    while (Registers[FX_REG_SP] > frame->StartStackIndex) {
        Pop32();
    }

    --mCallFrameIndex;

    if (mCallFrameIndex == 0) {
        mIsInCallFrame = false;
    }
}

FoxVMCallFrame* FoxVM::GetCurrentCallFrame()
{
    if (!mIsInCallFrame || mCallFrameIndex < 1) {
        return nullptr;
    }

    return &mCallFrames[mCallFrameIndex - 1];
}

FoxExternalFunc* FoxVM::FindExternalFunction(FoxHash hashed_name)
{
    for (FoxExternalFunc& func : mExternalFuncs) {
        if (func.HashedName == hashed_name) {
            return &func;
        }
    }
    return nullptr;
}

void FoxVM::ExecuteOp()
{
    uint16 op_full = Read16();

    const uint8 op_base = static_cast<uint8>(op_full >> 8);
    const uint8 op_spec = static_cast<uint8>(op_full & 0xFF);

    switch (op_base) {
    case OpBase_Push:
        DoPush(op_base, op_spec);
        break;
    case OpBase_Pop:
        DoPop(op_base, op_spec);
        break;
    case OpBase_Load:
        DoLoad(op_base, op_spec);
        break;
    case OpBase_Arith:
        DoArith(op_base, op_spec);
        break;
    case OpBase_Jump:
        DoJump(op_base, op_spec);
        break;
    case OpBase_Save:
        DoSave(op_base, op_spec);
        break;
    case OpBase_Data:
        DoData(op_base, op_spec);
        break;
    case OpBase_Type:
        DoType(op_base, op_spec);
        break;
    case OpBase_Move:
        DoMove(op_base, op_spec);
        break;
    }
}

void FoxVM::DoLoad(uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == OpSpecLoad_Int32) {
        int16 offset = Read16();

        uint8* dataptr = &Stack[Registers[FX_REG_SP] + offset];
        uint32 data32 = *reinterpret_cast<uint32*>(dataptr);

        Registers[op_reg] = data32;
    }
    else if (op_spec == OpSpecLoad_AbsoluteInt32) {
        uint32 offset = Read32();

        uint8* dataptr = &Stack[offset];
        uint32 data32 = *reinterpret_cast<uint32*>(dataptr);

        Registers[op_reg] = data32;
    }
}

void FoxVM::DoPush(uint8 op_base, uint8 op_spec)
{
    if (mIsInParams) {
        if (mCurrentType != FoxValue::NONETYPE) {
            mPushedTypes.Insert(mCurrentType);
            mCurrentType = FoxValue::NONETYPE;
        }
        else {
            mPushedTypes.Insert(FoxValue::INT);
        }
    }

    if (op_spec == OpSpecPush_Int32) {
        uint32 value = Read32();
        Push32(value);
    }
    else if (op_spec == OpSpecPush_Reg32) {
        uint16 reg = Read16();
        Push32(Registers[reg]);
    }
}

void FoxVM::DoPop(uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == OpSpecPush_Int32) {
        uint32 value = Pop32();

        Registers[op_reg] = value;
    }

    if (mIsInParams) {
        mPushedTypes.RemoveLast();
    }
}

void FoxVM::DoArith(uint8 op_base, uint8 op_spec)
{
    uint8 a_reg = mBytecode[mPC++];
    uint8 b_reg = mBytecode[mPC++];

    if (op_spec == OpSpecArith_Add) {
        Registers[FX_REG_XR] = Registers[a_reg] + Registers[b_reg];
    }
}

void FoxVM::DoSave(uint8 op_base, uint8 op_spec)
{
    uint32 offset;

    if (op_spec == OpSpecSave_AbsoluteInt32 || op_spec == OpSpecSave_AbsoluteReg32) {
        offset = Read32();
    }
    else {
        // The offset is relative to the stack pointer, get the absolute value
        const int16 relative_offset = static_cast<int16>(Read16());
        offset = static_cast<uint32>(Registers[FX_REG_SP] + relative_offset);
    }

    uint32* dataptr = reinterpret_cast<uint32*>(&Stack[offset]);

    // Save a imm32 into an offset in the stack
    if (op_spec == OpSpecSave_Int32) {
        (*dataptr) = Read32();
    }

    // Save a register into an offset in the stack
    else if (op_spec == OpSpecSave_Reg32) {
        uint16 reg = Read16();
        (*dataptr) = Registers[reg];
    }

    else if (op_spec == OpSpecSave_AbsoluteInt32) {
        (*dataptr) = Read32();
    }

    else if (op_spec == OpSpecSave_AbsoluteReg32) {
        uint16 reg = Read16();
        (*dataptr) = Registers[reg];
    }
}

void FoxVM::DoJump(uint8 op_base, uint8 op_spec)
{
    if (op_spec == OpSpecJump_Relative) {
        uint16 offset = Read16();
        mPC += offset;
    }
    else if (op_spec == OpSpecJump_Absolute) {
        uint32 position = Read32();
        mPC = position;
    }
    else if (op_spec == OpSpecJump_AbsoluteReg32) {
        uint16 reg = Read16();
        mPC = Registers[reg];
    }
    else if (op_spec == OpSpecJump_CallAbsolute) {
        uint32 call_address = Read32();
        // printf("Call to address % 4u\n", call_address);

        // Push32(mPC);

        Registers[FX_REG_RA] = mPC;

        mPushedTypes.Clear();
        mIsInParams = false;

        PushCallFrame();

        // Jump to the function address
        mPC = call_address;
    }
    else if (op_spec == OpSpecJump_ReturnToCaller) {
        PopCallFrame();

        // uint32 return_address = Pop32();

        uint32 return_address = Registers[FX_REG_RA];
        // printf("Return to caller (%04d)\n", return_address);
        mPC = return_address;

        // Restore the return address register to its previous value. This is pushed when `paramsstart` is encountered.
        // Registers[FX_REG_RA] = Pop32();
    }
    else if (op_spec == OpSpecJump_CallExternal) {
        uint32 hashed_name = Read32();

        FoxExternalFunc* external_func = FindExternalFunction(hashed_name);

        if (!external_func) {
            printf("!!! Could not find external function in VM!\n");
            return;
        }

        std::vector<FoxValue> params;
        params.reserve(external_func->ParameterTypes.size());

        uint32 num_params = mPushedTypes.Size();

        printf("Num Params: %u\n", num_params);

        for (int i = 0; i < num_params; i++) {
            FoxValue::ValueType param_type = mPushedTypes.GetLast();

            FoxValue value;
            value.Type = param_type;

            if (param_type == FoxValue::INT) {
                value.ValueInt = Pop32();
                value.Type = param_type;
            }
            else if (param_type == FoxValue::STRING) {
                uint32 string_location = Pop32();
                uint8* str_base_ptr = &mBytecode[string_location];
                // uint16 str_length = *((uint16*)str_base_ptr);

                str_base_ptr += 2;

                value.ValueString = reinterpret_cast<char*>(str_base_ptr);
                value.Type = param_type;
            }

            mPushedTypes.RemoveLast();

            params.push_back(value);
        }

        mPushedTypes.Clear();
        mIsInParams = false;

        FoxValue return_value {};
        external_func->Function(this, params, &return_value);
    }
}

void FoxVM::DoData(uint8 op_base, uint8 op_spec)
{
    if (op_spec == OpSpecData_String) {
        uint16 length = Read16();
        mPC += length;
    }
    else if (op_spec == OpSpecData_ParamsStart) {
        mIsInParams = true;

        // Push the current return address pointer. This is so nested function calls can correctly navigate back
        // to the caller.
        // Push32(Registers[FX_REG_RA]);
    }
}

void FoxVM::DoType(uint8 op_base, uint8 op_spec)
{
    if (op_spec == OpSpecType_Int) {
        mCurrentType = FoxValue::INT;
    }
    else if (op_spec == OpSpecType_String) {
        mCurrentType = FoxValue::STRING;
    }
}

void FoxVM::DoMove(uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    FoxBCRegister op_reg = static_cast<FoxBCRegister>(op_spec_raw & 0x0F);

    if (op_spec == OpSpecMove_Int32) {
        int32 value = Read32();
        Registers[op_reg] = value;
    }
}


//////////////////////////////////////////////////
// Script Bytecode to x86 Transpiler
//////////////////////////////////////////////////

uint16 FoxTranspilerX86::Read16()
{
    uint8 lo = mBytecode[mBytecodeIndex++];
    uint8 hi = mBytecode[mBytecodeIndex++];

    return ((static_cast<uint16>(lo) << 8) | hi);
}

uint32 FoxTranspilerX86::Read32()
{
    uint16 lo = Read16();
    uint16 hi = Read16();

    return ((static_cast<uint32>(lo) << 16) | hi);
}

static const char* GetX86Register(FoxBCRegister reg)
{
    switch (reg) {
    case FX_REG_X0:
        return "eax";
    case FX_REG_X1:
        return "ebx";
    case FX_REG_X2:
        return "ecx";
    case FX_REG_X3:
        return "edx";
    case FX_REG_SP:
        return "esp";
    case FX_REG_RA:
        return "esi";
    case FX_REG_XR:
        return "eax";
    default:;
    }
    return "UNKNOWN";
}

// #define BC_PRINT_OP(fmt_, ...) snprintf(s, 128, fmt_, ##__VA_ARGS__)

void FoxTranspilerX86::DoLoad(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == OpSpecLoad_Int32) {
        int16 offset = Read16();
        // load32 [off32] [%r32]
        offset += 8;
        StrOut("mov %s, [ebp %c %d]", GetX86Register(static_cast<FoxBCRegister>(op_reg)), (offset <= 0 ? '+' : '-'), abs(offset));
    }
    else if (op_spec == OpSpecLoad_AbsoluteInt32) {
        uint32 offset = Read32();
        // StrOut("load32a %u, %s", offset, GetX86Register(static_cast<FoxBCRegister>(op_reg)));
        StrOut("mov %s, [esi + %u]", GetX86Register(static_cast<FoxBCRegister>(op_reg)), offset);
    }
}

void FoxTranspilerX86::DoPush(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == OpSpecPush_Int32) {
        uint32 value = Read32();
        // push32 [imm32]
        StrOut("push dword %u", value);
    }
    else if (op_spec == OpSpecPush_Reg32) {
        uint16 reg = Read16();
        // push32r [%reg32]
        StrOut("push %s", GetX86Register(static_cast<FoxBCRegister>(reg)));
    }

    if (mIsInFunction) {
        mSizePushedInFunction += 4;
    }
}

void FoxTranspilerX86::DoPop(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == OpSpecPop_Int32) {
        // pop32 [%reg32]
        StrOut("pop %s", GetX86Register(static_cast<FoxBCRegister>(op_reg)));
    }
}

void FoxTranspilerX86::DoArith(char* s, uint8 op_base, uint8 op_spec)
{
    uint8 a_reg = mBytecode[mBytecodeIndex++];
    uint8 b_reg = mBytecode[mBytecodeIndex++];

    if (op_spec == OpSpecArith_Add) {
        // add32 [%reg32] [%reg32]
        StrOut("add %s, %s", GetX86Register(static_cast<FoxBCRegister>(a_reg)), GetX86Register(static_cast<FoxBCRegister>(b_reg)));
    }
}

void FoxTranspilerX86::DoSave(char* s, uint8 op_base, uint8 op_spec)
{
    // Save a imm32 into an offset in the stack
    if (op_spec == OpSpecSave_Int32) {
        const int16 offset = Read16() + 8;
        const int32 value = Read32();

        StrOut("mov dword [ebp %c %d], %d", (offset <= 0 ? '+' : '-'), abs(offset), value);
        // BC_PRINT_OP("save32 %d, %u", offset, value);
    }

    // Save a register into an offset in the stack
    else if (op_spec == OpSpecSave_Reg32) {
        int16 offset = Read16();
        uint16 reg = Read16();

        offset += 8;

        StrOut("mov [ebp %c %d], %s", (offset <= 0 ? '+' : '-'), abs(offset), GetX86Register(static_cast<FoxBCRegister>(reg)));
        // BC_PRINT_OP("save32r %d, %s", offset, GetX86Register(static_cast<FoxBCRegister>(reg)));
    }
    else if (op_spec == OpSpecSave_AbsoluteInt32) {
        const uint32 offset = Read32();
        const int32 value = Read32();

        // StrOut("save32a %u, %u", offset, value);
        StrOut("mov [esi %c %d], %d", (offset <= 0 ? '+' : '-'), offset, value);
    }
    else if (op_spec == OpSpecSave_AbsoluteReg32) {
        const uint32 offset = Read32();
        uint16 reg = Read16();

        // StrOut("save32ar %u, %s", offset, GetX86Register(static_cast<FoxBCRegister>(reg)));
        StrOut("mov [esi %c %d], %s", (offset <= 0 ? '+' : '-'), offset, GetX86Register(static_cast<FoxBCRegister>(reg)));
    }
}

void FoxTranspilerX86::DoJump(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == OpSpecJump_Relative) {
        uint16 offset = Read16();

        mIsInFunction = true;

        StrOut("jmp _A_%u", mBytecodeIndex + offset);

        if (mTextIndent) {
            --mTextIndent;
        }
        StrOut("_L_%u:", mBytecodeIndex);

        ++mTextIndent;


        StrOut("pop esi ; save return address");
        StrOut("mov ebp, esp");

        StrOut("");
    }
    else if (op_spec == OpSpecJump_Absolute) {
        uint32 position = Read32();
        StrOut("jmpa %u", position);
    }
    else if (op_spec == OpSpecJump_AbsoluteReg32) {
        uint16 reg = Read16();
        StrOut("jmpar %s", GetX86Register(static_cast<FoxBCRegister>(reg)));
    }
    else if (op_spec == OpSpecJump_CallAbsolute) {
        uint32 position = Read32();
        // BC_PRINT_OP("calla %u", position);
        StrOut("call _L_%u", position);
    }
    else if (op_spec == OpSpecJump_ReturnToCaller) {
        StrOut("add esp, %u", mSizePushedInFunction);
        mSizePushedInFunction = 0;
        mIsInFunction = false;

        StrOut("");
        StrOut("push esi ; push return address");
        StrOut("ret");

        --mTextIndent;

        StrOut("_A_%u:", mBytecodeIndex);

        ++mTextIndent;
    }
    else if (op_spec == OpSpecJump_CallExternal) {
        uint32 hashed_name = Read32();
        // StrOut("callext %u", hashed_name);

        StrOut("nop ; callext %u", hashed_name);
    }
}

void FoxTranspilerX86::DoData(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == OpSpecData_String) {
        uint16 length = Read16();
        char* data_str = FX_SCRIPT_ALLOC_MEMORY(char, length);
        uint16* data_str16 = reinterpret_cast<uint16*>(data_str);

        uint32 bytecode_end = mBytecodeIndex + length;
        int data_index = 0;
        while (mBytecodeIndex < bytecode_end) {
            uint16 value16 = Read16();
            data_str16[data_index++] = ((value16 << 8) | (value16 >> 8));
        }

        StrOut("datastr %d, %.*s", length, length, data_str);

        FX_SCRIPT_FREE(char, data_str);
    }
    else if (op_spec == OpSpecData_ParamsStart) {
        StrOut("; Parameters start");
        // BC_PRINT_OP("paramsstart");
    }
}

void FoxTranspilerX86::DoType(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == OpSpecType_Int) {
        // BC_PRINT_OP("typeint");
    }
    else if (op_spec == OpSpecType_String) {
        // BC_PRINT_OP("typestr");
    }
}

void FoxTranspilerX86::DoMove(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    FoxBCRegister op_reg = static_cast<FoxBCRegister>(op_spec_raw & 0x0F);

    if (op_spec == OpSpecMove_Int32) {
        int32 value = Read32();
        // StrOut("move32 %s, %u", FoxBCEmitter::GetRegisterName(op_reg), value);
        StrOut("mov %s, %d", GetX86Register(op_reg), value);
    }
}


void FoxTranspilerX86::Print()
{
    // Print header
    StrOut("section .text");
    StrOut("global _start");
    StrOut("_start:");


    while (mBytecodeIndex < mBytecode.Size()) {
        PrintOp();
    }

    // Print footer

    StrOut("mov ebx, eax");
    StrOut("mov eax, 1");
    StrOut("int 0x80");
}

#include <cstdarg>

void FoxTranspilerX86::StrOut(const char* fmt, ...)
{
    for (int i = 0; i < mTextIndent; i++) {
        putchar('\t');
    }

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    puts("");
}

void FoxTranspilerX86::PrintOp()
{
    // uint32 bc_index = mBytecodeIndex;

    uint16 op_full = Read16();

    const uint8 op_base = static_cast<uint8>(op_full >> 8);
    const uint8 op_spec = static_cast<uint8>(op_full & 0xFF);

    char s[128];

    switch (op_base) {
    case OpBase_Push:
        DoPush(s, op_base, op_spec);
        break;
    case OpBase_Pop:
        DoPop(s, op_base, op_spec);
        break;
    case OpBase_Load:
        DoLoad(s, op_base, op_spec);
        break;
    case OpBase_Arith:
        DoArith(s, op_base, op_spec);
        break;
    case OpBase_Jump:
        DoJump(s, op_base, op_spec);
        break;
    case OpBase_Save:
        DoSave(s, op_base, op_spec);
        break;
    case OpBase_Data:
        DoData(s, op_base, op_spec);
        break;
    case OpBase_Type:
        DoType(s, op_base, op_spec);
        break;
    case OpBase_Move:
        DoMove(s, op_base, op_spec);
        break;
    }
    // printf("%-25s\n", s);
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

    printf("\n");

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
    else if (node->NodeType == FX_AST_ACTIONDECL) {
        return EmitFunction(reinterpret_cast<FoxAstFunctionDecl*>(node));
    }
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
        constexpr FoxHash return_val_hash = FoxHashStr(FX_SCRIPT_VAR_RETURN_VAL);

        FoxBytecodeVarHandle* return_var = FindVarHandle(return_val_hash);

        FoxIRRegister result_register = FindFreeReg32();
        MARK_REGISTER_USED(result_register);

        if (return_var) {
            DoLoad(return_var->Offset, result_register);
        }

        MARK_REGISTER_FREE(result_register);

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
    for (int register_index = FX_IR_GW0; register_index <= FX_IR_GW3; register_index++) {
        uint32 gp_r = (1 << register_index);

        if (!(mRegsInUse & gp_r)) {
            return static_cast<FoxIRRegister>(register_index);
        }
    }

    return FX_IR_GW3;
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
    mRegsInUse = static_cast<FoxRegisterFlag>(uint16(mRegsInUse) | register_flag);
}

void FoxIREmitter::MarkRegisterFree(FoxIRRegister reg)
{
    uint16 register_flag = (1 << reg);
    mRegsInUse = static_cast<FoxRegisterFlag>(uint16(mRegsInUse) & (~register_flag));
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

void FoxIREmitter::EmitMoveInt32(FoxIRRegister reg, uint32 value)
{
    WriteOp(IrBase_Move, (IrSpecMove_Int32 << 4) | (reg & 0x0F));
    Write32(value);
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
    WriteOp(IrBase_Data, IrSpecData_String);

    uint32 start_index = mBytecode.Size();

    uint16 final_length = length + 1;

    // If the length is not a factor of 2 (sizeof uint16) then add a byte of padding
    if ((final_length & 0x01)) {
        ++final_length;
    }

    Write16(final_length);

    for (int i = 0; i < final_length; i += 2) {
        mBytecode.Insert(str[i]);

        if (i >= length) {
            mBytecode.Insert(0);
            break;
        }

        mBytecode.Insert(str[i + 1]);
    }

    return start_index;
}


FoxIRRegister FoxIREmitter::EmitBinop(FoxAstBinop* binop, FoxBytecodeVarHandle* handle)
{
    bool will_preserve_lhs = false;
    // Load the A and B values into the registers
    FoxIRRegister a_reg = EmitRhs(binop->Left, RhsMode::RHS_FETCH_TO_REGISTER, handle);

    // Since there is a chance that this register will be clobbered (by binop, function call, etc), we will
    // push the value of the register here and return it after processing the RHS
    if (binop->Right->NodeType != FX_AST_LITERAL) {
        will_preserve_lhs = true;
        EmitPush32r(a_reg);
    }

    FoxIRRegister b_reg = EmitRhs(binop->Right, RhsMode::RHS_FETCH_TO_REGISTER, handle);

    // Retrieve the previous LHS
    if (will_preserve_lhs) {
        EmitPop32(a_reg);
    }

    if (binop->OpToken->Type == TT::Plus) {
        WriteOp(IrBase_Arith, IrSpecArith_Add);

        mBytecode.Insert(a_reg);
        mBytecode.Insert(b_reg);
    }

    // We no longer need the lhs or rhs registers, free em
    // MARK_REGISTER_FREE(a_reg);
    MARK_REGISTER_FREE(b_reg);

    return a_reg;
}

FoxIRRegister FoxIREmitter::EmitVarFetch(FoxAstVarRef* ref, RhsMode mode)
{
    FoxBytecodeVarHandle* var_handle = FindVarHandle(ref->Name->GetHash());

    bool force_absolute_load = false;

    // If the variable is from a previous scope, load it from an absolute address. local offsets
    // will change depending on where they are called.
    if (var_handle->ScopeIndex < mScopeIndex) {
        force_absolute_load = true;
    }

    if (!var_handle) {
        printf("Could not find var handle!");
        return FX_IR_GW0;
    }

    FoxIRRegister reg = FindFreeReg32();

    MARK_REGISTER_USED(reg);

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
        printf("!!! UNKNOWN TYPE\n");
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
        printf("!!! Var '%.*s' does not exist!\n", assign->Var->Name->Length, assign->Var->Name->Start);
        return;
    }

    // bool force_absolute_save = false;

    // if (var_handle->ScopeIndex < mScopeIndex) {
    //     force_absolute_save = true;
    // }

    // int output_offset = -(static_cast<int>(mStackOffset) - static_cast<int>(var_handle->Offset));

    if (!var_handle) {
        printf("Could not find var handle to assign to!");
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
        const bool force_absolute_save = (handle->ScopeIndex < mScopeIndex);
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
                //
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
            result_register = FX_IR_GW3;
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
            EmitPush32r(result_register);

            MARK_REGISTER_FREE(result_register);

            // Find a register to output to, and pop the value to there.
            FoxIRRegister output_reg = FindFreeReg32();
            EmitPop32(output_reg);

            // Mark the output register as used to store it
            MARK_REGISTER_USED(output_reg);

            return output_reg;
        }
        else if (mode == IRRhsMode::RHS_ASSIGN_TO_HANDLE) {
            const bool force_absolute_save = (handle->ScopeIndex < mScopeIndex);

            // Save the value back to the variable
            // DoSaveReg32(handle->Offset, result_register, force_absolute_save);

            EmitVariableSetReg32(handle->VarIndexInScope, result_register);
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

    // FoxBytecodeVarHandle handle {
    //     .HashedName = decl_hash,
    //     .Type = value_type, // Just int for now
    //     .Offset = (mStackOffset),
    //     .SizeOnStack = size_of_type,
    //     .ScopeIndex = mScopeIndex,
    // };

    // VarHandles.Insert(handle);

    // FoxBytecodeVarHandle* inserted_handle = &VarHandles[VarHandles.Size() - 1];

    FoxBytecodeVarHandle* var_handle = FindVarHandle(decl_hash);

    if (var_handle == nullptr) {
        printf("!!! Could not find var handle!\n");
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

    // Push all params to stack
    for (FoxAstNode* param : call->Params) {
        // FoxRegister reg =
        if (param->NodeType == FX_AST_ACTIONCALL) {
            EmitRhs(param, RhsMode::RHS_DEFINE_IN_MEMORY, nullptr);
            call_locations.push_back(mStackOffset - 4);
        }
        // MARK_REGISTER_FREE(reg);
    }

    // EmitPush32r(FX_REG_RA);

    EmitParamsStart();

    int call_location_index = 0;

    // Push all params to stack
    for (FoxAstNode* param : call->Params) {
        if (param->NodeType == FX_AST_ACTIONCALL) {
            FoxIRRegister temp_register = FindFreeReg32();

            DoLoad(call_locations[call_location_index], temp_register);
            call_location_index++;

            EmitPush32r(temp_register);

            continue;
        }

        EmitRhs(param, RhsMode::RHS_DEFINE_IN_MEMORY, nullptr);
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

    EmitJumpCallAbsolute(handle->BytecodeIndex);

    // EmitPop32(FX_REG_RA);
}

FoxBytecodeVarHandle* FoxIREmitter::DefineAndFetchParam(FoxAstNode* param_decl_node)
{
    if (param_decl_node->NodeType != FX_AST_VARDECL) {
        printf("!!! param node type is not vardecl!\n");
        return nullptr;
    }

    // Emit variable without emitting pushes or pops
    FoxBytecodeVarHandle* handle = DoVarDeclare(reinterpret_cast<FoxAstVarDecl*>(param_decl_node), DECLARE_NO_EMIT);

    if (!handle) {
        printf("!!! could not define and fetch param!\n");
        return nullptr;
    }

    assert(handle->SizeOnStack == 4);

    mStackOffset += handle->SizeOnStack;

    return handle;
}

FoxBytecodeVarHandle* FoxIREmitter::DefineReturnVar(FoxAstVarDecl* decl)
{
    RETURN_VALUE_IF_NO_NODE(decl, nullptr);

    return DoVarDeclare(decl);
}

void FoxIREmitter::EmitFunction(FoxAstFunctionDecl* function)
{
    RETURN_IF_NO_NODE(function);

    ++mScopeIndex;

    // Store the bytecode offset before the function is emitted

    const size_t start_of_function = mBytecode.Size();
    printf("Start of function %zu\n", start_of_function);

    // Emit the jump instruction, we will update the jump position after emitting all of the code inside the block
    EmitJumpRelative(0);

    // const uint32 initial_stack_offset = mStackOffset;

    const size_t header_jump_start_index = start_of_function + sizeof(uint16);

    size_t start_var_handle_count = VarHandles.Size();

    // Offset for the pushed return address
    mStackOffset += 4;

    // Emit the body of the function
    {
        for (FoxAstNode* param_decl_node : function->Params->Statements) {
            DefineAndFetchParam(param_decl_node);
        }

        FoxBytecodeVarHandle* return_var = DefineReturnVar(function->ReturnVar);

        EmitBlock(function->Block);

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

            FoxIRRegister result_register = FindFreeReg32();

            MARK_REGISTER_USED(result_register);

            if (return_var != nullptr) {
                DoLoad(return_var->Offset, result_register);
            }
        }
    }

    // Return offset back to pre-call
    mStackOffset -= 4;

    const size_t end_of_function = mBytecode.Size();
    const uint16 distance_to_function = static_cast<uint16>(end_of_function - (start_of_function)-4);

    // Update the jump to the end of the function
    mBytecode[header_jump_start_index] = static_cast<uint8>(distance_to_function >> 8);
    mBytecode[header_jump_start_index + 1] = static_cast<uint8>((distance_to_function & 0xFF));

    FoxBytecodeFunctionHandle function_handle {.HashedName = function->Name->GetHash(), .BytecodeIndex = static_cast<uint32>(start_of_function + 4)};

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

void FoxIREmitter::EmitBlock(FoxAstBlock* block)
{
    RETURN_IF_NO_NODE(block);

    EmitMarker(IrSpecMarker_FrameBegin);


    // For each var declared in the block, write a stack allocation in the frame header
    for (FoxAstNode* node : block->Statements) {
        if (node->NodeType == FX_AST_VARDECL) {
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

#define BC_PRINT_OP(fmt_, ...) snprintf(s, 128, fmt_, ##__VA_ARGS__)

void FoxIRPrinter::DoLoad(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == IrSpecLoad_Int32) {
        int16 offset = Read16();
        BC_PRINT_OP("load32 %d, %s", offset, FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg)));
    }
    else if (op_spec == IrSpecLoad_AbsoluteInt32) {
        uint32 offset = Read32();
        BC_PRINT_OP("load32a %u, %s", offset, FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg)));
    }
}

void FoxIRPrinter::DoPush(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == IrSpecPush_Int32) {
        uint32 value = Read32();
        BC_PRINT_OP("push32 %u", value);
    }
    else if (op_spec == IrSpecPush_Reg32) {
        uint16 reg = Read16();
        BC_PRINT_OP("push32r %s", FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(reg)));
    }
    else if (op_spec == IrSpecPush_StackAlloc) {
        uint16 size = Read16();
        BC_PRINT_OP("salloc %d", size);
    }
}

void FoxIRPrinter::DoPop(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == IrSpecPop_Int32) {
        BC_PRINT_OP("pop32 %s", FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg)));
    }
}

void FoxIRPrinter::DoArith(char* s, uint8 op_base, uint8 op_spec)
{
    uint8 a_reg = mBytecode[mBytecodeIndex++];
    uint8 b_reg = mBytecode[mBytecodeIndex++];

    if (op_spec == IrSpecArith_Add) {
        BC_PRINT_OP("add32 %s, %s", FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(a_reg)),
                    FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(b_reg)));
    }
}

void FoxIRPrinter::DoSave(char* s, uint8 op_base, uint8 op_spec)
{
    // Save a imm32 into an offset in the stack
    if (op_spec == IrSpecSave_Int32) {
        const int16 offset = Read16();
        const uint32 value = Read32();

        BC_PRINT_OP("save32 %d, %u", offset, value);
    }

    // Save a register into an offset in the stack
    else if (op_spec == IrSpecSave_Reg32) {
        const int16 offset = Read16();
        uint16 reg = Read16();

        BC_PRINT_OP("save32r %d, %s", offset, FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(reg)));
    }
    else if (op_spec == IrSpecSave_AbsoluteInt32) {
        const uint32 offset = Read32();
        const uint32 value = Read32();

        BC_PRINT_OP("save32a %u, %u", offset, value);
    }
    else if (op_spec == IrSpecSave_AbsoluteReg32) {
        const uint32 offset = Read32();
        uint16 reg = Read16();

        BC_PRINT_OP("save32ar %u, %s", offset, FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(reg)));
    }
}

void FoxIRPrinter::DoJump(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == IrSpecJump_Relative) {
        uint16 offset = Read16();
        printf("# jump relative to (%u)\n", mBytecodeIndex + offset);
        BC_PRINT_OP("jmpr %d", offset);
    }
    else if (op_spec == IrSpecJump_Absolute) {
        uint32 position = Read32();
        BC_PRINT_OP("jmpa %u", position);
    }
    else if (op_spec == IrSpecJump_AbsoluteReg32) {
        uint16 reg = Read16();
        BC_PRINT_OP("jmpar %s", FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(reg)));
    }
    else if (op_spec == IrSpecJump_CallAbsolute) {
        uint32 position = Read32();
        BC_PRINT_OP("calla %u", position);
    }
    else if (op_spec == IrSpecJump_ReturnToCaller) {
        BC_PRINT_OP("ret");
    }
    else if (op_spec == IrSpecJump_CallExternal) {
        uint32 hashed_name = Read32();
        BC_PRINT_OP("callext %u", hashed_name);
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

        BC_PRINT_OP("datastr %d, %.*s", length, length, data_str);

        FX_SCRIPT_FREE(char, data_str);
    }
}

void FoxIRPrinter::DoType(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == IrSpecType_Int) {
        BC_PRINT_OP("typeint");
    }
    else if (op_spec == IrSpecType_String) {
        BC_PRINT_OP("typestr");
    }
}

void FoxIRPrinter::DoMove(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == IrSpecMove_Int32) {
        uint32 value = Read32();
        BC_PRINT_OP("move32 %s, %u\t", FoxIREmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg)), value);
    }
}

void FoxIRPrinter::DoMarker(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == IrSpecMarker_FrameBegin) {
        BC_PRINT_OP("frame begin");
    }
    else if (op_spec == IrSpecMarker_FrameEnd) {
        BC_PRINT_OP("frame end");
    }
    else if (op_spec == IrSpecMarker_ParamsBegin) {
        BC_PRINT_OP("params begin");
    }
}


void FoxIRPrinter::DoVariable(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == IrSpecVariable_Get_Int32) {
        uint16 var_index = Read16();
        FoxIRRegister dest_reg = static_cast<FoxIRRegister>(Read16());
        BC_PRINT_OP("vget $%d, %s", var_index, FoxIREmitter::GetRegisterName(dest_reg));
    }
    else if (op_spec == IrSpecVariable_Set_Int32) {
        uint16 var_index = Read16();
        uint32 value = Read32();
        BC_PRINT_OP("vset $%d, %d", var_index, value);
    }
    else if (op_spec == IrSpecVariable_Set_Reg32) {
        uint16 var_index = Read16();
        FoxIRRegister reg = static_cast<FoxIRRegister>(Read16());
        BC_PRINT_OP("vset $%d, %s", var_index, FoxIREmitter::GetRegisterName(reg));
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
    uint32 bc_index = mBytecodeIndex;

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

    printf("%-25s", s);

    printf("\t# Offset: %u\n", bc_index);
}
