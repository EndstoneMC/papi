"""Endstone PlaceholderAPI.

A native C++20 PlaceholderAPI framework for Endstone and Minecraft Bedrock
Dedicated Server.  Provides a bracket placeholder parser, an owner-aware
expansion registry, and a service that resolves ``{identifier:params}``
placeholders through expansions supplied by C++ or Python plugins.
"""

# Load PAPI's native extension before Endstone's Python bindings so provider
# exceptions are translated by the module that owns them.
try:
    from ._native_loader import load_native as _load_native
    from ._native_loader import should_use_shadow as _should_use_shadow

    if _should_use_shadow(__file__):
        _load_native(__name__, __file__)

    from ._papi import (
        SERVICE_NAME,
        ExpansionInfo,
        ExpansionRegisteredEvent,
        ExpansionUnregisteredEvent,
        PlaceholderAPI,
        PlaceholderExpansion,
        UnregisterReason,
        __version__,
    )
except ImportError as _e:
    raise ImportError(
        f"Failed to load the native PAPI extension: {_e}. "
        "PAPI requires Endstone >=0.11.8,<0.12 (API 0.11). "
        "After updating PAPI or Endstone, restart the server before trying again."
    ) from _e

from .plugin import PlaceholderAPIPlugin

__all__ = [
    "SERVICE_NAME",
    "ExpansionInfo",
    "ExpansionRegisteredEvent",
    "ExpansionUnregisteredEvent",
    "PlaceholderAPI",
    "PlaceholderAPIPlugin",
    "PlaceholderExpansion",
    "UnregisterReason",
    "__version__",
]
