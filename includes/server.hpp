#pragma once

#include "socket.hpp"
#include "config.hpp"
#include <vector>
#include <map>
#include <poll.h>
#include <iostream>
#include <unistd.h>
#include <csignal>

#include "string_utils.hpp"

extern volatile sig_atomic_t g_shutdown;

class Server {
	private:
		std::vector<struct pollfd> poll_fds;

		// Phase 1.5 members.
		// config_ is just a borrow — the real Config lives in main()'s stack
		// frame, which outlives this Server, so holding a bare pointer is fine.
		// later, Phase 2.4's router reads fd_to_server_ to figure out which
		// server block a request belongs to.
		//
		// the thing I have to be careful about: Config must NOT change after
		// Config::load(). fd_to_server_ holds raw pointers into the vector
		// inside cfg.servers(), and a single push_back could reallocate that
		// vector and leave every pointer in the map dangling. main() loads it
		// once and never touches it again — that has to stay true.
		const Config*                                config_;
		std::vector<Socket*>                         listeners_;
		std::map<int, const ServerConfig*>           fd_to_server_;
		// second map on the same listener fds. it's so run() can grab the
		// owning Socket straight from the fd on POLLIN instead of looping over
		// listeners_ every time — only really matters once Phase 4's stress
		// test throws a lot of listeners at it.
		std::map<int, Socket*>                       fd_to_listener_;

		Server(const Server&);
		Server& operator=(const Server&);

	public:
		// Phase 1.5 ctor: walk cfg.servers(), open a Socket per ListenSpec,
		// fill the maps. if it fails partway it cleans up after itself, so the
		// caller never gets a half-built Server back (the dtor wouldn't run on
		// a ctor throw, so it has to be done inside).
		Server(const Config& cfg);

		~Server();
		void run();
		bool handle_client_data_read(int client_fd);
		void handle_client_data_write(int client_fd);
		void cleanup_sockets();
		static void signal_handler(int signum);
		static void setup_signal_handlers();
};
