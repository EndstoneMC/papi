"""Regressions for trusted upstream authentication and offline candidate builds."""

import copy
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
ARTIFACT = "endstone-v012-e7eab922-wheelhouse"


def load_workflow(name: str) -> dict:
    return yaml.load((ROOT / ".github/workflows" / name).read_text(encoding="utf-8"), Loader=yaml.BaseLoader)


def validate_staging(workflow: dict) -> None:
    assert workflow["on"] == {"workflow_dispatch": ""}
    assert set(workflow["jobs"]) == {"stage"}
    stage = workflow["jobs"]["stage"]
    assert stage["if"] == "github.ref == 'refs/heads/main'"
    assert stage["environment"] == "endstone-artifacts"
    steps = stage["steps"]
    checkout = steps[0]
    assert checkout["with"] == {"ref": "${{ github.sha }}", "persist-credentials": "false"}
    credential_steps = [step for step in steps if "ENDSTONE_ARTIFACTS_TOKEN" in str(step)]
    assert len(credential_steps) == 1
    prepare = credential_steps[0]
    assert prepare["env"] == {"GH_TOKEN": "${{ secrets.ENDSTONE_ARTIFACTS_TOKEN }}"}
    assert 'if [[ -z "$GH_TOKEN" ]]' in prepare["run"]
    assert "exit 1" in prepare["run"]
    assert "python tools/prepare_endstone_wheels.py --output-dir .endstone-wheelhouse" in prepare["run"]
    assert "--verify-only" not in prepare["run"]
    upload = next(step for step in steps if step.get("uses", "").startswith("actions/upload-artifact@"))
    assert upload["with"]["name"] == ARTIFACT
    assert upload["with"]["path"] == ".endstone-wheelhouse/"
    assert upload["with"]["include-hidden-files"] == "true"
    assert upload["with"]["if-no-files-found"] == "error"
    assert steps.index(prepare) < steps.index(upload)


def validate_consumer(workflow: dict, expected_jobs: set[str]) -> None:
    assert "ENDSTONE_ARTIFACTS_TOKEN" not in str(workflow)
    assert "secrets." not in str(workflow)
    assert "pull_request_target" not in workflow["on"]
    found = set()
    for name, job in workflow["jobs"].items():
        steps = job["steps"]
        prepare = [step for step in steps if step.get("name") == "Prepare verified Endstone runtime wheels"]
        if not prepare:
            continue
        found.add(name)
        assert job.get("permissions", workflow["permissions"])["actions"] == "read"
        assert len(prepare) == 1
        assert prepare[0]["run"] == (
            "python tools/prepare_endstone_wheels.py --output-dir .endstone-wheelhouse --verify-only"
        )
        assert "env" not in prepare[0]
        require = next(step for step in steps if step.get("name") == "Require staged Endstone wheelhouse")
        assert require["env"]["WHEELHOUSE_RUN_ID"] == "${{ vars.ENDSTONE_WHEELHOUSE_RUN_ID }}"
        assert "exit 1" in require["run"]
        download = next(step for step in steps if step.get("name") == "Download staged Endstone wheelhouse")
        assert download["uses"] == "actions/download-artifact@v4"
        assert download["with"] == {
            "name": ARTIFACT,
            "path": ".endstone-wheelhouse",
            "run-id": "${{ vars.ENDSTONE_WHEELHOUSE_RUN_ID }}",
            "repository": "${{ github.repository }}",
            "github-token": "${{ github.token }}",
        }
        assert steps.index(require) < steps.index(download) < steps.index(prepare[0])
    assert found == expected_jobs


def test_staging_only_runs_trusted_main_with_environment_credential() -> None:
    validate_staging(load_workflow("stage-endstone.yml"))


def test_candidate_jobs_verify_same_repository_artifacts_without_upstream_token() -> None:
    build = load_workflow("build.yml")
    assert build["on"]["pull_request"]["branches"] == ["develop"]
    assert "ref" in build["on"]["workflow_dispatch"]["inputs"]
    validate_consumer(build, {"native-linux", "native-windows", "wheel", "sdist"})
    validate_consumer(load_workflow("release.yml"), {"build-wheels", "build-sdist"})


def test_staging_contract_rejects_untrusted_ref_or_missing_main_guard() -> None:
    for changed_field in ("ref", "guard", "environment"):
        workflow = copy.deepcopy(load_workflow("stage-endstone.yml"))
        stage = workflow["jobs"]["stage"]
        if changed_field == "ref":
            stage["steps"][0]["with"]["ref"] = "${{ inputs.ref }}"
        elif changed_field == "guard":
            stage["if"] = "always()"
        else:
            stage["environment"] = "unrestricted"
        try:
            validate_staging(workflow)
        except AssertionError:
            pass
        else:
            raise AssertionError(f"unsafe staging accepted: {changed_field}")


def test_consumer_contract_rejects_online_helper_or_upstream_secret() -> None:
    for mutation in ("online", "secret"):
        workflow = copy.deepcopy(load_workflow("build.yml"))
        steps = workflow["jobs"]["wheel"]["steps"]
        prepare = next(step for step in steps if step.get("name") == "Prepare verified Endstone runtime wheels")
        if mutation == "online":
            prepare["run"] = prepare["run"].removesuffix(" --verify-only")
        else:
            prepare["env"] = {"GH_TOKEN": "${{ secrets.ENDSTONE_ARTIFACTS_TOKEN }}"}
        try:
            validate_consumer(workflow, {"native-linux", "native-windows", "wheel", "sdist"})
        except AssertionError:
            pass
        else:
            raise AssertionError(f"unsafe consumer accepted: {mutation}")


if __name__ == "__main__":
    tests = [value for name, value in globals().copy().items() if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
        print(f"PASS: {test.__name__}")
    print(f"{len(tests)}/{len(tests)} tests passed.")
