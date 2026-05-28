#include "../../includes/webserv.hpp"

//global signal handler for graceful shutdown
volatile sig_atomic_t g_shutdown = 0;

void Server::signal_handler(int signum) {
	(void)signum; // suppress unused parameter warning
	g_shutdown = 1;
}

void Server::setup_signal_handlers() {
	struct sigaction sa;
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	
	if (sigaction(SIGINT, &sa, NULL) == -1) {
		LOG_ERROR("<class Server -> signal_handler(): Error setting up signal handler for SIGINT");
		throw SocketException("Error setting up signal handler for SIGINT");
	}
	if (sigaction(SIGTERM, &sa, NULL) == -1) {
		LOG_ERROR("<class Server -> signal_handler(): Error setting up signal handler for SIGTERM");
		throw SocketException("Error setting up signal handler for SIGTERM");
	}
}

// Legacy ctor — to be removed in Task 5. Sets up the single-Socket reference
// path so existing main.cpp still works while we stage the refactor.
Server::Server(Socket &_socket)
	: poll_fds()
	, config_(NULL)
	, listeners_()
	, fd_to_server_()
	, legacy_socket_(&_socket)
{
	setup_signal_handlers();
	LOG_DEBUG("<class Server -> legacy ctor> socket fd "
	          + toString(_socket.getFileDescriptor()));
}

// Phase 1.5 ctor. The body has three phases:
//   1. Walk cfg.servers() and allocate one Socket per ListenSpec.
//   2. Each allocation that succeeds is pushed into listeners_ and registered
//      in fd_to_server_ immediately, so a later failure has a precise list of
//      what to clean up.
//   3. setup_signal_handlers() runs last so the invariant "handlers installed
//      iff Server fully constructed" holds.
//
// If any Socket ctor throws, we catch (...) and delete every Socket we
// already own, then rethrow. This is necessary because a constructor that
// throws does NOT trigger its own destructor — without manual cleanup we
// leak.
Server::Server(const Config& cfg)
	: poll_fds()
	, config_(&cfg)
	, listeners_()
	, fd_to_server_()
	, legacy_socket_(NULL)
{
	try {
		const std::vector<ServerConfig>& servers = cfg.servers();
		for (std::size_t i = 0; i < servers.size(); ++i) {
			const ServerConfig& srv = servers[i];
			for (std::size_t j = 0; j < srv.listens.size(); ++j) {
				const ListenSpec& ls = srv.listens[j];

				Socket* s = new Socket(ls.host, ls.port);
				listeners_.push_back(s);
				fd_to_server_[s->getFileDescriptor()] = &srv;
				s->startListening();
			}
		}
	} catch (...) {
		// Partial-init cleanup: a Socket ctor or startListening() threw.
		// listeners_ contains only the ones that fully succeeded; delete
		// each (their dtors close their fds).
		for (std::size_t k = 0; k < listeners_.size(); ++k)
			delete listeners_[k];
		listeners_.clear();
		fd_to_server_.clear();
		throw;
	}

	setup_signal_handlers();
	LOG_DEBUG("<class Server> ready, " + toString(listeners_.size())
	          + " listener(s)");
}

// Destructor: only runs when the ctor completed. We close any still-open
// client fds via the existing cleanup_sockets() helper, then delete every
// owned listener Socket (each Socket's own dtor closes its fd).
//
// Note: under the legacy ctor path (legacy_socket_ != NULL, listeners_
// empty) we still avoid double-closing the borrowed socket — the loop
// just iterates over an empty listeners_ vector.
Server::~Server() {
	cleanup_sockets();
	for (std::size_t i = 0; i < listeners_.size(); ++i)
		delete listeners_[i];
	listeners_.clear();
}

