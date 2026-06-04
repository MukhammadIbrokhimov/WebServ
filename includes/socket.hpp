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

		// no copying. the C++98 way is to declare these private and never
		// define them, so any attempt to copy fails at link time. I need this
		// because a Socket owns a raw fd: if it got copied, two Sockets would
		// both think they own that fd and both close it in their dtor. by the
		// second close the kernel may have handed the number to something else,
		// so I'd be closing a totally unrelated fd.
		Socket(const Socket&);
		Socket& operator=(const Socket&);

	public:
		// Phase 1.5 listener ctor, built straight from a config entry.
		// `host` is a numeric IPv4 string ("0.0.0.0", "127.0.0.1") and I use
		// AI_NUMERICHOST so getaddrinfo never does a DNS lookup. SO_REUSEADDR
		// goes on before bind so restarting doesn't hit EADDRINUSE. throws
		// SocketException if anything fails — the cleanup-on-throw details are
		// in socket.cpp.
		Socket(const std::string& host, int port);
		~Socket();
		void startListening(int backlog = SOMAXCONN);
		void close();
		int acceptClient();
		int getFileDescriptor() const;
};