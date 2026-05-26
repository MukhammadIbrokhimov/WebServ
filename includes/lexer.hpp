#pragma once

#include <string>
#include <vector>
#include <cstddef>   // std::size_t

// ---------------------------------------------------------------------------
// Token kinds for the config language.
//
// The vocabulary is intentionally minimal: five kinds cover the entire
// nginx-style grammar we accept. Anything richer (string literals, numbers,
// regex flags) would add new kinds here.
// ---------------------------------------------------------------------------
enum TokenKind {
	TOK_WORD,    // any non-whitespace, non-special run of characters
	TOK_LBRACE,  // "{"
	TOK_RBRACE,  // "}"
	TOK_SEMI,    // ";"
	TOK_EOF      // sticky: lexer keeps returning this once the stream ends
};

// A single token emitted by the lexer.
//   - kind: which of the five categories above.
//   - text: the literal characters from the source. Only meaningful for
//           TOK_WORD; the punctuation tokens carry their symbol for nicer
//           error messages but the parser doesn't compare on it.
//   - line: 1-based line number where the token starts. This is what turns
//           "syntax error" into "syntax error at line 7".
struct Token {
	TokenKind   kind;
	std::string text;
	int         line;

	Token();
	Token(TokenKind k, const std::string& t, int l);
};

// ---------------------------------------------------------------------------
// Lexer: turns a std::string of source into a token stream.
//
// Eager design: the constructor tokenises the entire input upfront into a
// std::vector<Token>. The parser walks that vector via peek()/next() like a
// cursor. Config files are tiny (kilobytes) so the memory cost is negligible
// and the code is far simpler than a streaming state machine.
//
// The lexer is "infallible" on this grammar: every byte sequence produces
// some valid token stream. There is no such thing as a lexer error in this
// project — semantic errors are entirely the parser's responsibility.
// ---------------------------------------------------------------------------
class Lexer {
	public:
		// Tokenises `source` immediately. `origin` is a label (typically a
		// file path) used by the parser when building error messages.
		Lexer(const std::string& source, const std::string& origin);

		// One-token lookahead. Returns TOK_EOF after the stream ends.
		// Never advances the cursor.
		const Token& peek() const;

		// Consume and return the next token. Idempotent at EOF: after the
		// last real token, every call keeps returning the TOK_EOF sentinel.
		const Token& next();

		// For error messages: where did this token stream come from?
		const std::string& origin() const;

	private:
		std::vector<Token> tokens_;
		std::string        origin_;
		std::size_t        pos_;

		void tokenize(const std::string& source);

		// Non-copyable: like Socket, a Lexer wraps unique state and copying
		// it makes no sense. Declared private and not implemented.
		Lexer(const Lexer&);
		Lexer& operator=(const Lexer&);
};
