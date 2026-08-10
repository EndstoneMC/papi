"""Verify that source layers respect architectural boundaries.

PAPI Architecture B dependency model:

    core/parser       -> core/parser only (lowest layer)
    core/diagnostics  -> core/diagnostics only
    core/registry     -> core/parser, core/diagnostics, core/platform.h
    core/service      -> core/parser, core/registry, core/diagnostics, core/platform.h
    platform/endstone -> core/*, platform/endstone/*, Endstone API
    python            -> core/*, platform/endstone/*, python/*, Endstone API, pybind11

Invariants enforced:

1. Nothing in core/ may include pybind11 or python/ internal headers.
2. Nothing in core/ may include platform/endstone/ internal headers.
3. Nothing in core/ may include Endstone server/plugin_manager/service_manager headers
   (those are platform-layer concerns).
4. Nothing in platform/endstone/ may include pybind11 or python/ internal headers.
5. Nothing in core/parser/ may include headers from core/registry/, core/service/,
   or core/diagnostics/.
6. Public headers (include/endstone_papi/) may not include pybind11, Python, or
   internal headers, and may only include the allow-listed Endstone headers.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
INCLUDE = ROOT / "include"

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)

# Endstone headers that expose server internals - not allowed in core/.
FORBIDDEN_ENDSTONE_IN_CORE = [
    re.compile(r"^endstone/server\.h$"),
    re.compile(r"^endstone/plugin/plugin_manager\.h$"),
    re.compile(r"^endstone/plugin/service_manager\.h$"),
    re.compile(r"^endstone/scheduler/"),
    re.compile(r"^endstone/command/"),
]

# Endstone headers permitted in public headers (include/endstone_papi/).
# Only Plugin, Player, OfflinePlayer, Service, and the Event bases are public API.
ALLOWED_ENDSTONE_IN_PUBLIC = {
    "endstone/offline_player.h",
    "endstone/player.h",
    "endstone/plugin/plugin.h",
    "endstone/plugin/service.h",
    "endstone/event/event.h",
    "endstone/event/server/server_event.h",
}

# Prefixes that mark an include as internal (never allowed in public headers).
INTERNAL_INCLUDE_PREFIXES = ("core/", "platform/", "python/", "src/")


def layer_of(path: Path) -> str | None:
    """Return the architectural layer of a source file, or None if not layered."""
    try:
        rel = path.relative_to(SRC)
    except ValueError:
        return None
    parts = rel.parts
    if parts[0] == "core":
        if len(parts) > 1 and parts[1] in ("parser", "registry", "service", "diagnostics"):
            return f"core/{parts[1]}"
        return "core"
    if parts[0] == "platform":
        return "platform"
    if parts[0] == "python":
        return "python"
    return None


def is_public_header(path: Path) -> bool:
    """Return True if the file is under include/endstone_papi/."""
    try:
        path.relative_to(INCLUDE / "endstone_papi")
    except ValueError:
        return False
    return True


def check_file(path: Path) -> list[str]:
    layer = layer_of(path)
    if layer is None:
        return []

    violations: list[str] = []
    text = path.read_text(encoding="utf-8", errors="replace")

    for m in INCLUDE_RE.finditer(text):
        inc = m.group(1)
        is_core = layer.startswith("core")

        # Rule 1: core/ must not include pybind11 or python/ internal headers.
        if is_core and inc.startswith(("pybind11/", "python/")):
            violations.append(f"{path.relative_to(ROOT)}: includes <{inc}> (pybind11/python headers forbidden in core)")

        # Rule 2: core/ must not include platform/endstone/ internal headers.
        if is_core and inc.startswith("platform/endstone/"):
            violations.append(f"{path.relative_to(ROOT)}: includes <{inc}> (platform headers forbidden in core)")

        # Rule 3: core/ must not include Endstone server-internal headers.
        if is_core:
            for pat in FORBIDDEN_ENDSTONE_IN_CORE:
                if pat.match(inc):
                    violations.append(
                        f"{path.relative_to(ROOT)}: includes <{inc}> (Endstone server header forbidden in core)"
                    )
                    break

        # Rule 4: platform/endstone/ must not include pybind11 or python/ internal headers.
        if layer == "platform" and inc.startswith(("pybind11/", "python/")):
            violations.append(
                f"{path.relative_to(ROOT)}: includes <{inc}> (pybind11/python headers forbidden in platform)"
            )

        # Rule 5: core/parser/ must not include higher core layers.
        if layer == "core/parser":
            for forbidden in ("core/registry/", "core/service/", "core/diagnostics/"):
                if inc.startswith(forbidden):
                    violations.append(
                        f"{path.relative_to(ROOT)}: includes <{inc}> (parser must not depend on higher core layers)"
                    )
                    break

    return violations


def check_public_header(path: Path) -> list[str]:
    """Check a public header for forbidden includes (Rule 6)."""
    if not is_public_header(path):
        return []

    violations: list[str] = []
    text = path.read_text(encoding="utf-8", errors="replace")

    for m in INCLUDE_RE.finditer(text):
        inc = m.group(1)

        # Public headers must not include pybind11 or Python headers.
        if inc.startswith(("pybind11/", "Python.h")):
            violations.append(
                f"{path.relative_to(ROOT)}: includes <{inc}> (pybind11/Python forbidden in public headers)"
            )
            continue

        # Public headers must not include internal headers.
        if inc.startswith(INTERNAL_INCLUDE_PREFIXES):
            violations.append(
                f"{path.relative_to(ROOT)}: includes <{inc}> (internal header forbidden in public headers)"
            )
            continue

        # Public headers may only include allow-listed Endstone headers.
        if inc.startswith("endstone/") and inc not in ALLOWED_ENDSTONE_IN_PUBLIC:
            violations.append(f"{path.relative_to(ROOT)}: includes <{inc}> (Endstone header not on public allow-list)")

    return violations


def check_includes_in_content(layer: str, rel_path: str, content: str) -> list[str]:
    """Check include directives in arbitrary content for a given layer.

    Used by negative tests to verify the checker catches violations without
    needing real files on disk.
    """
    violations: list[str] = []
    is_core = layer.startswith("core")

    for m in INCLUDE_RE.finditer(content):
        inc = m.group(1)

        if is_core and inc.startswith(("pybind11/", "python/")):
            violations.append(f"{rel_path}: includes <{inc}> (pybind11/python headers forbidden in core)")

        if is_core and inc.startswith("platform/endstone/"):
            violations.append(f"{rel_path}: includes <{inc}> (platform headers forbidden in core)")

        if is_core:
            for pat in FORBIDDEN_ENDSTONE_IN_CORE:
                if pat.match(inc):
                    violations.append(f"{rel_path}: includes <{inc}> (Endstone server header forbidden in core)")
                    break

        if layer == "platform" and inc.startswith(("pybind11/", "python/")):
            violations.append(f"{rel_path}: includes <{inc}> (pybind11/python headers forbidden in platform)")

        if layer == "core/parser":
            for forbidden in ("core/registry/", "core/service/", "core/diagnostics/"):
                if inc.startswith(forbidden):
                    violations.append(f"{rel_path}: includes <{inc}> (parser must not depend on higher core layers)")
                    break

    return violations


def check_public_header_content(rel_path: str, content: str) -> list[str]:
    """Check include directives in arbitrary public-header content (Rule 6).

    Used by negative tests to verify the checker catches public-header violations.
    """
    violations: list[str] = []

    for m in INCLUDE_RE.finditer(content):
        inc = m.group(1)

        if inc.startswith(("pybind11/", "Python.h")):
            violations.append(f"{rel_path}: includes <{inc}> (pybind11/Python forbidden in public headers)")
            continue

        if inc.startswith(INTERNAL_INCLUDE_PREFIXES):
            violations.append(f"{rel_path}: includes <{inc}> (internal header forbidden in public headers)")
            continue

        if inc.startswith("endstone/") and inc not in ALLOWED_ENDSTONE_IN_PUBLIC:
            violations.append(f"{rel_path}: includes <{inc}> (Endstone header not on public allow-list)")

    return violations


def run_negative_tests() -> list[str]:
    """Verify the checker detects intentionally-introduced violations.

    Each case constructs virtual violating content and asserts the checker
    flags it.  Returns a list of failure messages (empty if all pass).
    """
    failures: list[str] = []

    cases: list[tuple[str, str, str, str]] = [
        # (layer, rel_path, content, description)
        (
            "core/service",
            "core/service/violation.cpp",
            '#include "pybind11/pybind11.h"',
            "core including pybind11",
        ),
        (
            "core/registry",
            "core/registry/violation.cpp",
            '#include "python/module.h"',
            "core including python/ internal header",
        ),
        (
            "core/service",
            "core/service/violation.cpp",
            '#include "platform/endstone/server_platform.h"',
            "core including platform/endstone/ internal header",
        ),
        (
            "core/service",
            "core/service/violation.cpp",
            '#include "endstone/server.h"',
            "core including endstone/server.h",
        ),
        (
            "core/registry",
            "core/registry/violation.cpp",
            '#include "endstone/plugin/plugin_manager.h"',
            "core including endstone/plugin/plugin_manager.h",
        ),
        (
            "core/service",
            "core/service/violation.cpp",
            '#include "endstone/scheduler/scheduler.h"',
            "core including endstone scheduler",
        ),
        (
            "core/service",
            "core/service/violation.cpp",
            '#include "endstone/command/command.h"',
            "core including endstone command",
        ),
        (
            "platform",
            "platform/endstone/violation.cpp",
            '#include "pybind11/pybind11.h"',
            "platform including pybind11",
        ),
        (
            "platform",
            "platform/endstone/violation.cpp",
            '#include "python/expansion_trampoline.h"',
            "platform including python/ internal header",
        ),
        (
            "core/parser",
            "core/parser/violation.cpp",
            '#include "core/registry/expansion_manager.h"',
            "parser depending on core/registry",
        ),
        (
            "core/parser",
            "core/parser/violation.cpp",
            '#include "core/service/placeholder_api_impl.h"',
            "parser depending on core/service",
        ),
        (
            "core/parser",
            "core/parser/violation.cpp",
            '#include "core/diagnostics/throttle.h"',
            "parser depending on core/diagnostics",
        ),
    ]

    for layer, rel_path, content, desc in cases:
        v = check_includes_in_content(layer, rel_path, content)
        if not v:
            failures.append(f"negative test failed: checker did not detect {desc}")

    public_cases: list[tuple[str, str, str]] = [
        # (rel_path, content, description)
        (
            "include/endstone_papi/violation.h",
            '#include "pybind11/pybind11.h"',
            "public header including pybind11",
        ),
        (
            "include/endstone_papi/violation.h",
            '#include "Python.h"',
            "public header including Python.h",
        ),
        (
            "include/endstone_papi/violation.h",
            '#include "core/registry/expansion_manager.h"',
            "public header including internal core/ header",
        ),
        (
            "include/endstone_papi/violation.h",
            '#include "platform/endstone/server_platform.h"',
            "public header including internal platform/ header",
        ),
        (
            "include/endstone_papi/violation.h",
            '#include "python/module.h"',
            "public header including internal python/ header",
        ),
        (
            "include/endstone_papi/violation.h",
            '#include "endstone/server.h"',
            "public header including non-allow-listed endstone/server.h",
        ),
        (
            "include/endstone_papi/violation.h",
            '#include "endstone/scheduler/scheduler.h"',
            "public header including non-allow-listed endstone/scheduler",
        ),
    ]

    for rel_path, content, desc in public_cases:
        v = check_public_header_content(rel_path, content)
        if not v:
            failures.append(f"negative test failed: checker did not detect {desc}")

    # Positive control: allowed includes must NOT produce violations.
    positive_core = check_includes_in_content(
        "core/service",
        "core/service/ok.cpp",
        '#include "core/parser/identifier.h"\n#include "endstone/offline_player.h"',
    )
    if positive_core:
        failures.append(f"positive test failed: allowed core includes flagged as violations: {positive_core}")

    positive_public = check_public_header_content(
        "include/endstone_papi/ok.h",
        '#include "endstone/plugin/plugin.h"\n#include "endstone/player.h"',
    )
    if positive_public:
        failures.append(f"positive test failed: allowed public includes flagged as violations: {positive_public}")

    return failures


def main() -> int:
    extensions = {".h", ".hpp", ".cpp", ".cc"}
    src_files = [p for p in SRC.rglob("*") if p.suffix in extensions and layer_of(p) is not None]
    public_files = [p for p in (INCLUDE / "endstone_papi").rglob("*") if p.suffix in extensions]

    violations: list[str] = []
    for f in sorted(src_files):
        violations.extend(check_file(f))
    for f in sorted(public_files):
        violations.extend(check_public_header(f))

    if violations:
        for v in violations:
            print(v, file=sys.stderr)
        print(f"\n{len(violations)} architectural boundary violation(s) found.", file=sys.stderr)
        return 1

    negative_failures = run_negative_tests()
    if negative_failures:
        for f in negative_failures:
            print(f, file=sys.stderr)
        print(f"\n{len(negative_failures)} negative test failure(s).", file=sys.stderr)
        return 1

    total = len(src_files) + len(public_files)
    print(f"OK: {total} files checked, no boundary violations, negative tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
