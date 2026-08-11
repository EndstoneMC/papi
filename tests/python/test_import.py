"""Python-side checks on the native module."""

from __future__ import annotations

import importlib

import pytest


def test_native_module_reports_a_version() -> None:
    module = importlib.import_module("endstone_papi._papi")
    assert isinstance(module.__version__, str)
    assert module.__version__


def test_service_name_matches_the_endstone_contract() -> None:
    import endstone_papi

    assert endstone_papi.SERVICE_NAME == "PlaceholderAPI"


def test_service_is_an_endstone_service() -> None:
    from endstone.plugin import Service

    import endstone_papi

    assert issubclass(endstone_papi.PlaceholderAPI, Service)


def test_service_exposes_typed_service_manager_loader() -> None:
    import endstone_papi

    assert callable(endstone_papi.PlaceholderAPI.load)
    with pytest.raises(TypeError):
        endstone_papi.PlaceholderAPI.load(object())


def test_service_cannot_be_constructed_from_python() -> None:
    import endstone_papi

    with pytest.raises(TypeError):
        endstone_papi.PlaceholderAPI()


def test_service_cannot_be_subclassed_from_python() -> None:
    import endstone_papi

    with pytest.raises(TypeError):

        class Rogue(endstone_papi.PlaceholderAPI):
            pass


def test_architecture_a_surface_is_gone() -> None:
    import endstone_papi

    for removed in ("papi", "chars_replacer"):
        with pytest.raises(ImportError):
            importlib.import_module(f"endstone_papi.{removed}")

    assert not hasattr(endstone_papi, "register_placeholder")


def test_plugin_entry_point_is_a_thin_bootstrap() -> None:
    from endstone.plugin import Plugin

    import endstone_papi

    plugin = endstone_papi.PlaceholderAPIPlugin
    assert issubclass(plugin, Plugin)
    assert plugin.api_version == "0.11"

    for framework_method in ("set_placeholders", "register_expansion", "is_registered"):
        assert not hasattr(plugin, framework_method)
