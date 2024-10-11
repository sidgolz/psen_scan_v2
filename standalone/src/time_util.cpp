#include "time_util.h"
#include <iomanip>
#include <ctime>
#include <sstream>
#include <chrono>

std::string getFormattedTime() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm = *std::localtime(&now_c);
    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");  // Adjust the format as needed
    return oss.str();
}
