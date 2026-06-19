#pragma once

#include <string>
#include <sstream>

// my little stand-in for std::to_string, which doesn't exist in C++98. just
// shove the value through an ostringstream so I can build log strings inline.
template<typename T>
std::string toString(const T& value) {
	std::ostringstream oss;
	oss << value;
	return oss.str();
}

// ASCII-only lowercase. std::tolower is locale-dependent AND undefined
// behaviour on negative chars -- any byte >= 0x80 when char is signed, which
// network input absolutely contains. HTTP is ASCII, so I roll it by hand and
// skip <cctype> entirely. Both the request parser and the response builder
// need case-insensitive header keys, so it lives here instead of being copied
// into each. inline because this header lands in several translation units --
// a non-inline definition would break the ODR at link.
inline char asciiLower(char c) {
	if (c >= 'A' && c <= 'Z')
		return static_cast<char>(c + 32);
	return c;
}

inline std::string lowerCopy(const std::string& s) {
	std::string out(s);
	for (std::size_t i = 0; i < out.size(); ++i)
		out[i] = asciiLower(out[i]);
	return out;
}