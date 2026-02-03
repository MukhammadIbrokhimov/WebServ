#include "../../includes/webserv.hpp"

// constructor
Server::Server(Socket &_socket) : socket(_socket) {}
// destructor
Server::~Server() {
	socket.close();
}

// main server loop
void Server::run() {
	// add the listening socket to the poll_fds vector
	struct pollfd pfd_listener = {socket.getFileDescriptor(), POLLIN, 0};
	poll_fds.push_back(pfd_listener);
	
	// start the server loop
	while (true) {
		int ret = poll(&poll_fds[0], poll_fds.size(), TIME_OUT_MS);
		if (ret == -1) {
			LOG_ERROR("<class Server> Poll error");
			throw SocketException("<class Server> Poll error");
		} 
		// timeout occurred
		if (ret == 0) continue;
		// check for events
		for (size_t i = 0; i < poll_fds.size(); ++i) {
			if (poll_fds[i].revents & (POLLHUP | POLLERR)) {
				// handle disconnection or error
				LOG_DEBUG("<class Server> Client disconnected or error on fd: " + toString(poll_fds[i].fd));
				::close(poll_fds[i].fd);
				poll_fds.erase(poll_fds.begin() + i--);
				continue;
			}
			if (poll_fds[i].revents & POLLIN) {
				// new incoming connection
				if (poll_fds[i].fd == socket.getFileDescriptor()) {
					int client_fd = socket.acceptClient();
					if (client_fd != -1) {
						// add the new client socket to the poll_fds vector
						struct pollfd pfd_client = {client_fd, POLLIN | POLLOUT, 0};
						poll_fds.push_back(pfd_client);
						LOG_DEBUG("<class Server> New client connected, fd: " + toString(client_fd));
					}
				} else {
					// handle client data here
					LOG_DEBUG("<class Server> Data available to read on client fd: " + toString(poll_fds[i].fd));
					handle_client_data_read(poll_fds[i].fd);
					// For simplicity, we will just close the client connection
					::close(poll_fds[i].fd);
					LOG_DEBUG("<class Server> Client disconnected, fd: " + toString(poll_fds[i].fd));
					poll_fds.erase(poll_fds.begin() + i--);
				}
			}
			if (poll_fds[i].revents & POLLOUT) {
				// handle client data write here if needed
				LOG_DEBUG("<class Server> Ready to write on client fd: " + toString(poll_fds[i].fd));
				handle_client_data_write(poll_fds[i].fd);
				::close(poll_fds[i].fd);
				poll_fds.erase(poll_fds.begin() + i--);
			}
		}
	}
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