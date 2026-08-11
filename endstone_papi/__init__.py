"""Endstone PlaceholderAPI.

A native C++20 PlaceholderAPI framework for Endstone and Minecraft Bedrock
Dedicated Server.  Provides a bracket placeholder parser, an owner-aware
expansion registry, and a service that resolves ``{identifier_params}``
placeholders through expansions supplied by C++ or Python plugins.
"""

# On Linux manylinux wheels, _papi.so's NEEDED entries are patched at build time
# (tools/repair_wheel.py) from the standard SONAMEs (libc++.so.1,
# libc++abi.so.1) to Endstone's auditwheel-hashed SONAMEs
# (libc++-<hash>.so.1.0, libc++abi-<hash>.so.1.0).  DT_RPATH
# "$ORIGIN/../endstone.libs:$ORIGIN" (set via --disable-new-dtags so it
# propagates to transitive dependencies) resolves these from Endstone's bundled
# copies.  No import-time mutation of site-packages is needed.
#
# We must NOT import endstone._python here: doing so would register Endstone's
# pybind11 translate_exception as the global translator before _papi loads, and
# with -fvisibility=hidden that translator cannot catch std::exception thrown
# from _papi (cross-DSO RTTI mismatch), turning every provider error into "Caught
# an unknown exception!".  _papi's module init imports endstone.plugin itself,
# which triggers endstone._python loading at the right time (after _papi's own
# translator is registered).
try:
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