// main server loop
void Server::run() {
	// Seed poll_fds with all listening fds. We support both the legacy
	// single-socket path (until Task 5 removes it) and the new vector path.
	LOG_DEBUG("<class Server -> run() : adding listening sockets to poll_fds");
	if (!listeners_.empty()) {
		for (std::size_t i = 0; i < listeners_.size(); ++i) {
			struct pollfd pfd = {listeners_[i]->getFileDescriptor(),
			                     POLLIN, 0};
			poll_fds.push_back(pfd);
		}
	} else if (legacy_socket_ != NULL) {
		struct pollfd pfd = {legacy_socket_->getFileDescriptor(), POLLIN, 0};
		poll_fds.push_back(pfd);
	}

	while (g_shutdown != 1) {
		LOG_DEBUG("<class Server -> run() : polling for events");
		int ret = poll(&poll_fds[0], poll_fds.size(), TIME_OUT_MS);
		if (ret == -1) {
			if (errno == EINTR) {
				LOG_DEBUG("<class Server -> run() : EINTR, checking shutdown");
				continue;
			}
			LOG_ERROR("<class Server> Poll error");
			throw SocketException("<class Server> Poll error");
		}
		if (ret == 0) continue;

		for (size_t i = 0; i < poll_fds.size(); ++i) {
			const int fd = poll_fds[i].fd;
			const bool is_listener =
				fd_to_server_.count(fd) > 0
				|| (legacy_socket_ != NULL
				    && fd == legacy_socket_->getFileDescriptor());

			if ((poll_fds[i].revents & (POLLHUP | POLLERR)) && !is_listener) {
				LOG_DEBUG("<class Server -> run(): Client disconnected/error fd "
				          + toString(fd));
				::close(fd);
				poll_fds.erase(poll_fds.begin() + i--);
				continue;
			}
			if (poll_fds[i].revents & POLLIN) {
				if (is_listener) {
					LOG_DEBUG("<class Server -> run() : new incoming connection on fd "
					          + toString(fd));
					int client_fd = -1;
					// find the Socket* that owns this fd and accept on it
					for (std::size_t k = 0; k < listeners_.size(); ++k) {
						if (listeners_[k]->getFileDescriptor() == fd) {
							client_fd = listeners_[k]->acceptClient();
							break;
						}
					}
					if (client_fd == -1 && legacy_socket_ != NULL
					    && fd == legacy_socket_->getFileDescriptor())
					{
						client_fd = legacy_socket_->acceptClient();
					}
					if (client_fd != -1) {
						struct pollfd pfd_client = {client_fd, POLLIN, 0};
						poll_fds.push_back(pfd_client);
						LOG_DEBUG("<class Server -> run() : New client fd "
						          + toString(client_fd));
					}
				} else {
					LOG_DEBUG("<class Server -> run() : Data ready on client fd "
					          + toString(fd));
					if (!handle_client_data_read(fd)) {
						::close(fd);
						poll_fds.erase(poll_fds.begin() + i--);
						continue;
					}
				}
			}
			if (poll_fds[i].revents & POLLOUT) {
				LOG_DEBUG("<class Server -> run() : Ready to write on fd "
				          + toString(fd));
				handle_client_data_write(fd);
			}
		}
	}
	LOG_INFO("<class Server> Shutdown signal received, cleaning up sockets");
	this->cleanup_sockets();
	LOG_INFO("<class Server> Server shutdown complete.");
}

// Phase 1 minimal read: drain whatever the kernel has and detect when the
// peer has closed the connection. Phase 2.1 will replace this with a real
// HTTP request parser that accumulates a per-client read buffer.
//
// Return value contract used by run():
//   true  -> connection still healthy, keep the fd in poll_fds
//   false -> peer closed (recv == 0) or recv failed; caller must close+erase
//
// The 42 webserv subject forbids checking errno after a read/write call, so
// we cannot distinguish EAGAIN from a real error. We trust poll() to be the
// authoritative source of readiness and treat any -1 as terminal.
bool Server::handle_client_data_read(int client_fd) {
	char buffer[4096];
	ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
	if (n <= 0) {
		LOG_INFO("<class Server> Client fd " + toString(client_fd)
				 + " closed (recv returned " + toString(static_cast<long>(n)) + ")");
		return false;
	}
	LOG_DEBUG("<class Server> Read " + toString(static_cast<long>(n))
			  + " bytes from fd " + toString(client_fd));
	// Phase 1: data is intentionally discarded. Parsing arrives in 2.1.
	return true;
}

void Server::handle_client_data_write(int client_fd) {
	// Placeholder for writing data to client. Phase 2.2/2.5 will drain a
	// per-client write buffer here.
	LOG_DEBUG("<class Server> Writing data to client fd: " + toString(client_fd));
}

// Closes any still-open *client* fds in poll_fds. Listener fds are NOT
// closed here — their lifetime belongs to the owning Socket objects (either
// legacy_socket_ or the listeners_ vector); closing here would double-close.
void Server::cleanup_sockets() {
	LOG_DEBUG("<class Server -> cleanup_sockets() : closing client fds");
	for (size_t i = 0; i < poll_fds.size(); ++i) {
		const int fd = poll_fds[i].fd;
		const bool is_listener =
			fd_to_server_.count(fd) > 0
			|| (legacy_socket_ != NULL
			    && fd == legacy_socket_->getFileDescriptor());
		if (!is_listener) {
			LOG_DEBUG("<class Server -> cleanup_sockets() : closing client fd "
			          + toString(fd));
			::close(fd);
		}
	}
	poll_fds.clear();
}