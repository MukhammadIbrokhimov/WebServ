#pragma once

#include "socket.hpp"
#include <vector>
#include <poll.h>
#include <iostream>
#include <unistd.h>
#include <csignal>

#include "string_utils.hpp"

class Socket;

extern volatile sig_atomic_t g_shutdown;

class Server {
	private:
		std::vector<struct pollfd> poll_fds;
		// Reference, not value: Server borrows the listening Socket owned
		// by main(). With ownership in main, only main's destructor calls
		// ::close() on the listening fd, avoiding a double close.
		Socket &socket;

		// Server is not copyable: a reference member would alias the same
		// Socket anyway, and there is exactly one Server in main().
		Server(const Server&);
		Server& operator=(const Server&);

	public:
		Server(Socket &_socket);
		~Server();
		void run();
		// Returns false when the client has closed the connection (or recv
		// failed) so the poll loop knows to remove it. True means keep the fd.
		bool handle_client_data_read(int client_fd);
		void handle_client_data_write(int client_fd);
		void cleanup_sockets();
		static void signal_handler(int signum);
		static void setup_signal_handlers();
};
