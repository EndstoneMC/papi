"""T-005 regression: unbounded parse reentrancy is bounded.

A provider callback that re-enters set_placeholders (directly or indirectly) would
exhaust the C++ stack without the active-expansion cycle detector and the parse-depth
budget. Each scenario runs in a subprocess with a low recursion limit and a hard
timeout so a regression surfaces as a contained error, never a crash or hang.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

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
        "sys.path.insert(0, " + repr(str(_ROOT)) + ")\n"
        "from endstone_papi import PlaceholderExpansion\n"
        "from endstone_papi._papi import _TestService\n" + code
    )
    return subprocess.run(
        [sys.executable, "-c", wrapper],
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )


def test_self_parse_cycle_is_bounded() -> None:
    """A Python expansion that calls set_placeholders for its own token is bounded."""

    result = _run_subprocess(
        "host = _TestService('test')\n"
        "service = host.service\n"
        "\n"
        "class CycleExpansion(PlaceholderExpansion):\n"
        "    identifier = 'a'\n"
        "    author = 't'\n"
        "    version = '1'\n"
        "\n"
        "    def on_request(self, player, params):\n"
        "        return service.set_placeholders(player, '{a_x}')\n"
        "\n"
        "host.register_expansion(CycleExpansion())\n"
        "out = service.set_placeholders(None, '{a_x}')\n"
        "assert out == '{a_x}', repr(out)\n"
        "assert any('cycle' in w for w in host.warnings), host.warnings\n"
        "print('OK')\n"
    )
    assert result.returncode == 0, result.stderr
    assert "OK" in result.stdout


def test_indirect_parse_cycle_is_bounded() -> None:
    """An indirect cycle (A -> B -> A) is bounded the same way."""

    result = _run_subprocess(
        "host = _TestService('test')\n"
        "service = host.service\n"
        "\n"
        "class ExpansionA(PlaceholderExpansion):\n"
        "    identifier = 'a'\n"
        "    author = 't'\n"
        "    version = '1'\n"
        "\n"
        "    def on_request(self, player, params):\n"
        "        return service.set_placeholders(player, '{b_y}')\n"
        "\n"
        "class ExpansionB(PlaceholderExpansion):\n"
        "    identifier = 'b'\n"
        "    author = 't'\n"
        "    version = '1'\n"
        "\n"
        "    def on_request(self, player, params):\n"
        "        return service.set_placeholders(player, '{a_x}')\n"
        "\n"
        "host.register_expansion(ExpansionA())\n"
        "host.register_expansion(ExpansionB())\n"
        "out = service.set_placeholders(None, '{a_x}')\n"
        "assert out == '{a_x}', repr(out)\n"
        "assert any('cycle' in w for w in host.warnings), host.warnings\n"
        "print('OK')\n"
    )
    assert result.returncode == 0, result.stderr
    assert "OK" in result.stdout


def test_parse_depth_budget_is_enforced() -> None:
    """A deep non-cyclic chain that exceeds the depth budget preserves the input."""

    result = _run_subprocess(
        "host = _TestService('test')\n"
        "service = host.service\n"
        "\n"
        "chain_length = 12\n"
        "for i in range(chain_length):\n"
        "    next_token = '{%d_x}' % (i + 1) if i + 1 < chain_length else 'leaf'\n"
        "    cls = type(\n"
        "        'Exp%d' % i,\n"
        "        (PlaceholderExpansion,),\n"
        "        {\n"
        "            'identifier': str(i),\n"
        "            'author': 't',\n"
        "            'version': '1',\n"
        "            'on_request': lambda self, player, params, t=next_token: "
        "service.set_placeholders(player, t),\n"
        "        },\n"
        "    )\n"
        "    host.register_expansion(cls())\n"
        "\n"
        "out = service.set_placeholders(None, '{0_x}')\n"
        "assert out, repr(out)\n"
        "assert any('depth' in w for w in host.warnings), host.warnings\n"
        "print('OK')\n"
    )
    assert result.returncode == 0, result.stderr
    assert "OK" in result.stdout
