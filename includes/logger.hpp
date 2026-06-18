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

// macros instead of plain functions for two reasons: the level check happens
// at the call site so when we're below the threshold we never even build the
// string, and the do/while(0) wrapper makes each LOG_ behave like one
// statement (so `if (x) LOG_INFO(...);` without braces still works).
#define LOG_DEBUG(msg) \
	do { \
		if (Logger::DEBUG >= Logger::getLevel()) \
			Logger::log("\033[35m[DEBUG] " + std::string(msg)); \
	} while(0)

#define LOG_INFO(msg) \
	do { \
		if (Logger::INFO >= Logger::getLevel()) \
			Logger::log("\033[33m[INFO] " + std::string(msg)); \
	} while(0)

#define LOG_WARNING(msg) \
	do { \
		if (Logger::WARNING >= Logger::getLevel()) \
			Logger::log("\033[36m[WARNING] " + std::string(msg)); \
	} while(0)

#define LOG_ERROR(msg) \
	do { \
		if (Logger::ERROR >= Logger::getLevel()) \
			Logger::log("\033[31m[ERROR] " + std::string(msg)); \
	} while(0)

