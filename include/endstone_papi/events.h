#pragma once

#include <utility>

#include <endstone/event/event.h>
#include <endstone/event/server/server_event.h>

#include "endstone_papi/expansion_info.h"
#include "endstone_papi/unregister_reason.h"

namespace papi {

/**
 * @brief Fired after an expansion has been added to the registry.
 *
 * Synchronous, non-cancellable, and dispatched after the registry has committed,
 * so a listener querying PlaceholderAPI already observes the new expansion.
 *
 * Only copied metadata is exposed; the expansion object is never reachable from
 * the event.
 */
class ExpansionRegisteredEvent : public endstone::ServerEvent {
public:
    ENDSTONE_EVENT(ExpansionRegisteredEvent);

    explicit ExpansionRegisteredEvent(ExpansionInfo info) : info_(std::move(info)) {}

    /**
     * @brief Metadata describing the expansion that was registered.
     */
    [[nodiscard]] const ExpansionInfo &getExpansionInfo() const { return info_; }

private:
    ExpansionInfo info_;
};

/**
 * @brief Fired after an expansion has been removed from the registry.
 *
 * Synchronous, non-cancellable, and dispatched after the registry has removed the
 * entry and the expansion's cleanup callback has returned, so a listener querying
 * PlaceholderAPI no longer observes the expansion.
 *
 * This event is suppressed during PAPI's own shutdown, because the event system
 * and its listeners are being torn down at that point.
 */
class ExpansionUnregisteredEvent : public endstone::ServerEvent {
public:
    ENDSTONE_EVENT(ExpansionUnregisteredEvent);

    ExpansionUnregisteredEvent(ExpansionInfo info, const UnregisterReason reason)
        : info_(std::move(info)), reason_(reason)
    {
    }

    /**
     * @brief Metadata describing the expansion that was removed.
     */
    [[nodiscard]] const ExpansionInfo &getExpansionInfo() const { return info_; }

    /**
     * @brief Why the expansion was removed.
     */
    [[nodiscard]] UnregisterReason getReason() const { return reason_; }

private:
    ExpansionInfo info_;
    UnregisterReason reason_;
};

}  // namespace papi
