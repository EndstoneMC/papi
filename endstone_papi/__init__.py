"""Endstone PlaceholderAPI.

A native C++20 PlaceholderAPI framework for Endstone and Minecraft Bedrock
Dedicated Server.  Provides a bracket placeholder parser, an owner-aware
expansion registry, and a service that resolves ``{identifier_params}``
placeholders through expansions supplied by C++ or Python plugins.
"""

# Linux wheels contain build-time-created standard-SONAME bridge DSOs beside
# _papi. They forward to Endstone's auditwheel-hashed libc++/libc++abi through
# origin-relative RPATHs. Endstone owns the one runtime stack; this package does
# not mutate site-packages or select a system runtime at import time.
#
# We must NOT import endstone._python here: doing so would register Endstone's
# pybind11 translate_exception as the global translator before _papi loads, and
# with -fvisibility=hidden that translator cannot catch std::exception thrown
# from _papi (cross-DSO RTTI mismatch), turning every provider error into "Caught
# an unknown exception!".  _papi's module init imports endstone.plugin itself,
# which triggers endstone._python loading at the right time (after _papi's own
# translator is registered).
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
        "This usually means the Endstone C++ runtime is not available or "
        "the installed Endstone version is incompatible. "
        "PAPI requires endstone>=0.11.8,<0.12."
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
