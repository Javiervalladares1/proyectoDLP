#ifndef YALEX_REGEX_PARSE_H
#define YALEX_REGEX_PARSE_H

#include "yalex_types.h"

AST *parse_regex_with_lets(const char *text, LetVec *lets);

#endif
