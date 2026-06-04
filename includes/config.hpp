#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstddef>   // size_t

// just the data the parser fills in — deliberately no parsing logic in here.
// I split it into stages on purpose: parser produces these structs, validator
// checks them, server reads them and never writes. keeping the plain data on
// its own means I can test each stage without dragging the others in.
//
// the subject's required features map onto these fields like so:
//   interface:port pairs              -> ListenSpec, ServerConfig::listens
//   default error pages               -> ServerConfig::error_pages
//   max client body size              -> ServerConfig::client_max_body_size
//   accepted HTTP methods per route   -> LocationConfig::allowed_methods
//   HTTP redirection per route        -> LocationConfig::redirect
//   root per route (the /kapouet one) -> LocationConfig::root
//   directory listing on/off          -> LocationConfig::autoindex
//   default file for a directory      -> LocationConfig::index
//   where uploads get stored          -> LocationConfig::upload_store
//   CGI by file extension             -> LocationConfig::cgi

// one "listen" directive: an interface + a port. the parser takes all three
// syntaxes below and squashes them into this same struct:
//   listen 8080;              -> host = "0.0.0.0", port = 8080
//   listen 127.0.0.1:8080;    -> host = "127.0.0.1", port = 8080
//   listen 0.0.0.0:8080;      -> host = "0.0.0.0",  port = 8080
struct ListenSpec {
	std::string host;
	int         port;

	ListenSpec();
};

// optional per-location redirect. no std::optional in C++98, so I fake it with
// an `enabled` bool — the parser flips it true when the location actually has a
// `return CODE TARGET;`.
struct Redirect {
	int         code;
	std::string target;
	bool        enabled;

	Redirect();
};

// everything that can live inside a `location` block.
//
// instead of extra "is this set" flags I lean on empty string / empty vector
// to mean "not set", so I have to remember what empty means for each:
//   root.empty()             -> fall back to ServerConfig::root
//   index.empty()            -> no default file (autoindex or a 403/404 decides)
//   allowed_methods.empty()  -> allow everything (same as nginx's default)
//   upload_store.empty()     -> uploads not configured here
//   cgi.empty()              -> no CGI on this location
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

// one `server { ... }` block from the file.
struct ServerConfig {
	std::vector<ListenSpec>           listens;
	std::string                       server_name;
	std::string                       root;
	std::size_t                       client_max_body_size;
	std::map<int, std::string>        error_pages;     // 404 -> "/404.html"
	std::vector<LocationConfig>       locations;

	ServerConfig();
};

// the top of the tree: a list of server blocks plus the calls to build and
// read it. I added these one at a time as I built the lexer, then parser, then
// validator — the header compiles fine on its own in the meantime.
class Config {
	private:
		std::vector<ServerConfig> servers_;

		// the semantic checks I run after parsing. throws ConfigException with
		// a readable message if a rule is broken. the actual rules are listed
		// in config.cpp.
		void validate();

		// non-copyable: I build a Config once at startup and only ever read it.
		Config(const Config&);
		Config& operator=(const Config&);

	public:
		Config();

		// read `path`, then lexer -> parser -> validator. throws
		// ConfigException the moment anything along that chain goes wrong.
		void load(const std::string& path);

		// read-only handle for the rest of the server.
		const std::vector<ServerConfig>& servers() const;

};
