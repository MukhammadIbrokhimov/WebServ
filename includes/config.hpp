#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstddef>   // size_t

// ---------------------------------------------------------------------------
// Config data model.
//
// This header defines ONLY the data structures the parser will fill. There is
// no parsing logic here on purpose — the parser is a separate stage that
// produces these structs, the validator is a separate stage that checks them,
// and the server consumes them read-only. Keeping the data model isolated
// makes each stage independently testable.
//
//   - interface:port pairs              -> ListenSpec, ServerConfig::listens
//   - default error pages               -> ServerConfig::error_pages
//   - max client body size              -> ServerConfig::client_max_body_size
//   - accepted HTTP methods per route   -> LocationConfig::allowed_methods
//   - HTTP redirection per route        -> LocationConfig::redirect
//   - root per route (/kapouet example) -> LocationConfig::root
//   - enable/disable directory listing  -> LocationConfig::autoindex
//   - default file for directories      -> LocationConfig::index
//   - upload storage location           -> LocationConfig::upload_store
//   - CGI by file extension             -> LocationConfig::cgi
// ---------------------------------------------------------------------------

// A single "listen" directive: which interface and which port. The parser
// accepts three syntaxes and normalises them all into this struct:
//   listen 8080;              -> host = "0.0.0.0", port = 8080
//   listen 127.0.0.1:8080;    -> host = "127.0.0.1", port = 8080
//   listen 0.0.0.0:8080;      -> host = "0.0.0.0",  port = 8080
struct ListenSpec {
	std::string host;
	int         port;

	ListenSpec();
};

// Optional per-location redirect. C++98 has no std::optional, so we carry an
// explicit `enabled` flag. The parser sets it true when the location has a
// `return CODE TARGET;` directive.
struct Redirect {
	int         code;
	std::string target;
	bool        enabled;

	Redirect();
};

// All settings that can appear inside a `location` block.
//
// Empty-string and empty-vector conventions:
//   - root.empty()             -> inherit ServerConfig::root
//   - index.empty()            -> no default file (autoindex or 403/404 decides)
//   - allowed_methods.empty()  -> all methods allowed (matches nginx default)
//   - upload_store.empty()     -> uploads not configured for this location
//   - cgi.empty()              -> no CGI on this location
struct LocationConfig {
	std::string                         path;
	std::vector<std::string>            allowed_methods;
	std::string                         root;
	std::string                         index;
	bool                                autoindex;
	Redirect                            redirect;
	std::string                         upload_store;
	std::map<std::string, std::string>  cgi;   // ".py" -> "/usr/bin/python3"

	LocationConfig();
};

// One `server { ... }` block from the config file.
struct ServerConfig {
	std::vector<ListenSpec>           listens;
	std::string                       server_name;
	std::string                       root;
	std::size_t                       client_max_body_size;
	std::map<int, std::string>        error_pages;     // 404 -> "/404.html"
	std::vector<LocationConfig>       locations;

	ServerConfig();
};

// Top-level configuration: a list of server blocks and the operations to
// produce / inspect it.
//
// Methods are declared here but implemented in later steps. The header
// compiles on its own; the implementation file will appear as we build the
// lexer, parser, and validator one at a time.
class Config {
	private:
		std::vector<ServerConfig> servers_;

		// Semantic checks run after parsing. Throws ConfigException with a
		// human-readable message if any rule is violated. See config.cpp
		// for the rule list.
		void validate();

		// Non-copyable: a Config is built once at startup and read forever.
		Config(const Config&);
		Config& operator=(const Config&);

	public:
		Config();

		// Reads `path`, runs lexer -> parser -> validator. Throws
		// ConfigException on any error encountered along the way.
		void load(const std::string& path);

		// Read-only access for the rest of the server.
		const std::vector<ServerConfig>& servers() const;

};
