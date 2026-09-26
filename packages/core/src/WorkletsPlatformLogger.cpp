#include <iostream>
#include <string>
#include <worklets/Tools/PlatformLogger.h>

// The platform half react-native-worklets' Common/cpp leaves to each platform (#134): Android writes to logcat
// and Apple to NSLog, and a Linux process writes to stderr, beside every other diagnostic this platform prints.
namespace worklets {

void PlatformLogger::log(const char* str) { std::cerr << "[worklets] " << str << '\n'; }

void PlatformLogger::log(const std::string& str) { log(str.c_str()); }

void PlatformLogger::log(const double d) { std::cerr << "[worklets] " << d << '\n'; }

void PlatformLogger::log(const int i) { std::cerr << "[worklets] " << i << '\n'; }

void PlatformLogger::log(const bool b) { log(b ? "true" : "false"); }

} // namespace worklets
