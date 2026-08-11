"""Endstone PlaceholderAPI.

The registry, parser, and service implementation are native. This package provides
the Endstone plugin bootstrap plus the Python view of the native contract.

Consumers load the service from Endstone rather than constructing it:

    service = self.server.service_manager.load("PlaceholderAPI")
    if service is not None and service.active:
        service.register_expansion(self, MyExpansion())
        print(service.set_placeholders(player, "{demo_name}"))
"""

# On Linux manylinux wheels, _papi.so has NEEDED entries for the standard
# SONAMEs libc++.so.1 and libc++abi.so.1.  Endstone bundles these under
# auditwheel-hashed SONAMEs (libc++-<hash>.so.1.0, libc++abi-<hash>.so.1.0),
# so the dynamic linker cannot resolve the standard SONAMEs to Endstone's
# copies without help.  If a system libc++.so.1 / libc++abi.so.1 exists (e.g.
# an older LLVM package), the linker would load *second* instances alongside
# Endstone's, causing a cross-DSO RTTI mismatch and a segfault.
#
# We solve this in two steps:
#
#  1. Preload Endstone's bundled libc++ stack (libunwind -> libc++abi ->
#     libc++) with RTLD_GLOBAL so that the transitive dependencies are
#     already in the link map when _papi.so is loaded.
#
#  2. Create standard-SONAME symlinks (libc++.so.1, libc++abi.so.1) in this
#     package directory pointing to Endstone's hashed copies.  Combined with
#     the $ORIGIN RUNPATH baked into _papi.so, the dynamic linker resolves
#     the NEEDED entries to the already-loaded Endstone copies instead of
#     searching system paths.
#
# We must NOT import endstone._python here: doing so would register Endstone's
# pybind11 translate_exception as the global translator, and with
# -fvisibility=hidden that translator cannot catch std::exception thrown from
# _papi (cross-DSO RTTI mismatch), turning every provider error into "Caught
# an unknown exception!".  Preloading only the shared libraries leaves _papi
# as the first pybind11 module, so its own translator handles its own
# exceptions.
import sys

if sys.platform.startswith("linux"):
    import ctypes
    import os

    try:
        import endstone

        _libs_dir = os.path.join(os.path.dirname(os.path.dirname(endstone.__file__)), "endstone.libs")
        if os.path.isdir(_libs_dir):
            # Preload in dependency order: libunwind -> libc++abi -> libc++.
            # Each library has no RPATH, so its NEEDED entries can only be
            # resolved from already-loaded libraries or system paths.
            for _prefix in ("libunwind-", "libc++abi-", "libc++-"):
                for _name in sorted(os.listdir(_libs_dir)):
                    if _name.startswith(_prefix) and ".so." in _name:
                        try:
                            ctypes.CDLL(
                                os.path.join(_libs_dir, _name),
                                mode=ctypes.RTLD_GLOBAL,
                            )
                        except OSError:
                            pass
                        break

            # Create standard-SONAME symlinks so _papi's NEEDED libc++.so.1
            # and libc++abi.so.1 resolve to Endstone's copies via the $ORIGIN
            # RUNPATH.
            _papi_dir = os.path.dirname(__file__)
            for _soname, _prefix in (
                ("libc++.so.1", "libc++-"),
                ("libc++abi.so.1", "libc++abi-"),
            ):
                for _name in sorted(os.listdir(_libs_dir)):
                    if _name.startswith(_prefix) and ".so." in _name:
                        _target = os.path.join(_libs_dir, _name)
                        _link = os.path.join(_papi_dir, _soname)
                        try:
                            if os.path.islink(_link) or os.path.lexists(_link):
                                os.remove(_link)
                            os.symlink(_target, _link)
                        except OSError:
                            pass
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
