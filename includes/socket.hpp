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

	public:
		Socket(int port);
		~Socket();
		void startListening(int backlog = SOMAXCONN);
		void close();
		int acceptClient();
		int getFileDescriptor() const;
};