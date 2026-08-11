"""Static and state-model tests for the transactional release workflow."""

from __future__ import annotations

from dataclasses import dataclass, replace
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = ROOT / ".github" / "workflows" / "release.yml"


@dataclass(frozen=True)
class ReleaseState:
    base: str
    candidate: str | None = None
    main: str = "base"
    develop: str = "base"
    tag: str | None = None
    github_release: bool = False


def prepare_candidate(state: ReleaseState) -> ReleaseState:
    candidate = state.candidate or "candidate"
    assert state.main == state.base
    assert state.develop == state.base
    assert state.tag is None
    return replace(state, candidate=candidate)


def finalize_refs(state: ReleaseState) -> ReleaseState:
    assert state.candidate is not None
    if state.tag is None:
        assert state.main == state.base
        assert state.develop == state.base
        return replace(state, main=state.candidate, develop=state.candidate, tag=state.candidate)
    assert state.main == state.candidate
    assert state.develop == state.candidate
    assert state.tag == state.candidate
    return state


def publish(state: ReleaseState) -> ReleaseState:
    state = finalize_refs(state)
    return replace(state, github_release=True)


def test_candidate_build_failures_do_not_move_production_refs() -> None:
    for _failure in ("windows-wheel", "linux-wheel", "sdist", "artifact-verification", "cancelled"):
        state = prepare_candidate(ReleaseState(base="base"))
        assert state.main == "base"
        assert state.develop == "base"
        assert state.tag is None
        assert state.candidate == "candidate"


def test_candidate_preparation_retry_reuses_the_same_ref() -> None:
    state = prepare_candidate(ReleaseState(base="base"))
    assert prepare_candidate(state) == state


def test_github_release_failure_is_resumable_after_atomic_ref_finalization() -> None:
    state = finalize_refs(prepare_candidate(ReleaseState(base="base")))
    assert state.main == state.develop == state.tag == state.candidate
    assert not state.github_release
    assert publish(state).github_release


def test_completed_same_version_retry_is_a_no_op() -> None:
    state = publish(prepare_candidate(ReleaseState(base="base")))
    assert publish(state) == state


def test_mismatched_existing_tag_is_rejected() -> None:
    state = prepare_candidate(ReleaseState(base="base"))
    state = replace(state, tag="unrelated")
    rejected = False
    try:
        finalize_refs(state)
    except AssertionError:
        rejected = True
    assert rejected, "an unrelated existing production tag must be rejected"


def test_concurrent_branch_change_prevents_finalization() -> None:
    state = prepare_candidate(ReleaseState(base="base"))
    state = replace(state, main="concurrent")
    rejected = False
    try:
        finalize_refs(state)
    except AssertionError:
        rejected = True
    assert rejected, "concurrent branch movement must reject finalization"


def test_workflow_mutates_production_refs_only_after_artifact_verification() -> None:
    source = WORKFLOW.read_text(encoding="utf-8")
    prepare_job = source[source.index("  release:") : source.index("  build-wheels:")]
    publish_job = source[source.index("  publish:") :]

    prepare_pushes = [line.strip() for line in prepare_job.splitlines() if "git push" in line]
    assert prepare_pushes == ['git push origin "$CANDIDATE_SHA:$CANDIDATE_REF"']

    verification = publish_job.index("- name: Verify release assets")
    finalization = publish_job.index("- name: Atomically finalize release refs")
    github_release = publish_job.index("- name: Create GitHub Release")
    assert verification < finalization < github_release
    assert "git push --atomic origin" in publish_job
    assert '"$CANDIDATE_SHA:refs/heads/main"' in publish_job
    assert '"$CANDIDATE_SHA:refs/heads/develop"' in publish_job
    assert '"$CANDIDATE_SHA:refs/tags/v$VERSION"' in publish_job
    assert "--force" not in source


def test_workflow_has_explicit_same_version_resume_states() -> None:
    source = WORKFLOW.read_text(encoding="utf-8")

    assert "release-candidates/v$VERSION-$BASE_SHA" in source
    assert 'gh release view "v$VERSION"' in source
    assert "render_version_notes" in source
    assert "A previous run finalized refs but failed" in source
    assert "needs.release.outputs.published != 'true'" in source
    assert "cancel-in-progress: false" in source


def main() -> int:
    tests = [value for name, value in globals().items() if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
        print(f"  PASS: {test.__name__}")
    print(f"\n{len(tests)}/{len(tests)} tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
