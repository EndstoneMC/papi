// Standalone consumer built only against public headers and Endstone.

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <endstone_papi/papi.h>

namespace {

class DemoExpansion final : public papi::PlaceholderExpansion {
public:
    [[nodiscard]] std::string getIdentifier() const override { return "demo"; }
    [[nodiscard]] std::string getAuthor() const override { return "Endstone"; }
    [[nodiscard]] std::string getVersion() const override { return "1.0.0"; }

    [[nodiscard]] bool supportsRelationalPlaceholders() const override { return true; }

    [[nodiscard]] std::optional<std::string> onRequest(const endstone::OfflinePlayer *player,
                                                       const std::string_view params) override
    {
        if (params != "name") {
            return std::nullopt;
        }
        return player ? player->getName() : std::string("nobody");
    }

    [[nodiscard]] std::optional<std::string> onRelationalRequest(const endstone::Player &one,
                                                                 const endstone::Player &two,
                                                                 const std::string_view params) override
    {
        if (params != "same") {
            return std::nullopt;
        }
        return one.getUniqueId() == two.getUniqueId() ? "yes" : "no";
    }
};

class DemoPlugin final : public endstone::Plugin {
public:
    void onEnable() override
    {
        api_ =
            getServer().getServiceManager().load<papi::PlaceholderAPI>(std::string(papi::PlaceholderAPI::ServiceName));
        if (!api_ || !api_->isActive()) {
            getLogger().warning("PlaceholderAPI is unavailable.");
            return;
        }

        if (!api_->registerExpansion(*this, std::make_shared<DemoExpansion>())) {
            getLogger().warning("Could not register the demo expansion.");
            return;
        }

        for (const papi::ExpansionInfo &info : api_->getExpansions()) {
            getLogger().info("{} v{} by {}", info.identifier, info.version, info.author);
        }
    }

    void onDisable() override
    {
        if (api_) {
            api_->unregisterExpansions(*this);
            api_.reset();
        }
    }

    [[nodiscard]] const endstone::PluginDescription &getDescription() const override { return *description_; }

private:
    std::shared_ptr<papi::PlaceholderAPI> api_;
    const endstone::PluginDescription *description_ = nullptr;
};

}  // namespace

int main()
{
    return static_cast<int>(sizeof(DemoPlugin) + sizeof(DemoExpansion));
}
