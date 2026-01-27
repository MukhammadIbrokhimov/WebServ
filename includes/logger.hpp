#pragma once

#include <string>
#include <cstdio>
#include <ctime>

class Logger {
	public:
		enum Level {
			DEBUG,
			INFO,
			WARNING,
			ERROR
		};
		~Logger();
		static void setLevel(Level level);
		static void log(const std::string& message);
		static Level getLevel();

	private:
		static Level g_level;
		Logger();
};

// Logging macros
#define LOG_DEBUG(msg) \
	do { \
		if (Logger::DEBUG >= Logger::getLevel()) \
			Logger::log("[DEBUG] " + std::string(msg)); \
	} while(0)

#define LOG_INFO(msg) \
	do { \
		if (Logger::INFO >= Logger::getLevel()) \
			Logger::log("[INFO] " + std::string(msg)); \
	} while(0)

#define LOG_WARNING(msg) \
	do { \
		if (Logger::WARNING >= Logger::getLevel()) \
			Logger::log("[WARNING] " + std::string(msg)); \
	} while(0)

#define LOG_ERROR(msg) \
	do { \
		if (Logger::ERROR >= Logger::getLevel()) \
			Logger::log("[ERROR] " + std::string(msg)); \
	} while(0)

