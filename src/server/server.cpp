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

// constructor
Server::Server(Socket &_socket) : socket(_socket) {
	setup_signal_handlers();
	LOG_DEBUG("<class Server -> server() : socket received " + toString(socket.getFileDescriptor()));
}
// destructor
// Note: we do NOT close `socket` here — Server only borrows it (reference
// member). The listening Socket is owned by main() and will close itself
// when its destructor runs. Closing here as well caused a double ::close()
// on the same fd, which is unsafe (the kernel may have reused that fd
// number for an unrelated file by then).
Server::~Server() {
}

// main server loop
void Server::run() {
	// add the listening socket to the poll_fds vector
	LOG_DEBUG("<class Server -> run() : adding listening socket to poll_fds");
	struct pollfd pfd_listener = {socket.getFileDescriptor(), POLLIN, 0};
	poll_fds.push_back(pfd_listener);
	
	// start the server loop
	while (g_shutdown != 1) {
		LOG_DEBUG("<class Server -> run() : polling for events");
		int ret = poll(&poll_fds[0], poll_fds.size(), TIME_OUT_MS);
		if (ret == -1) {
			if (errno == EINTR) {
				LOG_DEBUG("<class Server -> run() : EINTR received, checking shutdown flag");
				continue; // Interrupted by signal, check shutdown flag and continue
			}
			LOG_ERROR("<class Server> Poll error");
			throw SocketException("<class Server> Poll error");
		} 
		// timeout occurred
		if (ret == 0) continue;
		// check for events
		for (size_t i = 0; i < poll_fds.size(); ++i) {
			if ((poll_fds[i].revents & (POLLHUP | POLLERR)) && poll_fds[i].fd != socket.getFileDescriptor()) {
				// handle disconnection or error
				LOG_DEBUG("<class Server -> run(): Client disconnected or error on fd: " + toString(poll_fds[i].fd));
				::close(poll_fds[i].fd);
				poll_fds.erase(poll_fds.begin() + i--);
				continue;
			}
			if (poll_fds[i].revents & POLLIN) {
				if (poll_fds[i].fd == socket.getFileDescriptor()) {
					// New incoming connection on the listening socket.
					LOG_DEBUG("<class Server -> run() : new incoming connection");
					int client_fd = socket.acceptClient();
					if (client_fd != -1) {
						// Subscribe to POLLIN only. A fresh TCP socket is
						// already writable, so registering POLLOUT here
						// would fire on the very next poll() iteration
						// with nothing to actually send. POLLOUT is
						// requested later, in Phase 2.5, only once a
						// response is queued for this client.
						struct pollfd pfd_client = {client_fd, POLLIN, 0};
						poll_fds.push_back(pfd_client);
						LOG_DEBUG("<class Server -> run() : New client connected, fd: " + toString(client_fd));
					}
				} else {
					// Data available from an existing client.
					LOG_DEBUG("<class Server -> run() : Data available to read on client fd: " + toString(poll_fds[i].fd));
					if (!handle_client_data_read(poll_fds[i].fd)) {
						// recv() returned 0 (peer closed) or -1 (error).
						// Per the subject we cannot inspect errno after
						// read/write, so any failure here means "drop it".
						::close(poll_fds[i].fd);
						poll_fds.erase(poll_fds.begin() + i--);
						continue; // skip the POLLOUT check below for this slot
					}
				}
			}
			if (poll_fds[i].revents & POLLOUT) {
				// Phase 1 stub: nothing is queued for writing yet, and we
				// no longer register POLLOUT on accept, so in practice this
				// branch is dormant until Phase 2 starts queuing responses.
				LOG_DEBUG("<class Server -> run() : Ready to write on client fd: " + toString(poll_fds[i].fd));
				handle_client_data_write(poll_fds[i].fd);
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

// Cleanup function to close all client sockets
void Server::cleanup_sockets() {
	LOG_DEBUG("<class Server -> cleanup_sockets() : Cleaning up client sockets");
	for (size_t i = 0; i < poll_fds.size(); ++i) {
		if (poll_fds[i].fd != socket.getFileDescriptor()) {
			LOG_DEBUG("<class Server -> cleanup_sockets() : Closing client socket fd: " + toString(poll_fds[i].fd));
			::close(poll_fds[i].fd);
		}
	}
	poll_fds.clear();
}