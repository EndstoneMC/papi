"""Build the exact Linux libc++/libc++abi link-time runtime used by PAPI wheels."""

from __future__ import annotations

import hashlib
import subprocess
import tarfile
import tempfile
import urllib.request
from pathlib import Path

_VERSION = "20.1.8"
_ARCHIVE = f"llvm-project-{_VERSION}.src.tar.xz"
_URL = f"https://github.com/llvm/llvm-project/releases/download/llvmorg-{_VERSION}/{_ARCHIVE}"
_SHA256 = "6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d"


def main() -> int:
    compiler = Path("/usr/bin/clang-20")
    compiler_cxx = Path("/usr/bin/clang++-20")
    if not compiler.is_file() or not compiler_cxx.is_file():
        raise SystemExit("build_linux_runtime: /usr/bin/clang-20 and clang++-20 are required")

    with tempfile.TemporaryDirectory(prefix="papi-llvm-runtime-") as temporary_directory:
        root = Path(temporary_directory)
        archive = root / _ARCHIVE
        urllib.request.urlretrieve(_URL, archive)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if digest != _SHA256:
            raise SystemExit(f"build_linux_runtime: SHA-256 mismatch: {digest}")

        with tarfile.open(archive) as source_archive:
            source_archive.extractall(root)
        source = root / f"llvm-project-{_VERSION}.src" / "runtimes"
        build = root / "build"
        subprocess.check_call(
            [
                "cmake",
                "-B",
                str(build),
                "-S",
                str(source),
                "-G",
                "Ninja",
                f"-DCMAKE_C_COMPILER={compiler}",
                f"-DCMAKE_CXX_COMPILER={compiler_cxx}",
                "-DCMAKE_BUILD_TYPE=Release",
                "-DLLVM_ENABLE_RUNTIMES=libcxx;libcxxabi",
                "-DLIBCXX_CXX_ABI=libcxxabi",
                "-DLIBCXXABI_USE_LLVM_UNWINDER=OFF",
                "-DLIBCXX_ENABLE_EXPERIMENTAL_LIBRARY=OFF",
                "-DLIBCXX_INCLUDE_TESTS=OFF",
                "-DLIBCXXABI_INCLUDE_TESTS=OFF",
            ]
        )
        subprocess.check_call(["cmake", "--build", str(build), "--target", "install-cxx", "install-cxxabi", "--"])

    print(f"build_linux_runtime: installed LLVM {_VERSION} libc++/libc++abi with the system unwinder")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
