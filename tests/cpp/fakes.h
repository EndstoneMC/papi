#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <endstone/logger.h>
#include <endstone/player.h>
#include <endstone/plugin/plugin.h>
#include <endstone/plugin/plugin_description.h>

#include "endstone_papi/events.h"
#include "endstone_papi/placeholder_expansion.h"

#include "core/platform.h"

namespace papi::testing {

/**
 * @brief A logger that records what PAPI wrote instead of printing it.
 */
class RecordingLogger final : public endstone::Logger {
public:
    struct Record {
        Level level;
        std::string message;
    };

    void setLevel(const Level level) override { level_ = level; }
    [[nodiscard]] bool isEnabledFor(const Level level) const override { return level >= level_; }
    [[nodiscard]] std::string_view getName() const override { return "papi-test"; }

    void log(const Level level, const std::string_view message) const override
    {
        records.push_back(Record{level, std::string(message)});
    }

    [[nodiscard]] std::size_t countAtLeast(const Level level) const
    {
        std::size_t count = 0;
        for (const auto &record : records) {
            if (record.level >= level) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] bool anyContains(const std::string_view needle) const
    {
        for (const auto &record : records) {
            if (record.message.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    void clear() const { records.clear(); }

    mutable std::vector<Record> records;

private:
    Level level_ = Trace;
};

/**
 * @brief A plugin identity for tests.
 *
 * Only the plugin's name and its enabled flag matter to the registry, and the
 * registry treats the object address as identity, so this stays deliberately thin.
 */
class FakePlugin final : public endstone::Plugin {
public:
    explicit FakePlugin(std::string name) : description_(std::move(name), "1.0.0") {}

    [[nodiscard]] const endstone::PluginDescription &getDescription() const override { return description_; }

private:
    endstone::PluginDescription description_;
};

/**
 * @brief A copy of a dispatched event, taken while it was still alive.
 */
struct RecordedEvent {
    std::string name;
    bool asynchronous = false;
    std::optional<ExpansionInfo> info;
    std::optional<UnregisterReason> reason;
};

/**
 * @brief A Platform that models plugin state and thread identity in memory.
 */
class FakePlatform final : public detail::Platform {
public:
    [[nodiscard]] bool isPrimaryThread() const override { return primary_thread; }

    [[nodiscard]] bool isPluginEnabled(const std::string_view name) const override
    {
        return enabled_names.contains(std::string(name));
    }

    [[nodiscard]] bool isPluginEnabled(const endstone::Plugin &plugin) const override
    {
        return enabled_plugins.contains(&plugin);
    }

    void log(const endstone::Logger::Level level, const std::string_view message) override
    {
        logger.log(level, message);
    }

    void callEvent(endstone::Event &event) override
    {
        // Recorded by value. Endstone dispatches events synchronously from stack
        // objects, so keeping the pointer would dangle the moment callEvent returns.
        RecordedEvent record{event.getEventName(), event.isAsynchronous(), std::nullopt, std::nullopt};
        if (const auto *registered = dynamic_cast<const ExpansionRegisteredEvent *>(&event)) {
            record.info = registered->getExpansionInfo();
        }
        else if (const auto *unregistered = dynamic_cast<const ExpansionUnregisteredEvent *>(&event)) {
            record.info = unregistered->getExpansionInfo();
            record.reason = unregistered->getReason();
        }
        events.push_back(record);

        // Stands in for a listener. Called while the event is being dispatched, so a test
        // can observe exactly what the registry looks like at that moment.
        if (on_event) {
            on_event(record);
        }
    }

    /**
     * @brief Invoked during dispatch, like a real listener would be.
     */
    std::function<void(const RecordedEvent &)> on_event;

    void enable(endstone::Plugin &plugin)
    {
        enabled_plugins.insert(&plugin);
        enabled_names.insert(plugin.getName());
    }

    void disable(endstone::Plugin &plugin)
    {
        enabled_plugins.erase(&plugin);
        enabled_names.erase(plugin.getName());
    }

    bool primary_thread = true;
    std::unordered_set<const endstone::Plugin *> enabled_plugins;
    std::unordered_set<std::string> enabled_names;
    std::vector<RecordedEvent> events;
    RecordingLogger logger;
};

/**
 * @brief A configurable expansion that records how the registry used it.
 */
class FakeExpansion final : public PlaceholderExpansion {
public:
    explicit FakeExpansion(std::string identifier) : identifier_(std::move(identifier)) {}

    [[nodiscard]] std::string getIdentifier() const override
    {
        ++identifier_calls;
        if (throw_from_identifier) {
            throw std::runtime_error("identifier failed");
        }
        return identifier_;
    }

    [[nodiscard]] std::string getAuthor() const override { return author; }
    [[nodiscard]] std::string getVersion() const override { return version; }

    [[nodiscard]] std::string getName() const override
    {
        if (report_empty_name) {
            return {};
        }
        return name.empty() ? identifier_ : name;
    }

    [[nodiscard]] std::optional<std::string> getRequiredPlugin() const override { return required_plugin; }

    [[nodiscard]] bool canRegister() const override
    {
        ++can_register_calls;
        if (throw_from_can_register) {
            throw std::runtime_error("can_register failed");
        }
        return can_register;
    }

    [[nodiscard]] bool supportsRelationalPlaceholders() const override { return relational; }
    [[nodiscard]] bool supportsPlayerCleanup() const override { return player_cleanup; }

    [[nodiscard]] std::optional<std::string> onRequest(const endstone::OfflinePlayer *player,
                                                       const std::string_view params) override
    {
        ++request_calls;
        last_player = player;
        last_params = std::string(params);
        if (throw_from_request) {
            throw std::runtime_error("request failed");
        }
        if (on_request) {
            return on_request(player, params);
        }
        return value;
    }

    [[nodiscard]] std::optional<std::string> onRelationalRequest(const endstone::Player &one,
                                                                 const endstone::Player &two,
                                                                 const std::string_view params) override
    {
        ++relational_calls;
        last_relational_params = std::string(params);
        last_relational_one = &one;
        last_relational_two = &two;
        if (throw_from_relational) {
            throw std::runtime_error("relational failed");
        }
        return relational_value;
    }

    void onPlayerQuit(const endstone::Player &player) override
    {
        ++player_quit_calls;
        last_quit_player = &player;
        if (throw_from_player_quit) {
            throw std::runtime_error("player quit failed");
        }
    }

    void onUnregister(const UnregisterReason reason) override
    {
        ++unregister_calls;
        last_unregister_reason = reason;
        if (on_unregister) {
            on_unregister(reason);
        }
        if (throw_from_unregister) {
            throw std::runtime_error("unregister failed");
        }
    }

    std::string author = "author";
    std::string version = "1.0.0";
    std::string name;
    bool report_empty_name = false;
    std::optional<std::string> required_plugin;
    std::optional<std::string> value = "value";
    std::optional<std::string> relational_value = "related";
    bool can_register = true;
    bool relational = false;
    bool player_cleanup = false;

    bool throw_from_identifier = false;
    bool throw_from_can_register = false;
    bool throw_from_request = false;
    bool throw_from_relational = false;
    bool throw_from_player_quit = false;
    bool throw_from_unregister = false;

    std::function<std::optional<std::string>(const endstone::OfflinePlayer *, std::string_view)> on_request;
    std::function<void(UnregisterReason)> on_unregister;

    mutable int identifier_calls = 0;
    mutable int can_register_calls = 0;
    int request_calls = 0;
    int relational_calls = 0;
    int player_quit_calls = 0;
    int unregister_calls = 0;

    const endstone::OfflinePlayer *last_player = nullptr;
    const endstone::Player *last_relational_one = nullptr;
    const endstone::Player *last_relational_two = nullptr;
    const endstone::Player *last_quit_player = nullptr;
    std::string last_params;
    std::string last_relational_params;
    std::optional<UnregisterReason> last_unregister_reason;

private:
    std::string identifier_;
};

/**
 * @brief Observes when an expansion is destroyed.
 *
 * Used to prove the registry releases provider objects at the right moment rather
 * than leaking them until process exit.
 */
class LifetimeTrackingExpansion final : public PlaceholderExpansion {
public:
    LifetimeTrackingExpansion(std::string identifier, int &destruction_counter)
        : identifier_(std::move(identifier)), destruction_counter_(destruction_counter)
    {
    }

    ~LifetimeTrackingExpansion() override { ++destruction_counter_; }

    [[nodiscard]] std::string getIdentifier() const override { return identifier_; }
    [[nodiscard]] std::string getAuthor() const override { return "author"; }
    [[nodiscard]] std::string getVersion() const override { return "1.0.0"; }

    [[nodiscard]] std::optional<std::string> onRequest(const endstone::OfflinePlayer *, std::string_view) override
    {
        return "value";
    }

private:
    std::string identifier_;
    int &destruction_counter_;
};

}  // namespace papi::testing
