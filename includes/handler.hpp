#pragma once

#include <string>
#include "http.hpp"
#include "config.hpp"

// The static-file unit for task 2.3. Request decodes bytes, Response builds
// bytes; this is the piece in between that turns "GET /style.css" into a file
// off the disk. I keep it deliberately dumb about routing: which location a
// URI belongs to is task 2.4's job, so handleGet() is HANDED the already-picked
// location plus the effective root and just trusts them. That keeps it a pure
// filesystem unit -- no sockets, no Config walking -- and unit-testable against
// a temp dir, the same way Request/Response are testable on their own.
//
// It lives in its own header on purpose: http.hpp stays the pure wire-type
// header, and only the files that actually serve files pull in config.hpp.
class StaticFileHandler {
	public:
		// `root` is the effective root the caller resolved (the location's
		// root, or the server root when the location leaves it empty -- 2.4/2.5
		// owns that fallback). `loc` supplies the index file (and, in 5.3, the
		// autoindex flag). Returns a Response ready to serialize -- 200 with the
		// file, or 403/404 with a small built-in error page.
		static Response handleGet(const Request& req,
		                          const std::string& root,
		                          const LocationConfig& loc);
};
