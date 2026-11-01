# Source style and reference documentation

## One policy, independently checked projects

`.editorconfig` defines shared whitespace/encoding defaults. `.clang-format`
defines C++ and Protobuf styles; `pyproject.toml` defines Ruff formatting and
Python docstring conventions. Versions are pinned in `scripts/ci/requirements.txt`:
clang-format 19.1.7, Ruff 0.17.3 and the syntax parsers used by source checks.
Do not rely on a different formatter bundled with an IDE.

Formatting contexts reuse `.ci/components.yaml` ownership paths. Tooling has its
own formatting-only context and does not become a CMake library. A component-only
change checks that component's owned sources without expanding to its consumers.
Shared style/parser-policy changes check every populated context. Build and
affected-consumer validation retain their separate dependency rules.

`reusable_format.yml` runs one independent matrix job per selected context, without
compiling/restoring C++ vendor products. A failure does not cancel other contexts.
The required validation gate rejects a failed or skipped selected formatting run.

## Developer commands

From the repository root, create/activate the supported Windows Python environment:

```powershell
py -3.12 -m venv .venv
./.venv/Scripts/Activate.ps1
python -m pip install -r scripts/ci/requirements.txt
```

Check or apply the same policy used by CI:

Apply fixes before starting a native build so compilers do not hold source files open.

```powershell
python -m scripts.ci.format_sources
python -m scripts.ci.format_sources --fix
python -m scripts.ci.format_sources --component jobs
python -m scripts.ci.format_sources --fix --component diagnostics
python -m scripts.ci.format_sources --fix --path libs/jobs/src/task_handle.cpp
```

The PowerShell wrapper supports the same operations and can locate the repository
from another working directory:

```powershell
./scripts/check_format.ps1
./scripts/check_format.ps1 -Fix -Component jobs
./scripts/check_format.ps1 -Path protocols/session.proto
```

Use `-Python` when the wrapper must select a specific interpreter, or activate the
environment first. `--path`/`-Path` accepts only owned files in the selected context;
generated output and external source trees are excluded. Check mode never rewrites
sources. Fix mode applies whitespace/braces/import ordering but still fails on
missing documentation: write the actual contract rather than generating placeholders.

## C++ and Protobuf

C++ uses four spaces, a 100-column target, Allman braces and explicit braces around
control bodies. Short/empty functions, lambdas, loops and enums do not collapse
onto one line. Definition blocks have blank boundaries. A syntax-tree rule adds
a blank line after a nonfinal control statement in a compound block, including
complete if/else, try/catch and do/while statements; it does not split their clauses
or count braces inside strings/comments. clang-format then preserves the boundary.

Separate additional logical stages when that improves reading: input validation,
ownership acquisition, computation, publication and cleanup often deserve distinct
paragraphs. Tools cannot infer all semantic boundaries. Keep cohesive functions
small; the analyzer's physical-body review threshold is 100 lines to account for
expanded braces, blank boundaries and documented local fixtures. Do not remove
explanatory comments or collapse code to evade a size check. Roughly 300 file lines
remain a review signal, not a reason to fragment one responsibility mechanically.

Protobuf uses two spaces and the same 100-column target. Comments explain schema
types, fields and RPC contracts; generated `.pb.*` files are not hand-formatted.
Wire field numbers and compatibility rules must not change during style-only work.

## Documentation contracts

All owned C++ files have a Doxygen `@file` summary. Explain named types, concepts,
aliases, callable declarations/definitions, namespace state/constants, members and enum values with adjacent
`///` or `/** ... */` documentation. Use `///<` only for a same-line member comment;
it must not document the next declaration. Document the template declaration,
not an unrelated line inside its body. Ordinary algorithm comments remain useful
but do not replace the reference contract.

```cpp
/// @brief Add a finite nonnegative amount to a cumulative counter.
/// @param name Stable metric family name.
/// @param amount Increment retained by the registry.
/// @return Whether this observation was accepted.
bool increment(std::string_view name, double amount);
```

Describe purpose and constraints, not just the symbol's spelling. Include parameter
meaning, return/failure behavior, units, ownership/borrowing, thread affinity,
concurrency and cleanup where relevant. Copyable handles do not automatically extend
borrowed-data lifetime. Internal helpers and test fixtures explain their invariant
or scenario; test descriptions do not claim broader coverage than their assertions.

Python uses Google-style docstrings. Modules, classes, methods and functions,
including private/nested helpers and pytest fixtures/tests, need a meaningful
docstring. Add Args/Returns/Raises sections when needed to explain a nontrivial
contract; simple predicates/fixtures may use a concise complete summary. Ruff
checks the convention, and the AST check also covers private helpers.

The documentation checker uses Python/C++ syntax trees and a limited Protobuf
declaration check. It checks source coverage/attachment, not prose accuracy, all
possible macro expansions or generated reference rendering. Review, compilation
and tests remain necessary. Unsupported/new schema syntax needs checker tests.

Doxygen generation, per-library reference navigation and synchronization of generated
pages to the repository wiki are the next task. This increment adds no generator,
publication workflow, wiki credentials or manual wiki content.

## IDE integration

Shared VS Code recommendations/settings use the workspace `.venv` for the supported
Windows environment. Select that interpreter after installation. The C++ extension
uses its pinned clang-format binary; Python uses its pinned Ruff binary. Protobuf
formatting uses the shared current-file task and needs no language-specific extension.
The shared tasks use the selected Python interpreter and provide:

- **Format owned sources**: apply the repository-wide policy.
- **Format current source file**: apply the full policy to the active owned source.
- **Check source style and documentation**: run the same read-only checks as CI.

Native Format Document handles the underlying language formatter; use the task to
include the supplementary control-block spacing and documentation checks. The
settings contain no local machine paths. Other Python-environment layouts can be
configured in user/local IDE settings; the CLI itself has no Windows SDK dependency.

Visual Studio and CLion can register the CLI as an external/custom tool: choose
the project environment's Python executable, use the repository root as working
directory, and pass `-m scripts.ci.format_sources --fix --path <active-file>` as
separate arguments. EditorConfig supplies the common editor defaults. IDE use is
optional; the command-line workflow remains authoritative.

Reference behavior: [clang-format options](https://clang.llvm.org/docs/ClangFormatStyleOptions.html),
[Ruff formatter](https://docs.astral.sh/ruff/formatter/) and
[Ruff docstring conventions](https://docs.astral.sh/ruff/settings/#lint_pydocstyle_convention).
