#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <string>
#include <fcntl.h>
#include <cerrno>
#include "string_utils.hpp"

class Socket {
	private:
		int fd_socket;
		struct sockaddr_in _address;

		// Disabled copy semantics (C++98 idiom: declared private, not implemented).
		// A Socket owns a raw file descriptor; copying it would let two objects
		// believe they own the same fd and each close it in their destructor,
		// which on POSIX may end up closing an unrelated fd reassigned by the
		// kernel in the meantime. Linking will fail if anyone tries to copy.
		Socket(const Socket&);
		Socket& operator=(const Socket&);

	public:
		Socket(int port);
		// Phase 1.5: config-driven listener constructor.
		// Resolves `host` (a numeric IPv4 string like "0.0.0.0" or "127.0.0.1")
		// via getaddrinfo with AI_NUMERICHOST so no DNS happens. Sets
		// SO_REUSEADDR before bind so we can restart inside TIME_WAIT without
		// EADDRINUSE. Throws SocketException on any setup failure; the body
		// guarantees the fd and addrinfo chain are released on every throw
		// path (see socket.cpp for the cleanup contract).
		Socket(const std::string& host, int port);
		~Socket();
		void startListening(int backlog = SOMAXCONN);
		void close();
		int acceptClient();
		int getFileDescriptor() const;
};