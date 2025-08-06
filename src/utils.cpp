#include "utils.h"

// Logger static member definitions
std::mutex Logger::log_mutex;
LogLevel Logger::current_level = LogLevel::INFO;
bool Logger::enable_timestamp = true;
std::string Logger::log_file = "";
