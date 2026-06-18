#include "../../includes/webserv.hpp"

// one global flag the signal handler can poke. volatile sig_atomic_t because
// that's the only type the standard promises is safe to touch from inside a
// handler — anything fancier (locks, logging, the poll loop) isn't async-safe.
volatile sig_atomic_t g_shutdown = 0;

void Server::signal_handler(int signum) {
	(void)signum;        // don't care which signal — SIGINT and SIGTERM both mean "stop"
	g_shutdown = 1;      // just raise the flag; run() does the actual cleanup
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

// Phase 1.5 ctor. Three things happen in order:
//   1. walk cfg.servers() and make one Socket per ListenSpec.
//   2. the moment a Socket is built I push it into listeners_ and record it in
//      both maps — that way if a later one throws, I have an exact list of
//      what's already mine to clean up.
//   3. install the signal handlers dead last, so "handlers installed" always
//      means "Server is fully built". don't want a signal arriving mid-init.
//
// the catch(...) deletes everything I've already made and rethrows. I have to
// do this by hand: when a ctor throws, the object's own dtor never runs, so
// without this the already-opened sockets just leak.
Server::Server(const Config& cfg)
	: poll_fds()
	, config_(&cfg)
	, listeners_()
	, fd_to_server_()
	, fd_to_listener_()
{
	try {
		const std::vector<ServerConfig>& servers = cfg.servers();
		for (std::size_t i = 0; i < servers.size(); ++i) {
			const ServerConfig& srv = servers[i];
			for (std::size_t j = 0; j < srv.listens.size(); ++j) {
				const ListenSpec& ls = srv.listens[j];

				Socket* s = new Socket(ls.host, ls.port);
				listeners_.push_back(s);
				fd_to_server_[s->getFileDescriptor()]   = &srv;
				fd_to_listener_[s->getFileDescriptor()] = s;
				s->startListening();
			}
		}
	} catch (...) {
		// something blew up mid-setup (a Socket ctor or startListening).
		// listeners_ only holds the ones that fully made it, so deleting each
		// is safe and their dtors close the fds for me.
		for (std::size_t k = 0; k < listeners_.size(); ++k)
			delete listeners_[k];
		listeners_.clear();
		fd_to_server_.clear();
		fd_to_listener_.clear();
		throw;
	}

	setup_signal_handlers();
	LOG_DEBUG("<class Server> ready, " + toString(listeners_.size())
	          + " listener(s)");
}

// this only ever runs if the ctor actually finished (see the note above about
// ctor-throw skipping the dtor). first close any client fds still hanging
// around via cleanup_sockets(), then delete the listener Sockets — each one's
// dtor closes its own fd, so I don't close them here directly.
Server::~Server() {
	cleanup_sockets();
	for (std::size_t i = 0; i < listeners_.size(); ++i)
		delete listeners_[i];
	listeners_.clear();
}

// main server loop
void Server::run() {
	// prime poll_fds with the listeners; client fds get appended as they connect
	LOG_DEBUG("<class Server -> run() : adding listening sockets to poll_fds");
	for (std::size_t i = 0; i < listeners_.size(); ++i) {
		struct pollfd pfd = {listeners_[i]->getFileDescriptor(),
		                     POLLIN, 0};
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
			const bool is_listener = fd_to_server_.count(fd) > 0;

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
					// map lookup to grab the owning Socket directly instead of
					// scanning listeners_. I fill this map alongside
					// fd_to_server_ in the ctor, so if is_listener is true the
					// fd is definitely a key here.
					int client_fd = fd_to_listener_[fd]->acceptClient();
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

// Phase 1 version: just read whatever's there and notice when the peer hangs
// up. the real HTTP parser with a per-client read buffer comes in Phase 2.1.
//
// what the return value means to run():
//   true  -> still connected, leave the fd in poll_fds
//   false -> peer closed (recv == 0) or recv failed; run() must close + erase it
//
// the subject won't let me check errno after read/write, so I genuinely can't
// tell EAGAIN apart from a real failure. I just let poll() be the source of
// truth for readiness and treat any -1 as "this connection is done".
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
	// for now I just throw the bytes away — parsing them is 2.1's job.
	return true;
}

void Server::handle_client_data_write(int client_fd) {
	// nothing to write yet. Phase 2.2/2.5 will drain a per-client write buffer
	// from here once responses actually exist.
	LOG_DEBUG("<class Server> Writing data to client fd: " + toString(client_fd));
}

// only closes *client* fds. the listener fds belong to the Socket objects in
// listeners_ and get closed by their dtors — if I closed them here too that'd
// be a double close.
void Server::cleanup_sockets() {
	LOG_DEBUG("<class Server -> cleanup_sockets() : closing client fds");
	for (size_t i = 0; i < poll_fds.size(); ++i) {
		const int fd = poll_fds[i].fd;
		const bool is_listener = fd_to_server_.count(fd) > 0;
		if (!is_listener) {
			LOG_DEBUG("<class Server -> cleanup_sockets() : closing client fd "
			          + toString(fd));
			::close(fd);
		}
	}
	poll_fds.clear();
}