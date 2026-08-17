# Changelog

All notable changes to Endstone PAPI are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- Linux wheels now advertise Endstone's `manylinux_2_31` baseline and fail packaging when their ELF requirements exceed it.
- `/papi` subcommands now use explicit command enum names, preventing duplicate enum registration while preserving both
  parse forms and multi-word text.
- Python consumers can now load a correctly typed `PlaceholderAPI` from
  Endstone's service manager without relying on unavailable cross-module RTTI
  downcasting or trusting an arbitrary provider with the same service name.
- Linux wheels establish their Endstone-owned C++ runtime-family dependency with
  standard-SONAME bridge DSOs, avoiding direct hashed-SONAME loader crashes and
  import-time mutation of the installed package directory.
- `endstone>=0.11.8,<0.12` is now declared as a runtime dependency, so
  `pip install endstone-papi` resolves the required Endstone C++ runtime
  automatically.

### Changed

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
  CPython 3.10–3.14 wheel matrix
- Architecture boundary enforcement via automated tests
- Deterministic changelog and release-note tooling
- Automated release workflow with dry-run mode

### Removed

- **BREAKING**: Python `PlaceholderAPI` constructor and subclassing (Architecture A)
- **BREAKING**: Python registry, pipe parser, and all built-in placeholders
- **BREAKING**: `{identifier|params}` pipe syntax
- **BREAKING**: `plugin:identifier` duplicate-namespace fallback
- **BREAKING**: 0.0.1 compatibility adapters `PlaceholderAPI::Processor`,
  `registerPlaceholder`, `getPlaceholderPattern`, Python `register_placeholder`,
  and `placeholder_pattern`
