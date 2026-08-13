// Registry and metadata behavior: test matrix .

#include <atomic>
#include <thread>

#include <gtest/gtest.h>

#include "core/parser/identifier.h"
#include "core/registry/expansion_manager.h"
#include "fakes.h"

// GoogleTest ASSERT_TRUE(x.has_value()) aborts on failure, but clang-tidy
// cannot track this through the macro, producing false positives below.
// NOLINTBEGIN(bugprone-unchecked-optional-access)
namespace {

using papi::UnregisterReason;
using papi::detail::ErrorThrottle;
using papi::detail::ExpansionManager;
using papi::detail::RegisterError;
using papi::testing::FakeExpansion;
using papi::testing::FakePlatform;
using papi::testing::FakePlugin;
using papi::testing::LifetimeTrackingExpansion;

class ExpansionManagerTest : public ::testing::Test {
protected:
    ExpansionManagerTest() : manager_(platform_, throttle_)
    {
        platform_.enable(owner_);
        platform_.enable(other_owner_);
    }

    std::shared_ptr<FakeExpansion> add(const std::string &identifier, FakePlugin &owner)
    {
        auto expansion = std::make_shared<FakeExpansion>(identifier);
        const auto result = manager_.registerExpansion(owner, expansion);
        EXPECT_TRUE(result.registered) << "identifier=" << identifier << " error=" << toString(result.error);
        return expansion;
    }

