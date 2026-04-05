#include "ast.h"

#include "charset.h"
#include "util.h"

#include <stdlib.h>

AST *ast_new(ASTType t, AST *l, AST *r) {
    AST *n = (AST *)xcalloc(1, sizeof(AST));
    n->type = t;
    n->left = l;
    n->right = r;
    return n;
}

AST *ast_empty(void) {
    return ast_new(AST_EMPTY, NULL, NULL);
}

AST *ast_charset(const CharSet *s) {
    AST *n = ast_new(AST_CHARSET, NULL, NULL);
    n->set = *s;
    return n;
}

AST *ast_clone(const AST *n) {
    AST *c;
    if (!n) {
        return NULL;
    }
    c = ast_new(n->type, NULL, NULL);
    c->set = n->set;
    c->left = ast_clone(n->left);
    c->right = ast_clone(n->right);
    return c;
}

void ast_free(AST *n) {
    if (!n) {
        return;
    }
    ast_free(n->left);
    ast_free(n->right);
    free(n);
}

int ast_eval_charset(const AST *n, CharSet *out) {
    CharSet a, b;
    if (!n) {
        return 0;
    }
    switch (n->type) {
    case AST_CHARSET:
        *out = n->set;
        return 1;
    case AST_ALT:
        if (!ast_eval_charset(n->left, &a) || !ast_eval_charset(n->right, &b)) {
            return 0;
        }
        charset_union(out, &a, &b);
        return 1;
    case AST_DIFF:
        if (!ast_eval_charset(n->left, &a) || !ast_eval_charset(n->right, &b)) {
            return 0;
        }
        charset_diff(out, &a, &b);
        return 1;
    default:
        return 0;
    }
}

AST *make_diff_ast(AST *left, AST *right) {
    CharSet a, b, d;
    if (ast_eval_charset(left, &a) && ast_eval_charset(right, &b)) {
        AST *n;
        charset_diff(&d, &a, &b);
        ast_free(left);
        ast_free(right);
        n = ast_charset(&d);
        return n;
    }
    return ast_new(AST_DIFF, left, right);
}
