#include "../../includes/webserv.hpp"

// ---------------------------------------------------------------------------
// Lexer implementation.
//
// Read the header (includes/lexer.hpp) first — it explains the design.
// This file contains nothing surprising: it's a straight loop over the
// source string emitting tokens into a vector.
// ---------------------------------------------------------------------------

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
	// tokenize() always appends a TOK_EOF, so tokens_ is never empty and
	// indexing pos_ is safe as long as pos_ <= tokens_.size() - 1, which
	// next() enforces by sticking at EOF.
	return tokens_[pos_];
}

const Token& Lexer::next() {
	// Sticky EOF: once we're on the sentinel, stay there. The parser can
	// loop until kind == TOK_EOF without bounds-checking.
	if (tokens_[pos_].kind == TOK_EOF)
		return tokens_[pos_];
	return tokens_[pos_++];
}

const std::string& Lexer::origin() const {
	return origin_;
}

// Local helpers, kept out of the header so they don't pollute the public
// namespace. `static` at file scope means "internal linkage" — these
// functions can't be referenced from other translation units.
static bool isWhitespace(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool isSpecial(char c) {
	// The four characters that terminate a WORD (besides whitespace).
	// '#' is in here because it starts a comment, which also ends a word.
	return c == '{' || c == '}' || c == ';' || c == '#';
}

void Lexer::tokenize(const std::string& src) {
	int         line = 1;
	std::size_t i    = 0;

	while (i < src.size()) {
		char c = src[i];

		// Newlines: count them, then move on.
		if (c == '\n') {
			++line;
			++i;
			continue;
		}

		// Other whitespace: just skip.
		if (isWhitespace(c)) {
			++i;
			continue;
		}

		// Comments: '#' to end of line. We do NOT increment `line` here —
		// the next loop iteration will see the '\n' and bump it. Keeping
		// the line-counter logic in one place avoids off-by-ones.
		if (c == '#') {
			while (i < src.size() && src[i] != '\n')
				++i;
			continue;
		}

		// Single-character punctuation tokens.
		if (c == '{') { tokens_.push_back(Token(TOK_LBRACE, "{", line)); ++i; continue; }
		if (c == '}') { tokens_.push_back(Token(TOK_RBRACE, "}", line)); ++i; continue; }
		if (c == ';') { tokens_.push_back(Token(TOK_SEMI,   ";", line)); ++i; continue; }

		// Otherwise: a WORD. Accumulate until we hit whitespace or a
		// special character. By the time we get here we already know `c`
		// itself is not whitespace and not special, so the word will have
		// at least one character.
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

	// Sentinel: the parser keeps reading until it sees this.
	tokens_.push_back(Token(TOK_EOF, "", line));
}
