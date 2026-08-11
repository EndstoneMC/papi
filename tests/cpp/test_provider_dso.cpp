// T-009: Validate the full external-provider DSO lifecycle.
//
// The provider fixture (tests/fixtures/provider/provider_demo.cpp) is a
// separately linked shared library built against the installed public headers
// only.  This test loads it, drives register -> request -> unregister ->
// unload, and verifies that the expansion object is destroyed *before* the DSO
// is unloaded.  If PAPI released the shared_ptr after dlclose/FreeLibrary, the
// virtual destructor call would jump into unmapped memory.

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

    // Wraps the raw pointer from the DSO in a shared_ptr whose deleter calls
    // back into the DSO, so destruction and deallocation stay on the fixture
    // side of the boundary.  After registering, the caller must release its
    // own reference so the service is the sole owner.
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

// The fixture is built from the public headers only and returns a raw pointer
// that crosses the DSO boundary.  Registration, virtual dispatch, and
// unregister must all work without symbol-resolution or ABI failures.  The
// expansion is destroyed when the service releases its reference -- before the
// DSO is unloaded.
TEST_F(ProviderDsoTest, FullLifecycleFromExternalModule)
{
    {
        auto expansion = makeExpansion();
        ASSERT_NE(expansion, nullptr);
        ASSERT_TRUE(service_->registerExpansion(owner_, expansion));
        EXPECT_EQ(service_->setPlaceholders(nullptr, "{demo_x}"), "value-x");
        EXPECT_EQ(service_->setPlaceholders(nullptr, "{demo_hello}"), "value-hello");
    }
    // The local reference is gone; the service is the sole owner.

    EXPECT_FALSE(is_destroyed_());
    EXPECT_TRUE(service_->unregisterExpansion(owner_, "demo"));
    EXPECT_TRUE(is_destroyed_());
    EXPECT_EQ(last_reason_(), UnregisterReason::Explicit);

    // After unregister the placeholder is unresolved again.
    EXPECT_EQ(service_->setPlaceholders(nullptr, "{demo_x}"), "{demo_x}");
}

// Owner disable must remove and destroy the expansion before the module is
// unloaded -- the same guarantee that prevents UB when a real provider plugin
// is unloaded by Endstone.
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

// PAPI shutdown releases every expansion.  The expansion must be destroyed
// while the DSO is still loaded so the virtual destructor is reachable.
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

// T-015: A retained shared_ptr<PlaceholderAPI> must remain inert after PAPI
// shutdown even when the backing provider DSO is unloaded.  The service must
// not call into the unloaded module -- no vtable dispatch, no callback, no
// metadata read.  This is the binary-lifetime guarantee that makes a server
// reload safe.
TEST_F(ProviderDsoTest, RetainedServiceIsInertAfterProviderDsoUnload)
{
    {
        auto expansion = makeExpansion();
        ASSERT_TRUE(service_->registerExpansion(owner_, expansion));
        EXPECT_EQ(service_->setPlaceholders(nullptr, "{demo_x}"), "value-x");
    }

    // Retain the service before shutdown -- a consumer does this to survive a
    // reload.
    auto retained = std::shared_ptr<papi::PlaceholderAPI>(service_);
    ASSERT_TRUE(retained->isActive());

    service_->shutdown();
    EXPECT_FALSE(retained->isActive());

    // The expansion was destroyed during shutdown, before the DSO is unloaded.
    EXPECT_TRUE(is_destroyed_());

    // Unload the provider fixture.  The retained service must not touch it.
    lib_.reset();

    // Every method on the retained service must be inert: no crash, no call
    // into the unloaded DSO, original token preserved.
    EXPECT_FALSE(retained->isActive());
    EXPECT_EQ(retained->setPlaceholders(nullptr, "{demo_x}"), "{demo_x}");
    EXPECT_TRUE(retained->getExpansions().empty());
    EXPECT_TRUE(retained->getRegisteredIdentifiers().empty());
    EXPECT_FALSE(retained->isRegistered("demo"));
    EXPECT_TRUE(retained->containsPlaceholders("{demo_x}"));

    // A fresh service works independently.
    auto fresh = std::make_shared<PlaceholderApiImpl>(platform_, papi_plugin_.getName());
    EXPECT_TRUE(fresh->isActive());
    EXPECT_EQ(fresh->setPlaceholders(nullptr, "{demo_x}"), "{demo_x}");
    fresh->shutdown();
}

}  // namespace
