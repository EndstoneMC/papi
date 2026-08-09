#pragma once

#include <optional>
#include <string>

namespace papi {

/**
 * @brief An immutable copy of a registered expansion's metadata.
 *
 * Instances are pure values. They contain no plugin pointer, expansion pointer,
 * callback, or registry reference, so they stay valid and unchanged after the
 * expansion they describe is unregistered and destroyed.
 */
struct ExpansionInfo {
    /**
     * @brief The canonical identifier, always ASCII lowercase.
     */
    std::string identifier;

    /**
     * @brief The human-readable name reported by the expansion.
     */
    std::string name;

    /**
     * @brief The author reported by the expansion.
     */
    std::string author;

    /**
     * @brief The version reported by the expansion.
     */
    std::string version;

    /**
     * @brief The name of the Endstone plugin that registered the expansion.
     */
    std::string owner;

    /**
     * @brief The plugin the expansion requires, if it declared one.
     */
    std::optional<std::string> required_plugin;

    /**
     * @brief Whether the expansion handles relational placeholders.
     */
    bool relational = false;

    [[nodiscard]] friend bool operator==(const ExpansionInfo &, const ExpansionInfo &) = default;
};

}  // namespace papi
