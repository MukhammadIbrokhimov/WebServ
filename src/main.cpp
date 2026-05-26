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
			const ServerConfig& s = servers[i];
			std::cerr << "server[" << i << "]" << std::endl;
			for (std::size_t j = 0; j < s.listens.size(); ++j) {
				std::cerr << "  listen " << s.listens[j].host
						  << ":" << s.listens[j].port << std::endl;
			}
			if (!s.server_name.empty())
				std::cerr << "  server_name " << s.server_name << std::endl;
			if (!s.root.empty())
				std::cerr << "  root " << s.root << std::endl;
			std::cerr << "  client_max_body_size " << s.client_max_body_size
					  << std::endl;
			for (std::map<int, std::string>::const_iterator it = s.error_pages.begin();
				 it != s.error_pages.end(); ++it)
			{
				std::cerr << "  error_page " << it->first
						  << " -> " << it->second << std::endl;
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