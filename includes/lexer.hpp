#pragma once

#include <string>
#include <vector>
#include <cstddef>   // std::size_t

// the whole token vocabulary for my config language — just five kinds, that's
// genuinely all the nginx-style grammar I accept needs. if I ever wanted
// string literals or numbers as real tokens I'd add them here, but I don't.
enum TokenKind {
	TOK_WORD,    // any non-whitespace, non-special run of characters
	TOK_LBRACE,  // "{"
	TOK_RBRACE,  // "}"
	TOK_SEMI,    // ";"
	TOK_EOF      // sticky: lexer keeps returning this once the stream ends
};

// one token out of the lexer.
//   kind: which of the five above.
//   text: the actual characters. only really matters for TOK_WORD — the
//         punctuation tokens carry their symbol too but only so error messages
//         read nicely, the parser never compares against it.
//   line: 1-based line the token starts on. this is the bit that lets me say
//         "syntax error at line 7" instead of just "syntax error".
struct Token {
	TokenKind   kind;
	std::string text;
	int         line;

	Token();
	Token(TokenKind k, const std::string& t, int l);
};

// Lexer: source string in, token stream out.
//
// I went with the eager approach — the ctor chews through the whole input at
// once into a vector<Token>, and the parser just walks that vector with
// peek()/next() like a cursor. config files are a few KB so holding it all in
// memory costs nothing, and it's way less fiddly than a streaming state machine.
//
// one nice property: the lexer can't fail on this grammar. any bytes at all
// produce some token stream. so there's no such thing as a "lexer error" here
// — every actual error is the parser's problem, not mine.
class Lexer {
	public:
		// tokenises `source` right away. `origin` is just a label (usually the
		// file path) that the parser stitches into its error messages.
		Lexer(const std::string& source, const std::string& origin);

		// look at the next token without moving the cursor. hands back TOK_EOF
		// once we're past the end.
		const Token& peek() const;

		// eat the next token and return it. safe to keep calling at EOF — it
		// just keeps handing back the same TOK_EOF sentinel forever.
		const Token& next();

		// where did this stream come from? (for error messages.)
		const std::string& origin() const;

	private:
		std::vector<Token> tokens_;
		std::string        origin_;
		std::size_t        pos_;

		void tokenize(const std::string& source);

		// non-copyable, same trick as Socket — it owns its own cursor/state and
		// copying it is meaningless, so declare and never define.
		Lexer(const Lexer&);
		Lexer& operator=(const Lexer&);
};
