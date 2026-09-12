"""Offline tests for the official Endstone wheel provenance contract."""

from __future__ import annotations

import copy
import hashlib
import io
import json
import subprocess
import sys
import zipfile
from pathlib import Path
from unittest.mock import patch

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import prepare_endstone_wheels as staging


def wheel_bytes(name: str, *, version=staging.VERSION, tag=None, commit=None, extra=None, missing=None) -> bytes:
    python, system = staging.EXPECTED[name]
    prefix = f"endstone-{staging.VERSION}.dist-info"
    members = {
        f"{prefix}/METADATA": f"Metadata-Version: 2.1\nName: endstone\nVersion: {version}\n",
        f"{prefix}/WHEEL": (
            "Wheel-Version: 1.0\nRoot-Is-Purelib: false\n"
            f"Tag: {tag or f'{python}-{python}-{staging.PLATFORMS[system]}'}\n"
        ),
        "endstone/_version.py": (
            f"__version__ = version = {version!r}\n__commit_id__ = commit_id = {commit or 'g' + staging.COMMIT[:9]!r}\n"
        ),
        "endstone/native.bin": b"fixture native bytes",
    }
    if missing:
        del members[missing]
    members.update(extra or {})
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as archive:
        for path, contents in members.items():
            info = zipfile.ZipInfo(path, date_time=(2026, 9, 1, 0, 0, 0))
            info.filename = path
            archive.writestr(info, contents)
    return output.getvalue()


class OfficialAPI:
    def __init__(self):
        self.run = {
            "id": staging.RUN_ID,
            "head_sha": staging.COMMIT,
            "status": "completed",
            "conclusion": "success",
            "repository": {"full_name": staging.REPOSITORY, "id": 673120693},
        }
        self.artifacts = []
        self.downloads = {}
        self.downloaded_ids = []
        for identifier, name in enumerate(sorted(staging.EXPECTED), 1):
            artifact = {
                "id": identifier,
                "name": name,
                "expired": False,
                "workflow_run": {
                    "id": staging.RUN_ID,
                    "head_sha": staging.COMMIT,
                    "repository_id": 673120693,
                    "head_repository_id": 673120693,
                },
            }
            self.artifacts.append(artifact)
            self.replace_wheel(name, wheel_bytes(name))

    def replace_wheel(self, name: str, contents: bytes):
        artifact = next(item for item in self.artifacts if item["name"] == name)
        artifact["digest"] = "sha256:" + hashlib.sha256(contents).hexdigest()
        artifact["size_in_bytes"] = len(contents)
        self.downloads[artifact["id"]] = contents

    def subprocess(self, command, *, stdout, stderr, timeout, check):
        assert command[:2] == ["gh", "api"]
        assert stderr == subprocess.PIPE and timeout == 300 and check is False
        endpoint = command[2]
        root = f"repos/{staging.REPOSITORY}/actions"
        if endpoint == f"{root}/runs/{staging.RUN_ID}":
            payload = json.dumps(self.run).encode()
        elif endpoint == f"{root}/runs/{staging.RUN_ID}/artifacts?per_page=100":
            payload = json.dumps({"total_count": len(self.artifacts), "artifacts": self.artifacts}).encode()
        elif endpoint.startswith(f"{root}/artifacts/") and endpoint.endswith("/zip"):
            identifier = int(endpoint.split("/")[-2])
            self.downloaded_ids.append(identifier)
            payload = self.downloads[identifier]
        else:
            raise AssertionError(f"Unexpected endpoint: {endpoint}")
        if stdout != subprocess.PIPE:
            stdout.write(payload)
            payload = None
        return subprocess.CompletedProcess(command, 0, stdout=payload, stderr=b"")


@pytest.fixture
def official():
    api = OfficialAPI()
    pinned = {item["name"]: (item["id"], item["size_in_bytes"], item["digest"]) for item in api.artifacts}
    with (
        patch.object(staging.subprocess, "run", side_effect=api.subprocess),
        patch.object(staging, "PINNED_ARTIFACTS", pinned),
    ):
        yield api


