"""Regressions for archive-only sdist acceptance and shared wheel repair."""

import importlib.util
import sys
import tempfile
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
BACKEND_SPEC = importlib.util.spec_from_file_location("papi_build_backend", ROOT / "papi_build_backend.py")
assert BACKEND_SPEC is not None and BACKEND_SPEC.loader is not None
papi_build_backend = importlib.util.module_from_spec(BACKEND_SPEC)
sys.modules[BACKEND_SPEC.name] = papi_build_backend
BACKEND_SPEC.loader.exec_module(papi_build_backend)
VERIFIER_SPEC = importlib.util.spec_from_file_location(
    "papi_verify_linux_wheel", ROOT / "tools" / "verify_linux_wheel.py"
)
assert VERIFIER_SPEC is not None and VERIFIER_SPEC.loader is not None
verify_linux_wheel = importlib.util.module_from_spec(VERIFIER_SPEC)
sys.modules[VERIFIER_SPEC.name] = verify_linux_wheel
VERIFIER_SPEC.loader.exec_module(verify_linux_wheel)

BUILD_WORKFLOW = ROOT / ".github" / "workflows" / "build.yml"
RELEASE_WORKFLOW = ROOT / ".github" / "workflows" / "release.yml"


def _assert_isolated_sdist_flow(source: str) -> None:
    assert "Stage only the sdist archive" in source or "Verify and isolate sdist archive" in source
    assert "cp dist/*.tar.gz /tmp/papi-sdist-input/" in source
    assert "python -m venv /tmp/papi-sdist-build-env" in source
    assert "cd /tmp/papi-sdist-build" in source
    assert "/tmp/papi-sdist-input/*.tar.gz" in source
    assert "python -m venv /tmp/papi-sdist-smoke-env" in source
    assert "cd /tmp/papi-sdist-smoke" in source
    assert 'python "$GITHUB_WORKSPACE/tools/wheel_smoke_test.py"' in source
    assert "Build checkout wheel through shared backend" in source
    assert "Verify checkout and sdist wheel runtime contracts" in source
    assert "tools/verify_linux_wheel.py" in source
    assert "--compare" in source
    assert "auditwheel show" in source
    assert 'ldd "$module"' in source
    assert "native import unexpectedly succeeded without Endstone libc++" in source
    assert "assert not any('=> /usr/' in line for line in runtime)" in source
    assert "/tmp/papi-sdist-smoke-env/bin/pip install endstone==0.11.8" not in source


def _fake_delegate_wheel(wheel_directory: str, *_args: object) -> str:
    wheel = Path(wheel_directory) / "raw.whl"
    wheel.write_bytes(b"raw")
    return wheel.name


def test_linux_backend_always_repairs_wheel() -> None:
    with tempfile.TemporaryDirectory() as temporary_directory:
        destination = Path(temporary_directory)

        def fake_repair(wheel: Path, output: Path) -> Path:
            assert wheel.read_bytes() == b"raw"
            repaired = output / "repaired.whl"
            repaired.write_bytes(b"repaired")
            return repaired

        with (
            mock.patch.object(sys, "path", [str(ROOT), *sys.path]),
            mock.patch.object(papi_build_backend.sys, "platform", "linux"),
            mock.patch.object(papi_build_backend._delegate, "build_wheel", side_effect=_fake_delegate_wheel),
            mock.patch("tools.repair_wheel.repair_wheel", side_effect=fake_repair) as repair,
        ):
            result = papi_build_backend.build_wheel(str(destination))

        assert result == "repaired.whl"
        assert (destination / result).read_bytes() == b"repaired"
        repair.assert_called_once()


def test_linux_backend_propagates_repair_failure_without_raw_wheel() -> None:
    with tempfile.TemporaryDirectory() as temporary_directory:
        destination = Path(temporary_directory)
        with (
            mock.patch.object(sys, "path", [str(ROOT), *sys.path]),
            mock.patch.object(papi_build_backend.sys, "platform", "linux"),
            mock.patch.object(papi_build_backend._delegate, "build_wheel", side_effect=_fake_delegate_wheel),
            mock.patch("tools.repair_wheel.repair_wheel", side_effect=RuntimeError("repair failed")),
            mock.patch.object(papi_build_backend.shutil, "move") as move,
        ):
            try:
                papi_build_backend.build_wheel(str(destination))
            except RuntimeError as error:
                assert str(error) == "repair failed"
            else:
                raise AssertionError("Linux wheel build unexpectedly ignored repair failure")

        move.assert_not_called()
        assert list(destination.iterdir()) == []


def test_windows_backend_delegates_without_linux_repair() -> None:
    with tempfile.TemporaryDirectory() as temporary_directory:
        destination = Path(temporary_directory)
        with (
            mock.patch.object(sys, "path", [str(ROOT), *sys.path]),
            mock.patch.object(papi_build_backend.sys, "platform", "win32"),
            mock.patch.object(papi_build_backend._delegate, "build_wheel", side_effect=_fake_delegate_wheel),
            mock.patch("tools.repair_wheel.repair_wheel") as repair,
        ):
            result = papi_build_backend.build_wheel(str(destination))

        assert result == "raw.whl"
        assert (destination / result).read_bytes() == b"raw"
        repair.assert_not_called()


def test_runtime_bundle_check_ignores_empty_directory_but_rejects_cpp_runtime_files() -> None:
    assert not verify_linux_wheel._is_papi_owned_cpp_runtime("endstone_papi.libs/")
    assert not verify_linux_wheel._is_papi_owned_cpp_runtime("endstone_papi.libs/libother-123.so.2")
    assert verify_linux_wheel._is_papi_owned_cpp_runtime("endstone_papi.libs/libc++-123.so.1.0")
    assert verify_linux_wheel._is_papi_owned_cpp_runtime("endstone_papi.libs/libc++abi-123.so.1.0")
    assert verify_linux_wheel._is_papi_owned_cpp_runtime("endstone_papi.libs/libunwind-123.so.1")


def test_ci_builds_and_smokes_only_the_copied_archive() -> None:
    source = BUILD_WORKFLOW.read_text(encoding="utf-8")
    sdist_job = source[source.index("  sdist:") :]
    _assert_isolated_sdist_flow(sdist_job)


def test_release_accepts_sdist_before_publish_finalization() -> None:
    source = RELEASE_WORKFLOW.read_text(encoding="utf-8")
    sdist_job = source[source.index("  build-sdist:") : source.index("  publish:")]
    _assert_isolated_sdist_flow(sdist_job)
    assert sdist_job.index("Build wheel from isolated sdist archive") < sdist_job.index("actions/upload-artifact")
    assert sdist_job.index("Runtime smoke test from isolated sdist-built wheel") < sdist_job.index(
        "actions/upload-artifact"
    )


def main() -> int:
    tests = [value for name, value in globals().items() if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
        print(f"  PASS: {test.__name__}")
    print(f"\n{len(tests)}/{len(tests)} tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
