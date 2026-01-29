#include "../includes/webserv.hpp"

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	std::cout << "WebServ starting..." << std::endl;

	if (getenv("DEBUG")) {
		Logger::setLevel(Logger::DEBUG);
		LOG_INFO("Debug mode enabled.");
	}


    try {
        Socket server(8080);
        server.startListening(10);
        std::cout << "Server is open! Waiting for a client..." << std::endl;

        while (true) {
            // This will check for a client every second
            int client_fd = server.acceptClient();

            if (client_fd != -1) {
                std::cout << "Success! A client connected on FD: " << client_fd << std::endl;
                
                // Send a tiny response so the browser doesn't hang
                const char* msg = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nHello World!";
                send(client_fd, msg, strlen(msg), 0);
                
                close(client_fd); // Hang up
                std::cout << "Client handled and disconnected." << std::endl;
            }
            
            // Small sleep so your CPU doesn't hit 100% 
            // (Only for this basic test, don't use sleep in your final poll version!)
            usleep(100000); 
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
	return 0;
}