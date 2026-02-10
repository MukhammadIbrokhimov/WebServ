#include "../../includes/webserv.hpp"

//global signal handler for graceful shutdown
volatile sig_atomic_t g_shutdown = 0;

void Server::signal_handler(int signum) {
	(void)signum; // suppress unused parameter warning
	g_shutdown = 1;
}

void Server::setup_signal_handlers() {
	struct sigaction sa;
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	
	if (sigaction(SIGINT, &sa, NULL) == -1) {
		LOG_ERROR("<class Server -> signal_handler(): Error setting up signal handler for SIGINT");
		throw SocketException("Error setting up signal handler for SIGINT");
	}
	if (sigaction(SIGTERM, &sa, NULL) == -1) {
		LOG_ERROR("<class Server -> signal_handler(): Error setting up signal handler for SIGTERM");
		throw SocketException("Error setting up signal handler for SIGTERM");
	}
}

// constructor
Server::Server(Socket &_socket) : socket(_socket) {
	setup_signal_handlers();
	LOG_DEBUG("<class Server -> server() : socket received " + toString(socket.getFileDescriptor()));
}
// destructor
Server::~Server() {
	socket.close();
}

// main server loop
void Server::run() {
	// add the listening socket to the poll_fds vector
	LOG_DEBUG("<class Server -> run() : adding listening socket to poll_fds");
	struct pollfd pfd_listener = {socket.getFileDescriptor(), POLLIN, 0};
	poll_fds.push_back(pfd_listener);
	
	// start the server loop
	while (g_shutdown != 1) {
		LOG_DEBUG("<class Server -> run() : polling for events");
		int ret = poll(&poll_fds[0], poll_fds.size(), TIME_OUT_MS);
		if (ret == -1) {
			if (errno == EINTR) {
				LOG_DEBUG("<class Server -> run() : EINTR received, checking shutdown flag");
				continue; // Interrupted by signal, check shutdown flag and continue
			}
			LOG_ERROR("<class Server> Poll error");
			throw SocketException("<class Server> Poll error");
		} 
		// timeout occurred
		if (ret == 0) continue;
		// check for events
		for (size_t i = 0; i < poll_fds.size(); ++i) {
			if ((poll_fds[i].revents & (POLLHUP | POLLERR)) && poll_fds[i].fd != socket.getFileDescriptor()) {
				// handle disconnection or error
				LOG_DEBUG("<class Server -> run(): Client disconnected or error on fd: " + toString(poll_fds[i].fd));
				::close(poll_fds[i].fd);
				poll_fds.erase(poll_fds.begin() + i--);
				continue;
			}
			if (poll_fds[i].revents & POLLIN) {
				// new incoming connection
				if (poll_fds[i].fd == socket.getFileDescriptor()) {
					LOG_DEBUG("<class Server -> run() : new incoming connection");
					int client_fd = socket.acceptClient();
					if (client_fd != -1) {
						// add the new client socket to the poll_fds vector
						struct pollfd pfd_client = {client_fd, POLLIN | POLLOUT, 0};
						poll_fds.push_back(pfd_client);
						LOG_DEBUG("<class Server -> run() : New client connected, fd: " + toString(client_fd));
					}
				} else {
					// handle client data here
					LOG_DEBUG("<class Server -> run() : Data available to read on client fd: " + toString(poll_fds[i].fd));
					handle_client_data_read(poll_fds[i].fd);
					// For simplicity, we will just close the client connection
					::close(poll_fds[i].fd);
					LOG_DEBUG("<class Server -> run() : Client disconnected, fd: " + toString(poll_fds[i].fd));
					poll_fds.erase(poll_fds.begin() + i--);
				}
			}
			if (poll_fds[i].revents & POLLOUT) {
				// handle client data write here if needed
				LOG_DEBUG("<class Server -> run() : Ready to write on client fd: " + toString(poll_fds[i].fd));
				handle_client_data_write(poll_fds[i].fd);
				::close(poll_fds[i].fd);
				poll_fds.erase(poll_fds.begin() + i--);
			}
		}
	}
	LOG_INFO("<class Server> Shutdown signal received, cleaning up sockets");
	this->cleanup_sockets();
	LOG_INFO("<class Server> Server shutdown complete.");
}

void Server::handle_client_data_read(int client_fd) {
	// Placeholder for reading data from client
	LOG_DEBUG("<class Server> Reading data from client fd: " + toString(client_fd));
	// Actual implementation would go here
}

void Server::handle_client_data_write(int client_fd) {
	// Placeholder for writing data to client
	LOG_DEBUG("<class Server> Writing data to client fd: " + toString(client_fd));
	// Actual implementation would go here
}

// Cleanup function to close all client sockets
void Server::cleanup_sockets() {
	LOG_DEBUG("<class Server -> cleanup_sockets() : Cleaning up client sockets");
	for (size_t i = 0; i < poll_fds.size(); ++i) {
		if (poll_fds[i].fd != socket.getFileDescriptor()) {
			LOG_DEBUG("<class Server -> cleanup_sockets() : Closing client socket fd: " + toString(poll_fds[i].fd));
			::close(poll_fds[i].fd);
		}
	}
	poll_fds.clear();
}