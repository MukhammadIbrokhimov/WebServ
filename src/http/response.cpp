#include "../../includes/http.hpp"
#include "../../includes/string_utils.hpp"
#include <sstream>
#include <iomanip>

// HTTP response builder -- the mirror of request.cpp. The why-it's-I/O-free
// and why-a-fresh-one-is-200 reasoning lives in http.hpp. This file just
// turns fields into the wire format.

// asciiLower / lowerCopy now live in string_utils.hpp -- request.cpp wanted
// the exact same case-insensitive keys, so the two copies became one.

// Turn the lowercased storage key back into the on-the-wire spelling:
// uppercase the first letter and every letter after a '-'. Gives
// "content-type" -> "Content-Type", "date" -> "Date". Casing is cosmetic
// (clients compare case-insensitively), but emitting the canonical form is
// what a real server does and keeps the output legible at a 42 defense.
static std::string canonicalHeaderName(const std::string& lower) {
	std::string out(lower);
	bool atStart = true;
	for (std::size_t i = 0; i < out.size(); ++i) {
		if (atStart && out[i] >= 'a' && out[i] <= 'z')
			out[i] = static_cast<char>(out[i] - 32);
		atStart = (out[i] == '-');
	}
	return out;
}

// The Request parser is paranoid about CTLs in header names/values because
// they become CGI env vars in 3.3; the builder owes the wire the same paranoia
// in reverse. A CR or LF that slips into a header -- or the status-line reason
// -- is HTTP response splitting: a CGI script (3.3) or error page could inject
// its own headers or a forged body. So I refuse to EMIT what the parser refuses
// to ACCEPT, and at the same boundary.

// Header name grammar (RFC 7230 3.2.6 token, kept strict): non-empty, no CTLs,
// no SP/HTAB, no ':' (that's the field separator). Anything else is a name
// that would break framing the moment it's written out.
static bool validHeaderName(const std::string& name) {
	if (name.empty())
		return false;
	for (std::size_t i = 0; i < name.size(); ++i) {
		unsigned char uc = static_cast<unsigned char>(name[i]);
		if (uc < 0x20 || uc == 0x7F) return false;
		if (name[i] == ' ' || name[i] == '\t' || name[i] == ':') return false;
	}
	return true;
}

// Header value: no CTLs except HTAB, which RFC 7230 field-content allows inside
// a value (same rule the request parser uses). CR and LF are CTLs, so this is
// what actually stops the splitting.
static bool validHeaderValue(const std::string& value) {
	for (std::size_t i = 0; i < value.size(); ++i) {
		unsigned char uc = static_cast<unsigned char>(value[i]);
		if ((uc < 0x20 && uc != '\t') || uc == 0x7F) return false;
	}
	return true;
}

// The reason phrase lands in "HTTP/1.1 <code> <reason>\r\n", so any CTL there
// (CR/LF above all) splits the status line. No HTAB exception -- a reason
// phrase never needs one.
static bool reasonHasCtl(const std::string& reason) {
	for (std::size_t i = 0; i < reason.size(); ++i) {
		unsigned char uc = static_cast<unsigned char>(reason[i]);
		if (uc < 0x20 || uc == 0x7F) return true;
	}
	return false;
}

Response::Response()
	: status_(200)
	, reason_("OK")
	, body_()
	, headers_()
{}

void Response::setStatusCode(int code) {
	status_ = code;
	reason_ = reasonPhrase(code);   // "" for a code we don't name; that's fine
}

void Response::setStatusCode(int code, const std::string& reason) {
	status_ = code;
	// A CTL-bearing reason is an injection attempt (or a buggy CGI line) --
	// drop it back to the table phrase rather than echo attacker bytes into
	// the status line. Falls through to "" for a code we don't name, which is
	// still a safe (if bare) status line.
	reason_ = reasonHasCtl(reason) ? reasonPhrase(code) : reason;
}

