#include "../../includes/webserv.hpp"
#include "../../includes/parser.hpp"
#include <fstream>
#include <sstream>

// the ctors for the config structs, and nothing else (this was the first
// chunk I wrote — just the shape + defaults, lexer/parser/validator came after).
//
// reason all the defaults sit in ctor init-lists instead of next to the member
// declarations: C++98 won't let me write `int port = 0;` in a struct for
// non-static, non-const members. the init-list is the only spot. none of these
// values are arbitrary — see the comment on each.

ListenSpec::ListenSpec()
	: host("0.0.0.0")   // just a port given? bind every interface
	, port(0)           // 0 = "nobody set this"; the validator rejects it later
{}

Redirect::Redirect()
	: code(0)
	, target("")
	, enabled(false)    // off until a `return` directive turns it on
{}

LocationConfig::LocationConfig()
	: path("")
	, allowed_methods()       // empty == allow everything
	, root("")                // empty == inherit ServerConfig::root
	, index("")
	, autoindex(false)        // off unless the config says otherwise
	, redirect()              // off by default (see Redirect's ctor)
	, upload_store("")
	, cgi()
{}

ServerConfig::ServerConfig()
	: listens()
	, server_name("")
	, root("")
	, client_max_body_size(1024 * 1024)   // 1 MiB unless the config overrides it
	, error_pages()
	, locations()
{}

Config::Config()
	: servers_()
{}

const std::vector<ServerConfig>& Config::servers() const {
	return servers_;
}

// Config::load is just the glue that wires the stages together:
//   1. slurp the file into a string
//   2. give the string to the Lexer (tokenises it all at once)
//   3. give the Lexer to the Parser (fills servers_)
//   4. validate()
// anything that goes wrong anywhere in there comes back up as a
// ConfigException formatted "<path>:<line>: ...".

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

// the checks the grammar can't catch on its own. I kept the list small — just
// what the mandatory part needs — and can grow it later:
//   1. there has to be at least one server block.
//   2. every server needs at least one `listen`.
//   3. no two servers can claim the same (host, port). virtual hosting is out
//      of scope per the subject, so a duplicate listen is just a config error,
//      not a cue to start routing on the Host header.
//
// stuff I deliberately do NOT check here:
//   - whether `root` dirs exist on disk — check at request time instead, since
//     a directory can legitimately show up after the server starts.
//   - whether the CGI interpreter exists — same reasoning.
//   - whether a redirect target makes sense — can't know that at parse time.
void Config::validate() {
	if (servers_.empty())
		throw ConfigException("config: no server blocks defined");

	// "host:port" -> index of whichever server grabbed it first. just keying on
	// the joined string is the easiest way to dedupe; a std::pair would be overkill.
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
