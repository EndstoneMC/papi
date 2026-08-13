#include "platform/endstone/bootstrap.h"

#include <utility>

#include <endstone/event/player/player_quit_event.h>
#include <endstone/event/server/plugin_disable_event.h>
#include <endstone/plugin/service_priority.h>
#include <endstone/server.h>

#include "platform/endstone/service_publication.h"

namespace papi::detail {

PapiBootstrap::PapiBootstrap() = default;

PapiBootstrap::~PapiBootstrap()  // NOLINT(bugprone-exception-escape)
{
    stop();
}

bool PapiBootstrap::start(endstone::Plugin &plugin)
{
    if (service_) {
        return true;
    }

    plugin_ = &plugin;
    platform_ = std::make_shared<ServerPlatform>(plugin.getServer(), plugin.getLogger());
    service_ = std::make_shared<PlaceholderApiImpl>(platform_, plugin.getName());

    auto &service_manager = plugin.getServer().getServiceManager();
    service_manager.registerService(std::string(PlaceholderAPI::ServiceName), service_, plugin,
                                    endstone::ServicePriority::Normal);
    ServicePublication::publish(service_manager, service_);
    registerListeners(plugin);
    return true;
}

void PapiBootstrap::registerListeners(endstone::Plugin &plugin)
{
    // Captured by value: the handlers must not resurrect a dead service, and a
    // weak reference would be pointless because stop() runs before the plugin's
    // Python object or module can be released.
    auto service = service_;

    plugin.registerEvent<endstone::PluginDisableEvent>(
        [service](endstone::PluginDisableEvent &event) { service->handlePluginDisabled(event.getPlugin()); },
        endstone::EventPriority::Normal, false);

    plugin.registerEvent<endstone::PlayerQuitEvent>(
        [service](endstone::PlayerQuitEvent &event) { service->handlePlayerQuit(event.getPlayer()); },
        endstone::EventPriority::Normal, false);
}

void PapiBootstrap::stop()
{
    if (!service_) {
        return;
    }

    // The frozen lifecycle requires this order:
    //   1. Transition the service non-operational (Stopping) so nothing new can
    //      start and isActive returns false.
    //   2. Unregister the named service from the ServiceManager so shutdown
    //      callbacks cannot rediscover PAPI as a still-published service.
    //   3. Drain the registry: run onUnregister(PapiShutdown), release every
    //      provider object while the owning modules are still loaded.
    //   4. Drop platform references.
    service_->beginShutdown();

    if (plugin_) {
        auto &service_manager = plugin_->getServer().getServiceManager();
        service_manager.unregister(std::string(PlaceholderAPI::ServiceName), *service_);
        ServicePublication::withdraw(service_manager, *service_);
        // Defensive: PAPI registers no other service, but this guarantees the manager
        // holds nothing owned by this plugin.
        service_manager.unregisterAll(*plugin_);
    }

    service_->finishShutdown();

    if (platform_) {
        platform_->detach();
    }

    // A consumer that kept a reference now holds a fully inert native object. It
    // co-owns the detached platform, so nothing it can call reaches the server, and
    // the bootstrap's own references go away here.
    service_.reset();
    platform_.reset();
    plugin_ = nullptr;
}

std::shared_ptr<PlaceholderAPI> PapiBootstrap::getService() const
{
    return service_;
}

}  // namespace papi::detail
