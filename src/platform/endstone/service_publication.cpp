#include "platform/endstone/service_publication.h"

#include <mutex>
#include <unordered_map>

namespace papi::detail {
namespace {

struct PublicationState {
    std::mutex mutex;
    std::unordered_map<const endstone::ServiceManager *, std::weak_ptr<PlaceholderAPI>> services;
};

PublicationState &publicationState()
{
    // Process-lifetime storage avoids extension teardown ordering hazards.
    static auto *state = new PublicationState;
    return *state;
}

}  // namespace

void ServicePublication::publish(const endstone::ServiceManager &manager,
                                 const std::shared_ptr<PlaceholderAPI> &service)
{
    auto &state = publicationState();
    const std::scoped_lock lock(state.mutex);
    state.services[&manager] = service;
}

void ServicePublication::withdraw(const endstone::ServiceManager &manager, const PlaceholderAPI &service)
{
    auto &state = publicationState();
    const std::scoped_lock lock(state.mutex);
    const auto it = state.services.find(&manager);
    if (it == state.services.end()) {
        return;
    }

    const auto published = it->second.lock();
    if (!published || published.get() == &service) {
        state.services.erase(it);
    }
}

std::shared_ptr<PlaceholderAPI> ServicePublication::load(const endstone::ServiceManager &manager)
{
    const auto selected = manager.get(std::string(PlaceholderAPI::ServiceName));
    if (!selected) {
        return nullptr;
    }

    std::shared_ptr<PlaceholderAPI> published;
    {
        auto &state = publicationState();
        const std::scoped_lock lock(state.mutex);
        const auto it = state.services.find(&manager);
        if (it == state.services.end()) {
            return nullptr;
        }
        published = it->second.lock();
        if (!published) {
            state.services.erase(it);
            return nullptr;
        }
    }

    if (selected.get() != published.get() || !published->isActive()) {
        return nullptr;
    }
    return published;
}

}  // namespace papi::detail
