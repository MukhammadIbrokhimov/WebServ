#include "../../includes/webserv.hpp"
#include <netdb.h>
#include <cstring>


// dtor just closes the fd. all the real logic is in close(), which is
// idempotent, so it doesn't matter if I also called close() by hand earlier.
Socket::~Socket() {
	Socket::close();
	LOG_INFO("<class Socket> Socket closed");
}

// guard on -1 so calling this twice is a no-op, and reset to -1 afterwards so
// a later close never hits an fd the kernel has since handed to someone else.
void Socket::close() {
	if (fd_socket != -1) {
		::close(fd_socket);
		fd_socket = -1;
	}
}

// bind() already happened in the ctor; this is the separate step that flips the
// socket into accepting mode. backlog defaults to SOMAXCONN — let the OS pick.
void Socket::startListening(int backlog) {
	if (listen(fd_socket, backlog) == -1) {
		LOG_ERROR("<class Socket> Failed to listen on socket");
		throw SocketException("<class Socket> Failed to listen on socket");
	}
	LOG_INFO("<class Socket> Socket listening for incoming connections");
}

// returns the new client fd, or -1 if there was simply nothing to accept right
// now. the -1 case is normal with a non-blocking listener and the caller
// (Server::run) just shrugs and moves on; anything else is a real error.
int Socket::acceptClient() {
	struct sockaddr_in client_address;
	socklen_t client_address_len = sizeof(client_address);
	int client_fd = accept(fd_socket, (struct sockaddr *)&client_address, &client_address_len);
	if (client_fd == -1) {
		// poll() said readable but accept() came up empty — EAGAIN/EWOULDBLOCK.
		// not an error, just "someone else grabbed it / spurious wakeup".
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return -1;
		}
		LOG_ERROR("<class Socket> Failed to accept client");
		throw SocketException("Failed to accept client");
	}
	// the accepted fd does NOT inherit O_NONBLOCK from the listener, so I have
	// to set it again here — subject says every fd must be non-blocking.
	if (fcntl(client_fd, F_SETFL, O_NONBLOCK) == -1) {
		LOG_ERROR("<class Socket> Failed to set client socket to non-blocking");
		::close(client_fd);
		throw SocketException("Failed to set client socket to non-blocking");
	}
	LOG_INFO("<class Socket> Client accepted");
	return client_fd;
}

int Socket::getFileDescriptor() const {
	return fd_socket;
}

// Phase 1.5 ctor. The annoying part here is cleanup-on-failure: I grab two
// things that each need releasing if anything later throws —
//   1. the addrinfo* chain from getaddrinfo  -> freeaddrinfo
//   2. the fd from socket()                   -> ::close(fd)
// so I wrap everything after getaddrinfo in a try/catch that frees the chain
// before rethrowing, and close the fd inline at each spot it can fail. a ctor
// that throws never runs its own dtor, so none of this is automatic.
Socket::Socket(const std::string& host, int port) : fd_socket(-1) {
	// Config::validate doesn't check port range yet, so I check it here.
	// without this, the htons cast below silently truncates to uint16_t and
	// `listen 70000;` would quietly bind some unrelated port.
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

	// passing NULL for the service since I already have the port as an int and
	// write it straight into the sockaddr further down — no point making
	// getaddrinfo parse a port string for me.
	if (getaddrinfo(host.c_str(), NULL, &hints, &res) != 0 || res == NULL) {
		LOG_ERROR("<class Socket> getaddrinfo failed for " + host);
		throw SocketException("getaddrinfo failed for host '" + host + "'");
	}

	// with AI_NUMERICHOST + AF_INET there's only ever one result, so I just use
	// res and never bother walking res->ai_next.
	try {
		fd_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd_socket == -1) {
			LOG_ERROR("<class Socket> Failed to create socket");
			throw SocketException("Failed to create socket");
		}

		// SO_REUSEADDR so I can restart the server right away instead of
		// getting EADDRINUSE while the old port sits in TIME_WAIT. has to go
		// before bind() to matter. setsockopt is allowed per subject p.6.
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

		// copy the address into my own member so I'm not still pointing into
		// res after freeaddrinfo. getaddrinfo left the port at 0 (NULL service),
		// so I fill it in myself — htons because sin_port is network byte order.
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
		// got past getaddrinfo, so the chain is allocated — free it before the
		// exception leaves, otherwise it leaks.
		freeaddrinfo(res);
		throw;
	}
	freeaddrinfo(res);
}
