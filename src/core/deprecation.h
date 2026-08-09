#pragma once

// Overriding and calling PAPI's own deprecated compatibility members is intentional
// inside the implementation, so the warning is silenced only at those sites.
#if defined(__clang__)
#define PAPI_SUPPRESS_DEPRECATED_BEGIN \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wdeprecated-declarations\"")
#define PAPI_SUPPRESS_DEPRECATED_END _Pragma("clang diagnostic pop")
#elif defined(__GNUC__)
#define PAPI_SUPPRESS_DEPRECATED_BEGIN \
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define PAPI_SUPPRESS_DEPRECATED_END _Pragma("GCC diagnostic pop")
#else
#define PAPI_SUPPRESS_DEPRECATED_BEGIN
#define PAPI_SUPPRESS_DEPRECATED_END
#endif
