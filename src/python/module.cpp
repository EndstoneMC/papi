#include <pybind11/pybind11.h>

#include "endstone_papi/version.h"

namespace py = pybind11;

PYBIND11_MODULE(_papi, m)
{
    m.doc() = "Native core of the Endstone PlaceholderAPI framework.";
    m.attr("__version__") = std::string(papi::getVersion());
}
