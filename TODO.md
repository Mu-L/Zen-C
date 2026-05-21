# Zen C TODO

## Recently Completed

### Structural Refactoring (File Splits)
- [x] Split `parser_expr.c` (8,147L → 7 files in `src/parser/expr/`)
- [x] Split `parser_utils.c` (6,996L → 11 files in `src/parser/utils/`)
- [x] Split `parser_stmt.c` (4,948L → 6 files in `src/parser/stmt/`)
- [x] Split `parser_core.c`, `parser_struct.c`, `parser_type.c`, `parser_decl.c` into subdirectory modules
- [x] Split `codegen_stmt.c` (2,548L → 3 files) and `codegen_decl.c` (2,299L → 5 files)
- [x] Split `codegen.c` (2,587L → 10 files)
- [x] Split `codegen_expr_handlers.c` (1,978L → 5 files: simple, member, control, misc + handlers)
- [x] Split `codegen_decl_defs.c` (2,000L → 3 files: defs, struct, emit)
- [x] Split `typecheck.c` (extracted `typecheck_utils.c`, `typecheck_call.c`, `typecheck_expr.c`, `typecheck_stmt.c`)
- [x] Eliminated `g_parser_ctx` global (92→0 code refs)
- [x] Cleaned up `struct VisitedModules` + module-visit helpers to `codegen_internal.h` (non-static)

### Compiler Warning Flags & Sanitizers
- [x] Added `-Wundef`, `-Wfloat-equal`, `-Wmissing-field-initializers`, `-Wsign-compare`,
      `-Wtype-limits`, `-Wuninitialized`, `-Wdouble-promotion`, `-Wtautological-compare`,
      `-Wshift-negative-value`, `-Wdangling-else`, `-Wreturn-local-addr` to base CFLAGS
- [x] Added GCC-only: `-Wduplicated-cond`, `-Wlogical-op`, `-fanalyzer`
- [x] Added Clang-only: `-Wassign-enum`, `-Wcomma`, `-Wsometimes-uninitialized`,
      `-Wloop-analysis`, `-Wsizeof-array-div`
- [x] Fixed all `-Wfloat-equal` and `-Wundef` warnings (excluding vendored cJSON/TRE)
- [x] Fixed 21 `-fanalyzer` warnings across `move_check.c`, `misra.c`, `zen_facts.c`, `utils.c`,
      `repl.c`, `repl_readline.c`, `repl_eval.c`, `lsp_formatter.c`
- [x] Added sanitizer targets: `make asan`, `make tsan`, `make msan`, `make lsan`, `make analyzer`
- [x] Homogenized CI sanitizer jobs to build-only pattern (tests only for ASAN)
- [x] Fixed `struct_hash` ASAN use-after-return by changing `char *` to `char[256]` embedded buffer

### C++ Mode & Platform Fixes
- [x] Fixed `--backend cpp` not setting `use_cpp = 1`
- [x] Added `extern "C"` for extern functions in C++ mode
- [x] Added `is_extern` AST field, forced C compilation for linked `.c` files
- [x] Fixed `test_info.zc` stack smashing (added `domainname` field to `struct utsname`)
- [x] Fixed `zc run` absolute output path handling
- [x] Created `.version` file with `v0.4.4` as fallback; updated Makefile, CMakeLists.txt, build.bat
- [x] Updated `build.bat` to read `src-sources.txt` instead of hardcoded file list

### LSP Crash Fixes, Memory Leaks & Test Coverage
- [x] 7 new LSP tests (21 total): partial code goto-def/references, unopened file
      request, empty source, didChange incremental update, codeAction
- [x] Fixed 3 NULL-pointer crashes: `callee` unchecked in goto-def (lsp_analysis.c:240),
      find-references (lsp_analysis.c:1522), and missing `ctx` check in completion (lsp_analysis.c:913)
- [x] Fixed real heap leaks: replaced `strdup()` with `xstrdup()` in json_rpc.c
      (system malloc vs arena mismatch — `zfree` is a no-op)
- [x] Fixed `tmpfile()` file descriptor leak on project reinit (lsp_project.c)
- [x] Clear stale AST on parse failure instead of retaining outdated index (lsp_project.c:280)

### Tuple Infrastructure Fix + 13 New Tests
- [x] Fixed nested tuple struct emission: field types now use `types[i]` array instead
      of naively splitting the mangled signature by `__` (which broke for nested tuples
      like `((int, int), (int, int))` — the inner `Tuple__int__int` contains `__`)
