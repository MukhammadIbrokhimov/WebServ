#include "../includes/webserv.hpp"

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	std::cout << "WebServ starting..." << std::endl;

	if (getenv("DEBUG")) {
		Logger::setLevel(Logger::DEBUG);
		LOG_INFO("Debug mode enabled.");
	}
	Socket server_socket(8080);
	server_socket.startListening();
	Server web_server(server_socket);
	web_server.run();
	return 0;
}