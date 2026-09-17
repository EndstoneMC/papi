# Changelog

All notable changes to Endstone PAPI are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-09-17

### Fixed

- Linux wheels now target Endstone's `manylinux_2_31` baseline and reject packaging when generated binaries require a newer baseline.
- `/papi` parse commands now register reliably while preserving both supported parse forms and multi-word text.
- Python plugins can reliably load the active `PlaceholderAPI` service through Endstone's service manager.
- Fixed Linux wheel loading with Endstone's bundled C++ runtime.
- PAPI now declares support for Endstone `>=0.11.8,<0.12` (API 0.11).
- Linux source builds support Clang 18 or newer; official wheels are built with Clang 20.

### Changed

- Placeholder routing now uses a colon namespace boundary: ordinary placeholders are
  `{identifier:params}` and relational placeholders are `{rel:identifier:params}`.
  Only the routing colon(s) are interpreted by PAPI; provider params preserve
  underscores, dots, later colons, case, spaces, and empty values exactly as written.
- `rel` is reserved for relational dispatch and cannot be registered as an ordinary
  expansion identifier. Legacy underscore- and dot-separated outer syntax is not
  accepted.

### Added

- Native C++20 PlaceholderAPI framework with a bracket parser, owner-aware expansion lifecycle, and shared service.
- A common `PlaceholderExpansion` contract for C++ and Python providers.
- `{identifier:params}` ordinary placeholder syntax with first-colon splitting and exact parameter preservation.
- `{rel:identifier:params}` relational placeholder dispatch with explicit provider opt-in.
- Python expansions can be registered, retained, and invoked through the same native service as C++ expansions.
- `/papi parse`, `/papi list`, and `/papi info` commands with permission checks and input validation.
- `ExpansionRegisteredEvent` and `ExpansionUnregisteredEvent` for expansion lifecycle changes.
- Repeated expansion errors are rate-limited to prevent log spam.
- Strict ASCII identifier grammar `[A-Za-z0-9][A-Za-z0-9-]*`.
- Windows and Linux wheels for CPython 3.10–3.14.

### Removed

- **BREAKING**: Python plugins must load `PlaceholderAPI` from Endstone's service manager; direct construction and subclassing are no longer supported.
- **BREAKING**: Removed the 0.0.1 Python-only registry, parser, and built-in placeholders.
- **BREAKING**: Removed the `{identifier|params}` pipe syntax.
- **BREAKING**: Removed the `plugin:identifier` duplicate-namespace fallback.
- **BREAKING**: Removed 0.0.1 compatibility adapters: `PlaceholderAPI::Processor`,
  `registerPlaceholder`, `getPlaceholderPattern`, Python `register_placeholder`,
  and `placeholder_pattern`.

[Unreleased]: https://github.com/EndstoneMC/papi/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/EndstoneMC/papi/releases/tag/v0.1.0
