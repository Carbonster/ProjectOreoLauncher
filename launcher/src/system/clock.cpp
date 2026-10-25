#include "system/clock.h"

#include <windows.h>

#include <cstdio>
#include <ctime>

namespace oreo {

std::string timestamp_now() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour,
                  st.wMinute, st.wSecond);
    return buf;
}

long long unix_time_now() { return (long long)::time(nullptr); }

}  // namespace oreo