    FakePlatform platform_;
    ErrorThrottle throttle_;
    ExpansionManager manager_;
    FakePlugin owner_{"provider"};
    FakePlugin other_owner_{"other"};
};
TEST_F(ExpansionManagerTest, RegistersValidExpansionAndCopiesMetadata)
{
    auto expansion = std::make_shared<FakeExpansion>("Demo");
    expansion->author = "Endstone";
    expansion->version = "2.3.4";
    expansion->name = "Demo Expansion";

    const auto result = manager_.registerExpansion(owner_, expansion);
    ASSERT_TRUE(result.registered);
    EXPECT_EQ(result.error, RegisterError::None);
    EXPECT_EQ(result.info.identifier, "demo");
    EXPECT_EQ(result.info.name, "Demo Expansion");
    EXPECT_EQ(result.info.author, "Endstone");
    EXPECT_EQ(result.info.version, "2.3.4");
    EXPECT_EQ(result.info.owner, "provider");
    EXPECT_FALSE(result.info.required_plugin.has_value());
    EXPECT_FALSE(result.info.relational);

    ASSERT_EQ(manager_.size(), 1U);
    EXPECT_TRUE(manager_.isRegistered("demo"));
}
TEST_F(ExpansionManagerTest, IdentifierLookupIsCaseInsensitiveAndStoredLowercase)
{
    add("Demo", owner_);

    EXPECT_TRUE(manager_.isRegistered("demo"));
    EXPECT_TRUE(manager_.isRegistered("DEMO"));
    EXPECT_TRUE(manager_.isRegistered("DeMo"));
    ASSERT_EQ(manager_.getRegisteredIdentifiers().size(), 1U);
    EXPECT_EQ(manager_.getRegisteredIdentifiers().front(), "demo");
}
TEST_F(ExpansionManagerTest, DuplicateCanonicalIdentifierFailsAtomically)
{
    auto first = add("demo", owner_);

    auto same_owner = std::make_shared<FakeExpansion>("DEMO");
    const auto same_owner_result = manager_.registerExpansion(owner_, same_owner);
    EXPECT_FALSE(same_owner_result.registered);
    EXPECT_EQ(same_owner_result.error, RegisterError::DuplicateIdentifier);

    auto other = std::make_shared<FakeExpansion>("Demo");
    const auto other_result = manager_.registerExpansion(other_owner_, other);
    EXPECT_FALSE(other_result.registered);
    EXPECT_EQ(other_result.error, RegisterError::DuplicateIdentifier);

    // The original survives untouched, and no plugin:identifier fallback appears.
    ASSERT_EQ(manager_.size(), 1U);
    const auto entry = manager_.findCanonical("demo");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->getExpansion().get(), first.get());
    EXPECT_FALSE(manager_.isRegistered("other"));
}
TEST_F(ExpansionManagerTest, RejectsEveryInvalidIdentifierClass)
{
    const std::vector<std::string> invalid = {
        "",       "_",   "_demo", "demo_x", ".demo",  "-demo", "{demo}", "demo}",  "%demo%",
        "a:b",    "a b", " demo", "demo ",  "de\tmo", "démo",  "日本",   "demo\n", "demo/x",
        "demo\\", "a+b", "a,b",   "a;b",    "a\"b",   "a'b",   "a(b)",   "a[b]",   "a|b",
    };

    for (const auto &identifier : invalid) {
        auto expansion = std::make_shared<FakeExpansion>(identifier);
        const auto result = manager_.registerExpansion(owner_, expansion);
        EXPECT_FALSE(result.registered) << "unexpectedly accepted: '" << identifier << "'";
        EXPECT_EQ(result.error, RegisterError::InvalidIdentifier) << "identifier='" << identifier << "'";
        // Validation must fail before the expansion is asked whether it may register.
        EXPECT_EQ(expansion->can_register_calls, 0) << "identifier='" << identifier << "'";
    }
    EXPECT_EQ(manager_.size(), 0U);
}
TEST_F(ExpansionManagerTest, AcceptsAlphanumericDottedAndHyphenatedIdentifiers)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"demo", "demo"},
        {"Alpha", "alpha"},
        {"demo1", "demo1"},
        {"1demo", "1demo"},
        {"my.expansion", "my.expansion"},
        {"My-Expansion", "my-expansion"},
        {"a.b-c.D", "a.b-c.d"},
        {"x", "x"},
        {"9", "9"},
    };

    for (const auto &[raw, canonical] : cases) {
        auto expansion = std::make_shared<FakeExpansion>(raw);
        const auto result = manager_.registerExpansion(owner_, expansion);
        ASSERT_TRUE(result.registered) << "rejected: '" << raw << "'";
        EXPECT_EQ(result.info.identifier, canonical);
    }
    EXPECT_EQ(manager_.size(), cases.size());
}
TEST_F(ExpansionManagerTest, EmptyOrThrowingMetadataFailsWithoutSideEffects)
{
    auto no_author = std::make_shared<FakeExpansion>("demo");
    no_author->author = "";
    auto result = manager_.registerExpansion(owner_, no_author);
    EXPECT_FALSE(result.registered);
    EXPECT_EQ(result.error, RegisterError::InvalidMetadata);

    auto no_version = std::make_shared<FakeExpansion>("demo");
    no_version->version = "";
    result = manager_.registerExpansion(owner_, no_version);
    EXPECT_FALSE(result.registered);
    EXPECT_EQ(result.error, RegisterError::InvalidMetadata);

    auto throwing = std::make_shared<FakeExpansion>("demo");
    throwing->throw_from_identifier = true;
    result = manager_.registerExpansion(owner_, throwing);
    EXPECT_FALSE(result.registered);
    EXPECT_EQ(result.error, RegisterError::ProviderThrew);

    EXPECT_EQ(manager_.size(), 0U);
    EXPECT_TRUE(manager_.getExpansions().empty());
}

TEST_F(ExpansionManagerTest, MissingNameFallsBackToIdentifier)
{
    auto expansion = std::make_shared<FakeExpansion>("Demo");
    expansion->report_empty_name = true;
    const auto result = manager_.registerExpansion(owner_, expansion);
    ASSERT_TRUE(result.registered);
    EXPECT_EQ(result.info.name, "demo");
}
TEST_F(ExpansionManagerTest, PreflightRefusalPreventsRegistration)
{
    auto expansion = std::make_shared<FakeExpansion>("demo");
    expansion->can_register = false;

    const auto result = manager_.registerExpansion(owner_, expansion);
    EXPECT_FALSE(result.registered);
    EXPECT_EQ(result.error, RegisterError::PreflightRefused);
    EXPECT_EQ(expansion->can_register_calls, 1);
    EXPECT_EQ(manager_.size(), 0U);
}

