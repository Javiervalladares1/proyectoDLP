#ifndef YALEX_TYPES_H
#define YALEX_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned char bits[256];
} CharSet;

typedef enum {
    AST_EMPTY,
    AST_CHARSET,
    AST_CONCAT,
    AST_ALT,
    AST_STAR,
    AST_PLUS,
    AST_QMARK,
    AST_DIFF,
    AST_EOF
} ASTType;

typedef struct AST AST;
struct AST {
    ASTType type;
    AST *left;
    AST *right;
    CharSet set;
};

typedef struct {
    char *name;
    char *regex;
    AST *ast;
    int resolving;
} LetDef;

typedef struct {
    char *regex;
    char *action;
    char *token_name;
    int skip;
    int is_eof;
    AST *ast;
} RuleDef;

typedef struct {
    LetDef *data;
    int count;
    int cap;
} LetVec;

typedef struct {
    RuleDef *data;
    int count;
    int cap;
} RuleVec;

typedef struct {
    LetVec lets;
    RuleVec rules;
    char *header;
    char *trailer;
    char *entrypoint;
    char *entry_args;
} YalSpec;

typedef struct {
    int to;
    int set_id; /* -1 => epsilon */
} NFAEdge;

typedef struct {
    NFAEdge *edges;
    int edge_count;
    int edge_cap;
    int accept_token; /* -1 => non-accepting */
} NFAState;

typedef struct {
    NFAState *states;
    int state_count;
    int state_cap;
    CharSet *sets;
    int set_count;
    int set_cap;
} NFA;

typedef struct {
    int start;
    int end;
} Frag;

typedef struct {
    uint64_t **subsets; /* one bitset per DFA state */
    int **trans;        /* [state][byte] */
    int *accept;        /* token index per DFA state */
    int count;
    int cap;
    int words;
} DFA;

typedef struct {
    const char *s;
    size_t pos;
    LetVec *lets;
} RegexParser;

#endif
