"""PEP 517 backend enforcing PAPI's Linux runtime contract for every wheel build."""

from __future__ import annotations

import shutil
import sys
import tempfile
from pathlib import Path
from typing import Any

from scikit_build_core_conan import build as _delegate


def _delegate_hook(name: str, *args: Any, **kwargs: Any) -> Any:
    return getattr(_delegate, name)(*args, **kwargs)


def build_wheel(
    wheel_directory: str,
    config_settings: dict[str, list[str] | str] | None = None,
    metadata_directory: str | None = None,
) -> str:
    destination = Path(wheel_directory).resolve()
    destination.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="papi-wheel-") as temporary_directory:
        temporary = Path(temporary_directory)
        wheel_name = _delegate.build_wheel(str(temporary), config_settings, metadata_directory)
        wheel = temporary / wheel_name

        if sys.platform.startswith("linux"):
            from tools.repair_wheel import repair_wheel

            return repair_wheel(wheel, destination).name

        shutil.move(str(wheel), destination / wheel.name)
        return wheel.name


def build_sdist(
    sdist_directory: str,
    config_settings: dict[str, list[str] | str] | None = None,
) -> str:
    return _delegate.build_sdist(sdist_directory, config_settings)


def build_editable(
    wheel_directory: str,
    config_settings: dict[str, list[str] | str] | None = None,
    metadata_directory: str | None = None,
) -> str:
    return _delegate.build_editable(wheel_directory, config_settings, metadata_directory)


def get_requires_for_build_wheel(config_settings: dict[str, list[str] | str] | None = None) -> list[str]:
    return _delegate_hook("get_requires_for_build_wheel", config_settings)


def get_requires_for_build_sdist(config_settings: dict[str, list[str] | str] | None = None) -> list[str]:
    return _delegate_hook("get_requires_for_build_sdist", config_settings)


def get_requires_for_build_editable(config_settings: dict[str, list[str] | str] | None = None) -> list[str]:
    return _delegate_hook("get_requires_for_build_editable", config_settings)


def prepare_metadata_for_build_wheel(
    metadata_directory: str,
    config_settings: dict[str, list[str] | str] | None = None,
) -> str:
    return _delegate.prepare_metadata_for_build_wheel(metadata_directory, config_settings)


def prepare_metadata_for_build_editable(
    metadata_directory: str,
    config_settings: dict[str, list[str] | str] | None = None,
) -> str:
    return _delegate.prepare_metadata_for_build_editable(metadata_directory, config_settings)
