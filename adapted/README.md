# Especificaciones adaptadas al generador `yalexgen`

Los archivos en **`first_test/`** y **`Second_test/`** (raíz del repo) son los que entregó el catedrático y **no deben modificarse** para la entrega.

Esta carpeta **`adapted/`** contiene versiones **semánticamente equivalentes** (mismas entradas de prueba, mismos tokens esperados en la práctica) escritas en la sintaxis que entiende este proyecto, para poder:

- generar el lexer con `./yalexgen`,
- compilar el `.c` generado,
- ejecutar las pruebas con `make test-catedra`.

Si el enunciado del curso pide demostrar el comportamiento con el archivo **literal** del profesor, la limitación es técnica: ese dialecto YALex no coincide byte a byte con el parser de `yalexgen` (comillas, `\s`, `let or = or`, UTF-8 en clases de caracteres, etc.). En el informe se puede citar esta carpeta como “puente de compatibilidad” sin alterar los entregables originales.
