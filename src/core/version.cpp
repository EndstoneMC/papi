#include "endstone_papi/version.h"

#ifndef PAPI_VERSION
#define PAPI_VERSION "0.0.0"
#endif

namespace papi {

std::string_view getVersion() noexcept
{
    return PAPI_VERSION;
}

}  // namespace papi
