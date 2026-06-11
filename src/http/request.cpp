#include "../../includes/http.hpp"

// Incremental HTTP request parser -- the state machine picture and the
// no-exceptions rationale live in http.hpp. This file grows with the
// phases: 2.1 parses the head, 3.1 adds bodies, 5.1 adds chunked.

// Hard caps. Enforced against the RAW buffer on every call -- before line
// extraction -- so a client streaming bytes with no newline still trips
// them. Without that an attacker sends gigabytes of 'A' and the subject's
// no-crash-even-OOM rule takes the grade to 0. Numbers ~ nginx defaults
// (large_client_header_buffers). 414 on the request line is nginx's
// convention too: an over-long request line is an over-long URI in every
// realistic case. The header caps answer 431 (RFC 6585). Bonus: these
// caps are also what keeps the per-line front-erase in parseFromBuffer
// cheap -- don't raise them casually.
static const std::size_t MAX_REQUEST_LINE = 8 * 1024;
static const std::size_t MAX_HEADER_LINE  = 8 * 1024;
static const std::size_t MAX_HEADER_COUNT = 100;

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

// OWS = "optional whitespace" in the RFC 7230 grammar: SP and HTAB only.
// Not isspace() -- that would also eat \v\f\r, which the grammar doesn't.
static std::string trimOws(const std::string& s) {
	std::size_t b = 0;
	std::size_t e = s.size();
	while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
	return s.substr(b, e - b);
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
		// Cap depends on what we're in the middle of reading.
		const std::size_t cap = (state_ == S_REQUEST_LINE)
			? MAX_REQUEST_LINE : MAX_HEADER_LINE;
		const int overflow_code = (state_ == S_REQUEST_LINE) ? 414 : 431;

		std::size_t nl = buf_.find('\n');
		if (nl == std::string::npos) {
			// No complete line. If the partial already busts the cap,
			// fail NOW -- waiting for a newline that may never come is
			// the unbounded-memory hole.
			if (buf_.size() > cap)
				setError(overflow_code);
			break;
		}
		if (nl > cap) {   // line terminated, but over-long
			setError(overflow_code);
			break;
		}

		// Pull the line out. \r\n is canonical, but RFC 7230 3.5 says
		// tolerate a bare \n -- this is what keeps testing with nc sane.
		std::string line = buf_.substr(0, nl);
		buf_.erase(0, nl + 1);
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);

		// After stripping the terminator, a CR anywhere left in the line
		// is a bare CR -- RFC 7230 3.5 says MUST reject (or replace; we
		// reject). Catching it here covers request line AND headers in
		// one place. Found by Task 2's code review.
		if (line.find('\r') != std::string::npos) {
			setError(400);
			break;
		}

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

	// No control bytes in the URI. The nasty one is NUL: path_.c_str()
	// eventually reaches open()/stat() in 2.3, and an embedded NUL
	// truncates the C string there -- the %00.jpg bypass class, no
	// percent-decoding needed. Cast to unsigned char so bytes >= 0x80
	// (raw UTF-8) stay legal; only actual CTLs die here.
	for (std::size_t i = 0; i < uri.size(); ++i) {
		unsigned char uc = static_cast<unsigned char>(uri[i]);
		if (uc < 0x20 || uc == 0x7F) { setError(400); return; }
	}

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

void Request::parseHeaderLine(const std::string& line) {
	if (line.empty()) {
		// Blank line = end of head. HTTP/1.1 made Host mandatory
		// (RFC 7230 5.4 -- it's what enables name-based virtual hosting);
		// 1.0 predates it, so no Host check there.
		if (version_ == "HTTP/1.1" && headers_.count("host") == 0) {
			setError(400);
			return;
		}
		state_ = S_COMPLETE;   // 3.1 will branch to S_BODY here instead
		return;
	}

	++header_count_;   // counts lines; cap enforced here
	if (header_count_ > MAX_HEADER_COUNT) {
		setError(431);
		return;
	}

	// Split at the FIRST colon -- values keep theirs ("Host: x:8080").
	std::size_t colon = line.find(':');
	if (colon == std::string::npos || colon == 0) {
		setError(400);   // no colon at all, or empty name
		return;
	}

	std::string name = line.substr(0, colon);

	// No SP/TAB or CTL bytes anywhere in the name. The interesting case is
	// trailing ("Host : x"): RFC 7230 3.2.4 explicitly demands 400 because
	// two hops disagreeing on where a header name ends is a real
	// request-smuggling vector. The CTL check (uc < 0x20 || uc == 0x7F) is
	// the same guard the URI got -- header names become CGI env vars in 3.3
	// (HTTP_FOO), where a NUL truncates the env name; rejecting here is one
	// loop, debugging it in 3.3 is an afternoon. Bytes >= 0x80 stay legal.
	for (std::size_t i = 0; i < name.size(); ++i) {
		unsigned char uc = static_cast<unsigned char>(name[i]);
		if (uc < 0x20 || uc == 0x7F) { setError(400); return; }
		if (name[i] == ' ' || name[i] == '\t') { setError(400); return; }
	}

	std::string key   = lowerCopy(name);  // normalize at the boundary,
	                                      // compare exactly inside
	std::string value = trimOws(line.substr(colon + 1));

	// No CTL bytes in the value either, except HTAB which RFC 7230
	// field-content allows inside a value (OWS at the edges was already
	// trimmed above). Same CGI env-var rationale; Host's value feeds
	// vhost matching in 2.4/2.5, so garbage there is a live routing bug.
	for (std::size_t i = 0; i < value.size(); ++i) {
		unsigned char uc = static_cast<unsigned char>(value[i]);
		if ((uc < 0x20 && uc != '\t') || uc == 0x7F) { setError(400); return; }
	}

	// Empty Host is rejected -- nginx (the subject's reference) does the
	// same, and 2.5's vhost matcher shouldn't need a fallback for an
	// accidental zero-length host.
	if (key == "host" && value.empty()) { setError(400); return; }

	std::map<std::string, std::string>::iterator it = headers_.find(key);
	if (it == headers_.end()) {
		headers_[key] = value;
		return;
	}

	// Duplicate header. Host is special: a second Host MUST be rejected
	// (RFC 7230 5.4) -- comma-joining "Host: a" + "Host: b" into "a, b"
	// is exactly the smuggling trick. Everything else combines per 3.2.2.
	// Forward note for 3.1: duplicate Content-Length gets the same
	// treatment there ("5, 7" must fail the integer parse -> 400).
	if (key == "host") {
		setError(400);
		return;
	}
	it->second += ", " + value;
}
