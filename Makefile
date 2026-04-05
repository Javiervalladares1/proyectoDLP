CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic

GEN := yalexgen
GEN_SRCS := \
	src/main.c \
	src/util.c \
	src/charset.c \
	src/ast.c \
	src/yal_spec.c \
	src/regex_parse.c \
	src/nfa.c \
	src/dfa.c \
	src/emit.c

.PHONY: all clean example example-ok example-error example-features verify

all: $(GEN)

$(GEN): $(GEN_SRCS)
	$(CC) $(CFLAGS) -o $@ $(GEN_SRCS)

clean:
	rm -f $(GEN) lexer_generated lexer_generated.c regex_tree.dot
	rm -f regex_tree.png
	rm -f examples/lexer_generated examples/lexer_generated.c examples/regex_tree.dot examples/regex_tree.png
	rm -f examples/lexer_features examples/lexer_features.c examples/features_tree.dot examples/features_tree.png

example: $(GEN)
	./$(GEN) examples/calculator.yal -o examples/lexer_generated.c --dot examples/regex_tree.dot
	$(CC) $(CFLAGS) -o examples/lexer_generated examples/lexer_generated.c
	./examples/lexer_generated examples/input_ok.txt

example-ok: example

example-error: example
	./examples/lexer_generated examples/input_error.txt

example-features: $(GEN)
	./$(GEN) examples/yalex_features.yal -o examples/lexer_features.c --dot examples/features_tree.dot
	$(CC) $(CFLAGS) -o examples/lexer_features examples/lexer_features.c
	./examples/lexer_features examples/features_input.txt

verify: all example example-error example-features
