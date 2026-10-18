#pragma once

#include "winpplus_version.generated.h"

#ifndef RC_INVOKED
#define WINPPLUS_WIDE_IMPL(value) L##value
#define WINPPLUS_WIDE(value) WINPPLUS_WIDE_IMPL(value)
inline constexpr wchar_t kAppVersion[] = WINPPLUS_WIDE(WINPPLUS_VERSION_STRING);
#endif
