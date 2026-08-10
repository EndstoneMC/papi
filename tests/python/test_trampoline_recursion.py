"""T-004 regression: trampoline super()/.member re-entry is bounded.

A Python override that calls ``super().member`` re-enters the pybind base property,
which dispatches back into the trampoline, which calls ``subclassMember`` again for
the same (self, name). Without the thread-local recursion guard this is unbounded
native/Python recursion that exhausts memory in seconds.

Each scenario runs in a subprocess with a low recursion limit and a hard timeout so a
regression surfaces as a contained RecursionError or RuntimeError, never a crash or
hang.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

from endstone_papi import PlaceholderExpansion

try:
    import endstone_papi._papi  # noqa: F401
except ImportError:
    pytest.skip("native module not built", allow_module_level=True)

_ROOT = Path(__file__).resolve().parents[2]


def _run_subprocess(code: str, timeout: float = 10.0) -> subprocess.CompletedProcess:
    """Run code in a subprocess with a low recursion limit and hard timeout."""
    wrapper = (
        "import sys\n"
        "sys.setrecursionlimit(80)\n"
        "sys.path.insert(0, %r)\n"
        "from endstone_papi import PlaceholderExpansion, UnregisterReason\n"
        "import endstone_papi._papi\n" + code
    ) % str(_ROOT)
    return subprocess.run(
        [sys.executable, "-c", wrapper],
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )


def test_super_identifier_reentry_is_bounded() -> None:
    """super().identifier in an override must not recurse unboundedly."""

    class SuperIdentifier(PlaceholderExpansion):
        @property
        def identifier(self) -> str:
            return super().identifier  # type: ignore[no-any-return]

        author = "a"
        version = "1"

        def on_request(self, player, params):
            return None

    expansion = SuperIdentifier()
    # Accessing the property must raise (contained), not crash or hang.
    with pytest.raises((RuntimeError, RecursionError)):
        _ = expansion.identifier


def test_super_name_reentry_falls_back_to_identifier() -> None:
    """super().name re-enters getName, which falls back to the identifier.

    The guard returns empty for the re-entered 'name' lookup, so getName falls back
    to requiredString(identifier). If identifier is a plain class attribute, the
    fallback succeeds and returns the identifier value.
    """

    class SuperName(PlaceholderExpansion):
        identifier = "demo"
        author = "a"
        version = "1"

        @property
        def name(self) -> str:
            return super().name  # type: ignore[no-any-return]

        def on_request(self, player, params):
            return None

    expansion = SuperName()
    # The guard breaks the name re-entry; getName falls back to identifier.
    assert expansion.name == "demo"


def test_super_can_register_reentry_is_bounded() -> None:
    """super().can_register() in an override must not recurse unboundedly."""

    class SuperCanRegister(PlaceholderExpansion):
        identifier = "demo"
        author = "a"
        version = "1"

        def can_register(self) -> bool:
            return super().can_register()

        def on_request(self, player, params):
            return None

    expansion = SuperCanRegister()
    # The guard returns empty for the re-entered 'can_register' lookup;
    # booleanCall falls back to its default (True).
    assert expansion.can_register() is True


def test_super_on_request_reentry_is_bounded() -> None:
    """super().on_request() in an override must not recurse unboundedly."""

    class SuperOnRequest(PlaceholderExpansion):
        identifier = "demo"
        author = "a"
        version = "1"

        def on_request(self, player, params):
            return super().on_request(player, params)

    expansion = SuperOnRequest()
    # The guard returns empty for the re-entered 'on_request' lookup;
    # the trampoline throws "must implement on_request", which is contained.
    with pytest.raises((RuntimeError, RecursionError)):
        _ = expansion.on_request(None, "x")


def test_subprocess_super_identifier_does_not_crash() -> None:
    """In a fresh subprocess with a low recursion limit, super().identifier is bounded."""
    result = _run_subprocess(
        "class E(PlaceholderExpansion):\n"
        "    @property\n"
        "    def identifier(self):\n"
        "        return super().identifier\n"
        "    author = 'a'\n"
        "    version = '1'\n"
        "    def on_request(self, player, params):\n"
        "        return None\n"
        "try:\n"
        "    E().identifier\n"
        "    print('RESULT: no error')\n"
        "except (RuntimeError, RecursionError) as e:\n"
        "    print('RESULT: contained error:', type(e).__name__)\n"
        "except BaseException as e:\n"
        "    print('RESULT: unexpected:', type(e).__name__)\n"
    )
    assert result.returncode == 0, f"process crashed: {result.stderr}"
    output = result.stdout.strip()
    assert "RESULT: contained error" in output or "RESULT: no error" in output, (
        f"unexpected output: {output}\nstderr: {result.stderr}"
    )


def test_subprocess_super_name_falls_back() -> None:
    """In a fresh subprocess, super().name falls back to the identifier."""
    result = _run_subprocess(
        "class E(PlaceholderExpansion):\n"
        "    identifier = 'demo'\n"
        "    author = 'a'\n"
        "    version = '1'\n"
        "    @property\n"
        "    def name(self):\n"
        "        return super().name\n"
        "    def on_request(self, player, params):\n"
        "        return None\n"
        "try:\n"
        "    val = E().name\n"
        "    print('RESULT:', val)\n"
        "except BaseException as e:\n"
        "    print('RESULT: error:', type(e).__name__)\n"
    )
    assert result.returncode == 0, f"process crashed: {result.stderr}"
    output = result.stdout.strip()
    assert "RESULT: demo" in output, f"expected fallback to identifier, got: {output}\nstderr: {result.stderr}"
