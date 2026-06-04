#pragma once

#include <string>
#include <vector>
#include <set>
#include "config.hpp"
#include "lexer.hpp"

// Parser: eats the Lexer's token stream and builds a list of ServerConfig
// structs. only parseFile() is public; all the recursion hides in the private
// grammar methods.
//
// it's a recursive-descent parser — one method per grammar production, named
// after the production, so I can basically read the grammar off the method
// list. anything malformed throws ConfigException reading
// "<origin>:<line>: <what broke>". the lexer never throws, so every failure
// path is in here.
class Parser {
	public:
		// the caller keeps the Lexer; I just hold a reference, no ownership.
		Parser(Lexer& lex);

		// the entry point. clears `out`, then appends one ServerConfig per
		// server block in the input.
		void parseFile(std::vector<ServerConfig>& out);

	private:
		// the ctor I use when recursing into an included file. it shares the
		// cycle guard (so I can spot cycles that span includes) and carries the
		// base dir for resolving any further relative paths.
		Parser(Lexer& lex, std::set<std::string>& shared_cycle_guard,
			   const std::string& base_dir);

		Lexer& lex_;

		// cycle detection for `include`. the top-level Parser owns the real
		// set; sub-Parsers just point at it, so every parser in one run sees
		// the same "currently open" set.
		std::set<std::string>  owned_cycle_guard_;
		std::set<std::string>* cycle_guard_;

		// what relative include paths resolve against. set from the Lexer's
		// origin in the top-level ctor, swapped to the included file's
		// directory in sub-Parsers.
		std::string base_dir_;

		// the token-stream primitives everything else leans on.
		// match: eat the next token if it's the right kind and return true,
		// otherwise leave the stream alone and return false. never throws —
		// I use it when a token is optional.
		bool match(TokenKind kind);

		// expect: demand the next token be a given kind. eat and return it if
		// so, otherwise throw ConfigException naming `context` (e.g. "after
		// 'listen' value") so the error says where I was.
		const Token& expect(TokenKind kind, const char* context);

		// builds the shared "<origin>:<line>: " error prefix from a token —
		// everything in here uses it so the messages all look the same.
		std::string locOf(const Token& t) const;

		// the grammar productions.
		void parseServerBlock(ServerConfig& server);
		void parseServerDirective(ServerConfig& server);
		void parseListen(ServerConfig& server);
		void parseServerName(ServerConfig& server);
		void parseRoot(ServerConfig& server);
		void parseClientMaxBodySize(ServerConfig& server);
		void parseErrorPage(ServerConfig& server);

		// "location <path> { ... }": parseLocation grabs the path,
		// parseLocationBlock walks the directives inside.
		void parseLocation(ServerConfig& server);
		void parseLocationBlock(LocationConfig& loc);
		void parseLocationDirective(LocationConfig& loc);

		void parseAllowedMethods(LocationConfig& loc);
		void parseIndex(LocationConfig& loc);
		void parseAutoindex(LocationConfig& loc);
		void parseReturn(LocationConfig& loc);
		void parseLocRoot(LocationConfig& loc);     // location-level override
		void parseUploadStore(LocationConfig& loc);
		void parseCgi(LocationConfig& loc);

		// `include FILE;` inside a server block — recursively parses that
		// file's server-level directives into `server`. throws on a cycle or
		// a file I can't read.
		void parseInclude(ServerConfig& server);

		// my error recovery: skip ahead to the next ';' at brace depth 0 (and
		// eat it) or a '}' at depth 0 (and leave it). counts braces so an
		// unknown block-form directive gets skipped cleanly too.
		void skipToEndOfDirective();

		// the value converters.
		// the workhorse: read a WORD as an int in [lo, hi]. `what` is just the
		// name to put in an error ("port", "HTTP status code", ...). throws
		// ConfigException on anything wrong.
		long parseIntInRange(const Token& tok, long lo, long hi,
							 const char* what);

		// thin wrappers over parseIntInRange.
		int parsePort(const Token& tok);
		int parseStatusCode(const Token& tok);

		// parse a size with an optional case-insensitive K/M/G suffix:
		// "10000" -> 10000, "10K" -> 10240, "1M" -> 1048576. throws on garbage,
		// a missing number, or overflow.
		std::size_t parseSize(const Token& tok);

		// split a "listen" argument into (host, port). handles "PORT",
		// "HOST:PORT", and "[IPv6]:PORT".
		void splitHostPort(const Token& tok,
						   std::string& host_out, int& port_out);

		// non-copyable — it holds a reference and its own parse-progress state.
		Parser(const Parser&);
		Parser& operator=(const Parser&);
};
