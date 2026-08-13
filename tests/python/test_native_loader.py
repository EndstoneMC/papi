"""Focused tests for the Windows content-addressed native loader."""

from __future__ import annotations

import ast
import hashlib
import importlib
import importlib.machinery
import os
import sys
import types
from pathlib import Path
from typing import Self

import pytest

from endstone_papi import _native_loader as loader


@pytest.fixture
def local_package(tmp_path: Path) -> tuple[Path, Path, Path]:
    root = tmp_path / "plugins" / ".local"
    package = root / "Lib" / "site-packages" / "endstone_papi"
    package.mkdir(parents=True)
    return root, package, package / "_papi.fake.pyd"


@pytest.fixture
def clean_persistent_modules(monkeypatch: pytest.MonkeyPatch):
    names = (loader.PERSISTENT_MODULE_NAME, "_endstone_papi_native", "test_loader_pkg._papi")
    saved = {name: sys.modules.get(name) for name in names}
    for name in names:
        sys.modules.pop(name, None)
    yield
    for name in names:
        sys.modules.pop(name, None)
        if saved[name] is not None:
            sys.modules[name] = saved[name]


def _write_payload(package: Path, content: bytes = b"native payload") -> Path:
    suffix = importlib.machinery.EXTENSION_SUFFIXES[0]
    source = package / f"_papi{suffix}"
    source.write_bytes(content)
    return source


def test_source_discovery_requires_one_exact_current_interpreter_payload(
    local_package: tuple[Path, Path, Path],
) -> None:
    _, package, _ = local_package
    source = _write_payload(package)

    assert loader._find_native_payload(package) == source

    (package / "_papi.other.pyd").write_bytes(b"wrong suffix")
    assert loader._find_native_payload(package) == source

    source.unlink()
    with pytest.raises(ImportError, match="could not find packaged native PAPI payload"):
        loader._find_native_payload(package)


def test_shadow_root_is_exact_and_case_insensitive(local_package: tuple[Path, Path, Path]) -> None:
    root, package, _ = local_package
    assert loader.shadow_root(package) == root
    if os.name == "nt":
        assert loader.shadow_root(Path(str(package).upper())) == root
    assert loader.shadow_root(package.parent) is None


def test_content_addressed_target_is_path_independent(local_package: tuple[Path, Path, Path], tmp_path: Path) -> None:
    root, _, _ = local_package
    content = b"same bytes"
    first = tmp_path / "first.pyd"
    second = tmp_path / "nested" / "second.pyd"
    second.parent.mkdir()
    first.write_bytes(content)
    second.write_bytes(content)
    digest = hashlib.sha256(content).hexdigest()

    assert loader._target_path(root, digest) == root / f"endstone_papi-{digest}.pyd"
    assert loader._payload_digest(first.read_bytes()) == loader._payload_digest(second.read_bytes())


def test_existing_target_is_verified_without_overwrite(local_package: tuple[Path, Path, Path]) -> None:
    root, _, _ = local_package
    content = b"immutable payload"
    digest = loader._payload_digest(content)
    target = loader._target_path(root, digest)
    target.write_bytes(content)
    before = target.stat().st_mtime_ns

    loader._ensure_shadow_copy(target, content, digest)

    assert target.read_bytes() == content
    assert target.stat().st_mtime_ns == before


