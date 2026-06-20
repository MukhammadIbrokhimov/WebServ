#include "../../includes/handler.hpp"
#include "../../includes/string_utils.hpp"
#include "../../includes/utils.hpp"
#include <sys/stat.h>
#include <unistd.h>

// Static file serving for task 2.3. The why-it-takes-a-resolved-location and
// why-it's-a-separate-header reasoning lives in handler.hpp. This file is the
// filesystem half: normalize the path, classify it, hand back a Response.
//
// Subject note: every syscall here (stat, access) is on the authorized list,
// and regular disk files are explicitly exempt from the poll() rule -- so
// reading one straight off the disk is allowed. I never read errno to decide
// 403 vs 404; access()/stat() return values carry everything I need, which
// also dodges the "no errno after read/write" landmine entirely.

// A small built-in error page. Custom error_page files are task 3.5; until
// then a 404/403 still needs a body a browser can show.
static Response errorResponse(int code) {
	Response r;
	r.setStatusCode(code);
	std::string phrase = Response::reasonPhrase(code);
	std::string body = "<html><head><title>" + toString(code) + " " + phrase
	                 + "</title></head><body><h1>" + toString(code) + " "
	                 + phrase + "</h1></body></html>\n";
	r.setHeader("Content-Type", "text/html");
	r.setBody(body);
	return r;
}

Response StaticFileHandler::handleGet(const Request& req,
                                      const std::string& root,
                                      const LocationConfig& loc) {
	(void)loc;
	// root has no trailing slash by convention; getPath() always starts "/".
	std::string base = root;
	if (!base.empty() && base[base.size() - 1] == '/')
		base.erase(base.size() - 1);
	std::string fsPath = base + req.getPath();

	if (::access(fsPath.c_str(), F_OK) != 0)
		return errorResponse(404);   // doesn't exist -- return value, not errno

	return errorResponse(404);       // placeholder until Task 2 adds serving
}
