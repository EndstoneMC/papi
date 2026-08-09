#pragma once

#include <string_view>

namespace papi {

/**
 * @brief The version of Endstone PAPI that this build provides.
 */
[[nodiscard]] std::string_view getVersion() noexcept;

}  // namespace papi
