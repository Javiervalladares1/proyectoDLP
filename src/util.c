#include "util.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fatal(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) {
        fatal("out of memory");
    }
    return p;
}

void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n, sz);
    if (!p) {
        fatal("out of memory");
    }
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *r = realloc(p, n);
    if (!r) {
        fatal("out of memory");
    }
    return r;
}

char *xstrdup(const char *s) {
    size_t n = strlen(s);
    char *r = (char *)xmalloc(n + 1);
    memcpy(r, s, n + 1);
    return r;
}

char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    size_t n;
    char *buf;
    if (!f) {
        fatal("cannot open input file: %s", path);
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        fatal("cannot seek input file");
    }
    n = (size_t)ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        fatal("cannot seek input file");
    }
    buf = (char *)xmalloc(n + 1);
    if (n > 0 && fread(buf, 1, n, f) != n) {
        fclose(f);
        free(buf);
        fatal("cannot read input file");
    }
    fclose(f);
    buf[n] = '\0';
    if (out_len) {
        *out_len = n;
    }
    return buf;
}

char *trim_copy(const char *s, size_t n) {
    size_t i = 0;
    size_t j = n;
    char *r;
    while (i < n && isspace((unsigned char)s[i])) {
        i++;
    }
    while (j > i && isspace((unsigned char)s[j - 1])) {
        j--;
    }
    r = (char *)xmalloc(j - i + 1);
    memcpy(r, s + i, j - i);
    r[j - i] = '\0';
    return r;
}

void append_text_block(char **dst, const char *text) {
    size_t a, b;
    char *r;
    if (!text || !*text) {
        return;
    }
    if (!*dst) {
        *dst = xstrdup(text);
        return;
    }
    a = strlen(*dst);
    b = strlen(text);
    r = (char *)xmalloc(a + b + 2);
    memcpy(r, *dst, a);
    if (a > 0 && r[a - 1] != '\n') {
        r[a++] = '\n';
    }
    memcpy(r + a, text, b);
    r[a + b] = '\0';
    free(*dst);
    *dst = r;
}
