# Changelog

All notable changes to Endstone PAPI are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- Python event metadata is now an independent snapshot that remains safe to retain after the callback.
- Linux wheels now advertise Endstone's `manylinux_2_31` baseline and fail packaging when their ELF requirements exceed it.
- `/papi` subcommands now use explicit command enum names, preventing duplicate enum registration while preserving both
  parse forms and multi-word text.
- Python consumers can now load a correctly typed `PlaceholderAPI` from
  Endstone's service manager without relying on unavailable cross-module RTTI
  downcasting or trusting an arbitrary provider with the same service name.
- Linux wheels establish their Endstone-owned C++ runtime-family dependency with
  standard-SONAME bridge DSOs, avoiding direct hashed-SONAME loader crashes and
  import-time mutation of the installed package directory.
- Runtime installation and Linux wheel repair now use the same verified official
  Endstone `0.11.11.dev392` wheels (API 0.12, build `33562961160`). Snapshot installs
  require the supplied wheelhouse; production releases remain blocked until a
  supported stable runtime is selected.
- Linux developer wheel repair now accepts Clang 18 or newer while official wheels remain pinned to Clang 20.

### Changed

- **BREAKING**: Event dispatch names are now `endstone_papi.ExpansionRegisteredEvent` and
  `endstone_papi.ExpansionUnregisteredEvent`, matching automatic Python listener registration.
  Rebuild native listeners and update raw event-name registrations; old binaries and unqualified names are incompatible.
- Development builds now accept Clang/clang-cl 18 or newer; official release
  wheels remain pinned to Clang 20 for reproducibility.
- **BREAKING**: Ordinary provider callbacks now receive `const endstone::Player*`
  (`Player | None` in Python) instead of `OfflinePlayer`. Rebuild all native
  consumers and providers against the migrated SDK and pinned Endstone API 0.12 snapshot.
- Supported Python versions are now 3.11–3.14, with eight release wheels across
  Windows and Linux.
- Placeholder routing now uses a colon namespace boundary: ordinary placeholders are
  `{identifier:params}` and relational placeholders are `{rel:identifier:params}`.
  Only the routing colon(s) are interpreted by PAPI; provider params preserve
  underscores, dots, later colons, case, spaces, and empty values exactly as written.
- `rel` is reserved for relational dispatch and cannot be registered as an ordinary
  expansion identifier. Legacy underscore- and dot-separated outer syntax is not
  accepted.

### Added

- Native C++20 PlaceholderAPI framework with bracket parser, owner-aware
  expansion registry, and inert retained-service lifecycle
- `PlaceholderExpansion` contract for C++ and Python providers, sharing one
  native registry
- `{identifier:params}` ordinary placeholder syntax with first-colon split,
  ASCII-lowercase identifier, and exact parameter preservation
- `{rel:identifier:params}` relational placeholder dispatch with explicit
  capability declaration and two-player API
- GIL-safe Python expansion bridge using pybind11 3 `smart_holder` and
  `trampoline_self_life_support`
- `/papi parse`, `/papi list`, and `/papi info` commands with permission checks
  and input validation
- `ExpansionRegisteredEvent` and `ExpansionUnregisteredEvent` metadata-only
  post-commit events
- Bounded 60-second error throttling with injectable monotonic clock
- Strict ASCII identifier grammar `[A-Za-z0-9][A-Za-z0-9-]*`
- Windows and Linux CI with clang-cl/Clang 20, Conan 2, CMake 3.29, and
  CPython 3.11–3.14 wheel matrix
- Architecture boundary enforcement via automated tests
- Deterministic changelog and release-note tooling
- Automated release workflow with dry-run mode

### Removed

- **BREAKING**: Python 3.10 support and `cp310` wheels
- **BREAKING**: Python `PlaceholderAPI` constructor and subclassing (Architecture A)
- **BREAKING**: Python registry, pipe parser, and all built-in placeholders
- **BREAKING**: `{identifier|params}` pipe syntax
- **BREAKING**: `plugin:identifier` duplicate-namespace fallback
- **BREAKING**: 0.0.1 compatibility adapters `PlaceholderAPI::Processor`,
  `registerPlaceholder`, `getPlaceholderPattern`, Python `register_placeholder`,
  and `placeholder_pattern`
