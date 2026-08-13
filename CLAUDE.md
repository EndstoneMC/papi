# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Endstone PAPI is a native C++20 PlaceholderAPI framework for
[Endstone](https://github.com/EndstoneMC/endstone) and Minecraft Bedrock Dedicated
Server. It provides a bracket placeholder parser, an owner-aware expansion registry,
and a service that resolves `{identifier_params}` placeholders through expansions
supplied by C++ or Python plugins.

PAPI is a **framework**, not a placeholder provider. The core contains **no** business
placeholders—no player name, ping, coordinates, online count, time, economy,
permissions, prefix, or similar values. Every value comes from a PlaceholderExpansion
registered by a plugin.

Architecture B is frozen: the native C++ core owns the parser, registry, service,
lifecycle, ownership, relational dispatch, and error containment. Python is a consumer,
a PlaceholderExpansion provider, and the binding/package layer. Python must never
become the PlaceholderAPI core.

## Build Commands

### Prerequisites

- CMake 3.29 or newer
- Ninja
- Conan 2
- Windows: LLVM clang-cl 20, the MSVC x64 build environment, and the Windows SDK
- Linux: Clang 20 with libc++ and libc++abi
- Python 3.10+ for the package and test tooling

The repository owns `.conan2/profiles/default` and `.conanrc`. The profile selects
RelWithDebInfo, C++20, Ninja, clang-cl on Windows, and Clang with libc++ on Linux. Do
not run `conan profile detect`, because that would replace the project profile.

### Configure and build

Run Windows commands from an x64 MSVC developer environment with LLVM's `clang-cl`
on `PATH`.

```shell
python -m pip install "conan==2.30.0"
conan install . --build=missing
cmake --preset papi-dev
cmake --build --preset papi-dev
```

The main outputs are:

- Windows: `build/RelWithDebInfo/_papi.cp314-win_amd64.pyd`
- Linux: `build/RelWithDebInfo/_papi.cpython-3xx-linux.so` (suffix varies by Python)
- Test binary: `build/RelWithDebInfo/tests/papi_test.exe` (or `.bin`)

For local Endstone development, CMake accepts
`-DFETCHCONTENT_SOURCE_DIR_ENDSTONE=<path>` to use an existing Endstone checkout.
Otherwise it fetches Endstone `v0.11.8`.

## Test Commands

### Native tests

```shell
ctest --preset papi-dev --output-on-failure
```

### Python tests

```shell
python -m pytest -q
```

### Architecture boundary tests

```shell
python tests/test_architecture_boundaries.py
```

### Changelog tooling tests

```shell
python tools/test_release_changelog.py
```

### Formatting and static analysis

```shell
# C++ formatting (check only)
git ls-files '*.cpp' '*.h' | xargs clang-format --dry-run --Werror

# C++ static analysis
python tools/run_clang_tidy.py --build-dir build/RelWithDebInfo

# Python lint and format
python -m ruff check .
python -m ruff format --check .
```

### Wheel and sdist

```shell
python -m build --wheel
python -m build --sdist
```

## Code Style

### C++

- Follow the `.clang-format` file (Microsoft base, Stroustrup braces, include
  regrouping). Formatting is mechanically enforced; never hand-format.
- C++20, extensions off.
- Use `[[nodiscard]]` on query methods that return values the caller should not
  discard.
- Keep hot paths bounded and allocation-free where the surrounding design requires it.
- Keep comments terse and explain only non-obvious constraints.
- Do not leave development-process notes or comments that merely restate code.

### Python

- Use `ruff` for formatting and linting (`line-length = 120`).
- Use snake_case for methods and properties, matching the Endstone Python convention.
- Class attributes for plugin metadata (`api_version`, `commands`, `permissions`) follow
  the Endstone plugin convention and are exempt from `RUF012`.

### Include conventions

Internal headers use paths relative to `src/`:

```cpp
#include "core/parser/identifier.h"
#include "core/registry/expansion_manager.h"
#include "platform/endstone/server_platform.h"
```

Public headers use the `endstone_papi/` prefix and never include internal headers.

## Architecture

### Directory layout

```
include/endstone_papi/   # Public SDK headers (installed with the wheel)
src/
  core/                  # Framework core (no Python, no Endstone server impl)
    parser/              # Bracket scanner and identifier grammar
    registry/            # Owner-aware ExpansionManager
    service/             # Concrete PlaceholderAPI implementation
    diagnostics/         # Error throttling
    platform.h           # Abstract platform interface
  platform/
    endstone/            # Endstone-specific adapters
      bootstrap.cpp      # Service lifecycle: create, register, shut down
      server_platform.*  # Platform impl forwarding to endstone::Server
  python/                # Python bindings (depends on core, never vice versa)
    module.cpp           # pybind11 module definition
    expansion_trampoline.*  # Python-to-native expansion trampoline
    gil_safe_proxy.*     # GIL-acquiring wrapper for Python-backed expansions
tests/
  cpp/                   # Native C++ tests (GoogleTest)
  python/                # Python tests (pytest)
  test_architecture_boundaries.py
tools/
  release_changelog.py   # Deterministic changelog/release-note generation
  run_clang_tidy.py      # Static analysis runner
  test_release_changelog.py
```

### Dependency direction

```
core/parser          → (no internal deps, only public headers)
core/diagnostics     → (no internal deps)
core/registry        → core/diagnostics, core/platform.h, public headers
core/service         → core/parser, core/registry, core/diagnostics, core/platform.h
platform/endstone    → core/service, core/platform.h, Endstone API
python               → core/service, platform/endstone, Endstone API, pybind11
```

Nothing in `core/` may include Endstone server headers, Python headers, or
`platform/endstone/` headers. Nothing in `platform/endstone/` may include `python/`
headers. The dependency direction is enforced by `tests/test_architecture_boundaries.py`.

## Public API Boundaries

### C++ public types (`include/endstone_papi/`)

- `ExpansionInfo` — immutable copied metadata, safe to retain after unregister.
- `UnregisterReason` — enum: Explicit, OwnerDisabled, RequiredPluginDisabled, PapiShutdown.
- `PlaceholderExpansion` — abstract extension point for providers.
- `PlaceholderAPI` — abstract service contract, derives from `endstone::Service`.
- `ExpansionRegisteredEvent` / `ExpansionUnregisteredEvent` — metadata-only events.
- `papi::getVersion()` — build version string.

### What must not leak into public headers

- Python/pybind11 headers.
- Internal registry types (`ExpansionManager`, entries, call leases).
- Private implementation details (locks, maps, throttle state).
- Endstone server implementation headers (only `endstone::Plugin`, `endstone::Player`,
  `endstone::OfflinePlayer`, `endstone::Service` are acceptable).

### Python public surface

- `PlaceholderAPI` — native, final, non-subclassable. Loaded from the service manager.
- `PlaceholderExpansion` — subclassable. Uses pybind11 3 `py::smart_holder` and
  `py::trampoline_self_life_support`.
- `ExpansionInfo`, `UnregisterReason`, events — value/enum types.
- `_PapiBootstrap` — internal native lifecycle state owned by the PAPI plugin.

## C++/Python Interop

### Four provider/consumer paths

All four paths share one native registry:

1. C++ expansion → C++ consumer (direct native dispatch)
2. C++ expansion → Python consumer (Python loads native service)
3. Python expansion → C++ consumer (trampoline + GIL proxy)
4. Python expansion → Python consumer (full round-trip)

### Trampoline dispatch safety

The trampoline resolves overridden members by walking the Python type's MRO **above**
the pybind base class. A plain `hasattr` or `self.attr(name)` on the instance re-enters
the base binding's property or method, which dispatches back into the trampoline,
causing unbounded native↔Python recursion. This was a real incident that consumed 18 GB
of RAM in seconds. The regression is covered by tests that assert bounded behavior via a
lowered recursion limit and exact dispatch counts.

### GIL rules

- Every Python callback and every final release of a Python-backed expansion **must**
  acquire the GIL.
- Never release the GIL while holding the registry lock.
- Never let a Python exception cross into Endstone command/event/service code.
- Null C++ `OfflinePlayer*` maps to Python `None`; never dereference before binding.
- Python return values: only `None` or exact `str` are accepted. Wrong types are
  contained provider errors, never coerced with `str()`.

## Ownership and Lifecycle

### Ownership graph

```
Endstone ServiceManager --shared--> native PlaceholderApiImpl
consumer               --shared-------------^
PAPI bootstrap         --shared-------------^

PlaceholderApiImpl --owns--> ExpansionManager
ExpansionManager   --shared--> ExpansionEntry
Entry              --shared--> C++ expansion
or Entry           --shared--> GIL-safe proxy --owns-under-GIL--> Python trampoline
```

`Plugin*` is identity-only, valid while the record is active, and cleared at retirement.

### Lifecycle invariants

- Registration is atomic: validate metadata/owner/dependency/preflight first, then
  commit under a lock. A failed registration leaves no partial state.
- Owner disable removes every owned expansion before module unload. Required-plugin
  disable removes dependents even if owned elsewhere.
- PAPI's own `onDisable` drives teardown directly: transition inactive, unregister the
  named service, remove all entries, release providers, clear platform references.
- Explicit unregister marks the entry retired immediately; cleanup waits for any
  in-flight callback to return (call lease mechanism).
- A retained `shared_ptr<PlaceholderAPI>` after PAPI shutdown is permanently inert:
  `isActive()` returns false, parsing returns input unchanged, queries return empty,
  mutations fail, no method touches any plugin, server, or provider state.
- Shutdown is idempotent: calling it twice does not re-run callbacks or events.

### Cleanup ordering

- `PluginDisableEvent` fires after `onDisable`/`setEnabled(false)` but before
  task/listener cleanup and module unload. PAPI's cleanup must complete at this point.
- PAPI's own `onDisable` must not rely on receiving its own `PluginDisableEvent`,
  because its listeners may already be disabled.
- `PapiShutdown` bulk teardown suppresses unregister events because the event system
  is itself shutting down.

## Parser Semantics

### Ordinary syntax

`{identifier_params}` — first underscore splits; identifier is ASCII-lowercased;
params preserve exact case/content; no recursion/nesting/regex.

- No underscore → literal (e.g., `{player}` stays unchanged).
- Unknown/error/null → original token unchanged.
- Replacement text is never reparsed.
- Space before the separator invalidates the token (e.g., `{ player_x}`, `{play er_x}`).
- First closing brace ends the candidate; nesting is not recognized.

### Relational syntax

`{rel_identifier_params}` — processed only by `setRelationalPlaceholders`. The `rel_`
prefix is stripped, then first underscore splits. Only expansions with
`supportsRelationalPlaceholders() == true` are consulted. Ordinary parsing never
invokes relational callbacks.

### Contains semantics

`containsPlaceholders(text)` is a cheap lexical check: true when `{` has a later `}`.
Does not require an underscore, valid identifier, or registration. Pure and safe from
any thread.

### Registration grammar

`[A-Za-z0-9][A-Za-z0-9.-]*`, canonicalized with ASCII lowercase. Reject underscore
(separator), colon (historical namespace), braces, percent, whitespace, non-ASCII.

## Threading

- Parsing, registration, unregister, and lifecycle transitions require
  `Server::isPrimaryThread()`. Off-thread use is a caller bug, reported once per 60
  seconds and then suppressed.
- `containsPlaceholders`, `isActive`, and copied metadata queries are thread-safe and
  may run from any thread.
- Never hold a registry lock while invoking provider code, Python/GIL work, logging,
  Endstone events, scheduler, or PluginManager calls.
- Never transparently schedule or defer; the caller's thread is the dispatch thread.

## Error Containment

- Catch `std::exception`, unknown C++ exceptions, and `py::error_already_set` at the
  provider boundary. An exception, wrong Python return type, or null/None result
  preserves the exact original placeholder.
- Log the first failure with identifier, owner, operation, and useful traceback/message.
  Suppress matching repeats for 60 seconds per entry generation and operation. On the
  next permitted message, include the suppression count.
- Error tracking is bounded and removed with the entry. Injectable monotonic clock
  enables deterministic tests.
- Do not claim to catch access violations, signals, or undefined behavior.

## Packaging

### Wheel contents

- `endstone_papi/_papi.*.pyd` (or `.so`) — native extension module
- `endstone_papi/__init__.py` — public re-exports
- `endstone_papi/plugin.py` — thin Endstone plugin bootstrap
- `endstone_papi/_papi.pyi` — type stubs
- `endstone_papi/include/endstone_papi/*.h` — public C++ headers
- `LICENSE`
- Entry point: `endstone` → `endstone_papi:PlaceholderAPIPlugin`

### Supported matrix

- CPython 3.10–3.14 (`cp310`–`cp314`)
- Windows x86-64 (`win_amd64`)
- manylinux x86-64 (`manylinux_x86_64`)

### ABI

- Tied to Endstone 0.11, C++20, target architecture, standard library, compiler runtime,
  and Python minor wheel.
- C++ providers must use a toolchain compatible with the running Endstone/PAPI build.
- Implementation/private locks/maps/pybind types must not leak into public headers.

## Release Workflow

The canonical implementation is `.github/workflows/release.yml`. Do not replace or bypass
it with manual version commits, tags, or GitHub Releases.

### Prepare `develop`

1. Finish all release changes as focused commits on `develop`.
2. Keep release notes under `## [Unreleased]` in `CHANGELOG.md`.
3. Keep the current released version unchanged in `CMakeLists.txt`.
   The wheel version in `pyproject.toml` is derived dynamically from
   `CMakeLists.txt` via the scikit-build regex metadata provider, and the
   C++ `getVersion()` is generated from `cmake/version.h.in` at configure time.
4. Run tests, build, and the self-test.
5. Push `develop` to `personal/develop` and wait for CI to pass.

### Synchronize the pre-release commit

The non-dry-run workflow requires official `main` and `develop` to point to the same
commit. After personal CI passes:

1. Fetch both remotes and verify ancestry.
2. Fast-forward the pre-release commit to official `main` and `develop`.
3. Wait for official CI.
4. Synchronize personal branches.

### Preview

```shell
gh workflow run release.yml --repo EndstoneMC/papi --ref main -f version=X.Y.Z -f dry_run=true
```

### Publish

```shell
gh workflow run release.yml --repo EndstoneMC/papi --ref main -f version=X.Y.Z -f dry_run=false
```

The workflow validates the version and prepares an immutable, base-keyed release-candidate
ref. It builds and runtime-validates Windows/Linux wheels and the sdist from that candidate,
then atomically advances `main`, `develop`, and the production tag only after the complete
artifact set is accepted. A retry reuses the candidate and can finish a GitHub Release after
refs were already finalized without rewriting history.

### Final synchronization

After the release workflow passes, synchronize all branches and verify that main,
develop, and the tag all point to the same commit.

## Git Conventions

- Normal development is performed on `develop`. Do not create another branch unless the
  user explicitly asks.
- Keep commits focused, independently buildable, and easy to revert.
- Never add a `Co-Authored-By` line for Claude.
- Do not force-push or rewrite official `origin` history.
- `origin` is `ReallocAll/papi`, `upstream` is `EndstoneMC/papi`. Verify remote URLs
  before any release operation.
- Before a commit has reached `origin`, local and `personal` history may be rewritten
  when the user authorizes it. Use `--force-with-lease`, never an unguarded force push.
- Once a commit is covered by an official branch or a public release tag, preserve it.

## Changelog

`CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com/):

- Add unreleased user-visible and protocol/API changes only under `## [Unreleased]`.
- Use Added, Changed, Deprecated, Removed, Fixed, Security headings.
- Write for server administrators and plugin users. Omit internal refactoring details.
- Prefix breaking changes with `**BREAKING**:`.
- The Release workflow handles section movement, dating, and comparison links.

## Clean-Room GPL Rule

Java PlaceholderAPI is GPL. Endstone PAPI is MIT. Use the frozen Java tree only to
confirm architecture, observable inputs/outputs, and edge cases. Never copy source,
comments, private control flow, or mechanically translate methods. Implement
independently from the specifications in the workdocs. If a workdoc requires
clarification, study behavior and state the result in input/output/invariant terms.

## Deferred Features

The following are explicitly **not** implemented in v1 and must not be added without
architectural review:

- Core `config.toml` or `/papi reload` command
- Standalone expansion loader / jar/whl discovery
- eCloud, marketplace, or network distribution
- Business placeholder implementations in core
- Configurable recursion depth or nested placeholder parsing
- RTTI/dynamic_cast-based relational capability detection
- Public mutable registry/manager handle
- Python `PlaceholderAPI` subclassing or constructor
