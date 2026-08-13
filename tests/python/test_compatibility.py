"""Compatibility and migration surface.

These tests assert that removed 0.0.1 surfaces stay absent and the Architecture B
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

    # Members a consumer relies on must be typed, not just present at runtime.
    for member in (
        "load",
        "active",
        "set_placeholders",
        "set_relational_placeholders",
        "contains_placeholders",
        "is_registered",
        "registered_identifiers",
        "expansions",
        "register_expansion",
        "unregister_expansion",
        "unregister_expansions",
    ):
        assert member in stub, member

    for removed in ("register_placeholder", "placeholder_pattern"):
        assert removed not in stub, removed


def test_legacy_adapters_are_absent_from_placeholder_api() -> None:
    for removed in ("register_placeholder", "placeholder_pattern"):
        assert not hasattr(endstone_papi.PlaceholderAPI, removed), removed
