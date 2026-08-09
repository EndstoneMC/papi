#include "platform/endstone/bootstrap.h"

#include <utility>

#include <endstone/event/player/player_quit_event.h>
#include <endstone/event/server/plugin_disable_event.h>
#include <endstone/plugin/service_priority.h>
#include <endstone/server.h>

namespace papi::detail {

PapiBootstrap::PapiBootstrap() = default;

PapiBootstrap::~PapiBootstrap()
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

    plugin.getServer().getServiceManager().registerService(std::string(PlaceholderAPI::ServiceName), service_, plugin,
                                                           endstone::ServicePriority::Normal);
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

    // Order matters. The service goes inert and releases every provider object while
    // the owning modules and the interpreter are still loaded; only afterwards are
    // the platform references dropped.
    service_->shutdown();

    if (plugin_) {
        plugin_->getServer().getServiceManager().unregister(std::string(PlaceholderAPI::ServiceName), *service_);
        // Defensive: PAPI registers no other service, but this guarantees the manager
        // holds nothing owned by this plugin.
        plugin_->getServer().getServiceManager().unregisterAll(*plugin_);
    }

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