TEST_F(ExpansionManagerTest, ThrowingPreflightIsContained)
{
    auto expansion = std::make_shared<FakeExpansion>("demo");
    expansion->throw_from_can_register = true;

    const auto result = manager_.registerExpansion(owner_, expansion);
    EXPECT_FALSE(result.registered);
    EXPECT_EQ(result.error, RegisterError::ProviderThrew);
    EXPECT_EQ(manager_.size(), 0U);
}
TEST_F(ExpansionManagerTest, DisabledOwnerCannotRegister)
{
    platform_.disable(owner_);
    auto expansion = std::make_shared<FakeExpansion>("demo");

    const auto result = manager_.registerExpansion(owner_, expansion);
    EXPECT_FALSE(result.registered);
    EXPECT_EQ(result.error, RegisterError::OwnerNotEnabled);
    // Metadata is not even requested from a plugin that is not enabled.
    EXPECT_EQ(expansion->identifier_calls, 0);
    EXPECT_EQ(manager_.size(), 0U);
}

TEST_F(ExpansionManagerTest, NullExpansionIsRejected)
{
    const auto result = manager_.registerExpansion(owner_, nullptr);
    EXPECT_FALSE(result.registered);
    EXPECT_EQ(result.error, RegisterError::NullExpansion);
}
TEST_F(ExpansionManagerTest, MissingOrDisabledRequiredPluginFails)
{
    auto absent = std::make_shared<FakeExpansion>("demo");
    absent->required_plugin = "economy";
    auto result = manager_.registerExpansion(owner_, absent);
    EXPECT_FALSE(result.registered);
    EXPECT_EQ(result.error, RegisterError::RequiredPluginUnavailable);
    EXPECT_TRUE(platform_.logger.anyContains("economy"));

    auto empty = std::make_shared<FakeExpansion>("demo");
    empty->required_plugin = "";
    result = manager_.registerExpansion(owner_, empty);
    EXPECT_FALSE(result.registered);
    EXPECT_EQ(result.error, RegisterError::InvalidRequiredPluginName);

    EXPECT_EQ(manager_.size(), 0U);
}
TEST_F(ExpansionManagerTest, EnabledRequiredPluginIsAcceptedAndCopied)
{
    FakePlugin economy{"economy"};
    platform_.enable(economy);

    auto expansion = std::make_shared<FakeExpansion>("demo");
    expansion->required_plugin = "economy";

    const auto result = manager_.registerExpansion(owner_, expansion);
    ASSERT_TRUE(result.registered);
    ASSERT_TRUE(result.info.required_plugin.has_value());
    EXPECT_EQ(*result.info.required_plugin, "economy");
}

TEST_F(ExpansionManagerTest, RequiredPluginNameIsCaseSensitive)
{
    FakePlugin economy{"economy"};
    platform_.enable(economy);

    auto expansion = std::make_shared<FakeExpansion>("demo");
    expansion->required_plugin = "Economy";

    const auto result = manager_.registerExpansion(owner_, expansion);
    EXPECT_FALSE(result.registered);
    EXPECT_EQ(result.error, RegisterError::RequiredPluginUnavailable);
}
TEST_F(ExpansionManagerTest, DetachRemovesEntryImmediately)
{
    add("demo", owner_);

    const auto removed = manager_.detach(owner_, "DEMO", UnregisterReason::Explicit);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(removed->info.identifier, "demo");
    EXPECT_EQ(removed->reason, UnregisterReason::Explicit);
    EXPECT_FALSE(removed->entry->isActive());

    EXPECT_EQ(manager_.size(), 0U);
    EXPECT_FALSE(manager_.isRegistered("demo"));
    EXPECT_EQ(manager_.findCanonical("demo"), nullptr);
}

