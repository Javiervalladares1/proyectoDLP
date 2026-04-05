#include "charset.h"

#include <string.h>

void charset_clear(CharSet *s) {
    memset(s->bits, 0, sizeof(s->bits));
}

void charset_fill(CharSet *s) {
    memset(s->bits, 1, sizeof(s->bits));
}

void charset_add(CharSet *s, unsigned char c) {
    s->bits[c] = 1;
}

void charset_add_range(CharSet *s, unsigned char a, unsigned char b) {
    unsigned int i;
    if (a > b) {
        unsigned char t = a;
        a = b;
        b = t;
    }
    for (i = a; i <= b; i++) {
        s->bits[i] = 1;
    }
}

void charset_union(CharSet *out, const CharSet *a, const CharSet *b) {
    int i;
    for (i = 0; i < 256; i++) {
        out->bits[i] = (unsigned char)(a->bits[i] || b->bits[i]);
    }
}

void charset_diff(CharSet *out, const CharSet *a, const CharSet *b) {
    int i;
    for (i = 0; i < 256; i++) {
        out->bits[i] = (unsigned char)(a->bits[i] && !b->bits[i]);
    }
}

void charset_not(CharSet *s) {
    int i;
    for (i = 0; i < 256; i++) {
        s->bits[i] = (unsigned char)!s->bits[i];
    }
}

int charset_count(const CharSet *s) {
    int i;
    int n = 0;
    for (i = 0; i < 256; i++) {
        if (s->bits[i]) {
            n++;
        }
    }
    return n;
}
