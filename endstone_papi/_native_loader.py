"""Windows content-addressed loading for the native PAPI extension."""

from __future__ import annotations

import hashlib
import importlib.machinery
import importlib.util
import os
import re
import sys
import types
from pathlib import Path

PERSISTENT_MODULE_NAME = "_endstone_papi_native._papi"
PERSISTENT_DIGEST_ATTRIBUTE = "_papi_payload_sha256"
_PERSISTENT_PARENT_NAME = "_endstone_papi_native"
_PACKAGE_SUFFIX = ("plugins", ".local", "Lib", "site-packages", "endstone_papi")
_RESTART_REQUIRED = "native binary changed while server runs; restart server to apply update"
_SHADOW_TARGET_RE = re.compile(r"endstone_papi-[0-9a-f]{64}\.pyd\Z", re.IGNORECASE)


def _package_dir(package_path: str | os.PathLike[str]) -> Path:
    path = Path(package_path)
    if path.name == "__init__.py" or (path.suffix and not path.is_dir()):
        path = path.parent
    return path.resolve()


def shadow_root(package_path: str | os.PathLike[str]) -> Path | None:
    """Return the Endstone local-plugin root for an eligible package path."""
    package_dir = _package_dir(package_path)
    expected = tuple(part.casefold() for part in _PACKAGE_SUFFIX)
    actual = tuple(part.casefold() for part in package_dir.parts[-len(expected) :])
    if actual != expected:
        return None
    return package_dir.parents[2]


def should_use_shadow(package_path: str | os.PathLike[str]) -> bool:
    return os.name == "nt" and shadow_root(package_path) is not None


def _find_native_payload(package_dir: Path) -> Path:
    matches: list[Path] = []
    seen: set[str] = set()
    for suffix in importlib.machinery.EXTENSION_SUFFIXES:
        candidate = package_dir / f"_papi{suffix}"
        key = str(candidate).casefold()
        if key in seen or not candidate.is_file():
            continue
        seen.add(key)
        matches.append(candidate)

    if not matches:
        suffixes = ", ".join(importlib.machinery.EXTENSION_SUFFIXES)
        raise ImportError(f"could not find packaged native PAPI payload (_papi + one of: {suffixes})")
    if len(matches) != 1:
        names = ", ".join(str(path) for path in matches)
        raise ImportError(f"ambiguous packaged native PAPI payloads: {names}")
    return matches[0]


def _payload_digest(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _target_path(root: Path, digest: str) -> Path:
    return root / f"endstone_papi-{digest}.pyd"


def _ensure_shadow_copy(target: Path, payload: bytes, digest: str) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        stream = target.open("xb")
    except FileExistsError:
        try:
            existing = target.read_bytes()
        except OSError as error:
            raise ImportError(f"could not verify existing native shadow target {target}") from error
        if _payload_digest(existing) != digest:
            raise ImportError(f"existing native shadow target does not match payload digest: {target}")
        return

    try:
        with stream:
            stream.write(payload)
            stream.flush()
    except Exception as error:
        try:
            target.unlink()
        except OSError:
            pass
        raise ImportError(f"failed to create native shadow target {target}") from error


def _make_persistent_parent() -> tuple[types.ModuleType, bool]:
    existing = sys.modules.get(_PERSISTENT_PARENT_NAME)
    if existing is not None:
        if not isinstance(existing, types.ModuleType):
            raise ImportError(f"persistent native namespace is not a module: {_PERSISTENT_PARENT_NAME}")
        return existing, False

    parent = types.ModuleType(_PERSISTENT_PARENT_NAME)
    parent.__path__ = []
    parent.__package__ = _PERSISTENT_PARENT_NAME
    sys.modules[_PERSISTENT_PARENT_NAME] = parent
    return parent, True


def _execute_extension(target: Path) -> types.ModuleType:
    loader = importlib.machinery.ExtensionFileLoader(PERSISTENT_MODULE_NAME, str(target))
    spec = importlib.util.spec_from_file_location(PERSISTENT_MODULE_NAME, str(target), loader=loader)
    if spec is None:
        raise ImportError(f"could not create import spec for native PAPI payload: {target}")

    parent, parent_created = _make_persistent_parent()
    module: types.ModuleType | None = None
    try:
        module = importlib.util.module_from_spec(spec)
        sys.modules[PERSISTENT_MODULE_NAME] = module
        loader.exec_module(module)
        return module
    except BaseException:
        if module is not None and sys.modules.get(PERSISTENT_MODULE_NAME) is module:
            del sys.modules[PERSISTENT_MODULE_NAME]
        if parent_created and sys.modules.get(_PERSISTENT_PARENT_NAME) is parent:
            del sys.modules[_PERSISTENT_PARENT_NAME]
        raise


def _cleanup_stale_targets(root: Path, current: Path) -> None:
    current_key = str(current.absolute()).casefold()
    try:
        candidates = root.glob("endstone_papi-*.pyd")
        for candidate in candidates:
            if _SHADOW_TARGET_RE.fullmatch(candidate.name) is None:
                continue
            if str(candidate.absolute()).casefold() == current_key:
                continue
            try:
                candidate.unlink()
            except OSError:
                pass
    except OSError:
        pass


def _mismatch_error() -> ImportError:
    return ImportError(_RESTART_REQUIRED)


def load_native(package_name: str, package_path: str | os.PathLike[str]) -> types.ModuleType:
    """Load and alias the native extension for an eligible Windows package."""
    package_dir = _package_dir(package_path)
    root = shadow_root(package_dir)
    if os.name != "nt" or root is None:
        raise ImportError("native shadow loading is only available for Endstone local plugins on Windows")

    source = _find_native_payload(package_dir)
    payload = source.read_bytes()
    digest = _payload_digest(payload)

    persistent = sys.modules.get(PERSISTENT_MODULE_NAME)
    if persistent is not None:
        if getattr(persistent, PERSISTENT_DIGEST_ATTRIBUTE, None) != digest:
            raise _mismatch_error()
        module = persistent
    else:
        target = _target_path(root, digest)
        _ensure_shadow_copy(target, payload, digest)
        module = _execute_extension(target)
        _make_persistent_parent()
        sys.modules[PERSISTENT_MODULE_NAME] = module
        setattr(module, PERSISTENT_DIGEST_ATTRIBUTE, digest)
        _cleanup_stale_targets(root, target)

    alias_name = f"{package_name}._papi"
    sys.modules[alias_name] = module
    package = sys.modules.get(package_name)
    if package is not None:
        package._papi = module
    parent = sys.modules.get(_PERSISTENT_PARENT_NAME)
    if parent is not None:
        parent._papi = module
    return module