TEST_F(ExpansionManagerTest, DetachIsNotRepeatable)
{
    add("demo", owner_);
    EXPECT_TRUE(manager_.detach(owner_, "demo", UnregisterReason::Explicit).has_value());
    EXPECT_FALSE(manager_.detach(owner_, "demo", UnregisterReason::Explicit).has_value());
}

TEST_F(ExpansionManagerTest, DetachOfUnknownOrInvalidIdentifierFails)
{
    add("demo", owner_);
    EXPECT_FALSE(manager_.detach(owner_, "missing", UnregisterReason::Explicit).has_value());
    EXPECT_FALSE(manager_.detach(owner_, "not a valid id", UnregisterReason::Explicit).has_value());
    EXPECT_EQ(manager_.size(), 1U);
}
TEST_F(ExpansionManagerTest, WrongOwnerCannotDetach)
{
    add("demo", owner_);

    EXPECT_FALSE(manager_.detach(other_owner_, "demo", UnregisterReason::Explicit).has_value());
    EXPECT_EQ(manager_.size(), 1U);
    EXPECT_TRUE(manager_.isRegistered("demo"));
}
TEST_F(ExpansionManagerTest, BulkDetachRemovesOnlyOwnEntries)
{
    add("beta", owner_);
    add("alpha", owner_);
    add("gamma", owner_);
    add("theirs", other_owner_);

    const auto removed = manager_.detachByOwner(owner_, UnregisterReason::Explicit);
    ASSERT_EQ(removed.size(), 3U);
    // Deterministic canonical order makes cleanup and logging reproducible.
    EXPECT_EQ(removed[0].info.identifier, "alpha");
    EXPECT_EQ(removed[1].info.identifier, "beta");
    EXPECT_EQ(removed[2].info.identifier, "gamma");

    EXPECT_EQ(manager_.size(), 1U);
    EXPECT_TRUE(manager_.isRegistered("theirs"));
}
TEST_F(ExpansionManagerTest, IntrospectionIsSortedAndCopiesOnly)
{
    add("zulu", owner_);
    add("alpha", other_owner_);
    add("mike", owner_);

    const auto identifiers = manager_.getRegisteredIdentifiers();
    ASSERT_EQ(identifiers.size(), 3U);
    EXPECT_EQ(identifiers[0], "alpha");
    EXPECT_EQ(identifiers[1], "mike");
    EXPECT_EQ(identifiers[2], "zulu");

    const auto infos = manager_.getExpansions();
    ASSERT_EQ(infos.size(), 3U);
    EXPECT_EQ(infos[0].identifier, "alpha");
    EXPECT_EQ(infos[1].identifier, "mike");
    EXPECT_EQ(infos[2].identifier, "zulu");
    EXPECT_EQ(infos[0].owner, "other");
    EXPECT_EQ(infos[1].owner, "provider");

    // Mutating a returned copy cannot reach the registry.
    auto mutated = infos;
    mutated[0].identifier = "hacked";
    EXPECT_EQ(manager_.getExpansions()[0].identifier, "alpha");
}
TEST_F(ExpansionManagerTest, MetadataCopySurvivesUnregisterAndProviderDestruction)
{
    int destroyed = 0;
    {
        auto expansion = std::make_shared<LifetimeTrackingExpansion>("demo", destroyed);
        ASSERT_TRUE(manager_.registerExpansion(owner_, expansion).registered);
    }

    const auto captured = manager_.getExpansions();
    ASSERT_EQ(captured.size(), 1U);

    auto removed = manager_.detach(owner_, "demo", UnregisterReason::Explicit);
    ASSERT_TRUE(removed.has_value());
    auto provider = removed->entry->releaseExpansion();
    provider.reset();
    removed.reset();
    EXPECT_EQ(destroyed, 1);

    // The snapshot taken before removal is unaffected by any of that.
    EXPECT_EQ(captured[0].identifier, "demo");
    EXPECT_EQ(captured[0].owner, "provider");
    EXPECT_EQ(captured[0].version, "1.0.0");
}

