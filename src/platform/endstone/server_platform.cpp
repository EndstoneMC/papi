#include "platform/endstone/server_platform.h"

#include <string>

namespace papi::detail {

ServerPlatform::ServerPlatform(endstone::Server &server, endstone::Logger &logger) : server_(&server), logger_(&logger)
{
}

bool ServerPlatform::isPrimaryThread() const
{
    // Detached means PAPI is gone; reporting false makes every gated operation refuse
    // rather than reach a server that may no longer be there.
    return server_ != nullptr && server_->isPrimaryThread();
}

bool ServerPlatform::isPluginEnabled(const std::string_view name) const
{
    if (!server_) {
        return false;
    }
    return server_->getPluginManager().isPluginEnabled(std::string(name));
}

bool ServerPlatform::isPluginEnabled(const endstone::Plugin &plugin) const
{
    if (!server_) {
        return false;
    }
    return server_->getPluginManager().isPluginEnabled(const_cast<endstone::Plugin *>(&plugin));
}

const endstone::Player *ServerPlatform::getOnlinePlayer(const endstone::UUID id) const
{
    if (!server_) {
        return nullptr;
    }
    return server_->getPlayer(id);
}

void ServerPlatform::log(const endstone::Logger::Level level, const std::string_view message)
{
    if (!logger_) {
        return;
    }
    logger_->log(level, message);
}

void ServerPlatform::callEvent(endstone::Event &event)
{
    if (!server_) {
        return;
    }
    server_->getPluginManager().callEvent(event);
}

void ServerPlatform::detach()
{
    server_ = nullptr;
    logger_ = nullptr;
}

}  // namespace papi::detail
