#include "../../includes/http.hpp"

// Incremental HTTP request parser -- the state machine picture and the
// no-exceptions rationale live in http.hpp. This file grows with the
// phases: 2.1 parses the head, 3.1 adds bodies, 5.1 adds chunked.

// Helper: std::tolower is locale-dependent AND undefined behaviour on
// negative chars -- any byte >= 0x80 when char is signed, which network
// input absolutely contains. HTTP is ASCII, so roll it by hand and skip
// <cctype> entirely.
static char asciiLower(char c) {
	if (c >= 'A' && c <= 'Z')
		return static_cast<char>(c + 32);
	return c;
}

static std::string lowerCopy(const std::string& s) {
	std::string out(s);
	for (std::size_t i = 0; i < out.size(); ++i)
		out[i] = asciiLower(out[i]);
	return out;
}

Request::Request()
	: state_(S_REQUEST_LINE)
	, buf_()
	, error_code_(0)
	, header_count_(0)
	, method_(), uri_(), path_(), query_(), version_(), body_()
	, headers_()
{}

void Request::reset() {
	state_ = S_REQUEST_LINE;
	buf_.clear();
	error_code_ = 0;
	header_count_ = 0;
	method_.clear();
	uri_.clear();
	path_.clear();
	query_.clear();
	version_.clear();
	body_.clear();
	headers_.clear();
}

bool Request::isComplete() const { return state_ == S_COMPLETE; }
bool Request::hasError() const   { return state_ == S_ERROR; }
int  Request::getErrorCode() const { return error_code_; }

const std::string& Request::getMethod() const  { return method_; }
const std::string& Request::getUri() const     { return uri_; }
const std::string& Request::getPath() const    { return path_; }
const std::string& Request::getQuery() const   { return query_; }
const std::string& Request::getVersion() const { return version_; }
const std::string& Request::getBody() const    { return body_; }

std::string Request::getHeader(const std::string& name) const {
	std::map<std::string, std::string>::const_iterator it
		= headers_.find(lowerCopy(name));
	if (it == headers_.end())
		return "";
	return it->second;
}

bool Request::hasHeader(const std::string& name) const {
	return headers_.count(lowerCopy(name)) > 0;
}

void Request::setError(int code) {
	error_code_ = code;
	state_ = S_ERROR;
}

// Grows in Task 2 (request line), Task 3 (headers), Task 5 (limits).
std::size_t Request::parseFromBuffer(const std::string& data) {
	// Terminal states eat nothing -- the caller (2.5) either re-feeds the
	// leftovers to a fresh Request or closes the connection.
	if (state_ == S_COMPLETE || state_ == S_ERROR)
		return 0;

	buf_.append(data);

	while (state_ != S_COMPLETE && state_ != S_ERROR) {
		std::size_t nl = buf_.find('\n');
		if (nl == std::string::npos)
			break;   // no complete line yet; keep buffering, stay incomplete

		// Pull the line out. \r\n is canonical, but RFC 7230 3.5 says
		// tolerate a bare \n -- this is what keeps testing with nc sane.
		std::string line = buf_.substr(0, nl);
		buf_.erase(0, nl + 1);
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);

		if (state_ == S_REQUEST_LINE)
			parseRequestLine(line);
		else
			parseHeaderLine(line);
	}

	// Consumed-byte accounting, the subtle part (worked example in the
	// 2.1 design doc):
	//   complete -> bytes after the blank line belong to the NEXT request;
	//     exclude them. They provably arrived in THIS call (a blank line
	//     buffered earlier would have completed earlier), so no underflow.
	//   error -> framing is unrecoverable, connection dies after the error
	//     response; "consumed everything" is the honest answer.
	if (state_ == S_COMPLETE) {
		std::size_t leftover = buf_.size();
		buf_.clear();
		return data.size() - leftover;
	}
	if (state_ == S_ERROR) {
		buf_.clear();
		return data.size();
	}
	return data.size();
}

void Request::parseRequestLine(const std::string& line) {
	// RFC 7230 3.5 again: ignore empty lines before the request line.
	if (line.empty())
		return;

	// METHOD SP URI SP VERSION -- exactly 3 parts, single spaces
	// (RFC 7230 3.1.1). Two finds + a third-space check is the whole
	// grammar; a double space makes part 2 empty and fails below.
	std::size_t sp1 = line.find(' ');
	std::size_t sp2 = (sp1 == std::string::npos)
		? std::string::npos
		: line.find(' ', sp1 + 1);
	if (sp1 == std::string::npos || sp2 == std::string::npos
		|| line.find(' ', sp2 + 1) != std::string::npos) {
		setError(400);
		return;
	}

	std::string method  = line.substr(0, sp1);
	std::string uri     = line.substr(sp1 + 1, sp2 - sp1 - 1);
	std::string version = line.substr(sp2 + 1);

	// Method charset: [A-Z]+ only. Deliberately stricter than the RFC
	// token (which allows digits, '-', etc.) -- the subject only mandates
	// GET/POST/DELETE, and anything not even shaped like a method is a
	// malformed line. PATCH parses fine here; whether we *implement* it
	// is the router's 405/501 call (task 2.4). Parser = syntax,
	// router = policy.
	if (method.empty()) { setError(400); return; }
	for (std::size_t i = 0; i < method.size(); ++i) {
		if (method[i] < 'A' || method[i] > 'Z') { setError(400); return; }
	}

	// origin-form only ("/path?query") -- browsers only send the absolute
	// form to proxies, and we aren't one.
	if (uri.empty() || uri[0] != '/') { setError(400); return; }

	// Version check is two-step on purpose. Grammar first: literal
	// "HTTP/" digit "." digit, case-sensitive. Garbage like "FOO/1.1" is
	// a malformed LINE -> 400. 505 is reserved for a well-formed version
	// we don't speak ("HTTP/2.0") -- mixing those up is exactly the
	// "inaccurate status code" the subject docks.
	if (version.size() != 8
		|| version.compare(0, 5, "HTTP/") != 0
		|| version[5] < '0' || version[5] > '9'
		|| version[6] != '.'
		|| version[7] < '0' || version[7] > '9') {
		setError(400);
		return;
	}
	if (version != "HTTP/1.0" && version != "HTTP/1.1") {
		setError(505);
		return;
	}

	method_  = method;
	uri_     = uri;
	version_ = version;

	// Query splits at the FIRST '?' -- "/a?b?c" keeps "b?c" whole.
	std::size_t q = uri.find('?');
	if (q == std::string::npos) {
		path_  = uri;
		query_ = "";
	} else {
		path_  = uri.substr(0, q);
		query_ = uri.substr(q + 1);
	}

	state_ = S_HEADERS;
}

void Request::parseHeaderLine(const std::string& line) { (void)line; }
