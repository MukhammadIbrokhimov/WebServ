#pragma once

#include "socket.hpp"
#include <vector>
#include <poll.h>
#include <iostream>
#include <unistd.h>
#include "string_utils.hpp"

class Socket;
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
