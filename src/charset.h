#ifndef YALEX_CHARSET_H
#define YALEX_CHARSET_H

#include "yalex_types.h"

#include <stddef.h>

void charset_clear(CharSet *s);
void charset_fill(CharSet *s);
void charset_add(CharSet *s, unsigned char c);
void charset_add_range(CharSet *s, unsigned char a, unsigned char b);
void charset_union(CharSet *out, const CharSet *a, const CharSet *b);
void charset_diff(CharSet *out, const CharSet *a, const CharSet *b);
void charset_not(CharSet *s);
int charset_count(const CharSet *s);

#endif
