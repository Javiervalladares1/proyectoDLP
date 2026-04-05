#ifndef YALEX_YAL_SPEC_H
#define YALEX_YAL_SPEC_H

#include "yalex_types.h"

LetDef *find_let(LetVec *lets, const char *name);
char *yal_strip_comments(const char *src);
void parse_spec(const char *src, YalSpec *spec);
void free_spec(YalSpec *spec);

#endif
