#include "../includes/webserv.hpp"
#include <fstream>
#include <sstream>

// --- DEV-ONLY diagnostic, remove before submission (Phase 4 cleanup) ------
// If the env var LEXER_DUMP=<path> is set, tokenise that file and print
// every token to stderr, then exit. Useful while iterating on the parser.
static const char* kindName(TokenKind k) {
	switch (k) {
		case TOK_WORD:   return "WORD  ";
		case TOK_LBRACE: return "LBRACE";
		case TOK_RBRACE: return "RBRACE";
		case TOK_SEMI:   return "SEMI  ";
		case TOK_EOF:    return "EOF   ";
	}
	return "??????";
}

static int dumpTokens(const std::string& path) {
	std::ifstream in(path.c_str());
	if (!in) {
		std::cerr << "cannot open " << path << std::endl;
		return 1;
	}
	std::stringstream ss;
	ss << in.rdbuf();

	Lexer lex(ss.str(), path);
	while (true) {
		const Token& t = lex.next();
		std::cerr << "L" << t.line << "  " << kindName(t.kind)
				  << "  [" << t.text << "]" << std::endl;
		if (t.kind == TOK_EOF) break;
	}
	return 0;
}
static int dumpConfig(const std::string& path) {
	try {
		Config cfg;
		cfg.load(path);
		const std::vector<ServerConfig>& servers = cfg.servers();
		std::cerr << "parsed " << servers.size() << " server block(s)" << std::endl;
		for (std::size_t i = 0; i < servers.size(); ++i) {
			std::cerr << "server[" << i << "]" << std::endl;
			for (std::size_t j = 0; j < servers[i].listens.size(); ++j) {
				std::cerr << "  listen " << servers[i].listens[j].host
						  << ":" << servers[i].listens[j].port << std::endl;
			}
		}
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "config error: " << e.what() << std::endl;
		return 1;
	}
}
// --- end DEV-ONLY ---------------------------------------------------------

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;

	if (getenv("DEBUG")) {
		Logger::setLevel(Logger::DEBUG);
		LOG_INFO("Debug mode enabled.");
	}
	if (const char* path = getenv("LEXER_DUMP"))
		return dumpTokens(path);
	if (const char* path = getenv("CONFIG_DUMP"))
		return dumpConfig(path);

	std::cout << "WebServ starting..." << std::endl;
	Socket server_socket(8080);
	server_socket.startListening();
	Server web_server(server_socket);
	web_server.run();
	return 0;
}