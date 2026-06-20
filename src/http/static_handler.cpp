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

// Content-Type from the file extension: the substring after the last '.' that
// comes AFTER the last '/'. That guard means a dot in a directory name
// ("/v1.2/page") and a dotfile with no real extension both fall through to the
// safe default rather than being mis-typed. Matched case-insensitively because
// "INDEX.HTML" is still HTML. Only the handful of types a static site actually
// ships; anything else is octet-stream and the browser downloads it.
static std::string contentTypeFor(const std::string& path) {
	std::size_t slash = path.find_last_of('/');
	std::size_t dot = path.find_last_of('.');
	if (dot == std::string::npos
	    || (slash != std::string::npos && dot < slash)
	    || dot + 1 == path.size())
		return "application/octet-stream";
	std::string ext = lowerCopy(path.substr(dot + 1));
	if (ext == "html" || ext == "htm") return "text/html";
	if (ext == "css")  return "text/css";
	if (ext == "js")   return "text/javascript";
	if (ext == "txt")  return "text/plain";
	if (ext == "json") return "application/json";
	if (ext == "png")  return "image/png";
	if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
	if (ext == "gif")  return "image/gif";
	if (ext == "ico")  return "image/x-icon";
	if (ext == "svg")  return "image/svg+xml";
	if (ext == "pdf")  return "application/pdf";
	return "application/octet-stream";
}

// Read a known-regular file into a 200. R_OK first so an unreadable file is a
// clean 403 (DoD) rather than an empty 200; the read can still fail on a race
// after the check, and readFileToString reports that as a plain bool -- no
// errno -- which we also turn into 403.
static Response serveFile(const std::string& fsPath) {
	if (::access(fsPath.c_str(), R_OK) != 0)
		return errorResponse(403);
	std::string body;
	if (!readFileToString(fsPath, body))
		return errorResponse(403);
	Response r;
	r.setStatusCode(200);
	r.setHeader("Content-Type", contentTypeFor(fsPath));
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

	struct stat st;
	if (::stat(fsPath.c_str(), &st) != 0)
		return errorResponse(404);   // vanished between access and stat

	if (S_ISREG(st.st_mode))
		return serveFile(fsPath);

	return errorResponse(403);       // dir/FIFO/etc -- Task 4 handles dirs
}
