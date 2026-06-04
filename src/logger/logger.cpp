#include "../../includes/webserv.hpp"

// start at INFO so the DEBUG spam stays hidden until main() flips this to
// DEBUG when it sees the env var. lives here (not the header) because it's a
// definition, not just a declaration.
Logger::Level Logger::g_level = Logger::INFO;

// ctor is private + empty on purpose: this class is all-static, nobody should
// ever actually make a Logger object.
Logger::Logger() {}
Logger::~Logger() {}

void Logger::setLevel(Level level) {
	g_level = level;
}

Logger::Level Logger::getLevel() {
	return g_level;
}

// everything goes to stderr, never stdout — I want stdout free for real HTTP
// output later. the \033[0m at the end resets whatever colour the LOG_ macros
// turned on, so the next thing printed isn't tinted.
void Logger::log(const std::string& message) {
	std::cerr << message << "\033[0m" << std::endl;
}