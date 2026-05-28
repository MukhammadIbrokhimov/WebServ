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

		// Phase 1.5 members --------------------------------------------------
		// `config_` is a borrow: it lives in main()'s stack frame, longer
		// than this Server, so the pointer is safe for our lifetime.
		// Phase 2.4's router will read fd_to_server_ to know which server
		// block a request lands on.
		//
		// INVARIANT: `Config` must NOT be mutated after `Config::load()` for
		// the lifetime of this Server. fd_to_server_ stores raw pointers to
		// elements of cfg.servers()'s internal vector; any push_back would
		// reallocate that vector and invalidate every pointer in the map.
		// Today main() loads cfg once and never touches it again — keep that.
		const Config*                                config_;
		std::vector<Socket*>                         listeners_;
		std::map<int, const ServerConfig*>           fd_to_server_;
		// Parallel map keyed on the same listener fds as fd_to_server_. Lets
		// run() jump straight to the owning Socket on POLLIN instead of
		// scanning listeners_ linearly — matters once Phase 4's stress test
		// pushes the listener count up.
		std::map<int, Socket*>                       fd_to_listener_;
		// --------------------------------------------------------------------

		Server(const Server&);
		Server& operator=(const Server&);

	public:
		// Phase 1.5 ctor. Walks cfg.servers(), opens one Socket per
		// ListenSpec, populates fd_to_server_. Partial-init failures are
		// cleaned up inside the ctor so the caller never sees a half-built
		// Server (ctor exceptions skip the dtor).
		Server(const Config& cfg);

		~Server();
		void run();
		bool handle_client_data_read(int client_fd);
		void handle_client_data_write(int client_fd);
		void cleanup_sockets();
		static void signal_handler(int signum);
		static void setup_signal_handlers();
};
