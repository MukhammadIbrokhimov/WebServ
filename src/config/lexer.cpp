#include "../../includes/webserv.hpp"

// the design notes are in includes/lexer.hpp — read that first. this file is
// the boring part: one loop over the source string spitting tokens into a vector.

Token::Token()
	: kind(TOK_EOF), text(""), line(0)
{}

Token::Token(TokenKind k, const std::string& t, int l)
	: kind(k), text(t), line(l)
{}

Lexer::Lexer(const std::string& source, const std::string& origin)
	: tokens_(), origin_(origin), pos_(0)
{
	tokenize(source);
}

const Token& Lexer::peek() const {
	// tokenize() always tacks a TOK_EOF on the end, so tokens_ is never empty
	// and indexing pos_ can't go out of bounds — next() makes sure pos_ never
	// runs past the EOF slot.
	return tokens_[pos_];
}

const Token& Lexer::next() {
	// once we land on the EOF sentinel, stay there. that's what lets the parser
	// loop "until TOK_EOF" without ever bounds-checking pos_ itself.
	if (tokens_[pos_].kind == TOK_EOF)
		return tokens_[pos_];
	return tokens_[pos_++];
}

const std::string& Lexer::origin() const {
	return origin_;
}

// helpers I keep in the .cpp instead of the header so they don't leak out.
// `static` at file scope = internal linkage, i.e. invisible to other .cpp files.
static bool isWhitespace(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool isSpecial(char c) {
	// the chars that end a WORD on their own (whitespace aside). '#' is in here
	// because it kicks off a comment, and a comment ends the current word too.
	return c == '{' || c == '}' || c == ';' || c == '#';
}

void Lexer::tokenize(const std::string& src) {
	int         line = 1;
	std::size_t i    = 0;

	while (i < src.size()) {
		char c = src[i];

		// newline: bump the line counter and keep going.
		if (c == '\n') {
			++line;
			++i;
			continue;
		}

		// any other whitespace: skip it.
		if (isWhitespace(c)) {
			++i;
			continue;
		}

		// comment: skip from '#' to the end of the line. on purpose I do NOT
		// bump `line` here — I let the next iteration hit the '\n' and bump it,
		// so all the line counting stays in one place and I avoid off-by-ones.
		if (c == '#') {
			while (i < src.size() && src[i] != '\n')
				++i;
			continue;
		}

		// the single-char punctuation tokens.
		if (c == '{') { tokens_.push_back(Token(TOK_LBRACE, "{", line)); ++i; continue; }
		if (c == '}') { tokens_.push_back(Token(TOK_RBRACE, "}", line)); ++i; continue; }
		if (c == ';') { tokens_.push_back(Token(TOK_SEMI,   ";", line)); ++i; continue; }

		// anything else is a WORD: keep eating chars until whitespace or a
		// special one. I already know the current `c` is neither, so the word
		// is guaranteed to be at least one char long.
		std::string word;
		while (i < src.size()) {
			char ch = src[i];
			if (isWhitespace(ch) || isSpecial(ch))
				break;
			word += ch;
			++i;
		}
		tokens_.push_back(Token(TOK_WORD, word, line));
	}

	// the sentinel the parser stops on.
	tokens_.push_back(Token(TOK_EOF, "", line));
}
