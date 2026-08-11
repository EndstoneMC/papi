"""Endstone PlaceholderAPI.

A native C++20 PlaceholderAPI framework for Endstone and Minecraft Bedrock
Dedicated Server.  Provides a bracket placeholder parser, an owner-aware
expansion registry, and a service that resolves ``{identifier_params}``
placeholders through expansions supplied by C++ or Python plugins.
"""

# On Linux manylinux wheels, _papi.so has NEEDED entries for the standard
# SONAMEs libc++.so.1 and libc++abi.so.1.  Endstone bundles these under
# auditwheel-hashed SONAMEs (libc++-<hash>.so.1.0, libc++abi-<hash>.so.1.0)
# in endstone.libs/, so the dynamic linker cannot resolve the standard SONAMEs
# to Endstone's copies without help.
#
# _papi.so uses DT_RPATH (not DT_RUNPATH) set to "$ORIGIN/../endstone.libs:$ORIGIN".
# DT_RPATH propagates to transitive dependencies, so libc++'s own NEEDED entries
# (which use the hashed SONAMEs) are resolved directly from endstone.libs/.
# The standard-SONAME NEEDED entries (libc++.so.1, libc++abi.so.1) are resolved
# via symlinks we create here in the package directory ($ORIGIN).
#
# We must NOT import endstone._python here: doing so would register Endstone's
# pybind11 translate_exception as the global translator before _papi loads, and
# with -fvisibility=hidden that translator cannot catch std::exception thrown
# from _papi (cross-DSO RTTI mismatch), turning every provider error into "Caught
# an unknown exception!".  _papi's module init imports endstone.plugin itself,
# which triggers endstone._python loading at the right time (after _papi's own
# translator is registered).
import os
import sys

if sys.platform.startswith("linux"):
    _PAPI_DEBUG = bool(os.environ.get("PAPI_DEBUG"))

    def _papi_log(msg: str) -> None:
        if _PAPI_DEBUG:
            print(f"[PAPI] {msg}", file=sys.stderr, flush=True)

    try:
        import endstone

        _libs_dir = os.path.join(os.path.dirname(os.path.dirname(endstone.__file__)), "endstone.libs")
        _papi_dir = os.path.dirname(__file__)
        _papi_log(f"endstone.__file__={endstone.__file__}")
        _papi_log(f"libs_dir={_libs_dir} exists={os.path.isdir(_libs_dir)}")
        _papi_log(f"papi_dir={_papi_dir}")
        _papi_log(f"LD_LIBRARY_PATH={os.environ.get('LD_LIBRARY_PATH', '')}")

        if os.path.isdir(_libs_dir):
            # Create standard-SONAME symlinks so _papi's NEEDED libc++.so.1
            # and libc++abi.so.1 resolve to Endstone's hashed copies via the
            # $ORIGIN entry in _papi's DT_RPATH.  The transitive dependencies
            # (hashed SONAMEs) are resolved by the propagated DT_RPATH entry
            # pointing directly at endstone.libs/.
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
                            _papi_log(f"symlink {_soname} -> {_target}")
                        except OSError as _e:
                            _papi_log(f"symlink {_soname} FAILED: {_e}")
                        break
                else:
                    _papi_log(f"no hashed lib found for prefix {_prefix!r} in {_libs_dir}")
        else:
            _papi_log(f"endstone.libs not found at {_libs_dir}")
    except (ImportError, OSError) as _e:
        _papi_log(f"setup failed: {_e}")

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
