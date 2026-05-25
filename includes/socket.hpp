#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
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
		~Socket();
		void startListening(int backlog = SOMAXCONN);
		void close();
		int acceptClient();
		int getFileDescriptor() const;
};