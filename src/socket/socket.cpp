#include "../../includes/webserv.hpp"
#include <netdb.h>
#include <cstring>


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

// Phase 1.5: config-driven ctor. Two resources are acquired here and must
// be released on every throw path:
//   1. the addrinfo* chain returned by getaddrinfo  -> freeaddrinfo
//   2. the fd returned by socket()                   -> ::close(fd)
// We nest a try/catch around steps 2-4 so any throw runs freeaddrinfo before
// propagating. The fd is closed inline at each failure point, matching the
// existing single-arg ctor pattern above.
Socket::Socket(const std::string& host, int port) : fd_socket(-1) {
	// The config validator (Config::validate) does not yet enforce port
	// range, so guard here. A silent uint16_t truncation later (htons cast)
	// would let `listen 70000;` bind to a different port without warning.
	if (port <= 0 || port > 65535) {
		throw SocketException("invalid port (must be 1-65535): "
		                      + toString(port));
	}

	struct addrinfo  hints;
	struct addrinfo* res = NULL;

	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_NUMERICHOST | AI_PASSIVE;

	// service NULL + port written manually into the sockaddr below; we don't
	// pass a service string because we already have the integer port.
	if (getaddrinfo(host.c_str(), NULL, &hints, &res) != 0 || res == NULL) {
		LOG_ERROR("<class Socket> getaddrinfo failed for " + host);
		throw SocketException("getaddrinfo failed for host '" + host + "'");
	}

	// AI_NUMERICHOST + AF_INET => getaddrinfo returns a chain of length 1,
	// so we walk only res (ignore res->ai_next).
	try {
		fd_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd_socket == -1) {
			LOG_ERROR("<class Socket> Failed to create socket");
			throw SocketException("Failed to create socket");
		}

		// SO_REUSEADDR: lets us re-bind a port that is still in TIME_WAIT from
		// a previous instance. Subject p.6 lists setsockopt in the allowed
		// functions table. Must be set BEFORE bind().
		int yes = 1;
		if (setsockopt(fd_socket, SOL_SOCKET, SO_REUSEADDR,
		               &yes, sizeof(yes)) == -1)
		{
			LOG_ERROR("<class Socket> setsockopt SO_REUSEADDR failed");
			::close(fd_socket);
			fd_socket = -1;
			throw SocketException("setsockopt SO_REUSEADDR failed");
		}

		if (fcntl(fd_socket, F_SETFL, O_NONBLOCK) == -1) {
			LOG_ERROR("<class Socket> Failed to set socket to non-blocking");
			::close(fd_socket);
			fd_socket = -1;
			throw SocketException("Failed to set socket to non-blocking");
		}

		// Copy the resolved address into our member (so we don't depend on
		// res after freeaddrinfo) and overwrite the port field with the
		// caller's integer port (getaddrinfo left it at 0 since service was
		// NULL). htons because sin_port is network byte order.
		std::memcpy(&_address, res->ai_addr, sizeof(struct sockaddr_in));
		_address.sin_port = htons(static_cast<uint16_t>(port));

		if (bind(fd_socket, (struct sockaddr*)&_address,
		         sizeof(_address)) == -1)
		{
			LOG_ERROR("<class Socket> Failed to bind socket on "
			          + host + ":" + toString(port));
			::close(fd_socket);
			fd_socket = -1;
			throw SocketException("bind failed on " + host + ":"
			                      + toString(port));
		}

		LOG_DEBUG("<class Socket> Socket bound to " + host + ":"
		          + toString(port) + " fd=" + toString(fd_socket));
	} catch (...) {
		// any throw after getaddrinfo succeeded must free the chain
		freeaddrinfo(res);
		throw;
	}
	freeaddrinfo(res);
}
