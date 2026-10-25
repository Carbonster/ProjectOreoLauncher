// clock.h - the time, for logs and for how old a cached answer is.
#pragma once

#include <string>

namespace oreo {

std::string timestamp_now();          // "2026-08-16 18:42:03" (local time, for logs)
long long unix_time_now();            // seconds since epoch, for cache expiry

}  // namespace oreo