def test_same_digest_reuses_persistent_module_and_alias(
    local_package: tuple[Path, Path, Path],
    clean_persistent_modules: None,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    _, package, _ = local_package
    _write_payload(package, b"persistent payload")
    package_module = types.ModuleType("test_loader_pkg")
    package_module.__path__ = [str(package)]
    sys.modules[package_module.__name__] = package_module
    calls: list[Path] = []
    native = types.ModuleType(loader.PERSISTENT_MODULE_NAME)

    def execute(target: Path) -> types.ModuleType:
        calls.append(target)
        return native

    monkeypatch.setattr(loader, "_execute_extension", execute)
    monkeypatch.setattr(loader, "os", types.SimpleNamespace(name="nt"))

    first = loader.load_native(package_module.__name__, package)
    second = loader.load_native(package_module.__name__, package)

    assert first is native
    assert second is native
    assert len(calls) == 1
    assert sys.modules[loader.PERSISTENT_MODULE_NAME] is native
    assert sys.modules["test_loader_pkg._papi"] is native
    assert package_module._papi is native
    assert sys.modules["_endstone_papi_native"]._papi is native
    assert native._papi_payload_sha256 == loader._payload_digest(b"persistent payload")
    assert calls[0].name.startswith("endstone_papi-")


def test_persistent_digest_mismatch_rejects_without_loading(
    local_package: tuple[Path, Path, Path],
    clean_persistent_modules: None,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    _, package, _ = local_package
    _write_payload(package, b"new payload")
    persistent = types.ModuleType(loader.PERSISTENT_MODULE_NAME)
    persistent._papi_payload_sha256 = loader._payload_digest(b"old payload")
    sys.modules[loader.PERSISTENT_MODULE_NAME] = persistent
    calls = 0

    def execute(_: Path) -> types.ModuleType:
        nonlocal calls
        calls += 1
        return persistent

    monkeypatch.setattr(loader, "_execute_extension", execute)
    monkeypatch.setattr(loader, "os", types.SimpleNamespace(name="nt"))

    with pytest.raises(ImportError, match="native binary changed while server runs.*restart server"):
        loader.load_native("test_loader_pkg", package)
    assert calls == 0


def test_stale_cleanup_preserves_current_and_ignores_unlink_failure(local_package: tuple[Path, Path, Path]) -> None:
    root, _, _ = local_package
    content = b"current"
    digest = loader._payload_digest(content)
    current = loader._target_path(root, digest)
    stale = root / ("endstone_papi-" + "0" * 64 + ".pyd")
    manual = root / "endstone_papi-manual.pyd"
    current.write_bytes(content)
    stale.write_bytes(b"stale")
    manual.write_bytes(b"manual")

    loader._cleanup_stale_targets(root, current)

    assert current.exists()
    assert not stale.exists()
    assert manual.exists()

    stale.write_bytes(b"stale")
    original_unlink = Path.unlink

    def fail_unlink(path: Path, *args: object, **kwargs: object) -> None:
        if path == stale:
            raise OSError("locked")
        original_unlink(path, *args, **kwargs)

    # A locked stale target is best-effort and must not make first load fail.
    monkeypatch = pytest.MonkeyPatch()
    monkeypatch.setattr(Path, "unlink", fail_unlink)
    try:
        loader._cleanup_stale_targets(root, current)
    finally:
        monkeypatch.undo()
    assert stale.exists()


def test_partial_shadow_copy_failure_removes_only_new_target(
    local_package: tuple[Path, Path, Path], monkeypatch: pytest.MonkeyPatch
) -> None:
    root, _, _ = local_package
    payload = b"payload that fails while writing"
    digest = loader._payload_digest(payload)
    target = loader._target_path(root, digest)
    original_open = Path.open

    class FailingStream:
        def __init__(self, stream: object) -> None:
            self._stream = stream

        def __enter__(self) -> Self:
            return self

        def __exit__(self, *args: object) -> object:
            return self._stream.__exit__(*args)  # type: ignore[attr-defined]

        def write(self, data: bytes) -> None:
            self._stream.write(data[:1])  # type: ignore[attr-defined]
            self._stream.flush()  # type: ignore[attr-defined]
            raise OSError("simulated write failure")

        def flush(self) -> None:
            self._stream.flush()  # type: ignore[attr-defined]

    def failing_open(path: Path, *args: object, **kwargs: object) -> FailingStream:
        return FailingStream(original_open(path, *args, **kwargs))

    monkeypatch.setattr(Path, "open", failing_open)
    with pytest.raises(ImportError, match="failed to create native shadow target") as raised:
        loader._ensure_shadow_copy(target, payload, digest)

    assert isinstance(raised.value.__cause__, OSError)
    assert not target.exists()


def test_non_windows_path_keeps_direct_import_decision(
    monkeypatch: pytest.MonkeyPatch, local_package: tuple[Path, Path, Path]
) -> None:
    _, package, _ = local_package
    monkeypatch.setattr(loader, "os", types.SimpleNamespace(name="posix"))
    assert not loader.should_use_shadow(package)


def test_loader_has_only_stdlib_imports() -> None:
    source = Path(loader.__file__).read_text(encoding="utf-8")
    tree = ast.parse(source)
    imported = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            imported.extend(alias.name.split(".")[0] for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and node.module:
            imported.append(node.module.split(".")[0])
    assert "endstone" not in imported
    assert "endstone_papi" not in imported
