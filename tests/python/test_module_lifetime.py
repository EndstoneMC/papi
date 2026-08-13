"""Verifies the module-unload assumption the inert-service design depends on.

A consumer may keep a service reference after PAPI is disabled, on the condition that
the extension binary providing that object's vtable stays loaded. Endstone's Python
loader invalidates ``endstone_*`` imports on reload by deleting them from ``sys.modules``,
so this asserts what that actually does to the loaded OS module: if deleting and
collecting the extension unloaded the binary, a retained inert service would become a
dangling vtable and the design would be unsound.

The check runs in a subprocess so the observation is made in a fresh interpreter, with
the module freshly imported and nothing else holding it.
"""

from __future__ import annotations

import subprocess
import sys
import sysconfig
import textwrap

import pytest

# The probe manipulates sys.modules and forces collection, which would corrupt the state
# of the test session it ran in.
PROBE = textwrap.dedent(
    """
    import ctypes
    import gc
    import sys


    def module_handle(name):
        # GetModuleHandleW does not take a reference, so a non-zero result means the
        # binary is still mapped.
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.GetModuleHandleW.restype = ctypes.c_void_p
        kernel32.GetModuleHandleW.argtypes = [ctypes.c_wchar_p]
        return kernel32.GetModuleHandleW(name) or 0


    import endstone_papi
    import endstone_papi._papi as native

    filename = native.__file__.rsplit("\\\\", 1)[-1]
    before = module_handle(filename)
    if before == 0:
        print("SKIP")
        raise SystemExit(0)

    # Reproduce what Endstone's Python loader does on reload.
    for module in [m for m in sys.modules if m.startswith("endstone_papi")]:
        del sys.modules[module]
    del endstone_papi
    del native
    for _ in range(3):
        gc.collect()

    after = module_handle(filename)
    print("UNLOADED" if after == 0 else "STILL_LOADED")
    """
)


@pytest.mark.skipif(sysconfig.get_platform().startswith("win") is False, reason="probe uses the Win32 loader API")
def test_extension_binary_survives_import_invalidation() -> None:
    result = subprocess.run(
        [sys.executable, "-X", "utf8", "-c", PROBE],
        capture_output=True,
        text=True,
        timeout=120,
        check=False,
    )
    assert result.returncode == 0, result.stderr

    verdict = result.stdout.strip().splitlines()[-1]
    if verdict == "SKIP":
        pytest.skip("could not resolve the extension module handle by name")

    # If this ever reports UNLOADED, a retained service can no longer be made safely inert
    # on this platform.
    assert verdict == "STILL_LOADED", (
        "the extension binary was unloaded after import invalidation, so a consumer's "
        "retained inert service would dangle"
    )
