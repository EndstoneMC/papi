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
    // Listener ownership cannot outlive stop().
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

    // Stop dispatch before withdrawing the service and releasing providers.
    service_->beginShutdown();

    if (plugin_) {
        auto &service_manager = plugin_->getServer().getServiceManager();
        service_manager.unregister(std::string(PlaceholderAPI::ServiceName), *service_);
        ServicePublication::withdraw(service_manager, *service_);
        service_manager.unregisterAll(*plugin_);
    }

    service_->finishShutdown();

    if (platform_) {
        platform_->detach();
    }

    // Retained services remain inert through their detached platform.
    service_.reset();
    platform_.reset();
    plugin_ = nullptr;
}

std::shared_ptr<PlaceholderAPI> PapiBootstrap::getService() const
{
    return service_;
}

}  // namespace papi::detail