TEST_F(ExpansionManagerTest, ProviderIsReleasedOnlyWhenTheRegistryLetsGo)
{
    int destroyed = 0;
    auto expansion = std::make_shared<LifetimeTrackingExpansion>("demo", destroyed);
    ASSERT_TRUE(manager_.registerExpansion(owner_, expansion).registered);

    // Dropping the provider's own reference must not destroy it: the registry owns
    // the registered lifetime.
    expansion.reset();
    EXPECT_EQ(destroyed, 0);
    EXPECT_TRUE(manager_.isRegistered("demo"));

    auto removed = manager_.detach(owner_, "demo", UnregisterReason::Explicit);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(destroyed, 0);

    auto released = removed->entry->releaseExpansion();
    ASSERT_NE(released, nullptr);
    EXPECT_EQ(destroyed, 0);
    released.reset();
    EXPECT_EQ(destroyed, 1);
}

TEST_F(ExpansionManagerTest, RetiredEntryClearsOwnerIdentity)
{
    add("demo", owner_);
    auto removed = manager_.detach(owner_, "demo", UnregisterReason::Explicit);
    ASSERT_TRUE(removed.has_value());

    // owner_ is cleared atomically at retirement, not deferred to cleanup.
    // The copied ExpansionInfo.owner string is still available for diagnostics.
    EXPECT_EQ(removed->entry->getOwner(), nullptr);
    EXPECT_FALSE(removed->entry->isOwnedBy(owner_));
    EXPECT_EQ(removed->info.owner, owner_.getName());
}

// even when cleanup is deferred by an active call lease, the retired
// entry's raw owner identity must be null immediately.  The self-unregister
// case -- a provider unregistering from inside its own callback -- is the
// scenario where the deferred window is longest.
TEST_F(ExpansionManagerTest, RetiredEntryClearsOwnerEvenWithDeferredCleanup)
{
    add("demo", owner_);
    const auto entry = manager_.findCanonical("demo");
    ASSERT_NE(entry, nullptr);

    {
        auto lease = manager_.lease("demo");
        ASSERT_TRUE(static_cast<bool>(lease));
        ASSERT_TRUE(manager_.detach(owner_, "demo", UnregisterReason::Explicit).has_value());

        // The lease is still active (deferred cleanup), but the owner is already gone.
        EXPECT_TRUE(entry->hasActiveCalls());
        EXPECT_EQ(entry->getOwner(), nullptr);
        EXPECT_FALSE(entry->isOwnedBy(owner_));

        // The copied metadata string is still intact for diagnostics/events.
        EXPECT_EQ(entry->getInfo().owner, owner_.getName());

        bool cleanup_ran = false;
        EXPECT_FALSE(entry->requestCleanup([&] { cleanup_ran = true; }));
        EXPECT_FALSE(cleanup_ran);
    }

    // Deferred cleanup runs when the lease exits; owner stays null throughout.
    EXPECT_EQ(entry->getOwner(), nullptr);
}

TEST_F(ExpansionManagerTest, GenerationsAreUniquePerRegistration)
{
    add("demo", owner_);
    const auto first = manager_.findCanonical("demo")->getGeneration();
    ASSERT_TRUE(manager_.detach(owner_, "demo", UnregisterReason::Explicit).has_value());

    add("demo", owner_);
    const auto second = manager_.findCanonical("demo")->getGeneration();
    EXPECT_NE(first, second);
}

TEST_F(ExpansionManagerTest, LeaseIsGrantedOnlyForActiveEntries)
{
    add("demo", owner_);

    auto lease = manager_.lease("demo");
    ASSERT_TRUE(static_cast<bool>(lease));
    EXPECT_TRUE(lease.isStillActive());
    EXPECT_NE(lease.getExpansion(), nullptr);

    // Removal is visible immediately, even while the lease is held.
    ASSERT_TRUE(manager_.detach(owner_, "demo", UnregisterReason::Explicit).has_value());
    EXPECT_FALSE(lease.isStillActive());
    EXPECT_FALSE(static_cast<bool>(manager_.lease("demo")));
}

