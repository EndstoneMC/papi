"""Stage the verified official Endstone API 0.12 snapshot wheels."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile
from email.parser import BytesParser
from pathlib import Path

REPOSITORY = "EndstoneMC/endstone"
RUN_ID = 33562961160
COMMIT = "e7eab9222abb92103714837ebe26672ee213f688"
VERSION = "0.11.11.dev392"
PYTHON_TAGS = ("cp311", "cp312", "cp313", "cp314")
PLATFORMS = {"windows": "win_amd64", "linux": "manylinux_2_31_x86_64"}
EXPECTED = {
    f"endstone-{VERSION}-{python}-{python}-{platform}.whl": (python, system)
    for python in PYTHON_TAGS
    for system, platform in PLATFORMS.items()
}
PINNED_ARTIFACTS = {
    "endstone-0.11.11.dev392-cp311-cp311-win_amd64.whl": (
        9822585464,
        26209376,
        "sha256:ecdfdff23229539df318ca15b76066539e6c2bdd1baf58e4edd949998350a674",
    ),
    "endstone-0.11.11.dev392-cp311-cp311-manylinux_2_31_x86_64.whl": (
        9822522076,
        59195079,
        "sha256:0f6c9001f4f5bb82f79b5d63487103564018547671507b4f5ea08fb4d7800bea",
    ),
    "endstone-0.11.11.dev392-cp312-cp312-win_amd64.whl": (
        9822745645,
        25996594,
        "sha256:ca6691cba1c176409b2cb2fd62e68e0f698b9de8543b3384e1355ea4a563caca",
    ),
    "endstone-0.11.11.dev392-cp312-cp312-manylinux_2_31_x86_64.whl": (
        9822599794,
        59592718,
        "sha256:086bffba06d19ca239fa813adf2c64000dad71931ccf37addb8d9eab75fad228",
    ),
    "endstone-0.11.11.dev392-cp313-cp313-win_amd64.whl": (
        9822822306,
        25995321,
        "sha256:c495ce9ea53de6256a1c2559982975d2fa1466209c1d70df0ba96b4f1c2dd75c",
    ),
    "endstone-0.11.11.dev392-cp313-cp313-manylinux_2_31_x86_64.whl": (
        9822549421,
        59594846,
        "sha256:f7c90fe6ef00fa29cda88e68ea58f8b9562fac67a08beb73e10460754085efe5",
    ),
    "endstone-0.11.11.dev392-cp314-cp314-win_amd64.whl": (
        9822498246,
        26262546,
        "sha256:37eb1f351d3508b775276dc18463e9a715fc30209f7dde84f8cfcf8486aa7830",
    ),
    "endstone-0.11.11.dev392-cp314-cp314-manylinux_2_31_x86_64.whl": (
        9822521403,
        59543758,
        "sha256:00ab68325db3018225a612afcea9eef276ccb8d829884a2c8061ddcdde68bbf6",
    ),
}
MANIFEST = "manifest.json"
MAX_ARCHIVE_BYTES = 256 * 1024 * 1024
MAX_UNCOMPRESSED_BYTES = 2 * 1024 * 1024 * 1024


class VerificationError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise VerificationError(message)


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def gh_api(endpoint: str, output=None) -> bytes:
    try:
        result = subprocess.run(
            ["gh", "api", endpoint],
            stdout=output if output is not None else subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=300,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise VerificationError("Cannot run gh api; check that gh is installed, authenticated, and online") from error
    if result.returncode:
        status = re.search(rb"HTTP [0-9]{3}", result.stderr or b"")
        detail = status.group().decode("ascii") if status else "no HTTP status reported"
        raise VerificationError(
            f"gh api failed for {endpoint} (exit {result.returncode}; stderr: {detail}); "
            "check gh authentication, Actions read access, and network connectivity"
        )
    return result.stdout or b""


def api_json(endpoint: str) -> dict:
    data = json.loads(gh_api(endpoint))
    require(isinstance(data, dict), "GitHub API returned a non-object response")
    return data


def official_artifacts() -> dict[str, dict]:
    require(PINNED_ARTIFACTS.keys() == EXPECTED.keys(), "Incomplete pinned artifact identities")
    root = f"repos/{REPOSITORY}/actions"
    run = api_json(f"{root}/runs/{RUN_ID}")
    require(run.get("id") == RUN_ID, "Workflow run ID mismatch")
    require(run.get("head_sha") == COMMIT, "Workflow run commit mismatch")
    require(run.get("repository", {}).get("full_name") == REPOSITORY, "Workflow repository mismatch")
    require(run.get("status") == "completed" and run.get("conclusion") == "success", "Workflow run did not succeed")
    repository_id = run["repository"].get("id")
    require(isinstance(repository_id, int) and repository_id > 0, "Missing workflow repository ID")
    listing = api_json(f"{root}/runs/{RUN_ID}/artifacts?per_page=100")
    artifacts = listing.get("artifacts")
    require(isinstance(artifacts, list), "Missing artifact listing")
    require(listing.get("total_count") == len(artifacts), "Incomplete artifact listing")
    wheels = {}
    for artifact in artifacts:
        require(isinstance(artifact, dict) and isinstance(artifact.get("name"), str), "Invalid artifact metadata")
        name = artifact["name"]
        if not name.endswith(".whl"):
            continue
        require(name in EXPECTED, f"Unexpected official wheel: {name}")
        require(name not in wheels, f"Ambiguous official wheel: {name}")
        identity = artifact.get("workflow_run", {})
        require(identity.get("id") == RUN_ID and identity.get("head_sha") == COMMIT, f"Artifact run mismatch: {name}")
        require(
            identity.get("repository_id") == repository_id and identity.get("head_repository_id") == repository_id,
            f"Artifact repository mismatch: {name}",
        )
        require(artifact.get("expired") is False, f"Expired artifact: {name}")
        require(isinstance(artifact.get("id"), int) and artifact["id"] > 0, f"Invalid artifact ID: {name}")
        require(
            isinstance(artifact.get("digest"), str) and re.fullmatch(r"sha256:[0-9a-f]{64}", artifact["digest"]),
            f"Missing trusted SHA256 digest: {name}",
        )
        require(
            isinstance(artifact.get("size_in_bytes"), int) and 0 < artifact["size_in_bytes"] <= MAX_ARCHIVE_BYTES,
            f"Invalid artifact size: {name}",
        )
        require(
            (artifact["id"], artifact["size_in_bytes"], artifact["digest"]) == PINNED_ARTIFACTS[name],
            f"Pinned artifact identity mismatch: {name}",
        )
        wheels[name] = artifact
    require(set(wheels) == set(EXPECTED), f"Missing official wheels: {sorted(set(EXPECTED) - set(wheels))}")
    require(len({item["id"] for item in wheels.values()}) == len(wheels), "Duplicate artifact IDs")
    return wheels


def safe_members(archive: zipfile.ZipFile) -> set[str]:
    members = archive.infolist()
    require(len(members) <= 10000, "Wheel contains too many ZIP entries")
    require(sum(item.file_size for item in members) <= MAX_UNCOMPRESSED_BYTES, "Wheel exceeds unpacked size limit")
    names = set()
    for item in members:
        name = item.orig_filename
        parts = name.rstrip("/").split("/")
        require(
            name and "\\" not in name and ":" not in name and all(part not in ("", ".", "..") for part in parts),
            "Unsafe ZIP member path",
        )
        require(not stat.S_ISLNK(item.external_attr >> 16), "ZIP symlinks are not supported")
        require(name not in names, "Duplicate ZIP member")
        require(not item.flag_bits & 1, "Encrypted ZIP member")
        names.add(name)
    return names


def small_member(archive: zipfile.ZipFile, name: str) -> bytes:
    require(archive.getinfo(name).file_size <= 1024 * 1024, f"Oversized wheel metadata: {name}")
    return archive.read(name)


def verify_wheel(path: Path, name: str, artifact: dict) -> dict:
    require(path.is_file() and not path.is_symlink(), f"Missing or unsafe cached artifact: {name}")
    _, pinned_size, pinned_digest = PINNED_ARTIFACTS[name]
    require(path.stat().st_size == pinned_size, f"Artifact size mismatch: {name}")
    digest = sha256(path)
    require(f"sha256:{digest}" == pinned_digest, f"Archive SHA256 mismatch: {name}")
    python, system = EXPECTED[name]
    expected_tag = f"{python}-{python}-{PLATFORMS[system]}"
    prefix = f"endstone-{VERSION}.dist-info"
    embedded_commit = None
    with zipfile.ZipFile(path) as archive:
        names = safe_members(archive)
        require(
            {entry for entry in names if entry.endswith(".dist-info/METADATA")} == {f"{prefix}/METADATA"},
            f"Missing or unexpected wheel METADATA: {name}",
        )
        require(f"{prefix}/WHEEL" in names, f"Missing WHEEL tags: {name}")
        metadata = BytesParser().parsebytes(small_member(archive, f"{prefix}/METADATA"))
        require(metadata.get_all("Name") == ["endstone"], f"Wheel package name mismatch: {name}")
        require(metadata.get_all("Version") == [VERSION], f"Wheel version mismatch: {name}")
        wheel = BytesParser().parsebytes(small_member(archive, f"{prefix}/WHEEL"))
        require(wheel.get_all("Tag") == [expected_tag], f"Wheel tags mismatch: {name}")
        require(wheel.get_all("Root-Is-Purelib") == ["false"], f"Expected native wheel: {name}")
        version_file = "endstone/_version.py"
        if version_file in names:
            assignments = {}
            for node in ast.parse(small_member(archive, version_file)).body:
                if isinstance(node, ast.Assign):
                    targets = node.targets
                elif isinstance(node, ast.AnnAssign) and node.value is not None:
                    targets = [node.target]
                else:
                    continue
                for target in targets:
                    if isinstance(target, ast.Name) and target.id in (
                        "version",
                        "__version__",
                        "commit_id",
                        "__commit_id__",
                    ):
                        assignments[target.id] = ast.literal_eval(node.value)
            versions = [value for key, value in assignments.items() if key in ("version", "__version__")]
            commits = [value for key, value in assignments.items() if key in ("commit_id", "__commit_id__")]
            require(versions and all(value == VERSION for value in versions), f"Embedded version mismatch: {name}")
            require(
                commits and all(value in (COMMIT, f"g{COMMIT}", COMMIT[:9], f"g{COMMIT[:9]}") for value in commits),
                f"Embedded commit mismatch: {name}",
            )
            embedded_commit = commits[0]
    return {
        "filename": name,
        "python_tag": python,
        "platform": system,
        "tag": expected_tag,
        "artifact_id": artifact["id"],
        "archive_digest": artifact["digest"],
        "wheel_sha256": digest,
        "size_in_bytes": path.stat().st_size,
        "embedded_commit": embedded_commit,
    }


def publish_file(source: Path, destination: Path) -> None:
    if destination.exists() or destination.is_symlink():
        require(
            destination.is_file() and not destination.is_symlink() and sha256(destination) == sha256(source),
            f"Conflicting destination: {destination.name}",
        )
        return
    os.link(source, destination)


def prepare(output_dir: Path, python_tag: str | None = None, platform: str | None = None) -> dict:
    artifacts = official_artifacts()
    require(not output_dir.is_symlink(), "Wheelhouse must not be a symlink")
    output_dir = output_dir.absolute()
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    require(not output_dir.exists() or output_dir.is_dir(), "Wheelhouse must be a directory")
    selected = {
        name
        for name, (python, system) in EXPECTED.items()
        if (python_tag is None or python == python_tag) and (platform is None or system == platform)
    }
    require(selected, "No wheels selected")
    existing = {path.name for path in output_dir.glob("endstone*.whl")}
    require(existing <= EXPECTED.keys(), "Wheelhouse contains unexpected Endstone wheels")
    selected |= existing
    cache = output_dir / ".archives"
    require(not cache.is_symlink(), "Archive cache must not be a symlink")
    manifest_path = output_dir / MANIFEST
    require(not manifest_path.is_symlink(), "Manifest must not be a symlink")
    records = []
    with tempfile.TemporaryDirectory(prefix=".endstone-wheels-", dir=output_dir.parent) as temporary:
        staging = Path(temporary)
        for name in sorted(selected):
            artifact = artifacts[name]
            cached = cache / f"{artifact['id']}.zip"
            destination = output_dir / name
            staged = staging / name
            if cached.exists() or cached.is_symlink():
                record = verify_wheel(cached, name, artifact)
                shutil.copyfile(cached, staged)
            elif destination.exists() or destination.is_symlink():
                record = verify_wheel(destination, name, artifact)
                shutil.copyfile(destination, staged)
            else:
                with staged.open("wb") as stream:
                    gh_api(f"repos/{REPOSITORY}/actions/artifacts/{artifact['id']}/zip", output=stream)
                record = verify_wheel(staged, name, artifact)
            if destination.exists() or destination.is_symlink():
                verify_wheel(destination, name, artifact)
            records.append(record)
        manifest = {
            "schema_version": 1,
            "repository": REPOSITORY,
            "run_id": RUN_ID,
            "commit": COMMIT,
            "version": VERSION,
            "wheels": records,
        }
        staged_manifest = staging / MANIFEST
        staged_manifest.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        output_dir.mkdir(exist_ok=True)
        cache.mkdir(exist_ok=True)
        for record in records:
            source = staging / record["filename"]
            publish_file(source, cache / f"{record['artifact_id']}.zip")
            publish_file(source, output_dir / record["filename"])
        os.replace(staged_manifest, manifest_path)
    return manifest


def verify_only(output_dir: Path, python_tag: str | None = None, platform: str | None = None) -> dict:
    require(PINNED_ARTIFACTS.keys() == EXPECTED.keys(), "Incomplete pinned artifact identities")
    require(
        output_dir.is_dir() and not output_dir.is_symlink(), "Wheelhouse must be an existing directory, not a symlink"
    )
    selected = {
        name
        for name, (python, system) in EXPECTED.items()
        if (python_tag is None or python == python_tag) and (platform is None or system == platform)
    }
    require(selected, "No wheels selected")
    entries = {path.name: path for path in output_dir.iterdir()}
    unexpected = entries.keys() - (PINNED_ARTIFACTS.keys() | {MANIFEST, ".archives"})
    require(not unexpected, f"Unexpected wheelhouse entries: {sorted(unexpected)}")
    if MANIFEST in entries:
        metadata = entries[MANIFEST]
        require(metadata.is_file() and not metadata.is_symlink(), "Manifest must be a regular nonsymlink file")
    if ".archives" in entries:
        cache = entries[".archives"]
        require(cache.is_dir() and not cache.is_symlink(), "Archive cache must be a nonsymlink directory")
    selected |= entries.keys() & PINNED_ARTIFACTS.keys()
    records = []
    for name in sorted(selected):
        identifier, size, digest = PINNED_ARTIFACTS[name]
        artifact = {"id": identifier, "size_in_bytes": size, "digest": digest}
        records.append(verify_wheel(output_dir / name, name, artifact))
    return {
        "schema_version": 1,
        "repository": REPOSITORY,
        "run_id": RUN_ID,
        "commit": COMMIT,
        "version": VERSION,
        "wheels": records,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--python-tag", choices=PYTHON_TAGS)
    parser.add_argument("--platform", choices=PLATFORMS)
    parser.add_argument(
        "--verify-only", action="store_true", help="Verify staged wheels offline without changing files"
    )
    arguments = parser.parse_args(argv)
    try:
        operation = verify_only if arguments.verify_only else prepare
        manifest = operation(arguments.output_dir, arguments.python_tag, arguments.platform)
    except (VerificationError, OSError, ValueError, SyntaxError, zipfile.BadZipFile) as error:
        print(f"Endstone wheel staging failed: {error}", file=sys.stderr)
        return 1
    print(f"Verified {len(manifest['wheels'])} official Endstone wheels in {arguments.output_dir.absolute()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
