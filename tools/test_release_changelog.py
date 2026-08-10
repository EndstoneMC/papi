"""Tests for the release changelog tooling."""

from __future__ import annotations

import sys
from pathlib import Path

# Ensure the tools directory is importable.
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from release_changelog import render_release  # noqa: E402

SAMPLE_CHANGELOG = """# Changelog

All notable changes to Endstone PAPI are documented here.

## [Unreleased]

### Added

- New expansion registration API
- Relational placeholder support

### Fixed

- Parser edge case with nested braces

## [1.0.0] - 2026-01-01

### Added

- Initial release

[Unreleased]: https://github.com/EndstoneMC/papi/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/EndstoneMC/papi/releases/tag/v1.0.0
"""


def test_release_moves_unreleased_into_versioned_section() -> None:
    changelog, _notes = render_release(SAMPLE_CHANGELOG, "1.1.0", "2026-08-10", "EndstoneMC/papi")

    assert "## [Unreleased]" in changelog
    assert "## [1.1.0] - 2026-08-10" in changelog
    assert "## [1.0.0] - 2026-01-01" in changelog

    # The unreleased content moved to the new version section.
    assert "New expansion registration API" in changelog
    assert "Relational placeholder support" in changelog
    assert "Parser edge case with nested braces" in changelog

    # The Unreleased section is now empty (just the header).
    unreleased_start = changelog.index("## [Unreleased]")
    version_start = changelog.index("## [1.1.0]")
    unreleased_body = changelog[unreleased_start:version_start].strip()
    assert unreleased_body == "## [Unreleased]"


def test_release_updates_comparison_links() -> None:
    changelog, _notes = render_release(SAMPLE_CHANGELOG, "1.1.0", "2026-08-10", "EndstoneMC/papi")

    assert "[Unreleased]: https://github.com/EndstoneMC/papi/compare/v1.1.0...HEAD" in changelog
    # 1.1.0 compares from the previous version (1.0.0) to itself.
    assert "[1.1.0]: https://github.com/EndstoneMC/papi/compare/v1.0.0...v1.1.0" in changelog
    # 1.0.0 is the oldest, so it links to its tag.
    assert "[1.0.0]: https://github.com/EndstoneMC/papi/releases/tag/v1.0.0" in changelog


def test_release_notes_contain_subsections() -> None:
    _changelog, notes = render_release(SAMPLE_CHANGELOG, "1.1.0", "2026-08-10", "EndstoneMC/papi")

    assert "## Release v1.1.0" in notes
    assert "### Added" in notes
    assert "New expansion registration API" in notes
    assert "### Fixed" in notes
    assert "Parser edge case with nested braces" in notes


def test_release_rejects_invalid_version() -> None:
    try:
        render_release(SAMPLE_CHANGELOG, "1.0", "2026-08-10", "EndstoneMC/papi")
        raise AssertionError("should have raised ValueError")
    except ValueError:
        pass


def test_release_rejects_empty_unreleased() -> None:
    empty_changelog = """# Changelog

## [Unreleased]

## [1.0.0] - 2026-01-01

### Added

- Initial release

[Unreleased]: https://github.com/EndstoneMC/papi/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/EndstoneMC/papi/releases/tag/v1.0.0
"""
    try:
        render_release(empty_changelog, "1.1.0", "2026-08-10", "EndstoneMC/papi")
        raise AssertionError("should have raised ValueError")
    except ValueError:
        pass


def test_release_with_no_previous_version() -> None:
    """First release: no previous version section exists."""
    first_changelog = """# Changelog

## [Unreleased]

### Added

- Initial release

[Unreleased]: https://github.com/EndstoneMC/papi/compare/v0.0.1...HEAD
"""
    changelog, notes = render_release(first_changelog, "1.0.0", "2026-08-10", "EndstoneMC/papi")

    assert "## [1.0.0] - 2026-08-10" in changelog
    assert "Initial release" in changelog
    assert "## Release v1.0.0" in notes


def main() -> int:
    tests = [
        test_release_moves_unreleased_into_versioned_section,
        test_release_updates_comparison_links,
        test_release_notes_contain_subsections,
        test_release_rejects_invalid_version,
        test_release_rejects_empty_unreleased,
        test_release_with_no_previous_version,
    ]
    passed = 0
    for test in tests:
        test()
        print(f"  PASS: {test.__name__}")
        passed += 1
    print(f"\n{passed}/{len(tests)} tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
