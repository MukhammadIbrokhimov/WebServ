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
	(void)data;
	return 0;
}

void Request::parseRequestLine(const std::string& line) { (void)line; }
void Request::parseHeaderLine(const std::string& line) { (void)line; }
