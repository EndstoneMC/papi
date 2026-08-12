#pragma once

#include <memory>

#include <endstone/plugin/service_manager.h>

#include "endstone_papi/placeholder_api.h"

namespace papi::detail {

/**
 * @brief Tracks the exact native PAPI service published through each manager.
 *
 * Endstone's typed ServiceManager loader is an unchecked static cast. Python
 * consumers instead use this PAPI-owned publication record to prove exact object
 * identity before receiving the already-typed shared pointer.
 */
class ServicePublication final {
public:
    static void publish(const endstone::ServiceManager &manager, const std::shared_ptr<PlaceholderAPI> &service);
    static void withdraw(const endstone::ServiceManager &manager, const PlaceholderAPI &service);

    [[nodiscard]] static std::shared_ptr<PlaceholderAPI> load(const endstone::ServiceManager &manager);
};

}  // namespace papi::detail
