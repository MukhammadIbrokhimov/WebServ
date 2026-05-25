#include "../../includes/webserv.hpp"
#include "../../includes/parser.hpp"
#include <sstream>
#include <iostream>   // std::cerr for unknown-directive warnings

// ---------------------------------------------------------------------------
// Parser implementation.
// Read includes/parser.hpp for the architecture and grammar overview.
// ---------------------------------------------------------------------------

Parser::Parser(Lexer& lex) : lex_(lex) {}

// ---------- Token-stream primitives ---------------------------------------

bool Parser::match(TokenKind kind) {
	if (lex_.peek().kind != kind)
		return false;
	lex_.next();
	return true;
}

const Token& Parser::expect(TokenKind kind, const char* context) {
	const Token& t = lex_.peek();
	if (t.kind != kind) {
		std::stringstream ss;
		ss << locOf(t) << "expected ";
		switch (kind) {
			case TOK_WORD:   ss << "a word";       break;
			case TOK_LBRACE: ss << "'{'";          break;
			case TOK_RBRACE: ss << "'}'";          break;
			case TOK_SEMI:   ss << "';'";          break;
			case TOK_EOF:    ss << "end of file";  break;
		}
		ss << " " << context << ", got '" << t.text << "'";
		throw ConfigException(ss.str());
	}
	return lex_.next();
}

// ---------- Error formatting ----------------------------------------------

std::string Parser::locOf(const Token& t) const {
	std::stringstream ss;
	ss << lex_.origin() << ":" << t.line << ": ";
	return ss.str();
}

// ---------- Recovery ------------------------------------------------------

void Parser::skipToEndOfDirective() {
	// Recovery for an unknown directive. The directive can be in two shapes:
	//
	//   leaf:  name args ... ;
	//   block: name args ... { ... }
	//
	// We don't know yet which one this is — we just saw the name. So we walk
	// tokens tracking brace depth:
	//   - At depth 0, a ';' ends a leaf directive: consume and return.
	//   - At depth 0, a '}' belongs to the *enclosing* block, not us: leave
	//     it in place and return.
	//   - A '{' opens a body; we increment depth.
	//   - At depth > 0, a '}' closes a nested body; decrement depth. When we
	//     close back to 0 we are done with the block-form directive.
	//   - A ';' at depth > 0 is a nested leaf inside our skipped body; just
	//     consume it.
	//
	// This is the standard "balanced-brace skipping" pattern. Getting it
	// wrong gives the kind of cascade error we just hit at default.conf:13.
	int depth = 0;
	while (true) {
		const Token& t = lex_.peek();
		if (t.kind == TOK_EOF) return;

		if (t.kind == TOK_RBRACE) {
			if (depth == 0) return;       // not ours; leave for caller
			lex_.next();
			--depth;
			if (depth == 0) return;       // matched the body's '{'
			continue;
		}
		if (t.kind == TOK_LBRACE) {
			lex_.next();
			++depth;
			continue;
		}
		if (t.kind == TOK_SEMI) {
			lex_.next();
			if (depth == 0) return;       // leaf directive done
			continue;                     // ';' inside skipped body
		}
		lex_.next();                      // any other token: just consume
	}
}

// ---------- Grammar productions -------------------------------------------

void Parser::parseFile(std::vector<ServerConfig>& out) {
	out.clear();
	while (lex_.peek().kind != TOK_EOF) {
		const Token& head = lex_.peek();
		if (head.kind == TOK_WORD && head.text == "server") {
			lex_.next();  // consume "server"
			ServerConfig server;
			parseServerBlock(server);
			out.push_back(server);
			continue;
		}
		throw ConfigException(locOf(head)
			+ "expected 'server' block at top level, got '" + head.text + "'");
	}
}

void Parser::parseServerBlock(ServerConfig& server) {
	expect(TOK_LBRACE, "to open server block");
	while (true) {
		const Token& t = lex_.peek();
		if (t.kind == TOK_RBRACE) { lex_.next(); return; }
		if (t.kind == TOK_EOF)
			throw ConfigException(locOf(t) + "unclosed server block, expected '}'");
		parseServerDirective(server);
	}
}

void Parser::parseServerDirective(ServerConfig& server) {
	const Token& name = expect(TOK_WORD, "as directive name");

	if (name.text == "listen") {
		parseListen(server);
		return;
	}

	// nginx-style leniency: warn and skip. The subject explicitly invites
	// extra keys in the config (see IV.3), so an unknown directive isn't
	// fatal — just a heads-up to the operator.
	std::cerr << locOf(name)
			  << "warning: unknown directive '" << name.text
			  << "', ignoring" << std::endl;
	skipToEndOfDirective();
}

// ---------- listen --------------------------------------------------------

void Parser::parseListen(ServerConfig& server) {
	const Token& value = expect(TOK_WORD, "as 'listen' argument");
	expect(TOK_SEMI, "after 'listen' value");

	ListenSpec spec;
	splitHostPort(value, spec.host, spec.port);
	server.listens.push_back(spec);
}

// ---------- Value converters ---------------------------------------------

int Parser::parsePort(const Token& tok) {
	// Reject anything that isn't pure digits before even trying to parse.
	// stringstream alone would accept leading whitespace ("  80"), which we
	// don't want here — the lexer already stripped whitespace.
	if (tok.text.empty())
		throw ConfigException(locOf(tok) + "expected port number, got empty");

	for (std::size_t k = 0; k < tok.text.size(); ++k) {
		char c = tok.text[k];
		if (c < '0' || c > '9')
			throw ConfigException(locOf(tok)
				+ "expected port number, got '" + tok.text + "'");
	}

	long n = 0;
	std::stringstream ss(tok.text);
	ss >> n;
	if (ss.fail() || !ss.eof())   // trailing junk would leave eof false
		throw ConfigException(locOf(tok)
			+ "invalid port number '" + tok.text + "'");
	if (n < 1 || n > 65535)
		throw ConfigException(locOf(tok)
			+ "port out of range (1..65535): " + tok.text);
	return static_cast<int>(n);
}

void Parser::splitHostPort(const Token& tok,
						   std::string& host_out, int& port_out)
{
	const std::string& s = tok.text;

	// Find the LAST ':' so we cope with bracketed IPv6 hosts like
	// "[::1]:8080" — though our project doesn't actually serve IPv6.
	std::string::size_type colon = s.rfind(':');
	if (colon == std::string::npos) {
		// Pure port form: "8080".
		host_out = "0.0.0.0";
		// Build a temporary token carrying just the digits so the error
		// message in parsePort still points at the same source line.
		Token portTok(TOK_WORD, s, tok.line);
		port_out = parsePort(portTok);
		return;
	}

	std::string host = s.substr(0, colon);
	std::string port = s.substr(colon + 1);

	// Strip the brackets of an IPv6 literal, defensively.
	if (host.size() >= 2 && host[0] == '[' && host[host.size() - 1] == ']')
		host = host.substr(1, host.size() - 2);

	if (host.empty())
		throw ConfigException(locOf(tok)
			+ "empty host in 'listen' value '" + s + "'");

	host_out = host;
	Token portTok(TOK_WORD, port, tok.line);
	port_out = parsePort(portTok);
}
