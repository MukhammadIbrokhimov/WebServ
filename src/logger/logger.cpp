#include "../../includes/webserv.hpp"

// default log level
Logger::Level Logger::g_level = Logger::INFO;

// constructor and destructor
Logger::Logger() {}
Logger::~Logger() {}

// getter and setter for log level
void Logger::setLevel(Level level) {
	g_level = level;
}

Logger::Level Logger::getLevel() {
	return g_level;
}

// log message to console
void Logger::log(const std::string& message) {
	std::cerr << message << std::endl;
}