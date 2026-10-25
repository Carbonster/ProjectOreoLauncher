// log.h - LoggingService. One log file, appended, thread safe.
// Everything the launcher decides ends up here: that is the only way to debug a launcher that
// deliberately never shows a window in the healthy case.
#pragma once

#include <string>

namespace oreo {

class Log {
public:
    // Opens <dir>/launcher.log (rotating the previous run to launcher.log.1 when it grows big).
    static void init(const std::string& log_dir, bool verbose);
    static void shutdown();

    static void info(const std::string& msg);
    static void warn(const std::string& msg);
    static void error(const std::string& msg);
    static void debug(const std::string& msg);   // only written when Verbose=1

    static std::string file_path();
    static bool verbose();
    // Verbose logging is a Settings switch, so it can change without restarting the launcher.
    static void set_verbose(bool verbose);
};

}  // namespace oreo
