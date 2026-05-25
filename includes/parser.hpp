#pragma once

#include <string>
#include <vector>
#include "config.hpp"
#include "lexer.hpp"

// ---------------------------------------------------------------------------
// Parser: consumes a token stream from a Lexer and produces a list of
// ServerConfig structs. The single public entry point is parseFile(); the
// recursion lives entirely in the private grammar methods.
//
// One method per grammar production — that's recursive descent. The naming
// mirrors the BNF directly so you can read the grammar straight off the
// declarations.
//
// Errors: any malformed input throws ConfigException with a message of the
// form "<origin>:<line>: <what went wrong>". The lexer never throws; all
// failure paths are here.
// ---------------------------------------------------------------------------
class Parser {
	public:
		// Caller owns the Lexer. We just borrow it (reference, no ownership).
		Parser(Lexer& lex);

		// Top-level entry point. Clears `out` and appends one ServerConfig
		// per server block found in the input.
		void parseFile(std::vector<ServerConfig>& out);

	private:
		Lexer& lex_;

		// ---- Token-stream primitives ----------------------------------
		// match: consume the next token if it has the requested kind, return
		// true. Otherwise leave the stream untouched and return false.
		// Never throws — used when a token is optional.
		bool match(TokenKind kind);

		// expect: require the next token to have the requested kind. On
		// success consume and return it. On failure throw ConfigException
		// with a message that names `context` (e.g. "after 'listen' value").
		const Token& expect(TokenKind kind, const char* context);

		// ---- Error formatting -----------------------------------------
		// Build a "<origin>:<line>: " prefix from a token. Every error
		// message in this module is constructed with this so the format
		// stays uniform.
		std::string locOf(const Token& t) const;

		// ---- Grammar productions --------------------------------------
		void parseServerBlock(ServerConfig& server);
		void parseServerDirective(ServerConfig& server);
		void parseListen(ServerConfig& server);

		// Recovery: skip tokens until we land on the next ';' (consumed)
		// or '}' (left in place). Used after a warning on an unknown
		// directive so we can keep going instead of aborting.
		void skipToEndOfDirective();

		// ---- Value converters ----------------------------------------
		// Parse a WORD token's text as a TCP port (1..65535). Throws on
		// non-numeric input, trailing junk, or out-of-range values.
		int parsePort(const Token& tok);

		// Split a "listen" argument into (host, port). Accepts "PORT",
		// "HOST:PORT", and "[IPv6]:PORT" (the IPv6 form is not used yet
		// but the splitter handles it harmlessly).
		void splitHostPort(const Token& tok,
						   std::string& host_out, int& port_out);

		// Non-copyable: Parser holds a reference and unique progress state.
		Parser(const Parser&);
		Parser& operator=(const Parser&);
};
