# Generador de Analizadores Léxicos (YALex) en Python

Proyecto para **Diseño de Lenguajes** que implementa un generador estilo YALex:

1. Lee una especificación `.yal`.
2. Construye representación intermedia de regex.
3. Genera autómatas (NFA → DFA).
4. Emite:
   - `lexer_generated.py` (analizador léxico generado).
   - `regex_tree.dot` y `regex_tree.png` (árbol de expresiones regular).

El lexer generado procesa un archivo de texto y muestra:
- Tokens reconocidos.
- Errores léxicos con línea y columna.

## 1. Requisitos cubiertos (según PDFs)

### A) Generador
- Entrada en YALex (`.yal`): `OK`
- Estructura general:
  - `header` opcional: `OK` (se parsea e inserta en el archivo generado)
  - `trailer` opcional: `OK` (se parsea e inserta al final del archivo generado)
  - `let ident = regexp`: `OK`
  - `rule entrypoint [args] =`: `OK` (entrypoint y args parseados)
  - `regexp { action }`: `OK` (se parsea; se infiere token y reglas de skip)
  - comentarios `(* ... *)`: `OK`
- Operadores regex:
  - literal `'c'`: `OK`
  - wildcard `_`: `OK`
  - string `"..."`: `OK`
  - set `[set]`: `OK`
  - complemento `[^set]`: `OK`
  - diferencia `#`: `OK` para clases de caracteres
  - `*`, `+`, `?`, concatenación, `|`: `OK`
- Precedencia: `OK` (`#` > unarios > concat > `|`)
- Selección de token:
  - lexema más largo: `OK`
  - empate por orden de definición: `OK`
- Salidas requeridas:
  - Árbol de expresión graficado: `OK` (`.dot` y `.png`)
  - Programa fuente del lexer: `OK` (`lexer_generated.py`)

### B) Analizador generado
- Entrada de texto plano: `OK`
- Imprime tokens: `OK`
- Imprime errores léxicos: `OK`
- Ejecutable y funcional con ejemplos: `OK`

### C) Evidencias/entrega técnica
- Código fuente completo: `OK`
- Ejemplos de ejecución reproducibles: `OK` (`make verify`)
- Documentación técnica y de diseño: `OK` (`README.md`, `docs/REQUIREMENTS_CHECKLIST.md`, `docs/VALIDATION.md`)

## 2. Arquitectura resumida

Fuentes del generador (orden del pipeline léxico), en `src_py/`:

| Paso | Archivos | Rol |
|------|----------|-----|
| Tipos | `yalex_types.py` | `YalSpec`, `AST`, `NFA`, `DFA`, etc. |
| Utilidades | `util.py` | E/S, `fatal`, `trim_copy` |
| Conjuntos | `charset.py` | Operaciones sobre `CharSet` |
| AST | `ast.py` | Nodos regex y `ast_eval_charset` |
| Especificación `.yal` | `yal_spec.py` | `yal_strip_comments`, `parse_spec`, `find_let` |
| Regex | `regex_parse.py` | Parser de expresiones y `let` |
| NFA | `nfa.py` | Construcción del autómata finito no determinista |
| DFA | `dfa.py` | Subconjuntos y tabla de transiciones |
| Emisión | `emit.py` | DOT, PNG opcional, `lexer_generated.py` |
| CLI | `main.py` | `run`, `usage` vía `argparse` |

Flujo:
1. `read_file` + `yal_strip_comments`
2. `parse_spec` (`header`, `let`, `rule`, `trailer`)
3. `parse_regex_with_lets` + expansión de `let`
4. `build_nfa_from_ast`
5. `build_dfa`
6. `emit_dot_file` (+ generación PNG con Graphviz)
7. `emit_generated_lexer`

Estructuras clave:
- `YalSpec`, `LetDef`, `RuleDef`
- `AST` (tipos regex)
- `NFA`, `DFA`

## 3. Ejecución

No requiere compilación en C. Solo Python 3:

```bash
python yalexgen archivo.yal -o lexer_generated.py
```

## 4. Uso del generador

```bash
python yalexgen archivo.yal -o lexer_generated.py --dot regex_tree.dot
```

También se incluye wrapper compatible con la llamada esperada del documento:

```bash
python yalex archivo.yal -o lexer_generated.py
```

Opciones:
- `-o <ruta>`: salida del lexer Python (default: `lexer_generated.py`)
- `--dot <ruta>`: salida DOT del árbol regex (default: `regex_tree.dot`)
- `--no-png`: desactiva generación automática de PNG

Si `dot` (Graphviz) está disponible, también se genera `regex_tree.png`.

## 5. Ejecutar lexer generado

```bash
python lexer_generated.py entrada.txt
```

## 6. Ejemplos incluidos

### Ejemplo básico
- `examples/calculator.yal`
- `examples/input_ok.txt`
- `examples/input_error.txt`

Comandos:
```bash
make example
make example-error
```

### Ejemplo extendido (más operadores y estructura)
- `examples/yalex_features.yal`
- `examples/features_input.txt`

Comando:
```bash
make example-features
```

### Validación completa
```bash
make verify
```

### Pruebas (cátedra)
- **`first_test/`** y **`Second_test/`** concentran especificaciones `.yal`, las entradas de prueba (`input_grammar*.txt`, `test1.py`, `hardtest.py`) y, al correr las pruebas, los lexers generados (`lexer_*.py`) y `.dot` asociados.
- Si el enunciado pide usar el archivo YALex **literal** del PDF del curso, puede haber diferencias de dialecto respecto al parser de este proyecto (comillas, `\s`, nombres de `let`, UTF-8 en clases de caracteres, etc.). Las `.yal` en estas carpetas están escritas para lo que **entendemos** este `yalexgen` y pueden no coincidir byte a byte con el original.

Al ejecutar:

```bash
make test-catedra
```

se generan los lexers en las mismas carpetas y se imprimen **los tokens** por sección (`==========`).

Artefactos generados (`.py`, `.dot`) de esas pruebas: `make clean-catedra`.

