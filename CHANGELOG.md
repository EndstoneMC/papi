# Changelog

All notable changes to Endstone PAPI are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Native C++20 PlaceholderAPI framework with bracket parser, owner-aware
  expansion registry, and inert retained-service lifecycle
- `PlaceholderExpansion` contract for C++ and Python providers, sharing one
  native registry
- `{identifier_params}` ordinary placeholder syntax with first-underscore split,
  ASCII-lowercase identifier, and exact parameter preservation
- `{rel_identifier_params}` relational placeholder dispatch with explicit
  capability declaration and two-player API
- GIL-safe Python expansion bridge using pybind11 3 `smart_holder` and
  `trampoline_self_life_support`
- `/papi parse`, `/papi list`, and `/papi info` commands with permission checks
  and input validation
- `ExpansionRegisteredEvent` and `ExpansionUnregisteredEvent` metadata-only
  post-commit events
- Bounded 60-second error throttling with injectable monotonic clock
- Strict ASCII identifier grammar `[A-Za-z0-9][A-Za-z0-9.-]*`
- Deprecated C++ `Processor` adapter and `getPlaceholderPattern` for one-release
  source compatibility
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
