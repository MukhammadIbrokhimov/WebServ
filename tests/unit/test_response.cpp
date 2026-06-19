#include "../../includes/http.hpp"
#include <iostream>
#include <string>

// Same framework-free style as test_request.cpp -- a check() counter is all
// a builder needs, and the subject bans external libs. Prints only failures;
// exit code feeds `make unit`.
static int g_failed = 0;
static int g_passed = 0;

static void check(bool cond, const std::string& name) {
	if (cond) { ++g_passed; return; }
	++g_failed;
	std::cerr << "FAIL: " << name << std::endl;
}

// True when `hay` contains `needle` -- the readable spelling of find != npos,
// used all over the serialize() assertions.
static bool contains(const std::string& hay, const std::string& needle) {
	return hay.find(needle) != std::string::npos;
}

static void test_default_is_empty_200() {
	// A fresh Response must already be a valid message: a handler that only
	// fills a body shouldn't have to remember to say "200 OK".
	Response r;
	std::string out = r.serialize();
	check(out.compare(0, 17, "HTTP/1.1 200 OK\r\n") == 0,
	      "fresh response starts with 200 OK status line");
	check(contains(out, "Content-Length: 0\r\n"), "empty body -> length 0");
	check(contains(out, "\r\n\r\n"), "blank line terminates the head");
}

static void test_status_code_looks_up_reason() {
	Response r;
	r.setStatusCode(404);
	check(contains(r.serialize(), "HTTP/1.1 404 Not Found\r\n"),
	      "404 fills in 'Not Found'");
}

static void test_status_code_explicit_reason() {
	// The escape hatch: a code we don't name, or a CGI-dictated reason.
	Response r;
	r.setStatusCode(418, "I'm a teapot");
	check(contains(r.serialize(), "HTTP/1.1 418 I'm a teapot\r\n"),
	      "explicit reason overrides the table");
}

static void test_set_header_appears() {
	Response r;
	r.setHeader("Content-Type", "text/html");
	check(contains(r.serialize(), "Content-Type: text/html\r\n"),
	      "a set header is emitted verbatim");
}

static void test_header_case_insensitive_overwrite() {
	// Two spellings of the same name are ONE header (RFC 7230 3.2), and the
	// output casing is canonical regardless of how it went in.
	Response r;
	r.setHeader("content-type", "text/plain");
	r.setHeader("Content-Type", "application/json");
	std::string out = r.serialize();
	check(contains(out, "Content-Type: application/json\r\n"),
	      "second set wins, name canonicalized");
	check(!contains(out, "text/plain"), "first value gone, no duplicate header");
}

static void test_body_after_blank_line() {
	Response r;
	r.setBody("hello");
	std::string out = r.serialize();
	std::size_t sep = out.find("\r\n\r\n");
	check(sep != std::string::npos, "head/body separator present");
	check(out.substr(sep + 4) == "hello", "body sits right after the blank line");
}

static void test_content_length_tracks_body() {
	Response r;
	r.setBody("12345");
	check(r.getContentLength() == 5, "getContentLength == body size");
	check(contains(r.serialize(), "Content-Length: 5\r\n"),
	      "auto Content-Length matches body");
}

static void test_content_length_is_authoritative() {
	// Body is the source of truth: a wrong caller-set length must NOT be able
	// to desync framing (smuggling vector). serialize() overrides it.
	Response r;
	r.setBody("abc");
	r.setHeader("Content-Length", "999");
	std::string out = r.serialize();
	check(contains(out, "Content-Length: 3\r\n"), "real body length wins");
	check(!contains(out, "Content-Length: 999\r\n"), "bogus length dropped");
}

static void test_date_auto_present_and_formed() {
	Response r;
	std::string out = r.serialize();
	check(contains(out, "Date: "), "Date auto-added");
	check(contains(out, " GMT\r\n"), "Date ends in GMT");
}

static void test_date_not_overwritten() {
	// If the caller pinned a Date (e.g. a CGI passthrough), keep theirs.
	Response r;
	r.setHeader("Date", "Mon, 01 Jan 2024 00:00:00 GMT");
	std::string out = r.serialize();
	check(contains(out, "Date: Mon, 01 Jan 2024 00:00:00 GMT\r\n"),
	      "caller-set Date preserved");
}

static void test_http_date_epoch() {
	// Epoch 0 is a Thursday -- a fixed, locale-independent anchor.
	check(Response::httpDate(0) == "Thu, 01 Jan 1970 00:00:00 GMT",
	      "httpDate(0) is the canonical epoch string");
}

static void test_reason_phrase_table() {
	check(Response::reasonPhrase(200) == "OK", "200 -> OK");
	check(Response::reasonPhrase(404) == "Not Found", "404 -> Not Found");
	check(Response::reasonPhrase(500) == "Internal Server Error",
	      "500 -> Internal Server Error");
	check(Response::reasonPhrase(799) == "", "unknown code -> empty phrase");
}

static void test_header_rejects_crlf_injection() {
	// Response splitting: a value carrying CRLF must never reach the wire, or
	// a CGI passthrough in 3.3 could smuggle in its own headers / fake body.
	// The whole poisoned header is dropped, mirroring how the request parser
	// 400s a CTL-bearing field rather than trying to clean it.
	Response r;
	r.setHeader("X-Test", "ok\r\nInjected: evil");
	std::string out = r.serialize();
	check(!contains(out, "Injected: evil"), "CRLF value can't inject a header");
	check(!contains(out, "X-Test"), "the poisoned header is dropped whole");
}

static void test_header_rejects_bad_name() {
	// A space, colon or CTL in the NAME breaks framing just as badly.
	Response r;
	r.setHeader("Bad Name", "v");        // space
	r.setHeader("Bad:Name", "v");        // colon
	r.setHeader("Bad\r\nName", "v");     // CRLF
	check(!contains(r.serialize(), "Bad"), "invalid header names dropped");
}

static void test_header_allows_tab_in_value() {
	// HTAB is legal inside a field value (RFC 7230), and the request parser
	// keeps it -- so the builder must not over-reject and drop a valid header.
	Response r;
	r.setHeader("X-Tab", "a\tb");
	check(contains(r.serialize(), "X-Tab: a\tb\r\n"), "tab preserved in value");
}

static void test_reason_phrase_strips_ctl() {
	// CR/LF in the reason would split the status line itself. Fall back to the
	// table phrase for the code rather than echoing attacker bytes.
	Response r;
	r.setStatusCode(404, "Not Found\r\nX-Evil: 1");
	std::string out = r.serialize();
	check(out.compare(0, 24, "HTTP/1.1 404 Not Found\r\n") == 0,
	      "CTL reason falls back to the table phrase");
	check(!contains(out, "X-Evil"), "no injection through the reason phrase");
}

int main() {
	test_default_is_empty_200();
	test_status_code_looks_up_reason();
	test_status_code_explicit_reason();
	test_set_header_appears();
	test_header_case_insensitive_overwrite();
	test_body_after_blank_line();
	test_content_length_tracks_body();
	test_content_length_is_authoritative();
	test_date_auto_present_and_formed();
	test_date_not_overwritten();
	test_http_date_epoch();
	test_reason_phrase_table();
	test_header_rejects_crlf_injection();
	test_header_rejects_bad_name();
	test_header_allows_tab_in_value();
	test_reason_phrase_strips_ctl();

	std::cout << g_passed << " passed, " << g_failed << " failed"
	          << std::endl;
	return g_failed == 0 ? 0 : 1;
}