def test_default_stages_exact_eight_and_revalidates_without_download(tmp_path, official):
    output = tmp_path / "wheelhouse"
    manifest = staging.prepare(output)
    assert {item.name for item in output.glob("*.whl")} == set(staging.EXPECTED)
    assert len(official.downloaded_ids) == 8
    assert manifest["run_id"] == staging.RUN_ID
    assert manifest["commit"] == staging.COMMIT
    assert manifest["version"] == staging.VERSION
    assert json.loads((output / staging.MANIFEST).read_text()) == manifest
    for record in manifest["wheels"]:
        artifact = next(item for item in official.artifacts if item["id"] == record["artifact_id"])
        assert "sha256:" + record["wheel_sha256"] == artifact["digest"] == record["archive_digest"]
        assert (output / record["filename"]).read_bytes() == official.downloads[record["artifact_id"]]
    assert staging.prepare(output) == manifest
    assert len(official.downloaded_ids) == 8
    assert not list(tmp_path.glob(".endstone-wheels-*"))


def test_filters_and_later_merge_preserve_prior_verified_wheels(tmp_path, official):
    output = tmp_path / "wheels"
    first = staging.prepare(output, "cp314", "windows")
    assert len(first["wheels"]) == 1
    assert first["wheels"][0]["tag"] == "cp314-cp314-win_amd64"
    (output / "unrelated.txt").write_text("keep")
    second = staging.prepare(output, "cp312", "linux")
    assert {record["tag"] for record in second["wheels"]} == {
        "cp314-cp314-win_amd64",
        "cp312-cp312-manylinux_2_31_x86_64",
    }
    assert len(official.downloaded_ids) == 2
    assert (output / "unrelated.txt").read_text() == "keep"


@pytest.mark.parametrize(
    ("field", "value", "message"),
    [("id", 1, "ID mismatch"), ("head_sha", "0" * 40, "commit mismatch"), ("conclusion", "failure", "did not succeed")],
)
def test_wrong_run_rejected_before_download(tmp_path, official, field, value, message):
    official.run[field] = value
    with pytest.raises(staging.VerificationError, match=message):
        staging.prepare(tmp_path / "wheels")
    assert official.downloaded_ids == []
    assert not (tmp_path / "wheels").exists()


@pytest.mark.parametrize(
    "fault", ["wrong_run", "wrong_head", "wrong_repo", "expired", "missing_digest", "ambiguous", "missing"]
)
def test_artifact_provenance_rejected_before_download(tmp_path, official, fault):
    artifact = official.artifacts[0]
    if fault == "wrong_run":
        artifact["workflow_run"]["id"] = 1
    elif fault == "wrong_head":
        artifact["workflow_run"]["head_sha"] = "0" * 40
    elif fault == "wrong_repo":
        artifact["workflow_run"]["head_repository_id"] = 1
    elif fault == "expired":
        artifact["expired"] = True
    elif fault == "missing_digest":
        del artifact["digest"]
    elif fault == "ambiguous":
        official.artifacts.append(copy.deepcopy(artifact))
    else:
        official.artifacts.pop()
    with pytest.raises(staging.VerificationError):
        staging.prepare(tmp_path / "wheels", "cp314", "windows")
    assert official.downloaded_ids == []


def test_archive_digest_mismatch_leaves_no_published_wheels(tmp_path, official):
    last = official.artifacts[-1]
    official.downloads[last["id"]] = official.downloads[last["id"]].replace(b"native bytes", b"broken bytes")
    with pytest.raises(staging.VerificationError, match="SHA256 mismatch"):
        staging.prepare(tmp_path / "wheels")
    assert not (tmp_path / "wheels").exists()
    assert not list(tmp_path.glob(".endstone-wheels-*"))


@pytest.mark.parametrize("field", ["id", "digest", "size_in_bytes"])
def test_same_run_replacement_identity_rejected_before_download(tmp_path, official, field):
    artifact = official.artifacts[-1]
    if field == "digest":
        artifact[field] = "sha256:" + "0" * 64
    else:
        artifact[field] += 1
    with pytest.raises(staging.VerificationError, match="Pinned artifact identity mismatch"):
        staging.prepare(tmp_path / "wheels")
    assert official.downloaded_ids == []
    assert not (tmp_path / "wheels").exists()


