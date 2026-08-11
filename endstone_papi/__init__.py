"""Endstone PlaceholderAPI.

The registry, parser, and service implementation are native. This package provides
the Endstone plugin bootstrap plus the Python view of the native contract.

Consumers load the service from Endstone rather than constructing it:

    service = self.server.service_manager.load("PlaceholderAPI")
    if service is not None and service.active:
        service.register_expansion(self, MyExpansion())
        print(service.set_placeholders(player, "{demo_name}"))
"""

# On Linux manylinux wheels, _papi.so has a NEEDED entry for libc++.so.1 that
# the dynamic linker must resolve before the module init runs.  The smoke-test
# runner (ubuntu-22.04) has no system libc++, so we preload the copy bundled in
# endstone.libs/ via ctypes.  We must NOT import endstone._python here: doing so
# would register endstone's pybind11 translate_exception as the global
# translator, and with -fvisibility=hidden that translator cannot catch
# std::exception thrown from _papi (cross-DSO RTTI mismatch), turning every
# provider error into "Caught an unknown exception!".  Preloading only the
# shared library leaves _papi as the first pybind11 module, so its own
# translator handles its own exceptions.
import sys

if sys.platform.startswith("linux"):
    import ctypes
    import os

    try:
        import endstone

        _libs_dir = os.path.join(os.path.dirname(os.path.dirname(endstone.__file__)), "endstone.libs")
        if os.path.isdir(_libs_dir):
            for _name in sorted(os.listdir(_libs_dir)):
                if _name.startswith("libc++") and ".so." in _name:
                    ctypes.CDLL(os.path.join(_libs_dir, _name))
                    break
    except (ImportError, OSError):
        pass

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