// a provider that unregisters itself from inside its callback must not be
// destroyed underneath its own stack frame.
TEST_F(ExpansionManagerTest, CleanupIsDeferredWhileACallIsInFlight)
{
    add("demo", owner_);
    auto removed_entry = manager_.findCanonical("demo");
    ASSERT_NE(removed_entry, nullptr);

    bool cleanup_ran = false;
    {
        auto lease = manager_.lease("demo");
        ASSERT_TRUE(static_cast<bool>(lease));
        ASSERT_TRUE(manager_.detach(owner_, "demo", UnregisterReason::Explicit).has_value());

        // While the callback is running, cleanup is deferred rather than executed.
        EXPECT_FALSE(removed_entry->requestCleanup([&] { cleanup_ran = true; }));
        EXPECT_FALSE(cleanup_ran);
        EXPECT_TRUE(removed_entry->hasActiveCalls());
    }

    // Releasing the last lease runs the deferred cleanup exactly once.
    EXPECT_TRUE(cleanup_ran);
    EXPECT_FALSE(removed_entry->hasActiveCalls());

    bool second_cleanup = false;
    EXPECT_FALSE(removed_entry->requestCleanup([&] { second_cleanup = true; }));
    EXPECT_FALSE(second_cleanup);
}

TEST_F(ExpansionManagerTest, CleanupRunsImmediatelyWhenNoCallIsInFlight)
{
    add("demo", owner_);
    auto removed = manager_.detach(owner_, "demo", UnregisterReason::Explicit);
    ASSERT_TRUE(removed.has_value());

    EXPECT_TRUE(removed->entry->requestCleanup([] {}));
    // A second request is refused, so cleanup can never run twice.
    EXPECT_FALSE(removed->entry->requestCleanup([] {}));
}

TEST_F(ExpansionManagerTest, DeferredCleanupExceptionsAreContained)
{
    add("demo", owner_);
    auto entry = manager_.findCanonical("demo");
    {
        auto lease = manager_.lease("demo");
        ASSERT_TRUE(manager_.detach(owner_, "demo", UnregisterReason::Explicit).has_value());
        EXPECT_FALSE(entry->requestCleanup([] { throw std::runtime_error("cleanup exploded"); }));
        // Releasing the lease runs the throwing cleanup; nothing may escape.
        EXPECT_NO_THROW(lease.release());
    }
}

TEST_F(ExpansionManagerTest, DetachForDisabledPluginRemovesOwnedAndDependentEntries)
{
    FakePlugin economy{"economy"};
    platform_.enable(economy);

    add("owned", owner_);
    auto dependent = std::make_shared<FakeExpansion>("dependent");
    dependent->required_plugin = "economy";
    ASSERT_TRUE(manager_.registerExpansion(other_owner_, dependent).registered);
    add("unrelated", other_owner_);

    platform_.disable(economy);
    const auto removed = manager_.detachForDisabledPlugin(economy, "economy");

    ASSERT_EQ(removed.size(), 1U);
    EXPECT_EQ(removed[0].info.identifier, "dependent");
    EXPECT_EQ(removed[0].reason, UnregisterReason::RequiredPluginDisabled);
    EXPECT_TRUE(manager_.isRegistered("owned"));
    EXPECT_TRUE(manager_.isRegistered("unrelated"));
}

