#pragma once

#include <string>
#include <fstream>
#include <sstream>

// Slurp a whole file into `out`. Returns false if the path can't be opened,
// true otherwise. Deliberately error-policy-free: the config loader turns a
// false into a ConfigException, the token-dump tool prints and bails, and the
// 2.3 static handler will turn it into a 404/403 -- each caller owns its own
// failure story, so this stays a plain bool and never throws or logs. inline
// because the header lands in several translation units.
inline bool readFileToString(const std::string& path, std::string& out) {
	std::ifstream in(path.c_str());
	if (!in)
		return false;
	std::stringstream ss;
	ss << in.rdbuf();
	out = ss.str();
	return true;
}
