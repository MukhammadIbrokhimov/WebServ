#include "../../includes/webserv.hpp"
#include "../../includes/parser.hpp"
#include <sstream>
#include <iostream>   // std::cerr — for the unknown-directive warnings
#include <limits>     // std::numeric_limits — the overflow check in parseSize
#include <fstream>    // std::ifstream — reading an included file

// path helpers for the `include` directive. static so they stay local to this
// file. there's near-identical code in config.cpp — I could pull both into a
// shared util in Phase 4 polish, but right now copying a few lines is cheaper
// than the header churn that would cause.

static std::string readIncludedFile(const std::string& path) {
	std::ifstream in(path.c_str());
	if (!in)
		throw ConfigException("cannot open included file '" + path + "'");
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

// grab the directory part of `path` — everything up to and including the last
// '/'. no slash means "" which I treat as the current directory.
static std::string dirnameOf(const std::string& path) {
	std::string::size_type slash = path.rfind('/');
	if (slash == std::string::npos) return "";
	return path.substr(0, slash + 1);
}

// turn a relative include path into one relative to the including file's dir.
// an absolute path (leading '/') is left alone.
static std::string joinPath(const std::string& dir, const std::string& name) {
	if (!name.empty() && name[0] == '/') return name;
	return dir + name;
}

// the architecture + grammar overview lives in includes/parser.hpp.

// top-level ctor. this one actually owns the cycle guard and the base dir. I
// seed the top file's own origin into the set straight away so that a file
// trying to include itself counts as a cycle too.
Parser::Parser(Lexer& lex)
	: lex_(lex)
	, owned_cycle_guard_()
	, cycle_guard_(&owned_cycle_guard_)
	, base_dir_(dirnameOf(lex.origin()))
{
	cycle_guard_->insert(lex.origin());
}

// sub-Parser ctor, used when I recurse into an included file. it points at the
// parent's cycle guard (shared, so cross-file cycles are visible) and gets its
// own base_dir, the directory of the included file. owned_cycle_guard_ exists
// but goes unused down this path.
Parser::Parser(Lexer& lex, std::set<std::string>& shared_cycle_guard,
			   const std::string& base_dir)
	: lex_(lex)
	, owned_cycle_guard_()
	, cycle_guard_(&shared_cycle_guard)
	, base_dir_(base_dir)
{}

// the little token-stream helpers everything else is built on.

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

// builds the "<file>:<line>: " prefix every error message starts with.

std::string Parser::locOf(const Token& t) const {
	std::stringstream ss;
	ss << lex_.origin() << ":" << t.line << ": ";
	return ss.str();
}

void Parser::skipToEndOfDirective() {
	// how I recover after hitting an unknown directive. all I've seen so far is
	// the name, and it could be either shape:
	//
	//   leaf:  name args ... ;
	//   block: name args ... { ... }
	//
	// so I walk forward tracking brace depth and let the structure tell me which:
	//   - depth 0, ';'  -> it was a leaf, eat the ';' and I'm done.
	//   - depth 0, '}'  -> that '}' closes the block I'm INSIDE, not this
	//                      directive — leave it for the caller and return.
	//   - '{'           -> a body opens, depth++.
	//   - depth >0, '}' -> closes a nested body, depth--. back at 0 means the
	//                      block-form directive is fully skipped.
	//   - depth >0, ';' -> a leaf inside the body I'm skipping, just eat it.
	//
	// it's the standard balanced-brace skip. get it wrong and you get exactly
	// the cascade of bogus errors I hit at default.conf:13.
	int depth = 0;
	while (true) {
		const Token& t = lex_.peek();
		if (t.kind == TOK_EOF) return;

		if (t.kind == TOK_RBRACE) {
			if (depth == 0) return;       // not mine — belongs to the enclosing block
			lex_.next();
			--depth;
			if (depth == 0) return;       // just closed this directive's own body
			continue;
		}
		if (t.kind == TOK_LBRACE) {
			lex_.next();
			++depth;
			continue;
		}
		if (t.kind == TOK_SEMI) {
			lex_.next();
			if (depth == 0) return;       // end of a leaf directive
			continue;                     // ';' buried in the body I'm skipping
		}
		lex_.next();                      // anything else: swallow and move on
	}
}

// the grammar itself — one method per production (recursive descent).

void Parser::parseFile(std::vector<ServerConfig>& out) {
	out.clear();
	while (lex_.peek().kind != TOK_EOF) {
		const Token& head = lex_.peek();
		if (head.kind == TOK_WORD && head.text == "server") {
			lex_.next();  // eat "server"
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

	// be lenient like nginx: warn and skip rather than die. the subject
	// actually says the config can have extra keys (IV.3), so an unknown
	// directive isn't fatal, I just print a heads-up.
	std::cerr << locOf(name)
			  << "warning: unknown directive '" << name.text
			  << "', ignoring" << std::endl;
	skipToEndOfDirective();
}

void Parser::parseListen(ServerConfig& server) {
	const Token& value = expect(TOK_WORD, "as 'listen' argument");
	expect(TOK_SEMI, "after 'listen' value");

	ListenSpec spec;
	splitHostPort(value, spec.host, spec.port);
	server.listens.push_back(spec);
}

// `server_name X;` and `root X;` have the exact same shape: one WORD, one ';',
// store the text. I wrote them out twice instead of making a helper — two
// cases isn't enough to bother, and being explicit makes each easy to grep for.

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

// `client_max_body_size 10M;` — still one WORD, but this time the word needs
// more work (digits plus an optional K/M/G). I keep that logic in parseSize so
// it can be reused.

void Parser::parseClientMaxBodySize(ServerConfig& server) {
	const Token& value = expect(TOK_WORD, "as 'client_max_body_size' argument");
	expect(TOK_SEMI, "after 'client_max_body_size' value");
	server.client_max_body_size = parseSize(value);
}

// `error_page CODE [CODE ...] PATH;` — variable number of args. one or more
// status codes, then a path, then ';'. the trick is to just read WORDs until
// the ';' and figure out what they mean afterwards from the list itself. first
// directive where I do that "collect, then interpret the shape" thing.

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
		// zero args, or just one — with one I can't tell if it's the code or
		// the path, so either way it's not enough.
		const Token& where = args.empty() ? lex_.peek() : args[0];
		throw ConfigException(locOf(where)
			+ "'error_page' needs at least one status code and a path");
	}

	// the last arg is the path, everything before it is a status code.
	const std::string& path = args.back().text;
	for (std::size_t i = 0; i < args.size() - 1; ++i) {
		int code = parseStatusCode(args[i]);
		// same code twice -> last one wins. that's what nginx does and it's the
		// least surprising rule.
		server.error_pages[code] = path;
	}
}

// location blocks.
//   location_block      ::= "location" WORD "{" location_directive* "}"
//   location_directive  ::= one of the ones I support, otherwise warn+skip
//
// by the time parseLocation runs the dispatcher already ate the "location"
// keyword, so I just read the path WORD, let parseLocationBlock handle the
// body, and push the finished thing onto ServerConfig::locations.
//
// parseLocationBlock is basically parseServerBlock again — same loop, just a
// different inner dispatcher.

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

// `allowed_methods` — same collect-until-';' shape as error_page, minus the
// "last one is the path" twist. only the three methods the subject requires
// (IV.1) are accepted; anything else is an error.

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
		// aim the error at the ';', since that's exactly where a method
		// should've shown up.
		throw ConfigException(locOf(lex_.peek())
			+ "'allowed_methods' needs at least one method");
	}
	expect(TOK_SEMI, "after 'allowed_methods' values");
}

