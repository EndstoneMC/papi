#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <endstone/endstone.hpp>

#include <endstone_papi/papi.h>

namespace {

/**
 * @brief A minimal provider expansion: answers {player.name}.
 *
 * PAPI's core supplies no placeholder values of its own; every value comes from an
 * expansion like this one, registered by a plugin.
 */
class NameExpansion final : public papi::PlaceholderExpansion {
public:
    [[nodiscard]] std::string getIdentifier() const override { return "player"; }
    [[nodiscard]] std::string getAuthor() const override { return "Endstone"; }
    [[nodiscard]] std::string getVersion() const override { return "1.0.0"; }

    [[nodiscard]] std::optional<std::string> onRequest(const endstone::OfflinePlayer *player,
                                                       const std::string_view params) override
    {
        if (params != "name") {
            // Unresolved: PAPI leaves the original placeholder text in place.
            return std::nullopt;
        }
        // The player may be null, and is borrowed for this call only.
        return player ? player->getName() : std::string("nobody");
    }
};

}  // namespace

class JoinExample : public endstone::Plugin {
public:
    void onEnable() override
    {
        api_ =
            getServer().getServiceManager().load<papi::PlaceholderAPI>(std::string(papi::PlaceholderAPI::ServiceName));
        if (!api_ || !api_->isActive()) {
            getLogger().warning("PlaceholderAPI is unavailable; disabling example.");
            getServer().getPluginManager().disablePlugin(*this);
            return;
        }

        if (!api_->registerExpansion(*this, std::make_shared<NameExpansion>())) {
            getLogger().warning("Could not register the example expansion.");
            return;
        }

        registerEvent(&JoinExample::onPlayerJoin, *this, endstone::EventPriority::Highest);
    }

    void onDisable() override
    {
        // PAPI removes owned expansions on disable anyway; doing it here is explicit.
        if (api_) {
            api_->unregisterExpansions(*this);
            api_.reset();
        }
    }

    void onPlayerJoin(endstone::PlayerJoinEvent &event)
    {
        if (!api_) {
            return;
        }
        event.setJoinMessage(api_->setPlaceholders(&event.getPlayer(), "{player.name} joined the server!"));
    }

private:
    std::shared_ptr<papi::PlaceholderAPI> api_;
};

ENDSTONE_PLUGIN(/*name=*/"papi_example", /*version=*/"1.0.0", /*main_class=*/JoinExample)
{
    prefix = "PlaceholderAPI Example";
    description = "An example plugin that provides and consumes placeholders";
    website = "https://github.com/EndstoneMC/papi";
    authors = {"Vincent <magicdroidx@gmail.com>"};
    soft_depend = {"papi"};
}
