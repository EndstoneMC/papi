import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


class PapiRecipe(ConanFile):
    """Dependency provider for the Endstone PAPI native core.

    PAPI itself is delivered as a Python wheel, so this recipe only pins the
    build dependencies that the CMake project consumes.
    """

    name = "endstone_papi"
    package_type = "shared-library"
    license = "MIT"
    url = "https://github.com/EndstoneMC/papi"
    homepage = "https://github.com/EndstoneMC/papi"
    description = "PlaceholderAPI framework for Endstone, with C++ and Python expansion providers."
    topics = ("plugin", "python", "c++", "minecraft", "bedrock", "papi", "placeholder")

    settings = "os", "arch", "compiler", "build_type"

    @property
    def _min_cppstd(self):
        return 20

    def layout(self):
        cmake_layout(self)

    def requirements(self):
        # Matches Endstone 0.11's own pin so the fetched Endstone headers resolve
        # find_package(expected-lite) instead of downloading a second copy.
        self.requires("expected-lite/0.9.0")
        # Endstone 0.11 builds its bindings against pybind11 3.x; the expansion
        # trampoline needs py::smart_holder and py::trampoline_self_life_support.
        self.requires("pybind11/3.0.1")

    def build_requirements(self):
        self.test_requires("gtest/1.16.0")

    def validate(self):
        check_min_cppstd(self, self._min_cppstd)

        if self.settings.arch != "x86_64":
            raise ConanInvalidConfiguration(
                f"{self.ref} can only be built on x86_64. {self.settings.arch} is not supported."
            )

        if self.settings.os not in ("Windows", "Linux"):
            raise ConanInvalidConfiguration(
                f"{self.ref} can only be built on Windows or Linux. {self.settings.os} is not supported."
            )

        if self.settings.os == "Linux" and self.settings.compiler.libcxx != "libc++":
            raise ConanInvalidConfiguration(f"{self.ref} requires libc++ on Linux for Endstone ABI compatibility.")

    def generate(self):
        CMakeDeps(self).generate()
        tc = CMakeToolchain(self)
        endstone_tag = os.environ.get("PAPI_ENDSTONE_TAG")
        if endstone_tag:
            tc.variables["PAPI_ENDSTONE_TAG"] = endstone_tag
        tc.generate()
