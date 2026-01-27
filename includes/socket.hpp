#pragma once

#include <sys/socket.h>
#include <string>

class Socket {
	public:
		Socket(int domain, int type);
		~Socket();
		void bind(int port, const std::string& host = "127.0.0.1");
		void listen(int backlog = SOMAXCONN);
		void accept();
		void close();
		int getFileDescriptor() const;
};