// an entry that both belongs to and depends on the disabled plugin is
// removed exactly once.
TEST_F(ExpansionManagerTest, OwnedAndDependentEntryIsRemovedOnce)
{
    FakePlugin self_dependent{"selfdep"};
    platform_.enable(self_dependent);

    auto expansion = std::make_shared<FakeExpansion>("both");
    expansion->required_plugin = "selfdep";
    ASSERT_TRUE(manager_.registerExpansion(self_dependent, expansion).registered);

    platform_.disable(self_dependent);
    const auto removed = manager_.detachForDisabledPlugin(self_dependent, "selfdep");

    ASSERT_EQ(removed.size(), 1U);
    EXPECT_EQ(removed[0].info.identifier, "both");
    EXPECT_EQ(removed[0].reason, UnregisterReason::OwnerDisabled);
    EXPECT_EQ(manager_.size(), 0U);
}

TEST_F(ExpansionManagerTest, DetachAllEmptiesTheRegistryInOrder)
{
    add("zulu", owner_);
    add("alpha", other_owner_);

    const auto removed = manager_.detachAll(UnregisterReason::PapiShutdown);
    ASSERT_EQ(removed.size(), 2U);
    EXPECT_EQ(removed[0].info.identifier, "alpha");
    EXPECT_EQ(removed[1].info.identifier, "zulu");
    EXPECT_EQ(removed[0].reason, UnregisterReason::PapiShutdown);
    EXPECT_EQ(manager_.size(), 0U);
    EXPECT_TRUE(manager_.detachAll(UnregisterReason::PapiShutdown).empty());
}

TEST_F(ExpansionManagerTest, PlayerCleanupSnapshotHonoursTheCopiedCapability)
{
    auto opted_in = std::make_shared<FakeExpansion>("cleanup");
    opted_in->player_cleanup = true;
    ASSERT_TRUE(manager_.registerExpansion(owner_, opted_in).registered);
    add("plain", owner_);

    const auto snapshot = manager_.snapshotPlayerCleanupEntries();
    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0]->getInfo().identifier, "cleanup");
    EXPECT_TRUE(snapshot[0]->supportsPlayerCleanup());
}

TEST_F(ExpansionManagerTest, RelationalCapabilityIsCopiedNotProbed)
{
    auto relational = std::make_shared<FakeExpansion>("rel");
    relational->relational = true;
    const auto result = manager_.registerExpansion(owner_, relational);
    ASSERT_TRUE(result.registered);
    EXPECT_TRUE(result.info.relational);

    const auto entry = manager_.findCanonical("rel");
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->supportsRelationalPlaceholders());

    // Flipping the provider's answer after registration changes nothing, proving the
    // registry never re-queries it.
    relational->relational = false;
    EXPECT_TRUE(manager_.findCanonical("rel")->supportsRelationalPlaceholders());
}
TEST_F(ExpansionManagerTest, MetadataQueriesAreSafeWhileTheRegistryMutates)
{
    std::atomic<bool> stop{false};
    std::atomic<int> reads{0};

    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            const auto infos = manager_.getExpansions();
            for (const auto &info : infos) {
                // A published entry is always complete: no half-built metadata.
                ASSERT_FALSE(info.identifier.empty());
                ASSERT_FALSE(info.author.empty());
                ASSERT_FALSE(info.version.empty());
                ASSERT_FALSE(info.owner.empty());
                ASSERT_EQ(info.identifier, papi::detail::canonicalizeIdentifier(info.identifier));
            }
            (void)manager_.getRegisteredIdentifiers();
            (void)manager_.isRegistered("demo0");
            reads.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Keep mutating until the reader has observed the registry many times, so the
    // two really do overlap rather than running one after the other.
    int rounds = 0;
    while (reads.load(std::memory_order_relaxed) < 200 || rounds < 200) {
        for (int i = 0; i < 8; ++i) {
            auto expansion = std::make_shared<FakeExpansion>("demo" + std::to_string(i));
            (void)manager_.registerExpansion(owner_, expansion);
        }
        (void)manager_.detachByOwner(owner_, UnregisterReason::Explicit);
        ++rounds;
    }

    stop.store(true, std::memory_order_relaxed);
    reader.join();
    EXPECT_GE(reads.load(), 200);
    EXPECT_EQ(manager_.size(), 0U);
}

}  // namespace
// NOLINTEND(bugprone-unchecked-optional-access)
