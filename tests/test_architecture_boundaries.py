#!/usr/bin/env python3
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
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)

# Endstone headers that expose server internals - not allowed in core/.
FORBIDDEN_ENDSTONE_IN_CORE = [
    re.compile(r"^endstone/server\.h$"),
    re.compile(r"^endstone/plugin/plugin_manager\.h$"),
    re.compile(r"^endstone/plugin/service_manager\.h$"),
    re.compile(r"^endstone/scheduler/"),
    re.compile(r"^endstone/command/"),
]


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


def main() -> int:
    extensions = {".h", ".hpp", ".cpp", ".cc"}
    files = [p for p in SRC.rglob("*") if p.suffix in extensions and layer_of(p) is not None]
    violations: list[str] = []
    for f in sorted(files):
        violations.extend(check_file(f))
    if violations:
        for v in violations:
            print(v, file=sys.stderr)
        print(f"\n{len(violations)} architectural boundary violation(s) found.", file=sys.stderr)
        return 1
    print(f"OK: {len(files)} files checked, no boundary violations.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
