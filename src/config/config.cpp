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
}