def test_same_metadata_rebuilt_payload_rejected_before_download(tmp_path, official):
    artifact = official.artifacts[-1]
    official.replace_wheel(
        artifact["name"], wheel_bytes(artifact["name"], extra={"endstone/rebuild.bin": "new binary"})
    )
    with pytest.raises(staging.VerificationError, match="Pinned artifact identity mismatch"):
        staging.prepare(tmp_path / "wheels")
    assert official.downloaded_ids == []
    assert not (tmp_path / "wheels").exists()


@pytest.mark.parametrize(
    ("options", "message"),
    [
        ({"version": "0.12.0"}, "version mismatch"),
        ({"tag": "cp310-cp310-win_amd64"}, "tags mismatch"),
        ({"commit": "g000000000"}, "commit mismatch"),
        ({"missing": f"endstone-{staging.VERSION}.dist-info/WHEEL"}, "Missing WHEEL"),
        ({"missing": f"endstone-{staging.VERSION}.dist-info/METADATA"}, "METADATA"),
        ({"extra": {"../escape.txt": "no"}}, "Unsafe ZIP"),
        ({"extra": {"/absolute.txt": "no"}}, "Unsafe ZIP"),
        ({"extra": {"C:/escape.txt": "no"}}, "Unsafe ZIP"),
        ({"extra": {"endstone\\escape.txt": "no"}}, "Unsafe ZIP"),
        ({"extra": {"endstone/./escape.txt": "no"}}, "Unsafe ZIP"),
    ],
)
def test_hash_verified_bad_wheel_is_rejected(tmp_path, official, options, message):
    name = official.artifacts[-1]["name"]
    official.replace_wheel(name, wheel_bytes(name, **options))
    artifact = official.artifacts[-1]
    staging.PINNED_ARTIFACTS[name] = (artifact["id"], artifact["size_in_bytes"], artifact["digest"])
    python, platform = staging.EXPECTED[name]
    with pytest.raises(staging.VerificationError, match=message):
        staging.prepare(tmp_path / "wheels", python, platform)
    assert not (tmp_path / "wheels").exists()
    assert not (tmp_path / "escape.txt").exists()


def test_tampered_existing_wheel_is_not_overwritten(tmp_path, official):
    output = tmp_path / "wheels"
    manifest = staging.prepare(output, "cp314", "windows")
    existing = output / manifest["wheels"][0]["filename"]
    # Replace the file independently of the archive cache's hard link.
    existing.unlink()
    existing.write_bytes(b"conflicting wheel")
    with pytest.raises(staging.VerificationError, match="size mismatch"):
        staging.prepare(output, "cp312", "linux")
    assert existing.read_bytes() == b"conflicting wheel"
    assert json.loads((output / staging.MANIFEST).read_text()) == manifest
    assert len(list(output.glob("*.whl"))) == 1


def test_forged_manifest_never_authorizes_cache(tmp_path, official):
    output = tmp_path / "wheels"
    manifest = staging.prepare(output, "cp314", "windows")
    record = manifest["wheels"][0]
    cache = output / ".archives" / f"{record['artifact_id']}.zip"
    contents = cache.read_bytes().replace(b"native bytes", b"broken bytes")
    cache.write_bytes(contents)
    record["wheel_sha256"] = hashlib.sha256(contents).hexdigest()
    (output / staging.MANIFEST).write_text(json.dumps(manifest))
    with pytest.raises(staging.VerificationError, match="SHA256 mismatch"):
        staging.prepare(output, "cp314", "windows")
    assert len(official.downloaded_ids) == 1


def test_existing_official_wheel_can_be_reused_without_a_manifest(tmp_path, official):
    output = tmp_path / "wheels"
    output.mkdir()
    artifact = next(item for item in official.artifacts if "cp314-cp314-win" in item["name"])
    (output / artifact["name"]).write_bytes(official.downloads[artifact["id"]])
    manifest = staging.prepare(output, "cp314", "windows")
    assert len(manifest["wheels"]) == 1
    assert official.downloaded_ids == []


