#include "../../includes/webserv.hpp"
#include "../../includes/parser.hpp"
#include <sstream>
#include <iostream>   // std::cerr for unknown-directive warnings
#include <limits>     // std::numeric_limits for overflow check in parseSize
#include <fstream>    // std::ifstream for include directive

// ---------- File and path helpers for `include` --------------------------
// Kept as static functions so they have internal linkage. These exist in
// config.cpp too in a similar form; once Phase 4 polish lands they can be
// merged into a shared utility. For now duplication is cheaper than the
// header churn.

static std::string readIncludedFile(const std::string& path) {
	std::ifstream in(path.c_str());
	if (!in)
		throw ConfigException("cannot open included file '" + path + "'");
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

// Return the directory portion of `path` (everything up to and including the
// last '/'). Returns "" if there is no slash, meaning "current directory".
static std::string dirnameOf(const std::string& path) {
	std::string::size_type slash = path.rfind('/');
	if (slash == std::string::npos) return "";
	return path.substr(0, slash + 1);
}

// Resolve a relative include path against the including file's directory.
// Absolute paths (leading '/') pass through unchanged.
static std::string joinPath(const std::string& dir, const std::string& name) {
	if (!name.empty() && name[0] == '/') return name;
	return dir + name;
}

// ---------------------------------------------------------------------------
// Parser implementation.
// Read includes/parser.hpp for the architecture and grammar overview.
// ---------------------------------------------------------------------------

// Top-level constructor: the cycle guard and base dir are owned here. The
// origin of the top file is seeded into the set so an include of the top
// file itself counts as a cycle.
Parser::Parser(Lexer& lex)
	: lex_(lex)
	, owned_cycle_guard_()
	, cycle_guard_(&owned_cycle_guard_)
	, base_dir_(dirnameOf(lex.origin()))
{
	cycle_guard_->insert(lex.origin());
}

// Sub-Parser constructor: shares the cycle guard with the parent and
// receives its own base_dir (the directory of the included file). The
// owned_cycle_guard_ member is present but unused on this path.
Parser::Parser(Lexer& lex, std::set<std::string>& shared_cycle_guard,
			   const std::string& base_dir)
	: lex_(lex)
	, owned_cycle_guard_()
	, cycle_guard_(&shared_cycle_guard)
	, base_dir_(base_dir)
{}

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

	if (name.text == "listen")               { parseListen(server);             return; }
	if (name.text == "server_name")          { parseServerName(server);         return; }
	if (name.text == "root")                 { parseRoot(server);               return; }
	if (name.text == "client_max_body_size") { parseClientMaxBodySize(server);  return; }
	if (name.text == "error_page")           { parseErrorPage(server);          return; }
	if (name.text == "location")             { parseLocation(server);           return; }
	if (name.text == "include")              { parseInclude(server);            return; }

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

// ---------- Pattern 1: trivial single-value directives -------------------
// `server_name X;` and `root X;` are identical in shape: one WORD, one SEMI,
// assign the text. Inlined twice rather than DRYed because two examples
// don't justify a helper — and the explicitness makes each one easy to find.

void Parser::parseServerName(ServerConfig& server) {
	const Token& value = expect(TOK_WORD, "as 'server_name' argument");
	expect(TOK_SEMI, "after 'server_name' value");
	server.server_name = value.text;
}

void Parser::parseRoot(ServerConfig& server) {
	const Token& value = expect(TOK_WORD, "as 'root' argument");
	expect(TOK_SEMI, "after 'root' value");
	server.root = value.text;
}

// ---------- Pattern 2: parsed-value directive ----------------------------
// `client_max_body_size 10M;` — one WORD, but the WORD's text needs further
// parsing (digits + optional K/M/G suffix). The parsing logic lives in
// parseSize so it's reusable.

void Parser::parseClientMaxBodySize(ServerConfig& server) {
	const Token& value = expect(TOK_WORD, "as 'client_max_body_size' argument");
	expect(TOK_SEMI, "after 'client_max_body_size' value");
	server.client_max_body_size = parseSize(value);
}

// ---------- Pattern 3: variable-arity directive --------------------------
// `error_page CODE [CODE ...] PATH;`
//
// Grammar: at least one status code, then a path, terminated by ';'. The
// idiom is "read WORDs until SEMI, then post-process the collected list."
// This is the parser's first taste of "look at the structure of the
// collected args to decide meaning."

void Parser::parseErrorPage(ServerConfig& server) {
	std::vector<Token> args;
	while (lex_.peek().kind != TOK_SEMI) {
		const Token& t = lex_.peek();
		if (t.kind != TOK_WORD)
			throw ConfigException(locOf(t)
				+ "unexpected token in 'error_page', got '" + t.text + "'");
		args.push_back(lex_.next());
	}
	expect(TOK_SEMI, "after 'error_page' values");

	if (args.size() < 2) {
		// Either zero args, or only one (no way to tell which is code vs path).
		const Token& where = args.empty() ? lex_.peek() : args[0];
		throw ConfigException(locOf(where)
			+ "'error_page' needs at least one status code and a path");
	}

	// Last arg is the path; every arg before it is a status code.
	const std::string& path = args.back().text;
	for (std::size_t i = 0; i < args.size() - 1; ++i) {
		int code = parseStatusCode(args[i]);
		// Last-write wins if the same code appears twice. That matches
		// nginx's behaviour and is the least-surprising rule.
		server.error_pages[code] = path;
	}
}

// =========================================================================
// Location blocks
// =========================================================================
//
// Grammar:
//   location_block      ::= "location" WORD "{" location_directive* "}"
//   location_directive  ::= one of the supported directives, else warn+skip
//
// parseLocation is called after the dispatcher has already consumed the
// "location" keyword. It owns the path WORD, hands off to
// parseLocationBlock for the body, and pushes the result onto
// ServerConfig::locations.
//
// parseLocationBlock is the structural twin of parseServerBlock — same
// shape, different inner dispatcher.

void Parser::parseLocation(ServerConfig& server) {
	const Token& pathTok = expect(TOK_WORD, "as 'location' path");
	LocationConfig loc;
	loc.path = pathTok.text;
	parseLocationBlock(loc);
	server.locations.push_back(loc);
}

void Parser::parseLocationBlock(LocationConfig& loc) {
	expect(TOK_LBRACE, "to open location block");
	while (true) {
		const Token& t = lex_.peek();
		if (t.kind == TOK_RBRACE) { lex_.next(); return; }
		if (t.kind == TOK_EOF)
			throw ConfigException(locOf(t)
				+ "unclosed location block, expected '}'");
		parseLocationDirective(loc);
	}
}

void Parser::parseLocationDirective(LocationConfig& loc) {
	const Token& name = expect(TOK_WORD, "as directive name");

	if (name.text == "allowed_methods") { parseAllowedMethods(loc); return; }
	if (name.text == "index")           { parseIndex(loc);          return; }
	if (name.text == "autoindex")       { parseAutoindex(loc);      return; }
	if (name.text == "return")          { parseReturn(loc);         return; }
	if (name.text == "root")            { parseLocRoot(loc);        return; }
	if (name.text == "upload_store")    { parseUploadStore(loc);    return; }
	if (name.text == "cgi")             { parseCgi(loc);            return; }

	std::cerr << locOf(name)
			  << "warning: unknown location directive '" << name.text
			  << "', ignoring" << std::endl;
	skipToEndOfDirective();
}

// ---------- allowed_methods (variable-arity, validated set) ---------------
// Identical shape to error_page but without the "last is path" rule. The
// validated set is the three methods the subject mandates (IV.1).

void Parser::parseAllowedMethods(LocationConfig& loc) {
	while (lex_.peek().kind != TOK_SEMI) {
		const Token& t = lex_.peek();
		if (t.kind != TOK_WORD)
			throw ConfigException(locOf(t)
				+ "unexpected token in 'allowed_methods', got '" + t.text + "'");
		const std::string& m = t.text;
		if (m != "GET" && m != "POST" && m != "DELETE")
			throw ConfigException(locOf(t)
				+ "unsupported HTTP method '" + m + "' (allowed: GET, POST, DELETE)");
		loc.allowed_methods.push_back(m);
		lex_.next();
	}
	if (loc.allowed_methods.empty()) {
		// Point the error at the ';' — that's where a method should have been.
		throw ConfigException(locOf(lex_.peek())
			+ "'allowed_methods' needs at least one method");
	}
	expect(TOK_SEMI, "after 'allowed_methods' values");
}

// ---------- Trivial single-value: index, root override, upload_store -----

void Parser::parseIndex(LocationConfig& loc) {
	const Token& value = expect(TOK_WORD, "as 'index' argument");
	expect(TOK_SEMI, "after 'index' value");
	loc.index = value.text;
}

void Parser::parseLocRoot(LocationConfig& loc) {
	const Token& value = expect(TOK_WORD, "as 'root' argument");
	expect(TOK_SEMI, "after 'root' value");
	loc.root = value.text;
}

void Parser::parseUploadStore(LocationConfig& loc) {
	const Token& value = expect(TOK_WORD, "as 'upload_store' argument");
	expect(TOK_SEMI, "after 'upload_store' value");
	loc.upload_store = value.text;
}

// ---------- Enum-style: autoindex on|off ---------------------------------
// One WORD constrained to exactly two valid strings. Reject anything else.

void Parser::parseAutoindex(LocationConfig& loc) {
	const Token& value = expect(TOK_WORD, "as 'autoindex' argument (on|off)");
	expect(TOK_SEMI, "after 'autoindex' value");
	if (value.text == "on")       loc.autoindex = true;
	else if (value.text == "off") loc.autoindex = false;
	else throw ConfigException(locOf(value)
		+ "'autoindex' expects 'on' or 'off', got '" + value.text + "'");
}

// ---------- Structured: return CODE TARGET -------------------------------
// The subject mandates HTTP redirection per route. We require both a status
// code and a target — `return 200;` (no body) is not in scope.

void Parser::parseReturn(LocationConfig& loc) {
	const Token& codeTok   = expect(TOK_WORD, "as 'return' status code");
	const Token& targetTok = expect(TOK_WORD, "as 'return' target");
	expect(TOK_SEMI, "after 'return' target");

	loc.redirect.code    = parseStatusCode(codeTok);
	loc.redirect.target  = targetTok.text;
	loc.redirect.enabled = true;
}

// ---------- Map entry: cgi EXTENSION INTERPRETER -------------------------
// Stored as a map { ".py" -> "/usr/bin/python3", ... }. The extension must
// start with '.' so the request handler can match by filename suffix.

void Parser::parseCgi(LocationConfig& loc) {
	const Token& ext    = expect(TOK_WORD, "as 'cgi' extension (e.g. '.py')");
	const Token& interp = expect(TOK_WORD, "as 'cgi' interpreter path");
	expect(TOK_SEMI, "after 'cgi' interpreter");

	if (ext.text.empty() || ext.text[0] != '.')
		throw ConfigException(locOf(ext)
			+ "'cgi' extension must start with '.': '" + ext.text + "'");
	loc.cgi[ext.text] = interp.text;
}

// =========================================================================
// include
// =========================================================================
//
// `include FILE;` at server scope: read FILE, tokenise it, then parse it
// directly into the current ServerConfig as if its contents had appeared
// in place of the include line.
//
// Implementation notes:
//   - Path resolution: relative paths are joined with base_dir_ (the
//     directory of the file that contains this `include` line), NOT the
//     CWD of the process. That matches nginx behaviour and makes configs
//     portable.
//   - Cycle detection: we maintain a set of files currently being parsed.
//     If an include resolves to a file already in the set, we throw. The
//     entry is removed once the include finishes, so a file may be
//     included multiple times in disjoint subtrees.
//   - Scope: only server-scope include is supported. File-scope (mixing
//     `server { ... }` blocks across files) and location-scope can be
//     added later by introducing analogous sub.parseFile() / sub.parseLocationDirective() loops.

void Parser::parseInclude(ServerConfig& server) {
	const Token& pathTok = expect(TOK_WORD, "as 'include' argument");
	expect(TOK_SEMI, "after 'include' value");

	const std::string resolved = joinPath(base_dir_, pathTok.text);

	if (cycle_guard_->count(resolved))
		throw ConfigException(locOf(pathTok)
			+ "include cycle: '" + resolved + "' is already being included");
	cycle_guard_->insert(resolved);

	std::string content = readIncludedFile(resolved);
	Lexer       subLex(content, resolved);
	Parser      sub(subLex, *cycle_guard_, dirnameOf(resolved));

	// Drive the sub-Parser through server-level directives until the
	// included file is exhausted. Same-class private access lets us call
	// the dispatcher directly.
	while (subLex.peek().kind != TOK_EOF)
		sub.parseServerDirective(server);

	cycle_guard_->erase(resolved);
}

// ---------- Value converters ---------------------------------------------

long Parser::parseIntInRange(const Token& tok, long lo, long hi,
							 const char* what)
{
	// Reject anything that isn't pure digits before even trying to parse.
	// stringstream alone would accept leading whitespace ("  80") or a
	// trailing junk after a parsed prefix; we want a strict contract.
	if (tok.text.empty())
		throw ConfigException(locOf(tok)
			+ "expected " + what + ", got empty");

	for (std::size_t k = 0; k < tok.text.size(); ++k) {
		char c = tok.text[k];
		if (c < '0' || c > '9')
			throw ConfigException(locOf(tok)
				+ "expected " + what + ", got '" + tok.text + "'");
	}

	long n = 0;
	std::stringstream ss(tok.text);
	ss >> n;
	if (ss.fail() || !ss.eof())
		throw ConfigException(locOf(tok)
			+ "invalid " + what + ": '" + tok.text + "'");
	if (n < lo || n > hi) {
		std::stringstream msg;
		msg << locOf(tok) << what << " out of range ("
			<< lo << ".." << hi << "): " << tok.text;
		throw ConfigException(msg.str());
	}
	return n;
}

int Parser::parsePort(const Token& tok) {
	return static_cast<int>(parseIntInRange(tok, 1, 65535, "port"));
}

int Parser::parseStatusCode(const Token& tok) {
	// HTTP status codes are 3-digit numbers. We accept the full 100..599
	// range — informational, success, redirect, client error, server error.
	return static_cast<int>(parseIntInRange(tok, 100, 599, "HTTP status code"));
}

std::size_t Parser::parseSize(const Token& tok) {
	// nginx accepts a trailing K/M/G suffix on size values, optionally
	// uppercase. "10K" -> 10 * 1024, "1M" -> 1 * 1048576, etc.
	// A bare number with no suffix means bytes.
	const std::string& s = tok.text;
	if (s.empty())
		throw ConfigException(locOf(tok) + "empty size value");

	std::size_t       digits_end = s.size();
	std::size_t       multiplier = 1;

	char last = s[s.size() - 1];
	if (last == 'k' || last == 'K') {
		multiplier = 1024UL;
		--digits_end;
	} else if (last == 'm' || last == 'M') {
		multiplier = 1024UL * 1024UL;
		--digits_end;
	} else if (last == 'g' || last == 'G') {
		multiplier = 1024UL * 1024UL * 1024UL;
		--digits_end;
	}

	if (digits_end == 0)
		throw ConfigException(locOf(tok)
			+ "size has no number before suffix: '" + s + "'");

	// Reuse the integer parser on just the digit prefix. We pass a synthetic
	// token so the error line still points at the original source.
	Token digitTok(TOK_WORD, s.substr(0, digits_end), tok.line);
	long n = parseIntInRange(digitTok, 0, std::numeric_limits<long>::max(),
							 "size");

	// Overflow check: would n * multiplier exceed size_t?
	std::size_t un = static_cast<std::size_t>(n);
	if (multiplier > 1
		&& un > std::numeric_limits<std::size_t>::max() / multiplier)
	{
		throw ConfigException(locOf(tok) + "size overflow: '" + s + "'");
	}
	return un * multiplier;
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
