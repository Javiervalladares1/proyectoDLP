#ifndef YALEX_AST_H
#define YALEX_AST_H

#include "yalex_types.h"

AST *ast_new(ASTType t, AST *l, AST *r);
AST *ast_empty(void);
AST *ast_charset(const CharSet *s);
AST *ast_clone(const AST *n);
void ast_free(AST *n);
int ast_eval_charset(const AST *n, CharSet *out);
AST *make_diff_ast(AST *left, AST *right);

#endif
