// Exercise the documented provider-owned std::shared_ptr ABI across a
// separately linked module boundary.

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <memory>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "core/service/placeholder_api_impl.h"
#include "fakes.h"

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
            throw std::runtime_error("Failed to load shared_ptr provider fixture: " + path);
        }
    }

    ~DynamicLibrary() { (void)close(); }

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

    [[nodiscard]] bool close()
    {
        if (handle_ == nullptr) {
            return true;
        }
#ifdef _WIN32
        const bool closed = FreeLibrary(static_cast<HMODULE>(handle_)) != 0;
#else
        const bool closed = dlclose(handle_) == 0;
#endif
        if (closed) {
            handle_ = nullptr;
        }
        return closed;
    }

private:
    std::string path_;
    void *handle_ = nullptr;
};

using RegisterSharedFn = bool (*)(papi::PlaceholderAPI *, endstone::Plugin *);
using IsDestroyedFn = bool (*)();
using LastReasonFn = papi::UnregisterReason (*)();

class ProviderSharedPtrDsoTest : public ::testing::Test {
protected:
    ProviderSharedPtrDsoTest()
    {
        platform_ = std::make_shared<FakePlatform>();
        platform_->enable(papi_plugin_);
        platform_->enable(owner_);
        service_ = std::make_shared<PlaceholderApiImpl>(platform_, papi_plugin_.getName());
    }

    void SetUp() override
    {
        ASSERT_NO_THROW(lib_ = std::make_unique<DynamicLibrary>(PAPI_PROVIDER_SHARED_PTR_DSO_PATH));
        register_ = reinterpret_cast<RegisterSharedFn>(lib_->symbol("papi_register_shared_expansion"));
        is_destroyed_ = reinterpret_cast<IsDestroyedFn>(lib_->symbol("papi_is_shared_expansion_destroyed"));
        last_reason_ = reinterpret_cast<LastReasonFn>(lib_->symbol("papi_shared_expansion_last_reason"));
        ASSERT_NE(register_, nullptr);
        ASSERT_NE(is_destroyed_, nullptr);
        ASSERT_NE(last_reason_, nullptr);
    }

    void registerAndRequest()
    {
        ASSERT_TRUE(register_(service_.get(), &owner_));
        EXPECT_FALSE(is_destroyed_());
        EXPECT_EQ(service_->setPlaceholders(nullptr, "{shared-demo_x}"), "shared-value-x");
        EXPECT_EQ(service_->setPlaceholders(nullptr, "{shared-demo_hello}"), "shared-value-hello");
    }

    void expectDestroyedAndUnload(UnregisterReason reason)
    {
        EXPECT_TRUE(is_destroyed_());
        EXPECT_EQ(last_reason_(), reason);
        EXPECT_TRUE(lib_->close());
    }

    std::shared_ptr<FakePlatform> platform_;
    std::shared_ptr<PlaceholderApiImpl> service_;
    std::unique_ptr<DynamicLibrary> lib_;
    FakePlugin papi_plugin_{"papi"};
    FakePlugin owner_{"shared-provider"};
    RegisterSharedFn register_ = nullptr;
    IsDestroyedFn is_destroyed_ = nullptr;
    LastReasonFn last_reason_ = nullptr;
};

TEST_F(ProviderSharedPtrDsoTest, ProviderOwnedSharedPtrSurvivesRequestAndExplicitUnregister)
{
    registerAndRequest();

    EXPECT_TRUE(service_->unregisterExpansion(owner_, "shared-demo"));
    EXPECT_EQ(service_->setPlaceholders(nullptr, "{shared-demo_x}"), "{shared-demo_x}");
    expectDestroyedAndUnload(UnregisterReason::Explicit);
}

TEST_F(ProviderSharedPtrDsoTest, ProviderOwnedSharedPtrIsDestroyedBeforeOwnerModuleUnload)
{
    registerAndRequest();

    platform_->disable(owner_);
    service_->handlePluginDisabled(owner_);
    expectDestroyedAndUnload(UnregisterReason::OwnerDisabled);
}

TEST_F(ProviderSharedPtrDsoTest, ProviderOwnedSharedPtrIsDestroyedBeforePapiShutdownAndUnload)
{
    registerAndRequest();

    service_->shutdown();
    expectDestroyedAndUnload(UnregisterReason::PapiShutdown);
}

}  // namespace
