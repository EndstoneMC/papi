"""Static regressions for clean archive-only sdist acceptance."""

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
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


def test_ci_builds_and_smokes_only_the_copied_archive() -> None:
    _assert_isolated_sdist_flow(BUILD_WORKFLOW.read_text(encoding="utf-8"))


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
