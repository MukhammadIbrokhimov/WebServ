#include "webserv.hpp"

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	std::cout << "WebServ starting..." << std::endl;

	if (getenv("DEBUG")) {
		Logger::setLevel(Logger::DEBUG);
		LOG_INFO("Debug mode enabled.");
	}
	LOG_DEBUG("This is a debug message.");
	LOG_WARNING("This is a warning message.");
	LOG_ERROR("This is an error message.");
	LOG_INFO("WebServ initialized successfully.");
	return 0;
}