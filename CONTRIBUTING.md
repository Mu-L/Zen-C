# Contributing to Zen C

Thanks for contributing! This file covers the repository's expectations,
especially for changes to the compiler itself (`src/`).

## Compiler conventions

The compiler is intentionally written to be **coherent**: one rule per
concern, applied everywhere. When changing compiler code, follow these.

### Error handling

| Class | Macro | Behavior |
| :-- | :-- | :-- |
| Fatal / syntax | `zpanic_at(token, "…")` | Aborts (or delegates to the LSP handler in fault-tolerant mode). |
| Warning | `zwarn_at(token, "…")` | Non-fatal diagnostic. |
| Semantic (typechecker) | `tc_error(tc, token, "…")` | Recoverable type error. |
| Semantic (parser, recoverable) | `zerror_at(token, "…")` | Recoverable non-fatal error. |

- `"Expected …"` syntax errors use `zpanic_at`, consistently.
- Every error path that returns `NULL`/an error node must already have emitted
  a diagnostic.

### Symbol mangling

- The one canonical mangler is `mangle_method_symbol(base, trait, method)` in
  `src/parser/struct/struct_shared.c`. Use it everywhere; never re-implement
  `%s__%s` + `merge_underscores` inline.
- A method beginning with `_` keeps its triple underscore (`Struct___method`)
  so it cannot collide with `Struct__method`.
- `merge_underscores` collapses runs of **four or more** underscores; a run of
  exactly three is preserved (it encodes `__` + a leading-`_` name).

### Memory management

- Use `xmalloc` / `xcalloc` / `xrealloc` / `xstrdup` (arena allocation);
  `zfree` is a no-op (arena memory is reclaimed all at once).
- Never call libc `free` on arena memory. Use libc `malloc`/`free` only for
  short-lived buffers from a libc API (`realpath`, cJSON's allocator).

### Recursion safety

- Guard unbounded recursion with `RECURSION_GUARD` / `RECURSION_GUARD_TOKEN`.
- Recursive type/AST walkers that aren't parser-bounded carry a depth
  parameter (see `sync_type_linkage_depth`).

### Dead code

- No consecutive duplicate `return` statements; no code after an unconditional
  return; no empty `if`/`while` bodies without an explanatory comment.
- Run `clang-format` (enforced by the CI Lint job) and the fuzzer before
  landing compiler changes.

## Development workflow

- `make` builds the compiler; `make test` runs the full suite.
- `make fuzz-libfuzzer-build` builds the libFuzzer target; the nightly
  `Scheduled Fuzzing` workflow runs it.
- Keep changes in small, focused commits. The `std` library (Zen C), the
  website docs, and the awesome-zenc examples live in separate repositories.
