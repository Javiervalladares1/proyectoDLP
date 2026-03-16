# Generador de Analizadores Léxicos (YALex) en C

Proyecto para **Diseño de Lenguajes** que implementa un generador estilo YALex:

1. Lee una especificación `.yal`.
2. Construye representación intermedia de regex.
3. Genera autómatas (NFA → DFA).
4. Emite:
   - `lexer_generated.c` (analizador léxico generado).
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
  - Programa fuente del lexer: `OK` (`lexer_generated.c`)

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

Archivo principal: `src/yalexgen.c`

Flujo:
1. `read_file` + `strip_comments`
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

## 3. Compilación

```bash
make
```

Genera ejecutable:
- `yalexgen`

## 4. Uso del generador

```bash
./yalexgen archivo.yal -o lexer_generated.c --dot regex_tree.dot
```

También se incluye wrapper compatible con la llamada esperada del documento:

```bash
./yalex archivo.yal -o lexer_generated.c
```

Opciones:
- `-o <ruta>`: salida del lexer C (default: `lexer_generated.c`)
- `--dot <ruta>`: salida DOT del árbol regex (default: `regex_tree.dot`)
- `--no-png`: desactiva generación automática de PNG

Si `dot` (Graphviz) está disponible, también se genera `regex_tree.png`.

## 5. Compilar y ejecutar lexer generado

```bash
cc -std=c11 -O2 -Wall -Wextra -pedantic -o lexer_generated lexer_generated.c
./lexer_generated entrada.txt
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

## 7. Notas técnicas

- El proyecto prioriza diseño académico defendible:
  - separación clara de fases,
  - autómatas finitos,
  - evidencias de generación y ejecución.
- La diferencia `#` está implementada sobre clases de caracteres (caso práctico más común en especificaciones YALex de laboratorio).
