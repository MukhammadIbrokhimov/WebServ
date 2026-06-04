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