void Response::setHeader(const std::string& name, const std::string& value) {
	// Refuse to store a header that can't be safely written out. setHeader is
	// void, so the only sane failure is to drop it whole -- a half-cleaned
	// header is worse than none. The caller's contract is "give me a real
	// header"; nothing in this server legitimately needs a CTL in one.
	if (!validHeaderName(name) || !validHeaderValue(value))
		return;
	headers_[lowerCopy(name)] = value;   // lowercased key == case-insensitive
}

void Response::setBody(const std::string& body) {
	body_ = body;
}

std::size_t Response::getContentLength() const {
	return body_.size();
}

std::string Response::serialize() const {
	// Work on a copy so serialize() stays const and the auto headers never
	// stick to the object -- call it twice a second apart and only the Date
	// differs, nothing accumulates.
	std::map<std::string, std::string> out(headers_);

	// Content-Length is the body's truth, full stop. Overwriting any
	// caller-set value here is deliberate: a length that disagrees with the
	// body is the response-smuggling bug, not a feature.
	out["content-length"] = toString(body_.size());

	// Date is auto-filled only if the caller didn't pin one (a CGI script in
	// 3.3 may pass its own through). time(NULL) is the live clock; httpDate
	// formats it locale-independently. On the impossible out-of-range case it
	// returns "" -- skip the header entirely then rather than emit "Date:".
	if (out.find("date") == out.end()) {
		std::string date = httpDate(std::time(NULL));
		if (!date.empty())
			out["date"] = date;
	}

	std::ostringstream os;
	os << "HTTP/1.1 " << status_ << ' ' << reason_ << "\r\n";

	// std::map walks keys in sorted order; order among distinct header names
	// is meaningless in HTTP, so I don't fight it.
	for (std::map<std::string, std::string>::const_iterator it = out.begin();
	     it != out.end(); ++it) {
		os << canonicalHeaderName(it->first) << ": " << it->second << "\r\n";
	}

	os << "\r\n";     // blank line: end of head, start of body
	os << body_;
	return os.str();
}

std::string Response::httpDate(std::time_t t) {
	// IMF-fixdate (RFC 7231 7.1.1.1): "Sun, 06 Nov 1994 08:49:37 GMT". The
	// names are spelled out by hand -- strftime's %a/%b follow the locale,
	// and an evaluator on a French box would otherwise get "dim."/"nov.".
	static const char* const days[] =
		{ "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static const char* const months[] =
		{ "Jan", "Feb", "Mar", "Apr", "May", "Jun",
		  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

	std::tm* g = std::gmtime(&t);
	if (g == NULL)
		return "";   // out-of-range time_t: no crash, just no Date (serialize
		             // still emits a valid message). Can't happen for the
		             // time(NULL) serialize() feeds it, but gmtime is allowed
		             // to fail and the subject's no-crash rule is absolute.

	std::ostringstream os;
	os << days[g->tm_wday] << ", "
	   << std::setfill('0')
	   << std::setw(2) << g->tm_mday << ' '
	   << months[g->tm_mon] << ' '
	   << (g->tm_year + 1900) << ' '
	   << std::setw(2) << g->tm_hour << ':'
	   << std::setw(2) << g->tm_min  << ':'
	   << std::setw(2) << g->tm_sec  << " GMT";
	return os.str();
}

std::string Response::reasonPhrase(int code) {
	// Only the codes this server actually emits across the phases. Anything
	// else returns "" and the caller is expected to pass an explicit reason
	// via the two-arg setStatusCode.
	switch (code) {
		case 200: return "OK";
		case 201: return "Created";
		case 204: return "No Content";
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 304: return "Not Modified";
		case 400: return "Bad Request";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 408: return "Request Timeout";
		case 411: return "Length Required";
		case 413: return "Payload Too Large";
		case 414: return "URI Too Long";
		case 431: return "Request Header Fields Too Large";
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 504: return "Gateway Timeout";
		case 505: return "HTTP Version Not Supported";
		default:  return "";
	}
}
