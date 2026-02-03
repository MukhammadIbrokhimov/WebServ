#pragma once

#include <vector>
#include <poll.h>

class Server {
	private:
		std::vector<struct pollfd> poll_fds;
		Socket socket;
	
	public:
		Server(Socket &_socket);
		~Server();
		void run();
		void handle_client_data_read(int client_fd);
		void handle_client_data_write(int client_fd);
};
