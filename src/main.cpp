#include "../includes/webserv.hpp"

// --- DEV-ONLY diagnostic, remove before submission (Phase 4 cleanup) ------
// if I set LEXER_DUMP=<path>, dump every token of that file to stderr and bail.
// handy for eyeballing what the lexer actually produces while I poke at the parser.
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
	std::string source;
	if (!readFileToString(path, source)) {
		std::cerr << "cannot open " << path << std::endl;
		return 1;
	}

	Lexer lex(source, path);
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
			for (std::size_t k = 0; k < s.locations.size(); ++k) {
				const LocationConfig& l = s.locations[k];
				std::cerr << "  location " << l.path << std::endl;
				if (!l.allowed_methods.empty()) {
					std::cerr << "    allowed_methods";
					for (std::size_t m = 0; m < l.allowed_methods.size(); ++m)
						std::cerr << " " << l.allowed_methods[m];
					std::cerr << std::endl;
				}
				if (!l.root.empty())         std::cerr << "    root " << l.root << std::endl;
				if (!l.index.empty())        std::cerr << "    index " << l.index << std::endl;
				if (l.autoindex)             std::cerr << "    autoindex on" << std::endl;
				if (l.redirect.enabled)
					std::cerr << "    return " << l.redirect.code
							  << " " << l.redirect.target << std::endl;
				if (!l.upload_store.empty()) std::cerr << "    upload_store " << l.upload_store << std::endl;
				for (std::map<std::string, std::string>::const_iterator it = l.cgi.begin();
					 it != l.cgi.end(); ++it)
				{
					std::cerr << "    cgi " << it->first
							  << " -> " << it->second << std::endl;
				}
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
	if (getenv("DEBUG")) {
		Logger::setLevel(Logger::DEBUG);
		LOG_INFO("Debug mode enabled.");
	}
	if (const char* path = getenv("LEXER_DUMP"))
		return dumpTokens(path);
	if (const char* path = getenv("CONFIG_DUMP"))
		return dumpConfig(path);

	if (argc > 2) {
		std::cerr << "usage: ./webserv [config_file]" << std::endl;
		return 1;
	}
	const std::string path = (argc == 2) ? argv[1] : "config/default.conf";

	try {
		Config cfg;
		cfg.load(path);
		Server srv(cfg);
		srv.run();
	} catch (const std::exception& e) {
		std::cerr << "fatal: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}
