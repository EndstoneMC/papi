"""T-003 regression: the GIL-safe proxy is ownership-only.

The proxy constructor must not read metadata from the wrapped Python expansion. Every
metadata, capability, and preflight call must land behind the native core's canOperate
gate and invokeProvider containment, so off-thread or inactive-service registration
performs zero provider callbacks and a metadata exception surfaces as an atomic
registration failure rather than an uncontained Python error.

These tests construct the proxy directly through a test-only binding that is compiled
out of release wheels (``BUILD_TESTING=OFF``). No running server is needed.
"""

from __future__ import annotations

import pytest

from endstone_papi import PlaceholderExpansion

try:
    from endstone_papi._papi import _test_make_proxy
except ImportError:
    pytest.skip("_test_make_proxy test binding requires BUILD_TESTING=ON", allow_module_level=True)


class CountingExpansion(PlaceholderExpansion):
    """Records every metadata read so the proxy's ownership-only contract can be checked."""

    def __init__(self) -> None:
        super().__init__()
        self.reads: list[str] = []

    @property
    def identifier(self) -> str:
        self.reads.append("identifier")
        return "demo"

    @property
    def author(self) -> str:
        self.reads.append("author")
        return "author"

    @property
    def version(self) -> str:
        self.reads.append("version")
        return "1.0.0"

    @property
    def name(self) -> str:
        self.reads.append("name")
        return "demo"

    @property
    def required_plugin(self):
        self.reads.append("required_plugin")
        # Implicit None: required_plugin is optional and this expansion has none.

    def can_register(self) -> bool:
        self.reads.append("can_register")
        return True

    def supports_relational_placeholders(self) -> bool:
        self.reads.append("relational")
        return False

    def supports_player_cleanup(self) -> bool:
        self.reads.append("player_cleanup")
        return False

    def on_request(self, player, params):
        return None


def test_proxy_construction_reads_no_metadata() -> None:
    """The proxy constructor is ownership-only: zero provider callbacks."""
    expansion = CountingExpansion()
    proxy = _test_make_proxy(expansion)
    assert expansion.reads == []
    # Proxy is usable (not None), confirming construction succeeded.
    assert proxy is not None


def test_proxy_forwards_metadata_lazily() -> None:
    """Metadata is read through the virtual methods, not cached in the constructor."""
    expansion = CountingExpansion()
    proxy = _test_make_proxy(expansion)
    assert expansion.reads == []

    assert proxy.identifier == "demo"
    assert proxy.author == "author"
    assert proxy.version == "1.0.0"
    assert proxy.name == "demo"
    assert proxy.required_plugin is None
    assert proxy.can_register() is True
    assert proxy.supports_relational_placeholders() is False
    assert proxy.supports_player_cleanup() is False

    assert expansion.reads == [
        "identifier",
        "author",
        "version",
        "name",
        "required_plugin",
        "can_register",
        "relational",
        "player_cleanup",
    ]


def test_proxy_contains_metadata_exceptions() -> None:
    """A Python exception in a metadata method surfaces as RuntimeError, not the original."""

    class ThrowingExpansion(PlaceholderExpansion):
        @property
        def identifier(self) -> str:
            raise ValueError("metadata boom")

        @property
        def author(self) -> str:
            return "author"

        @property
        def version(self) -> str:
            return "1.0.0"

        def on_request(self, player, params):
            return None

    expansion = ThrowingExpansion()
    proxy = _test_make_proxy(expansion)

    with pytest.raises(RuntimeError, match="metadata boom"):
        _ = proxy.identifier


def test_proxy_works_with_class_attribute_metadata() -> None:
    """The common pattern (class attributes, not properties) still works through the proxy."""

    class SimpleExpansion(PlaceholderExpansion):
        identifier = "simple"
        author = "Endstone"
        version = "1.0.0"

        def on_request(self, player, params):
            return "ok"

    expansion = SimpleExpansion()
    proxy = _test_make_proxy(expansion)

    assert proxy.identifier == "simple"
    assert proxy.author == "Endstone"
    assert proxy.version == "1.0.0"
    # Name defaults to the identifier.
    assert proxy.name == "simple"
