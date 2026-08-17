"""Trampoline super()/.member re-entry is bounded.

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

from endstone_papi import PlaceholderExpansion, UnregisterReason

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
    class SuperIdentifier(PlaceholderExpansion):
        @property
        def identifier(self) -> str:
            return super().identifier  # type: ignore[no-any-return]

        author = "a"
        version = "1"

        def on_request(self, player, params):
            return None

    expansion = SuperIdentifier()
    with pytest.raises((RuntimeError, RecursionError)):
        _ = expansion.identifier


def test_super_name_reentry_falls_back_to_identifier() -> None:
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
    assert expansion.name == "demo"


def test_super_can_register_reentry_is_bounded() -> None:
    class SuperCanRegister(PlaceholderExpansion):
        identifier = "demo"
        author = "a"
        version = "1"

        def can_register(self) -> bool:
            return super().can_register()

        def on_request(self, player, params):
            return None

    expansion = SuperCanRegister()
    assert expansion.can_register() is True


def test_super_on_request_reentry_is_bounded() -> None:
    class SuperOnRequest(PlaceholderExpansion):
        identifier = "demo"
        author = "a"
        version = "1"

        def on_request(self, player, params):
            return super().on_request(player, params)

    expansion = SuperOnRequest()
    with pytest.raises((RuntimeError, RecursionError)):
        _ = expansion.on_request(None, "x")


def test_subprocess_super_identifier_does_not_crash() -> None:
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


def test_two_and_three_level_inheritance_dispatch_exactly_once() -> None:
    from endstone_papi._papi import _TestService

    calls: list[str] = []

    class Parent(PlaceholderExpansion):
        identifier = "multi"
        author = "a"
        version = "1"

        def on_request(self, player, params):
            calls.append("parent")
            return f"parent:{params}"

    class Child(Parent):
        def on_request(self, player, params):
            calls.append("child")
            return f"{super().on_request(player, params)}:child"

    class Grandchild(Child):
        def on_request(self, player, params):
            calls.append("grandchild")
            return f"{super().on_request(player, params)}:grandchild"

    child_host = _TestService("child-test")
    assert child_host.register_expansion(Child())
    assert child_host.service.set_placeholders(None, "{multi:x}") == "parent:x:child"
    assert calls == ["child", "parent"]

    calls.clear()
    grandchild_host = _TestService("grandchild-test")
    assert grandchild_host.register_expansion(Grandchild())
    assert grandchild_host.service.set_placeholders(None, "{multi:y}") == "parent:y:child:grandchild"
    assert calls == ["grandchild", "child", "parent"]


def test_mixin_descriptor_and_custom_getattribute_dispatch() -> None:
    from endstone_papi._papi import _TestService

    descriptor_reads = 0
    attribute_reads: list[str] = []

    class IdentifierDescriptor:
        def __get__(self, instance, owner):
            nonlocal descriptor_reads
            descriptor_reads += 1
            return "adversarial"

    class MetadataMixin:
        name = "Mixin Name"
        required_plugin = None

    class Mixed(MetadataMixin, PlaceholderExpansion):
        identifier = IdentifierDescriptor()
        author = "mixin-author"
        version = "1"

        def __getattribute__(self, name):
            if name in {"identifier", "author", "version", "name", "on_request"}:
                attribute_reads.append(name)
            return super().__getattribute__(name)

        def on_request(self, player, params):
            return f"mixed:{params}"

    host = _TestService("mro-test")
    assert host.register_expansion(Mixed())
    assert host.service.set_placeholders(None, "{adversarial:x}") == "mixed:x"
    [info] = host.service.expansions
    assert info.name == "Mixin Name"
    assert descriptor_reads == 1
    assert attribute_reads.count("identifier") == 1
    assert attribute_reads.count("author") == 1
    assert attribute_reads.count("version") == 1
    assert attribute_reads.count("name") == 1
    assert attribute_reads.count("on_request") == 1


def test_all_optional_virtuals_and_callbacks_use_subclass_overrides() -> None:
    from endstone_papi._papi import _TestService

    calls: list[object] = []

    class Complete(PlaceholderExpansion):
        identifier = "complete"
        author = "a"
        version = "1"
        name = "Complete Expansion"
        required_plugin = None

        def can_register(self):
            calls.append("can_register")
            return True

        def supports_relational_placeholders(self):
            calls.append("supports_relational")
            return True

        def supports_player_cleanup(self):
            calls.append("supports_cleanup")
            return True

        def on_request(self, player, params):
            calls.append(("ordinary", params))
            return f"ordinary:{params}"

        def on_relational_request(self, one, two, params):
            calls.append(("relational", one.name, two.name, params))
            return f"{one.name}+{two.name}:{params}"

        def on_player_quit(self, player):
            calls.append(("quit", player.name))

        def on_unregister(self, reason):
            calls.append(("unregister", reason))

    host = _TestService("optional-test")
    assert host.register_expansion(Complete())
    assert host.service.set_placeholders(None, "{complete:x}") == "ordinary:x"
    assert host.set_relational_placeholders("{rel:complete:since}") == "Alice+Bob:since"
    host.handle_player_quit()
    assert host.unregister_expansion("complete")

    assert calls.count("can_register") == 1
    assert calls.count("supports_relational") == 1
    assert calls.count("supports_cleanup") == 1
    assert [call for call in calls if isinstance(call, tuple)] == [
        ("ordinary", "x"),
        ("relational", "Alice", "Bob", "since"),
        ("quit", "Alice"),
        ("unregister", UnregisterReason.EXPLICIT),
    ]


def test_dispatch_guard_distinguishes_independent_instances() -> None:
    calls: list[str] = []

    class Nested(PlaceholderExpansion):
        author = "a"
        version = "1"

        def __init__(self, identifier, nested=None):
            super().__init__()
            self._identifier = identifier
            self.nested = nested

        @property
        def identifier(self):
            return self._identifier

        def on_request(self, player, params):
            calls.append(self._identifier)
            if self.nested is None:
                return f"{self._identifier}:{params}"
            value = PlaceholderExpansion.on_request(self.nested, player, params)
            return f"{self._identifier}>{value}"

    second = Nested("second")
    first = Nested("first", second)
    assert PlaceholderExpansion.on_request(first, None, "x") == "first>second:x"
    assert calls == ["first", "second"]


def test_exception_unwind_clears_guard_for_reuse() -> None:
    from endstone_papi._papi import _TestService

    class Flaky(PlaceholderExpansion):
        identifier = "flaky"
        author = "a"
        version = "1"

        def __init__(self):
            super().__init__()
            self.calls = 0

        def on_request(self, player, params):
            self.calls += 1
            if self.calls == 1:
                raise ValueError("first call fails")
            return f"recovered:{params}"

    expansion = Flaky()
    host = _TestService("unwind-test")
    assert host.register_expansion(expansion)
    assert host.service.set_placeholders(None, "{flaky:x}") == "{flaky:x}"
    assert host.service.set_placeholders(None, "{flaky:x}") == "recovered:x"
    assert expansion.calls == 2


def test_subprocess_descriptor_and_getattribute_reentry_are_bounded() -> None:
    result = _run_subprocess(
        "class ReenteringDescriptor:\n"
        "    def __get__(self, instance, owner):\n"
        "        return PlaceholderExpansion.identifier.__get__(instance)\n"
        "class DescriptorExpansion(PlaceholderExpansion):\n"
        "    identifier = ReenteringDescriptor()\n"
        "    author = 'a'\n"
        "    version = '1'\n"
        "    def on_request(self, player, params):\n"
        "        return None\n"
        "class GetattributeExpansion(PlaceholderExpansion):\n"
        "    identifier = 'demo'\n"
        "    author = 'a'\n"
        "    version = '1'\n"
        "    def __getattribute__(self, name):\n"
        "        if name == 'identifier':\n"
        "            return PlaceholderExpansion.identifier.__get__(self)\n"
        "        return super().__getattribute__(name)\n"
        "    def on_request(self, player, params):\n"
        "        return None\n"
        "for expansion in (DescriptorExpansion(), GetattributeExpansion()):\n"
        "    try:\n"
        "        PlaceholderExpansion.identifier.__get__(expansion)\n"
        "        print('RESULT: no error')\n"
        "    except (RuntimeError, RecursionError) as error:\n"
        "        print('RESULT: contained', type(error).__name__)\n"
    )
    assert result.returncode == 0, f"process crashed: {result.stderr}"
    assert result.stdout.count("RESULT: contained") == 2, result.stdout + result.stderr
