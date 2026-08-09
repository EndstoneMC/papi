"""Smoke tests establishing the Phase 0 pytest baseline."""

from __future__ import annotations

import importlib


def test_native_module_reports_a_version() -> None:
    module = importlib.import_module("endstone_papi._papi")
    assert isinstance(module.__version__, str)
    assert module.__version__