// the trivial one-value directives again: index, the location's root override,
// and upload_store. all the same WORD-then-';' shape.

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

// `autoindex on|off` — one WORD, but it has to be exactly "on" or "off".
// anything else is a config error.

void Parser::parseAutoindex(LocationConfig& loc) {
	const Token& value = expect(TOK_WORD, "as 'autoindex' argument (on|off)");
	expect(TOK_SEMI, "after 'autoindex' value");
	if (value.text == "on")       loc.autoindex = true;
	else if (value.text == "off") loc.autoindex = false;
	else throw ConfigException(locOf(value)
		+ "'autoindex' expects 'on' or 'off', got '" + value.text + "'");
}

// `return CODE TARGET` — the per-route redirect the subject asks for. I require
// both a code AND a target; the bare `return 200;` (no redirect, just a body)
// form isn't in scope for this project.

void Parser::parseReturn(LocationConfig& loc) {
	const Token& codeTok   = expect(TOK_WORD, "as 'return' status code");
	const Token& targetTok = expect(TOK_WORD, "as 'return' target");
	expect(TOK_SEMI, "after 'return' target");

	loc.redirect.code    = parseStatusCode(codeTok);
	loc.redirect.target  = targetTok.text;
	loc.redirect.enabled = true;
}

// `cgi EXTENSION INTERPRETER` — goes into a map like { ".py" ->
// "/usr/bin/python3" }. I force the extension to start with '.' so later the
// request handler can just match on the filename suffix.

