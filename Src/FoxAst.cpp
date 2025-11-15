#include "FoxAst.hpp"

#include "FoxVar.hpp"

void FoxAstPrinter::Print(FoxAstNode* node, int depth)
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
