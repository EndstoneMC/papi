"""Runtime smoke test for a built wheel.

Verifies the installed wheel imports, exposes the documented public API, and a
PlaceholderExpansion subclass behaves correctly. Intended to run after
``pip install`` of the wheel and its endstone dependency in CI; not part of the
pytest suite (which uses the dev build with test-only bindings).
"""

from __future__ import annotations

import sys
from pathlib import Path

import endstone_papi
from endstone_papi import (
    ExpansionInfo,
    PlaceholderAPI,
    PlaceholderExpansion,
    UnregisterReason,
)


def main() -> int:
    version = endstone_papi.__version__
    assert version, f"empty version: {version!r}"
    assert endstone_papi.SERVICE_NAME == "PlaceholderAPI"
    provenance = Path(endstone_papi.__file__).with_name("_toolchain_provenance.txt").read_text(encoding="utf-8")
    assert "compiler_id=Clang" in provenance, provenance
    assert "compiler_version=20." in provenance, provenance

    # PlaceholderExpansion is subclassable and metadata is readable through the
    # native trampoline.
    class TestExpansion(PlaceholderExpansion):
        identifier = "test"
        author = "t"
        version = "1.0.0"

        def on_request(self, player, params):
            return None

    exp = TestExpansion()
    assert exp.identifier == "test"
    assert exp.author == "t"
    assert exp.version == "1.0.0"
    assert exp.name == "test"
    assert exp.required_plugin is None
    assert exp.can_register() is True
    assert exp.supports_relational_placeholders() is False
    assert exp.supports_player_cleanup() is False

    # UnregisterReason has four distinct values.
    reasons = {
        UnregisterReason.EXPLICIT,
        UnregisterReason.OWNER_DISABLED,
        UnregisterReason.REQUIRED_PLUGIN_DISABLED,
        UnregisterReason.PAPI_SHUTDOWN,
    }
    assert len(reasons) == 4

    # ExpansionInfo is a value type with the documented read-only fields.
    for field in ("identifier", "name", "author", "version", "owner", "required_plugin", "relational"):
        assert hasattr(ExpansionInfo, field), f"ExpansionInfo missing field: {field}"

    # PlaceholderAPI is final: subclassing must be rejected.
    try:

        class BadAPI(PlaceholderAPI):  # type: ignore[no-redef]
            pass

        raise AssertionError("PlaceholderAPI should be final")
    except TypeError:
        pass

    print(f"OK: endstone_papi {version} runtime smoke test passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