def test_gh_failure_reports_actionable_status_without_secret_stderr(tmp_path, capsys):
    failure = subprocess.CompletedProcess([], 1, stdout=b"", stderr=b"HTTP 403 token=do-not-echo-this")
    with patch.object(staging.subprocess, "run", return_value=failure):
        assert staging.main(["--output-dir", str(tmp_path / "wheels")]) == 1
    error = capsys.readouterr().err
    assert "HTTP 403" in error and "Actions read access" in error
    assert "do-not-echo-this" not in error


def test_cli_filters(tmp_path, official, capsys):
    assert staging.main(["--output-dir", str(tmp_path / "wheels"), "--python-tag", "cp312", "--platform", "linux"]) == 0
    assert "Verified 1 official Endstone wheels" in capsys.readouterr().out


@pytest.fixture
def offline_wheelhouse(tmp_path, official):
    output = tmp_path / "wheels"
    output.mkdir()
    for artifact in official.artifacts:
        (output / artifact["name"]).write_bytes(official.downloads[artifact["id"]])
    with (
        patch.object(staging, "gh_api", side_effect=AssertionError("Offline verification called gh")),
        patch.object(staging.subprocess, "run", side_effect=AssertionError("Offline verification ran a subprocess")),
    ):
        yield output


def file_snapshot(output):
    return {
        str(path.relative_to(output)): (None if path.is_dir() else path.read_bytes(), path.stat().st_mtime_ns)
        for path in output.rglob("*")
    }


def test_verify_only_all_wheels_is_offline_and_read_only(offline_wheelhouse, capsys):
    output = offline_wheelhouse
    (output / staging.MANIFEST).write_text("untrusted, invalid manifest")
    before = file_snapshot(output)
    assert staging.main(["--output-dir", str(output), "--verify-only"]) == 0
    assert "Verified 8 official Endstone wheels" in capsys.readouterr().out
    assert file_snapshot(output) == before


@pytest.mark.parametrize(("python", "platform"), [("cp312", "linux"), ("cp314", "windows")])
def test_verify_only_filtered_subset_needs_no_other_wheels(offline_wheelhouse, python, platform, capsys):
    output = offline_wheelhouse
    for name, tags in staging.EXPECTED.items():
        if tags != (python, platform):
            (output / name).unlink()
    before = file_snapshot(output)
    assert (
        staging.main(["--output-dir", str(output), "--verify-only", "--python-tag", python, "--platform", platform])
        == 0
    )
    assert "Verified 1 official Endstone wheels" in capsys.readouterr().out
    assert file_snapshot(output) == before


@pytest.mark.parametrize(("fault", "message"), [("missing", "Missing"), ("corrupt", "SHA256 mismatch")])
def test_verify_only_rejects_missing_or_tampered_wheel(offline_wheelhouse, fault, message, capsys):
    output = offline_wheelhouse
    wheel = output / max(staging.EXPECTED)
    if fault == "missing":
        wheel.unlink()
    else:
        wheel.write_bytes(wheel.read_bytes().replace(b"native bytes", b"broken bytes"))
    (output / staging.MANIFEST).write_text(json.dumps({"wheels": [{"filename": wheel.name, "verified": True}]}))
    before = file_snapshot(output)
    assert staging.main(["--output-dir", str(output), "--verify-only"]) == 1
    assert message in capsys.readouterr().err
    assert file_snapshot(output) == before


