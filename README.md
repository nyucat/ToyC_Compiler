# ToyC Compiler

This is a C++20 ToyC compiler project scaffold.

Current implementation status:

- Frontend lexer for ToyC tokens.
- Recursive descent parser for declarations, functions, statements, and expressions.
- AST node hierarchy shared with later semantic analysis and IR stages.
- Command line entry that reads ToyC source from `stdin` and accepts `-opt`.

Build:

```sh
cmake -S . -B build
cmake --build build
```

Run:

```sh
./build/compiler < input.tc > output.s
./build/compiler -opt < input.tc > output.s
```

The backend is not implemented yet, so the current executable only validates that
the frontend can parse the input and emits a placeholder assembly comment.
