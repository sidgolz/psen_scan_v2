#include "time_util.h"
#include <iomanip>
#include <ctime>
#include <sstream>
#include <chrono>

std::string getFormattedTime() {
    // Get the current time point from the system clock
    auto now = std::chrono::system_clock::now();
    
    // Convert the current time to a time_t, representing calendar time
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    
    // Thread-safe localtime version for Linux (localtime_r)
    std::tm now_tm;
    localtime_r(&now_c, &now_tm);  // Thread-safe on Linux/POSIX systems

    // Create the output string stream and format the time
    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");

    // Return the formatted time string
    return oss.str();
}
