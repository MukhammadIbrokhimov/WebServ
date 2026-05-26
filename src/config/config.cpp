#include "../../includes/webserv.hpp"
#include "../../includes/parser.hpp"
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
// Step 1 of the config parser: constructors only.
//
// At this point we have the *shape* of a fully parsed config (the structs in
// includes/config.hpp) and sensible defaults for every optional field. The
// lexer, parser, and validator will arrive in subsequent steps.
//
// Why defaults live in constructors (and not at the declaration site):
// C++98 forbids in-class member initialisers for non-static, non-const
// members. The only place to set defaults for a struct is its constructor
// initialiser list. Every default below is a deliberate choice — see the
// inline comments.
// ---------------------------------------------------------------------------

ListenSpec::ListenSpec()
	: host("0.0.0.0")   // bind on all interfaces when only a port is given
	, port(0)           // 0 means "unset"; validator will reject it
{}

Redirect::Redirect()
	: code(0)
	, target("")
	, enabled(false)    // default: no redirect on this location
{}

LocationConfig::LocationConfig()
	: path("")
	, allowed_methods()       // empty vector == "all methods allowed"
	, root("")                // empty == inherit ServerConfig::root
	, index("")
	, autoindex(false)        // disabled unless explicitly turned on
	, redirect()              // disabled by default (see Redirect ctor)
	, upload_store("")
	, cgi()
{}

ServerConfig::ServerConfig()
	: listens()
	, server_name("")
	, root("")
	, client_max_body_size(1024 * 1024)   // 1 MiB default; overridable in config
	, error_pages()
	, locations()
{}

Config::Config()
	: servers_()
{}

const std::vector<ServerConfig>& Config::servers() const {
	return servers_;
}

// ---------------------------------------------------------------------------
// Config::load — pipeline glue.
//
// 1. Read the file into a std::string.
// 2. Hand the string to the Lexer (which tokenises eagerly).
// 3. Hand the Lexer to the Parser (which fills our servers_ vector).
// 4. Validation pass will come in a later step.
//
// Any failure along the way bubbles up as ConfigException with a message in
// "<path>:<line>: ..." format.
// ---------------------------------------------------------------------------

static std::string readFile(const std::string& path) {
	std::ifstream in(path.c_str());
	if (!in)
		throw ConfigException("cannot open config file '" + path + "'");
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

void Config::load(const std::string& path) {
	std::string source = readFile(path);
	Lexer       lex(source, path);
	Parser      parser(lex);
	parser.parseFile(servers_);
	validate();
}

// ---------------------------------------------------------------------------
// Config::validate — semantic checks that the grammar alone cannot enforce.
//
// Rules (kept deliberately small for the mandatory grade; extend as needed):
//   1. The config must contain at least one server block.
//   2. Each server must declare at least one `listen` directive.
//   3. No two servers may share the exact same (host, port). Per the subject
//      virtual hosting is out of scope, so duplicate listens are a config
//      error — not an opportunity for Host-header routing.
//
// Deferred checks (intentionally not run here):
//   - Existence of `root` directories on disk: defer to request time, files
//     can legitimately appear after startup.
//   - Existence of CGI interpreters: same reasoning.
//   - Sanity of redirect targets: not knowable at parse time.
// ---------------------------------------------------------------------------
void Config::validate() {
	if (servers_.empty())
		throw ConfigException("config: no server blocks defined");

	// "host:port" -> index of the server that first claimed it. Keying on a
	// flat string is the simplest dedupe; we don't need a std::pair here.
	std::map<std::string, std::size_t> seen_listens;

	for (std::size_t i = 0; i < servers_.size(); ++i) {
		const ServerConfig& s = servers_[i];

		if (s.listens.empty()) {
			std::stringstream msg;
			msg << "config: server[" << i << "] has no 'listen' directive";
			throw ConfigException(msg.str());
		}

		for (std::size_t j = 0; j < s.listens.size(); ++j) {
			const ListenSpec& ls = s.listens[j];

			std::stringstream key_ss;
			key_ss << ls.host << ":" << ls.port;
			const std::string key = key_ss.str();

			std::map<std::string, std::size_t>::iterator it
				= seen_listens.find(key);
			if (it != seen_listens.end()) {
				std::stringstream msg;
				msg << "config: duplicate listen " << key
					<< " (server[" << i << "] conflicts with server["
					<< it->second << "])";
				throw ConfigException(msg.str());
			}
			seen_listens[key] = i;
		}
	}
}
