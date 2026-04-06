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

.PHONY: all clean example example-ok example-error example-features verify \
	test-catedra clean-catedra

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

# Pruebas del catedrático: entradas en first_test/ y Second_test/ (originales del catedrático).
# Especificaciones .yal que compilan: adapted/ (ver adapted/README.md).
test-catedra: $(GEN)
	@echo "========== FIRST TEST: slr-1 + input_grammar1.txt =========="
	./$(GEN) adapted/first_test/slr-1.yal -o adapted/first_test/lexer_slr1.c --no-png --dot adapted/first_test/slr-1_tree.dot
	$(CC) $(CFLAGS) -o adapted/first_test/lexer_slr1 adapted/first_test/lexer_slr1.c
	./adapted/first_test/lexer_slr1 first_test/input_grammar1.txt
	@echo ""
	@echo "========== FIRST TEST: slr-2 + input_grammar2.txt =========="
	./$(GEN) adapted/first_test/slr-2.yal -o adapted/first_test/lexer_slr2.c --no-png --dot adapted/first_test/slr-2_tree.dot
	$(CC) $(CFLAGS) -o adapted/first_test/lexer_slr2 adapted/first_test/lexer_slr2.c
	./adapted/first_test/lexer_slr2 first_test/input_grammar2.txt
	@echo ""
	@echo "========== FIRST TEST: slr-3 + input_grammar3.txt =========="
	./$(GEN) adapted/first_test/slr-3.yal -o adapted/first_test/lexer_slr3.c --no-png --dot adapted/first_test/slr-3_tree.dot
	$(CC) $(CFLAGS) -o adapted/first_test/lexer_slr3 adapted/first_test/lexer_slr3.c
	./adapted/first_test/lexer_slr3 first_test/input_grammar3.txt
	@echo ""
	@echo "========== FIRST TEST: slr-4 + input_grammar4.txt =========="
	./$(GEN) adapted/first_test/slr-4.yal -o adapted/first_test/lexer_slr4.c --no-png --dot adapted/first_test/slr-4_tree.dot
	$(CC) $(CFLAGS) -o adapted/first_test/lexer_slr4 adapted/first_test/lexer_slr4.c
	./adapted/first_test/lexer_slr4 first_test/input_grammar4.txt
	@echo ""
	@echo "========== SECOND TEST: mediumYalex + test1.py =========="
	./$(GEN) adapted/Second_test/mediumYalex.yal -o adapted/Second_test/lexer_medium.c --no-png --dot adapted/Second_test/medium_tree.dot
	$(CC) $(CFLAGS) -o adapted/Second_test/lexer_medium adapted/Second_test/lexer_medium.c
	./adapted/Second_test/lexer_medium Second_test/test1.py
	@echo ""
	@echo "========== SECOND TEST: hardYalex + hardtest.py =========="
	./$(GEN) adapted/Second_test/hardYalex.yal -o adapted/Second_test/lexer_hard.c --no-png --dot adapted/Second_test/hard_tree.dot
	$(CC) $(CFLAGS) -o adapted/Second_test/lexer_hard adapted/Second_test/lexer_hard.c
	./adapted/Second_test/lexer_hard Second_test/hardtest.py
	@echo ""
	@echo "========== Pruebas catedrático: OK (6 lexers ejecutados) =========="

clean-catedra:
	rm -f adapted/first_test/lexer_slr1 adapted/first_test/lexer_slr1.c adapted/first_test/slr-1_tree.dot
	rm -f adapted/first_test/lexer_slr2 adapted/first_test/lexer_slr2.c adapted/first_test/slr-2_tree.dot
	rm -f adapted/first_test/lexer_slr3 adapted/first_test/lexer_slr3.c adapted/first_test/slr-3_tree.dot
	rm -f adapted/first_test/lexer_slr4 adapted/first_test/lexer_slr4.c adapted/first_test/slr-4_tree.dot
	rm -f adapted/Second_test/lexer_medium adapted/Second_test/lexer_medium.c adapted/Second_test/medium_tree.dot
	rm -f adapted/Second_test/lexer_hard adapted/Second_test/lexer_hard.c adapted/Second_test/hard_tree.dot
