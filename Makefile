PYTHON ?= python
GEN := $(PYTHON) yalexgen

.PHONY: all clean example example-ok example-error example-features verify \
	test-catedra clean-catedra

all: $(GEN)

$(GEN):
	@echo "Using Python generator"

clean:
	rm -f lexer_generated.py regex_tree.dot regex_tree.png
	rm -f regex_tree.png
	rm -f examples/lexer_generated.py examples/regex_tree.dot examples/regex_tree.png
	rm -f examples/lexer_features.py examples/features_tree.dot examples/features_tree.png

example: $(GEN)
	$(GEN) examples/calculator.yal -o examples/lexer_generated.py --dot examples/regex_tree.dot
	$(PYTHON) examples/lexer_generated.py examples/input_ok.txt

example-ok: example

example-error: example
	$(PYTHON) examples/lexer_generated.py examples/input_error.txt

example-features: $(GEN)
	$(GEN) examples/yalex_features.yal -o examples/lexer_features.py --dot examples/features_tree.dot
	$(PYTHON) examples/lexer_features.py examples/features_input.txt

verify: all example example-error example-features

# Pruebas del catedrático: especificaciones, entradas y lexers generados en first_test/ y Second_test/.
test-catedra: $(GEN)
	@echo "========== FIRST TEST: slr-1 + input_grammar1.txt =========="
	$(GEN) first_test/slr-1.yal -o first_test/lexer_slr1.py --no-png --dot first_test/slr-1_tree.dot
	$(PYTHON) first_test/lexer_slr1.py first_test/input_grammar1.txt
	@echo ""
	@echo "========== FIRST TEST: slr-2 + input_grammar2.txt =========="
	$(GEN) first_test/slr-2.yal -o first_test/lexer_slr2.py --no-png --dot first_test/slr-2_tree.dot
	$(PYTHON) first_test/lexer_slr2.py first_test/input_grammar2.txt
	@echo ""
	@echo "========== FIRST TEST: slr-3 + input_grammar3.txt =========="
	$(GEN) first_test/slr-3.yal -o first_test/lexer_slr3.py --no-png --dot first_test/slr-3_tree.dot
	$(PYTHON) first_test/lexer_slr3.py first_test/input_grammar3.txt
	@echo ""
	@echo "========== FIRST TEST: slr-4 + input_grammar4.txt =========="
	$(GEN) first_test/slr-4.yal -o first_test/lexer_slr4.py --no-png --dot first_test/slr-4_tree.dot
	$(PYTHON) first_test/lexer_slr4.py first_test/input_grammar4.txt
	@echo ""
	@echo "========== SECOND TEST: mediumYalex + test1.py =========="
	$(GEN) Second_test/mediumYalex.yal -o Second_test/lexer_medium.py --no-png --dot Second_test/medium_tree.dot
	$(PYTHON) Second_test/lexer_medium.py Second_test/test1.py
	@echo ""
	@echo "========== SECOND TEST: hardYalex + hardtest.py =========="
	$(GEN) Second_test/hardYalex.yal -o Second_test/lexer_hard.py --no-png --dot Second_test/hard_tree.dot
	$(PYTHON) Second_test/lexer_hard.py Second_test/hardtest.py
	@echo ""
	@echo "========== Pruebas catedrático: OK (6 lexers ejecutados) =========="

clean-catedra:
	rm -f first_test/lexer_slr1.py first_test/slr-1_tree.dot
	rm -f first_test/lexer_slr2.py first_test/slr-2_tree.dot
	rm -f first_test/lexer_slr3.py first_test/slr-3_tree.dot
	rm -f first_test/lexer_slr4.py first_test/slr-4_tree.dot
	rm -f Second_test/lexer_medium.py Second_test/medium_tree.dot
	rm -f Second_test/lexer_hard.py Second_test/hard_tree.dot