- [x] Added `TupleType.types[]` + `TupleType.count` fields to store individual
      field type names for correct codegen
- [x] Updated `register_tuple_with_types()` API and all callers (parser_type.c,
      parser_expr.c, parser_struct.c) to pass field type names individually
- [x] Fixed reverse-LIFO emission order so nested tuples are defined before parents
- [x] Two-pass model: forward declarations + struct bodies before enums
- [x] 13 new test files covering: 3/4/5/8/10-tuples, nested tuples, field mutation,
      comparison, enum variants, complex expressions, typed annotations, return types,
      edge cases, pointer type mangling, mixed arity destructuring

### Test Framework Modernization
- [x] Per-test failure isolation: `__zenc_assert` no longer calls `exit(1)` — uses counter instead
- [x] Named per-test output: each test prints name + OK/FAIL to stderr
- [x] `_z_run_tests()` returns failure count; `main()` uses it as exit code
- [x] Added `expect` keyword — non-fatal assertion, continues after failure
- [x] Multiple assertions in one test are all reported (not just the first)
- [x] 6 move checker tests added (tests/language/features/move/)
- [x] `expect` in comptime blocks already supported (comptime_interpreter.c handles `NODE_EXPECT`)

### Emitter Refactoring
- [x] Fix `realloc` NULL-return bug, `emitter_vprintf`, `emitter_putc`, auto-indent, push/pop API
- [x] Migrate ~84 hardcoded `"    "` in codegen to auto-indent
- [x] Comptime temp file collision fix (`rand()` → `getpid()` + counter)

### Type Checker Tests
- [x] 13 new test files (from 47 lines to 450+ lines): type compat, operators, calls,
      returns, move, struct init, vardecl, traits, lifetime, match, cast, const-fold, misc

## Architecture & Modularity
- [ ] Split `expr_primary.c` (2,455L, single huge `parse_primary_impl` function).
- [ ] Further split `codegen_expr_handlers.c` (1,294L, heavy handlers: struct_init, binary, call).
- [ ] Split `expr_prec.c` (2,950L, `parse_expr_prec_impl` at ~2,900 lines).
- [ ] Split ParserContext god struct (parser.h:291-404) into focused sub-structs:
      CodegenState, ModuleState, GenericRegistry, LSPState, TypeValidationState.
      (The `cg` block at lines 376-391 is already a partial extraction: finish it.)
- [ ] Eliminate `g_config` macro (119 refs via `g_compiler.config`) — pass config explicitly.
- [ ] Eliminate `g_compiler` global (32 refs across 10 files, deep in REPL/LSP/arena).
- [ ] Reduce AST dispatch duplication: 6+ parallel `switch(type)` chains across codegen, typechecker, LSP.
      Consider a formal visitor pattern or code-gen dispatchers.

## Features
- [ ] Improve traits.
- [ ] Improve comptime mechanics.

## Testing
- [x] Add test filtering via `ZC_TEST_FILTER` environment variable.
- [x] Add `expect_eq`/`expect_ne`/`expect_approx_eq` helpers to standard library.
- [x] Eliminate `strdup`→`zfree` mismatch (9 files: LSP/REPL/os/zen_facts) — all allocs go through arena
- [x] Fix `xrealloc` header bug — `xmalloc`/`xcalloc` now write size header for safe realloc
- [x] Convert `lsp_formatter.c` to arena allocations

## Memory
- [ ] Fix LSP buffer leak: lsp_main.c arena-allocates JSON body then calls `zfree` (no-op on arena).
      For a long-running LSP server this leaks until process exit.
- [ ] Introduce sub-arenas or reset points. LSP needs persistent arena (AST/project index)
      + per-request arena (diagnostics/responses) to avoid unbounded growth.

## Error Handling
- [ ] Unify 5+ error mechanisms: `zpanic_at`, `zpanic`, `zpanic_at_diag`, `fprintf(stderr, ...)`, return-NULL.
- [ ] Migrate `fprintf(stderr, ...)` in codegen runtime helpers to proper diagnostic pipeline
      (so LSP mode catches all errors).

## Cleanup
- [ ] Fix residual template and defer scope tracking bugs.
- [ ] Reduce function complexity hotspot warnings (for example, `parse_expr_prec_impl` at 2,900 lines,
      `parse_primary_impl` at ~2,400 lines).
- [ ] Clear temporary build files and logs.
