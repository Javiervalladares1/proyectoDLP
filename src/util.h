#ifndef YALEX_UTIL_H
#define YALEX_UTIL_H

#include <stddef.h>

void fatal(const char *fmt, ...);
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *read_file(const char *path, size_t *out_len);
char *trim_copy(const char *s, size_t n);
void append_text_block(char **dst, const char *text);

#endif
