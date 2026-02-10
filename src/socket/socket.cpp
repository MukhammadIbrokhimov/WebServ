#include "../../includes/webserv.hpp"


// Constructor with port number and create a socket with AF_INET and SOCK_STREAM
Socket::Socket(int port) : fd_socket(socket(AF_INET, SOCK_STREAM, 0)) {
	if (fd_socket == -1) {
		LOG_ERROR("<class Socket> Failed to create socket");
		::close(fd_socket);
		throw SocketException("Failed to create socket");
	}

	// set the socket to non-blocking
	if (fcntl(fd_socket, F_SETFL, O_NONBLOCK) == -1) {
		LOG_ERROR("<class Socket> Failed to set socket to non-blocking");
		::close(fd_socket);
		throw SocketException("Failed to set socket to non-blocking");
	}

	// bind the socket to the port
	_address.sin_family = AF_INET;
	_address.sin_port = htons(port);
	_address.sin_addr.s_addr = INADDR_ANY;
	if (bind(fd_socket, (struct sockaddr *)&_address, sizeof(_address)) == -1) {
		LOG_ERROR("<class Socket> Failed to bind socket");
		::close(fd_socket);
		throw SocketException("Failed to bind socket");
	}
	LOG_DEBUG("<class Socket> Socket bound to port " + toString(port));
	LOG_DEBUG("<class Socket> Socket file descriptor: " + toString(fd_socket));
}

// destructor to close the socket will call the close method
Socket::~Socket() {
	Socket::close();
	LOG_INFO("<class Socket> Socket closed");
}

// close the socket fd_socket
void Socket::close() {
	if (fd_socket != -1) {
		::close(fd_socket);
		fd_socket = -1;
	}
}

// start listening for incoming connections
void Socket::startListening(int backlog) {
	if (listen(fd_socket, backlog) == -1) {
		LOG_ERROR("<class Socket> Failed to listen on socket");
		throw SocketException("<class Socket> Failed to listen on socket");
	}
	LOG_INFO("<class Socket> Socket listening for incoming connections");
}

// accept a new client connection
int Socket::acceptClient() {
	struct sockaddr_in client_address;
	socklen_t client_address_len = sizeof(client_address);
	int client_fd = accept(fd_socket, (struct sockaddr *)&client_address, &client_address_len);
	if (client_fd == -1) {
		// if the socket is non-blocking and there is no client connection, return -1
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return -1;
		}
		LOG_ERROR("<class Socket> Failed to accept client");
		throw SocketException("Failed to accept client");
	}
	// set the client socket to non-blocking
	if (fcntl(client_fd, F_SETFL, O_NONBLOCK) == -1) {
		LOG_ERROR("<class Socket> Failed to set client socket to non-blocking");
		::close(client_fd);
		throw SocketException("Failed to set client socket to non-blocking");
	}
	LOG_INFO("<class Socket> Client accepted");
	return client_fd;
}

// get the file descriptor of the socket
int Socket::getFileDescriptor() const {
	return fd_socket;
}