#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <endstone/plugin/service_manager.h>
#include <endstone/plugin/service_priority.h>

#include <gtest/gtest.h>

#include "core/service/placeholder_api_impl.h"
#include "fakes.h"
#include "platform/endstone/service_publication.h"

namespace {

class TestServiceManager final : public endstone::ServiceManager {
public:
    void registerService(std::string name, std::shared_ptr<endstone::Service> provider, const endstone::Plugin &plugin,
                         const endstone::ServicePriority priority) override
    {
        registrations_.push_back({std::move(name), std::move(provider), &plugin, priority, next_order_++});
    }

    void unregisterAll(const endstone::Plugin &plugin) override
    {
        std::erase_if(registrations_, [&plugin](const Registration &entry) { return entry.plugin == &plugin; });
    }

    void unregister(std::string name, const endstone::Service &provider) override
    {
        std::erase_if(registrations_, [&name, &provider](const Registration &entry) {
            return entry.name == name && entry.provider.get() == &provider;
        });
    }

    void unregister(const endstone::Service &provider) override
    {
        std::erase_if(registrations_,
                      [&provider](const Registration &entry) { return entry.provider.get() == &provider; });
    }

    [[nodiscard]] std::shared_ptr<endstone::Service> get(std::string name) const override
    {
        const Registration *selected = nullptr;
        for (const auto &entry : registrations_) {
            if (entry.name != name) {
                continue;
            }
            if (selected == nullptr || entry.priority > selected->priority ||
                (entry.priority == selected->priority && entry.order > selected->order)) {
                selected = &entry;
            }
        }
        return selected == nullptr ? nullptr : selected->provider;
    }

private:
    struct Registration {
        std::string name;
        std::shared_ptr<endstone::Service> provider;
        const endstone::Plugin *plugin;
        endstone::ServicePriority priority;
        std::size_t order;
    };

    std::vector<Registration> registrations_;
    std::size_t next_order_ = 0;
};

class WrongService final : public endstone::Service {};

class ServicePublicationTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        platform_ = std::make_shared<papi::testing::FakePlatform>();
        platform_->enable(papi_plugin_);
        service_ = std::make_shared<papi::detail::PlaceholderApiImpl>(platform_, papi_plugin_.getName());
    }

    void TearDown() override
    {
        if (service_) {
            papi::detail::ServicePublication::withdraw(manager_, *service_);
            service_->shutdown();
        }
    }

    void publish()
    {
        manager_.registerService(std::string(papi::PlaceholderAPI::ServiceName), service_, papi_plugin_,
                                 endstone::ServicePriority::Normal);
        papi::detail::ServicePublication::publish(manager_, service_);
    }

    TestServiceManager manager_;
    papi::testing::FakePlugin papi_plugin_{"papi"};
    papi::testing::FakePlugin other_plugin_{"other"};
    std::shared_ptr<papi::testing::FakePlatform> platform_;
    std::shared_ptr<papi::detail::PlaceholderApiImpl> service_;
};

TEST_F(ServicePublicationTest, MissingServiceIsRejected)
{
    papi::detail::ServicePublication::publish(manager_, service_);
    EXPECT_EQ(papi::detail::ServicePublication::load(manager_), nullptr);
}

TEST_F(ServicePublicationTest, CorrectActiveServiceReturnsTypedSharedOwner)
{
    publish();

    auto loaded = papi::detail::ServicePublication::load(manager_);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded.get(), service_.get());

    manager_.unregister(std::string(papi::PlaceholderAPI::ServiceName), *service_);
    papi::detail::ServicePublication::withdraw(manager_, *service_);
    service_->shutdown();
    service_.reset();

    EXPECT_FALSE(loaded->isActive());
}

TEST_F(ServicePublicationTest, InactivePublishedServiceIsRejected)
{
    publish();
    service_->shutdown();

    EXPECT_EQ(papi::detail::ServicePublication::load(manager_), nullptr);
}

TEST_F(ServicePublicationTest, HigherPriorityWrongServiceIsRejectedWithoutTypedCast)
{
    publish();
    const auto wrong = std::make_shared<WrongService>();
    manager_.registerService(std::string(papi::PlaceholderAPI::ServiceName), wrong, other_plugin_,
                             endstone::ServicePriority::High);

    EXPECT_EQ(papi::detail::ServicePublication::load(manager_), nullptr);
}

TEST_F(ServicePublicationTest, LaterEqualPriorityWrongServiceIsRejectedAndRemovalRestoresPapi)
{
    publish();
    const auto wrong = std::make_shared<WrongService>();
    manager_.registerService(std::string(papi::PlaceholderAPI::ServiceName), wrong, other_plugin_,
                             endstone::ServicePriority::Normal);

    EXPECT_EQ(papi::detail::ServicePublication::load(manager_), nullptr);

    manager_.unregister(std::string(papi::PlaceholderAPI::ServiceName), *wrong);
    EXPECT_EQ(papi::detail::ServicePublication::load(manager_).get(), service_.get());
}

TEST_F(ServicePublicationTest, ReloadReplacesPublicationWithoutRevivingRetainedService)
{
    publish();
    auto retained = papi::detail::ServicePublication::load(manager_);
    ASSERT_NE(retained, nullptr);

    manager_.unregister(std::string(papi::PlaceholderAPI::ServiceName), *service_);
    papi::detail::ServicePublication::withdraw(manager_, *service_);
    service_->shutdown();

    const auto replacement = std::make_shared<papi::detail::PlaceholderApiImpl>(platform_, papi_plugin_.getName());
    manager_.registerService(std::string(papi::PlaceholderAPI::ServiceName), replacement, papi_plugin_,
                             endstone::ServicePriority::Normal);
    papi::detail::ServicePublication::publish(manager_, replacement);

    const auto loaded = papi::detail::ServicePublication::load(manager_);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded.get(), replacement.get());
    EXPECT_NE(loaded.get(), retained.get());
    EXPECT_FALSE(retained->isActive());

    papi::detail::ServicePublication::withdraw(manager_, *replacement);
    replacement->shutdown();
}

}  // namespace
