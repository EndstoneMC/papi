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
        "try:\n"
        "    import resource\n"
        "    limit = 1024 * 1024 * 1024\n"
        "    resource.setrlimit(resource.RLIMIT_AS, (limit, limit))\n"
        "except (ImportError, OSError, ValueError):\n"
        "    pass\n"
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


def test_three_expansion_cycle_is_bounded_and_guard_is_reusable() -> None:
    """A -> B -> C -> A is contained, then all guard state is cleared."""
    result = _run_subprocess(
        "host = _TestService('three-cycle')\n"
        "service = host.service\n"
        "state = {'cycle': True}\n"
        "class A(PlaceholderExpansion):\n"
        "    identifier = 'a'\n"
        "    author = 't'\n"
        "    version = '1'\n"
        "    def on_request(self, player, params):\n"
        "        return service.set_placeholders(player, '{b_x}') if state['cycle'] else 'recovered'\n"
        "class B(PlaceholderExpansion):\n"
        "    identifier = 'b'\n"
        "    author = 't'\n"
        "    version = '1'\n"
        "    def on_request(self, player, params):\n"
        "        return service.set_placeholders(player, '{c_x}')\n"
        "class C(PlaceholderExpansion):\n"
        "    identifier = 'c'\n"
        "    author = 't'\n"
        "    version = '1'\n"
        "    def on_request(self, player, params):\n"
        "        return service.set_placeholders(player, '{a_x}')\n"
        "for expansion in (A(), B(), C()):\n"
        "    assert host.register_expansion(expansion)\n"
        "assert service.set_placeholders(None, '{a_x}') == '{a_x}'\n"
        "assert any('cycle' in warning for warning in host.warnings), host.warnings\n"
        "state['cycle'] = False\n"
        "assert service.set_placeholders(None, '{a_x}') == 'recovered'\n"
        "print('OK')\n"
    )
    assert result.returncode == 0, result.stderr
    assert "OK" in result.stdout


def test_ordinary_relational_ordinary_cycle_is_bounded() -> None:
    """An ordinary callback may not cycle back through relational dispatch."""
    result = _run_subprocess(
        "host = _TestService('ordinary-relational')\n"
        "service = host.service\n"
        "class OrdinaryA(PlaceholderExpansion):\n"
        "    identifier = 'a'\n"
        "    author = 't'\n"
        "    version = '1'\n"
        "    def on_request(self, player, params):\n"
        "        return host.set_relational_placeholders('{rel_b_x}')\n"
        "class RelationalB(PlaceholderExpansion):\n"
        "    identifier = 'b'\n"
        "    author = 't'\n"
        "    version = '1'\n"
        "    def supports_relational_placeholders(self):\n"
        "        return True\n"
        "    def on_request(self, player, params):\n"
        "        return None\n"
        "    def on_relational_request(self, one, two, params):\n"
        "        return service.set_placeholders(one, '{a_x}')\n"
        "assert host.register_expansion(OrdinaryA())\n"
        "assert host.register_expansion(RelationalB())\n"
        "assert service.set_placeholders(None, '{a_x}') == '{a_x}'\n"
        "assert any('cycle' in warning for warning in host.warnings), host.warnings\n"
        "print('OK')\n"
    )
    assert result.returncode == 0, result.stderr
    assert "OK" in result.stdout


def test_relational_ordinary_relational_cycle_is_bounded() -> None:
    """A relational callback may not cycle back through ordinary dispatch."""
    result = _run_subprocess(
        "host = _TestService('relational-ordinary')\n"
        "service = host.service\n"
        "class RelationalA(PlaceholderExpansion):\n"
        "    identifier = 'a'\n"
        "    author = 't'\n"
        "    version = '1'\n"
        "    def supports_relational_placeholders(self):\n"
        "        return True\n"
        "    def on_request(self, player, params):\n"
        "        return None\n"
        "    def on_relational_request(self, one, two, params):\n"
        "        return service.set_placeholders(one, '{b_x}')\n"
        "class OrdinaryB(PlaceholderExpansion):\n"
        "    identifier = 'b'\n"
        "    author = 't'\n"
        "    version = '1'\n"
        "    def on_request(self, player, params):\n"
        "        return host.set_relational_placeholders('{rel_a_x}')\n"
        "assert host.register_expansion(RelationalA())\n"
        "assert host.register_expansion(OrdinaryB())\n"
        "assert host.set_relational_placeholders('{rel_a_x}') == '{rel_a_x}'\n"
        "assert any('cycle' in warning for warning in host.warnings), host.warnings\n"
        "print('OK')\n"
    )
    assert result.returncode == 0, result.stderr
    assert "OK" in result.stdout


def test_provider_exception_while_guarded_does_not_poison_next_parse() -> None:
    """The active-expansion guard is removed when provider dispatch throws."""
    from endstone_papi import PlaceholderExpansion
    from endstone_papi._papi import _TestService

    class Flaky(PlaceholderExpansion):
        identifier = "flaky-parse"
        author = "t"
        version = "1"

        def __init__(self):
            super().__init__()
            self.calls = 0

        def on_request(self, player, params):
            self.calls += 1
            if self.calls == 1:
                raise RuntimeError("guarded failure")
            return "recovered"

    host = _TestService("guard-exception")
    expansion = Flaky()
    assert host.register_expansion(expansion)
    assert host.service.set_placeholders(None, "{flaky-parse_x}") == "{flaky-parse_x}"
    assert host.service.set_placeholders(None, "{flaky-parse_x}") == "recovered"
    assert expansion.calls == 2


def test_self_unregister_during_guarded_callback_discards_result_and_allows_reregister() -> None:
    """Retirement inside a callback defers destruction but clears parse state."""
    from endstone_papi import PlaceholderExpansion
    from endstone_papi._papi import _TestService

    calls: list[str] = []
    host = _TestService("self-unregister")

    class SelfRemoving(PlaceholderExpansion):
        identifier = "self-remove"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            calls.append("old")
            assert host.unregister_expansion("self-remove")
            return "retired-value"

    class Replacement(PlaceholderExpansion):
        identifier = "self-remove"
        author = "t"
        version = "2"

        def on_request(self, player, params):
            calls.append("new")
            return "replacement-value"

    assert host.register_expansion(SelfRemoving())
    assert host.service.set_placeholders(None, "{self-remove_x}") == "{self-remove_x}"
    assert not host.service.is_registered("self-remove")
    assert host.register_expansion(Replacement())
    assert host.service.set_placeholders(None, "{self-remove_x}") == "replacement-value"
    assert calls == ["old", "new"]


def test_legitimate_finite_nested_parse_resolves_without_rescanning_output() -> None:
    """Finite explicit nesting is allowed; replacement output remains single-pass."""
    from endstone_papi import PlaceholderExpansion
    from endstone_papi._papi import _TestService

    host = _TestService("finite-nesting")
    service = host.service

    class Outer(PlaceholderExpansion):
        identifier = "outer"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            return service.set_placeholders(player, "{inner_x}")

    class Inner(PlaceholderExpansion):
        identifier = "inner"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            return "leaf:{unresolved_x}"

    assert host.register_expansion(Outer())
    assert host.register_expansion(Inner())
    assert service.set_placeholders(None, "{outer_x}") == "leaf:{unresolved_x}"
    assert host.warnings == []