@pytest.mark.parametrize(
    ("options", "message"),
    [
        ({"version": "0.12.0"}, "version mismatch"),
        ({"tag": "cp310-cp310-win_amd64"}, "tags mismatch"),
        ({"commit": "g000000000"}, "commit mismatch"),
        (
            {"extra": {f"endstone-{staging.VERSION}.dist-info/METADATA": "Name: other\nVersion: 0.11.11.dev392\n"}},
            "package name mismatch",
        ),
    ],
)
def test_verify_only_checks_metadata_after_pinned_hash(offline_wheelhouse, options, message, capsys):
    output = offline_wheelhouse
    name = max(staging.EXPECTED)
    contents = wheel_bytes(name, **options)
    (output / name).write_bytes(contents)
    identifier = staging.PINNED_ARTIFACTS[name][0]
    staging.PINNED_ARTIFACTS[name] = (identifier, len(contents), "sha256:" + hashlib.sha256(contents).hexdigest())
    before = file_snapshot(output)
    assert staging.main(["--output-dir", str(output), "--verify-only"]) == 1
    assert message in capsys.readouterr().err
    assert file_snapshot(output) == before


@pytest.mark.parametrize(
    ("name", "directory"),
    [
        ("endstone-0.11.11.dev392-1-cp314-cp314-win_amd64.whl", False),
        ("unrelated-1.0-py3-none-any.whl", False),
        ("unrelated-1.0.tar.gz", False),
        ("unexpected.txt", False),
        ("unexpected", True),
    ],
)
def test_verify_only_rejects_unknown_top_level_entries(offline_wheelhouse, name, directory, capsys):
    output = offline_wheelhouse
    extra = output / name
    if directory:
        extra.mkdir()
    else:
        extra.write_bytes(b"untrusted package")
    before = file_snapshot(output)
    assert (
        staging.main(["--output-dir", str(output), "--verify-only", "--python-tag", "cp312", "--platform", "linux"])
        == 1
    )
    error = capsys.readouterr().err
    assert "Unexpected wheelhouse entries" in error and name in error
    assert file_snapshot(output) == before


def test_verify_only_filter_validates_all_present_known_wheels(offline_wheelhouse, capsys):
    output = offline_wheelhouse
    (output / ".archives").mkdir()
    (output / ".archives" / "private-cache.zip").write_bytes(b"not a pip-visible distribution")
    before = file_snapshot(output)
    arguments = ["--output-dir", str(output), "--verify-only", "--python-tag", "cp312", "--platform", "linux"]
    assert staging.main(arguments) == 0
    assert "Verified 8 official Endstone wheels" in capsys.readouterr().out
    assert file_snapshot(output) == before
    unselected = output / max(staging.EXPECTED)
    unselected.write_bytes(unselected.read_bytes().replace(b"native bytes", b"broken bytes"))
    before = file_snapshot(output)
    assert staging.main(arguments) == 1
    assert "SHA256 mismatch" in capsys.readouterr().err
    assert file_snapshot(output) == before


@pytest.mark.parametrize("entry", ["manifest.json", ".archives", "wheel"])
def test_verify_only_rejects_wrong_entry_types(offline_wheelhouse, entry, capsys):
    output = offline_wheelhouse
    if entry == ".archives":
        (output / entry).write_bytes(b"not a directory")
    else:
        target = output / (max(staging.EXPECTED) if entry == "wheel" else entry)
        if target.exists():
            target.unlink()
        target.mkdir()
    before = file_snapshot(output)
    assert staging.main(["--output-dir", str(output), "--verify-only"]) == 1
    assert capsys.readouterr().err
    assert file_snapshot(output) == before


@pytest.mark.parametrize("entry", ["manifest.json", ".archives", "wheel"])
def test_verify_only_rejects_symlink_entries(offline_wheelhouse, entry, capsys):
    output = offline_wheelhouse
    target = output.parent / "outside"
    if entry == ".archives":
        target.mkdir()
    else:
        target.write_bytes(b"outside file")
    link = output / (max(staging.EXPECTED) if entry == "wheel" else entry)
    if link.exists():
        link.unlink()
    try:
        link.symlink_to(target, target_is_directory=entry == ".archives")
    except OSError as error:
        pytest.skip(f"Symlink creation unavailable: {error}")
    before = file_snapshot(output)
    assert staging.main(["--output-dir", str(output), "--verify-only"]) == 1
    assert capsys.readouterr().err
    assert file_snapshot(output) == before


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__]))
