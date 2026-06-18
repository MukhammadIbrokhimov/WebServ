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
	reason_ = reason;
}

void Response::setHeader(const std::string& name, const std::string& value) {
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
	// formats it locale-independently.
	if (out.find("date") == out.end())
		out["date"] = httpDate(std::time(NULL));

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
