#include "../../includes/webserv.hpp"

// constructor
Server::Server(Socket &_socket) : socket(_socket) {}
// destructor
Server::~Server() {}

// main server loop
void Server::run() {
	// add the listening socket to the poll_fds vector
	struct pollfd pfd_listener = {socket.getFileDescriptor(), POLLIN, 0};
	poll_fds.push_back(pfd_listener);
	
	// start the server loop
	while (true) {
		int ret = poll(&poll_fds[0], poll_fds.size(), TIME_OUT_MS);
		if (ret == -1) {
			LOG_ERROR("Poll error");
			throw SocketException("Poll error");
		} 
		// timeout occurred
		if (ret == 0) continue;
		// check for events
		for (size_t i = 0; i < poll_fds.size(); ++i) {
			if (poll_fds[i].revents & (POLLHUP | POLLERR)) {
				// handle disconnection or error
				LOG_INFO("Client disconnected or error on fd: " + std::to_string(poll_fds[i].fd));
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
						LOG_INFO("New client connected, fd: " + std::to_string(client_fd));
					}
				} else {
					// handle client data here
					LOG_INFO("Data available to read on client fd: " + std::to_string(poll_fds[i].fd));
					handle_client_data_read(poll_fds[i].fd);
					// For simplicity, we will just close the client connection
					::close(poll_fds[i].fd);
					LOG_INFO("Client disconnected, fd: " + std::to_string(poll_fds[i].fd));
					poll_fds.erase(poll_fds.begin() + i);
					--i; // adjust index after erasing
				}
			}
			if (poll_fds[i].revents & POLLOUT) {
				// handle client data write here if needed
				LOG_INFO("Ready to write on client fd: " + std::to_string(poll_fds[i].fd));
				handle_client_data_write(poll_fds[i].fd);
			}
		}
	}
}