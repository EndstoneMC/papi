"""Makes the native ``_papi`` extension importable for Python tests.

The dev build (``cmake --preset papi-dev``) emits ``_papi`` into ``build/RelWithDebInfo/``,
which is not on the Python path. This conftest copies the freshly built extension into
``endstone_papi/`` so that ``import endstone_papi._papi`` works without a full
``pip install``.

Wheel builds define ``BUILD_TESTING=OFF`` and never run pytest, so the copy does not
affect release artifacts. ``.gitignore`` already excludes ``*.pyd`` and ``*.so``.
"""

from __future__ import annotations

import shutil
from pathlib import Path

# tests/python/conftest.py -> tests/ -> project root
_ROOT = Path(__file__).resolve().parents[2]
_BUILD_DIR = _ROOT / "build" / "RelWithDebInfo"
_PACKAGE_DIR = _ROOT / "endstone_papi"

_PATTERNS = ("_papi*.pyd", "_papi*.so")


def _find_built_module() -> Path | None:
    for pattern in _PATTERNS:
        matches = sorted(_BUILD_DIR.glob(pattern))
        if matches:
            return matches[-1]
    return None


def _ensure_native_module() -> None:
    # Prefer the current dev build over an older importable copy left by a prior
    # pytest run. Importing first would lock that stale .pyd on Windows and make
    # newly-added test bindings invisible until it was copied manually.
    source = _find_built_module()
    if source is not None:
        dest = _PACKAGE_DIR / source.name
        if not dest.exists() or source.stat().st_mtime > dest.stat().st_mtime:
            shutil.copy2(source, dest)
        return

    # With no dev build, an installed/editable package may still provide the module.


_ensure_native_module()
