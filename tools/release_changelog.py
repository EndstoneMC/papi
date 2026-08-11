"""Deterministic changelog and release-note generation for Endstone PAPI.

Usage:
    python tools/release_changelog.py --version X.Y.Z --date YYYY-MM-DD \
        --repository Owner/repo --output CHANGELOG.md --notes release_body.md

Moves the [Unreleased] section into a new [X.Y.Z] - YYYY-MM-DD section, restores an
empty [Unreleased] section, and updates comparison links. Produces GitHub-flavored
release notes from the new section's content.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

SECTION_RE = re.compile(
    r"^## \[(?P<version>[^]]+)](?: - (?P<date>[^\n]+))?\s*$",
    re.MULTILINE,
)
SUBSECTION_RE = re.compile(r"^### (.+?)\s*$", re.MULTILINE)
REFERENCE_RE = re.compile(r"^\[([^]]+)]:\s+.*$", re.MULTILINE)
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")


def clean_block(value: str) -> str:
    return value.strip("\r\n ")


def split_subsections(value: str) -> tuple[str, list[tuple[str, str]]]:
    matches = list(SUBSECTION_RE.finditer(value))
    if not matches:
        return clean_block(value), []
    preamble = clean_block(value[: matches[0].start()])
    sections: list[tuple[str, str]] = []
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(value)
        sections.append((match.group(1), clean_block(value[match.end() : end])))
    return preamble, sections


def render_version_notes(source: str, version: str) -> tuple[str, str]:
    """Return the recorded date and release notes for an existing version section."""
    if not VERSION_RE.fullmatch(version):
        raise ValueError(f"invalid version: {version}")

    without_references = REFERENCE_RE.sub("", source).rstrip()
    all_matches = list(SECTION_RE.finditer(without_references))
    matches = [match for match in all_matches if match.group("version") == version]
    if len(matches) != 1:
        raise ValueError(f"CHANGELOG.md must contain exactly one [{version}] section")

    match = matches[0]
    if match.group("date") is None:
        raise ValueError(f"CHANGELOG.md [{version}] section has no release date")
    match_index = all_matches.index(match)
    end = all_matches[match_index + 1].start() if match_index + 1 < len(all_matches) else len(without_references)
    content = clean_block(without_references[match.end() : end])
    if not content:
        raise ValueError(f"CHANGELOG.md [{version}] section is empty")

    _preamble, subsections = split_subsections(content)
    notes_lines: list[str] = [f"## Release v{version}", ""]
    for heading, body in subsections:
        notes_lines.append(f"### {heading}")
        notes_lines.append("")
        if body:
            notes_lines.append(body)
            notes_lines.append("")
    return match.group("date"), "\n".join(notes_lines).rstrip() + "\n"


def render_release(source: str, version: str, date: str, repository: str) -> tuple[str, str]:
    if not VERSION_RE.fullmatch(version):
        raise ValueError(f"invalid version: {version}")

    without_references = REFERENCE_RE.sub("", source).rstrip()
    matches = list(SECTION_RE.finditer(without_references))
    if not matches:
        raise ValueError("CHANGELOG.md has no version sections")

    prefix = without_references[: matches[0].start()].rstrip()
    sections: list[tuple[str, str | None, str]] = []
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(without_references)
        sections.append((match.group(1), match.group(2), clean_block(without_references[match.end() : end])))

    unreleased_sections = [s for s in sections if s[0] == "Unreleased"]
    if not unreleased_sections:
        raise ValueError("CHANGELOG.md has no [Unreleased] section")

    unreleased_content = unreleased_sections[0][2]
    if not unreleased_content:
        raise ValueError("[Unreleased] section is empty")

    # Build the new changelog.
    lines: list[str] = []
    if prefix:
        lines.append(prefix)
        lines.append("")

    # New Unreleased section (empty).
    lines.append("## [Unreleased]")
    lines.append("")

    # New version section.
    lines.append(f"## [{version}] - {date}")
    lines.append("")
    lines.append(unreleased_content)
    lines.append("")

    # Remaining sections (skip the old Unreleased).
    for name, _date, content in sections[1:]:
        if name == "Unreleased":
            continue
        lines.append(f"## [{name}]" + (f" - {_date}" if _date else ""))
        lines.append("")
        if content:
            lines.append(content)
            lines.append("")

    # Comparison links (Keep a Changelog convention):
    # [Unreleased]: compare/v{latest}...HEAD
    # [latest]: compare/v{previous}...v{latest}
    # [oldest]: releases/tag/v{oldest}
    all_versions = [version] + [s[0] for s in sections[1:] if s[0] != "Unreleased"]
    lines.append(f"[Unreleased]: https://github.com/{repository}/compare/v{version}...HEAD")
    for i, name in enumerate(all_versions):
        if i + 1 < len(all_versions):
            prev = all_versions[i + 1]
            lines.append(f"[{name}]: https://github.com/{repository}/compare/v{prev}...v{name}")
        else:
            lines.append(f"[{name}]: https://github.com/{repository}/releases/tag/v{name}")

    changelog = "\n".join(lines).rstrip() + "\n"

    # Build release notes from the versioned section to keep initial preparation
    # and retry-after-finalization output identical.
    _recorded_date, notes = render_version_notes(changelog, version)

    return changelog, notes


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare changelog and release notes for a PAPI release.")
    parser.add_argument("--version", required=True, help="Target version (X.Y.Z)")
    parser.add_argument("--date", required=True, help="Release date (YYYY-MM-DD)")
    parser.add_argument("--repository", required=True, help="GitHub repository (Owner/repo)")
    parser.add_argument("--output", required=True, type=Path, help="Output changelog path")
    parser.add_argument("--notes", required=True, type=Path, help="Output release notes path")
    parser.add_argument("--input", type=Path, default=Path("CHANGELOG.md"), help="Input changelog path")
    args = parser.parse_args()

    source = args.input.read_text(encoding="utf-8")
    changelog, notes = render_release(source, args.version, args.date, args.repository)
    args.output.write_text(changelog, encoding="utf-8")
    args.notes.write_text(notes, encoding="utf-8")
    print(f"Changelog written to {args.output}")
    print(f"Release notes written to {args.notes}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