void Parser::parseCgi(LocationConfig& loc) {
	const Token& ext    = expect(TOK_WORD, "as 'cgi' extension (e.g. '.py')");
	const Token& interp = expect(TOK_WORD, "as 'cgi' interpreter path");
	expect(TOK_SEMI, "after 'cgi' interpreter");

	if (ext.text.empty() || ext.text[0] != '.')
		throw ConfigException(locOf(ext)
			+ "'cgi' extension must start with '.': '" + ext.text + "'");
	loc.cgi[ext.text] = interp.text;
}

// `include FILE;` inside a server block: read FILE, tokenise it, and parse it
// straight into the current ServerConfig as if I'd pasted its contents in place
// of the include line.
//
// things worth remembering here:
//   - relative paths resolve against base_dir_ (the directory of the file doing
//     the include), NOT the process CWD. that's what nginx does and it keeps
//     configs portable.
//   - cycle detection: I keep a set of files currently being parsed and throw
//     if an include lands on one that's already in it. the entry comes back out
//     when the include finishes, so the same file can be included more than once
//     as long as it's not nested inside itself.
//   - only server-scope include for now. file-scope (server blocks spread
//     across files) and location-scope could come later with similar
//     sub.parseFile() / sub.parseLocationDirective() loops.

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

	// run the sub-Parser over the included file's server-level directives until
	// it hits EOF. they're the same class so I can call the private dispatcher
	// on it directly.
	while (subLex.peek().kind != TOK_EOF)
		sub.parseServerDirective(server);

	cycle_guard_->erase(resolved);
}

// the value converters — turning a WORD's text into a real number.

long Parser::parseIntInRange(const Token& tok, long lo, long hi,
							 const char* what)
{
	// check it's pure digits before I even try to parse. a bare stringstream
	// would happily swallow leading spaces ("  80") or junk after a number, and
	// I want a strict yes/no here.
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
	// HTTP status codes are 3 digits. I take the whole 100..599 span —
	// informational, success, redirect, client error, server error.
	return static_cast<int>(parseIntInRange(tok, 100, 599, "HTTP status code"));
}

std::size_t Parser::parseSize(const Token& tok) {
	// nginx lets size values end in K/M/G (either case): "10K" -> 10*1024,
	// "1M" -> 1*1048576, and so on. no suffix just means bytes.
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

	// reuse the int parser on just the digit part. I hand it a fake token built
	// from the prefix but keep tok.line so any error still points at the real
	// source line.
	Token digitTok(TOK_WORD, s.substr(0, digits_end), tok.line);
	long n = parseIntInRange(digitTok, 0, std::numeric_limits<long>::max(),
							 "size");

	// guard against n * multiplier overflowing size_t before I actually multiply.
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

	// search for the LAST ':' so a bracketed IPv6 host like "[::1]:8080" still
	// splits correctly — even though I don't actually serve IPv6 here.
	std::string::size_type colon = s.rfind(':');
	if (colon == std::string::npos) {
		// just a port, "8080".
		host_out = "0.0.0.0";
		// wrap the digits in a temp token so a parsePort error still points at
		// the right source line.
		Token portTok(TOK_WORD, s, tok.line);
		port_out = parsePort(portTok);
		return;
	}

	std::string host = s.substr(0, colon);
	std::string port = s.substr(colon + 1);

	// strip the [] off an IPv6 literal, just in case one shows up.
	if (host.size() >= 2 && host[0] == '[' && host[host.size() - 1] == ']')
		host = host.substr(1, host.size() - 2);

	if (host.empty())
		throw ConfigException(locOf(tok)
			+ "empty host in 'listen' value '" + s + "'");

	host_out = host;
	Token portTok(TOK_WORD, port, tok.line);
	port_out = parsePort(portTok);
}
