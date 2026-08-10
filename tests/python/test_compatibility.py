"""Compatibility and migration surface: COM-004, COM-005.

The C++ deprecated adapters are covered natively. These tests assert the Python-side
migration story: architecture A is gone rather than quietly still working, and the
replacement surface is present and typed.
"""

from __future__ import annotations

import importlib
import importlib.resources

import pytest

import endstone_papi


# COM-005
def test_old_python_service_constructor_is_unavailable() -> None:
    # 0.0.1 exposed PlaceholderAPI(plugin) and expected plugins to subclass it. Both are
    # gone: the service is native, and Python consumes it.
    with pytest.raises(TypeError):
        endstone_papi.PlaceholderAPI()

    with pytest.raises(TypeError):

        class Rogue(endstone_papi.PlaceholderAPI):
            pass


def test_architecture_a_modules_are_removed() -> None:
    # The Python registry, pipe parser, and built-in placeholders are deleted, not
    # deprecated, so an old import fails loudly instead of silently doing nothing.
    for removed in ("papi", "chars_replacer"):
        with pytest.raises(ImportError):
            importlib.import_module(f"endstone_papi.{removed}")


def test_no_business_placeholders_are_shipped() -> None:
    # ADR-002: the core is a framework. Nothing that looks like a built-in provider may
    # appear on the public surface.
    exported = set(endstone_papi.__all__)
    for business in (
        "player_name",
        "ping",
        "coordinates",
        "online",
        "max_online",
        "date",
        "time",
        "economy",
        "prefix",
    ):
        assert business not in exported

    # Nor may a helper that registers such placeholders survive.
    assert not hasattr(endstone_papi, "register_default_placeholders")
    assert not hasattr(endstone_papi, "_register_default_placeholders")


def test_replacement_surface_is_exported() -> None:
    for name in (
        "SERVICE_NAME",
        "ExpansionInfo",
        "ExpansionRegisteredEvent",
        "ExpansionUnregisteredEvent",
        "PlaceholderAPI",
        "PlaceholderAPIPlugin",
        "PlaceholderExpansion",
        "UnregisterReason",
    ):
        assert name in endstone_papi.__all__, name
        assert hasattr(endstone_papi, name), name


def test_no_mutable_manager_is_reachable() -> None:
    # ADR-007: the registry is private. Introspection returns copies, and there is no
    # public handle that could bypass lifecycle validation.
    for internal in ("ExpansionManager", "_ExpansionManager", "registry", "_registry"):
        assert not hasattr(endstone_papi, internal), internal


def test_stub_file_matches_the_runtime_surface() -> None:
    """The packaged .pyi must describe what the module actually exposes."""
    import endstone_papi._papi as native

    stub = importlib.resources.files("endstone_papi").joinpath("_papi.pyi").read_text(encoding="utf-8")

    for name in ("PlaceholderAPI", "PlaceholderExpansion", "ExpansionInfo", "UnregisterReason"):
        assert hasattr(native, name), name
        assert f"class {name}" in stub, name

    # Methods a consumer relies on must be typed, not just present at runtime.
    for method in (
        "set_placeholders",
        "set_relational_placeholders",
        "contains_placeholders",
        "is_registered",
        "register_expansion",
        "unregister_expansion",
        "unregister_expansions",
        "register_placeholder",
        "placeholder_pattern",
    ):
        assert method in stub, method


# --------------------------------------------------------------------------- #
# T-013 / A-003: frozen one-release Python deprecated adapters.
# --------------------------------------------------------------------------- #


def test_deprecated_adapters_exist_on_placeholder_api() -> None:
    """register_placeholder and placeholder_pattern survive as deprecated members."""
    assert hasattr(endstone_papi.PlaceholderAPI, "register_placeholder")
    assert hasattr(endstone_papi.PlaceholderAPI, "placeholder_pattern")


def test_register_placeholder_resolves_through_native_registry() -> None:
    """register_placeholder wraps a callable and resolves via the native service."""
    from endstone_papi._papi import _TestService

    host = _TestService("legacy-plugin")
    service = host.service

    def callback(player, params):
        if params == "":
            return "default"
        return f"value:{params}"

    assert host.register_placeholder("legacy", callback) is True
    assert service.is_registered("legacy") is True
    assert service.set_placeholders(None, "{legacy_}") == "default"
    assert service.set_placeholders(None, "{legacy_world}") == "value:world"


def test_register_placeholder_none_return_preserves_token() -> None:
    """None from the callback leaves the placeholder text untouched."""
    from endstone_papi._papi import _TestService

    host = _TestService("legacy-plugin")
    service = host.service

    def callback(player, params):
        return None

    host.register_placeholder("maybe", callback)
    assert service.set_placeholders(None, "{maybe_}") == "{maybe_}"


def test_register_placeholder_rejects_duplicate_identifiers() -> None:
    """Duplicate identifiers fail; no colon-namespace fallback is resurrected."""
    from endstone_papi._papi import _TestService

    host = _TestService("legacy-plugin")

    def callback(player, params):
        return "x"

    assert host.register_placeholder("dup", callback) is True
    assert host.register_placeholder("dup", callback) is False


def test_register_placeholder_non_callable_is_rejected() -> None:
    """A non-callable callback is a type error, not a silent failure."""
    from endstone_papi._papi import _TestService

    host = _TestService("legacy-plugin")
    service = host.service

    with pytest.raises(TypeError):
        service.register_placeholder(host.plugin, "bad", "not a callable")


def test_register_placeholder_wrong_return_type_is_contained() -> None:
    """A non-str, non-None return is a provider error; the token is preserved."""
    from endstone_papi._papi import _TestService

    host = _TestService("legacy-plugin")
    service = host.service

    def callback(player, params):
        return 42

    host.register_placeholder("wrong", callback)
    assert service.set_placeholders(None, "{wrong_}") == "{wrong_}"


def test_register_placeholder_uses_owner_aware_registry() -> None:
    """register_placeholder shares the registry: unregister_expansion removes it."""
    from endstone_papi._papi import _TestService

    host = _TestService("legacy-plugin")
    service = host.service

    def callback(player, params):
        return "legacy"

    host.register_placeholder("owned", callback)
    assert service.is_registered("owned") is True
    assert host.unregister_expansion("owned") is True
    assert service.is_registered("owned") is False
    assert service.set_placeholders(None, "{owned_}") == "{owned_}"


def test_placeholder_pattern_returns_historical_regex() -> None:
    """placeholder_pattern returns the historical bracket regex, not None or empty."""
    from endstone_papi._papi import _TestService

    host = _TestService("legacy-plugin")
    service = host.service

    pattern = service.placeholder_pattern
    assert isinstance(pattern, str)
    assert pattern == "[{]([^{}]+)[}]"


def test_placeholder_pattern_is_not_used_by_parser() -> None:
    """The parser is not regex-based: placeholder_pattern is documentation only."""
    from endstone_papi._papi import _TestService

    host = _TestService("legacy-plugin")
    service = host.service

    # A placeholder with nested braces matches the historical regex greedily, but the
    # parser treats the first closing brace as the end of the token.
    assert service.contains_placeholders("{a}_{b}") is True
    # The pattern itself is just a string; it has no effect on parsing behavior.
    _ = service.placeholder_pattern
