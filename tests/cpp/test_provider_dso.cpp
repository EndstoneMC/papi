// External-provider DSO lifetime coverage.

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "core/service/placeholder_api_impl.h"
#include "fakes.h"
#include "platform/endstone/bootstrap.h"

namespace {

using papi::UnregisterReason;
using papi::detail::PlaceholderApiImpl;
using papi::testing::FakePlatform;
using papi::testing::FakePlugin;

class DynamicLibrary {
public:
    explicit DynamicLibrary(const std::string &path) : path_(path)
    {
#ifdef _WIN32
        handle_ = LoadLibraryA(path.c_str());
#else
        handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        if (handle_ == nullptr) {
            throw std::runtime_error("Failed to load provider fixture: " + path);
        }
    }

    ~DynamicLibrary()
    {
        if (handle_ != nullptr) {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(handle_));
#else
            dlclose(handle_);
#endif
        }
    }

    DynamicLibrary(const DynamicLibrary &) = delete;
    DynamicLibrary &operator=(const DynamicLibrary &) = delete;

    [[nodiscard]] void *symbol(const std::string &name) const
    {
#ifdef _WIN32
        return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(handle_), name.c_str()));
#else
        return dlsym(handle_, name.c_str());
#endif
    }

private:
    std::string path_;
    void *handle_ = nullptr;
};

using CreateDemoFn = papi::PlaceholderExpansion *(*)();
using DestroyExpansionFn = void (*)(papi::PlaceholderExpansion *);
using IsDemoDestroyedFn = bool (*)();
using DemoLastReasonFn = papi::UnregisterReason (*)();

class ProviderDsoTest : public ::testing::Test {
protected:
    ProviderDsoTest()
    {
        platform_ = std::make_shared<FakePlatform>();
        platform_->enable(papi_plugin_);
        platform_->enable(owner_);
        service_ = std::make_shared<PlaceholderApiImpl>(platform_, papi_plugin_.getName());
    }

    void SetUp() override
    {
        ASSERT_NO_THROW(lib_ = std::make_unique<DynamicLibrary>(PAPI_PROVIDER_DSO_PATH));
        create_ = reinterpret_cast<CreateDemoFn>(lib_->symbol("papi_create_demo_expansion"));
        destroy_ = reinterpret_cast<DestroyExpansionFn>(lib_->symbol("papi_destroy_expansion"));
        is_destroyed_ = reinterpret_cast<IsDemoDestroyedFn>(lib_->symbol("papi_is_demo_destroyed"));
        last_reason_ = reinterpret_cast<DemoLastReasonFn>(lib_->symbol("papi_demo_last_reason"));
        ASSERT_NE(create_, nullptr);
        ASSERT_NE(destroy_, nullptr);
        ASSERT_NE(is_destroyed_, nullptr);
        ASSERT_NE(last_reason_, nullptr);
    }

    std::shared_ptr<papi::PlaceholderExpansion> makeExpansion()
    {
        auto deleter = [this](papi::PlaceholderExpansion *p) {
            destroy_(p);
        };
        return {create_(), deleter};
    }

    std::shared_ptr<FakePlatform> platform_;
    std::shared_ptr<PlaceholderApiImpl> service_;
    std::unique_ptr<DynamicLibrary> lib_;
    FakePlugin papi_plugin_{"papi"};
    FakePlugin owner_{"provider"};
    CreateDemoFn create_ = nullptr;
    DestroyExpansionFn destroy_ = nullptr;
    IsDemoDestroyedFn is_destroyed_ = nullptr;
    DemoLastReasonFn last_reason_ = nullptr;
};

TEST_F(ProviderDsoTest, FullLifecycleFromExternalModule)
{
    {
        auto expansion = makeExpansion();
        ASSERT_NE(expansion, nullptr);
        ASSERT_TRUE(service_->registerExpansion(owner_, expansion));
        EXPECT_EQ(service_->setPlaceholders(nullptr, "{demo.x}"), "value-x");
        EXPECT_EQ(service_->setPlaceholders(nullptr, "{demo.hello}"), "value-hello");
    }
    EXPECT_FALSE(is_destroyed_());
    EXPECT_TRUE(service_->unregisterExpansion(owner_, "demo"));
    EXPECT_TRUE(is_destroyed_());
    EXPECT_EQ(last_reason_(), UnregisterReason::Explicit);

    EXPECT_EQ(service_->setPlaceholders(nullptr, "{demo.x}"), "{demo.x}");
}

TEST_F(ProviderDsoTest, OwnerDisableDestroysExpansionBeforeUnload)
{
    {
        auto expansion = makeExpansion();
        ASSERT_TRUE(service_->registerExpansion(owner_, expansion));
    }

    EXPECT_FALSE(is_destroyed_());
    platform_->disable(owner_);
    service_->handlePluginDisabled(owner_);
    EXPECT_TRUE(is_destroyed_());
    EXPECT_EQ(last_reason_(), UnregisterReason::OwnerDisabled);
}

TEST_F(ProviderDsoTest, ShutdownDestroysExpansionBeforeUnload)
{
    {
        auto expansion = makeExpansion();
        ASSERT_TRUE(service_->registerExpansion(owner_, expansion));
    }

    EXPECT_FALSE(is_destroyed_());
    service_->shutdown();
    EXPECT_TRUE(is_destroyed_());
    EXPECT_EQ(last_reason_(), UnregisterReason::PapiShutdown);
}

TEST_F(ProviderDsoTest, RetainedServiceIsInertAfterProviderDsoUnload)
{
    {
        auto expansion = makeExpansion();
        ASSERT_TRUE(service_->registerExpansion(owner_, expansion));
        EXPECT_EQ(service_->setPlaceholders(nullptr, "{demo.x}"), "value-x");
    }

    auto retained = std::shared_ptr<papi::PlaceholderAPI>(service_);
    ASSERT_TRUE(retained->isActive());

    service_->shutdown();
    EXPECT_FALSE(retained->isActive());
    EXPECT_TRUE(is_destroyed_());

    lib_.reset();

    EXPECT_FALSE(retained->isActive());
    EXPECT_EQ(retained->setPlaceholders(nullptr, "{demo.x}"), "{demo.x}");
    EXPECT_TRUE(retained->getExpansions().empty());
    EXPECT_TRUE(retained->getRegisteredIdentifiers().empty());
    EXPECT_FALSE(retained->isRegistered("demo"));
    EXPECT_TRUE(retained->containsPlaceholders("{demo.x}"));

    auto fresh = std::make_shared<PlaceholderApiImpl>(platform_, papi_plugin_.getName());
    EXPECT_TRUE(fresh->isActive());
    EXPECT_EQ(fresh->setPlaceholders(nullptr, "{demo.x}"), "{demo.x}");
    fresh->shutdown();
}

}  // namespace
