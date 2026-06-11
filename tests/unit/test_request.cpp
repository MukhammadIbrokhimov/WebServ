#include "../../includes/http.hpp"
#include <iostream>
#include <sstream>
#include <string>

// Framework-free on purpose -- the subject bans external libs, and a
// check() counter is all a parser needs. Prints only failures; exit code
// feeds `make unit`.
static int g_failed = 0;
static int g_passed = 0;

static void check(bool cond, const std::string& name) {
	if (cond) { ++g_passed; return; }
	++g_failed;
	std::cerr << "FAIL: " << name << std::endl;
}

// The request from the kanban 2.1 DoD -- reused all over this file.
static const char* SIMPLE_GET =
	"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";

static void test_initial_state() {
	Request r;
	check(!r.isComplete(), "fresh request is not complete");
	check(!r.hasError(), "fresh request has no error");
	check(r.getErrorCode() == 0, "fresh error code is 0");
	check(r.getMethod().empty(), "no method yet");
	check(!r.hasHeader("host"), "no headers yet");
	check(r.getHeader("host") == "", "getHeader(absent) returns empty");
	check(r.getBody().empty(), "body empty (stub until 3.1)");
}

static void test_request_line_happy() {
	Request r;
	std::size_t consumed = r.parseFromBuffer("GET /index.html HTTP/1.1\r\n");
	check(consumed == 26, "request line fully consumed");
	check(!r.isComplete(), "headers not finished yet");
	check(!r.hasError(), "valid request line, no error");
	check(r.getMethod() == "GET", "method extracted");
	check(r.getUri() == "/index.html", "uri extracted");
	check(r.getVersion() == "HTTP/1.1", "version extracted");
}

static void test_query_split() {
	Request r;
	r.parseFromBuffer("GET /search?q=42 HTTP/1.1\r\n");
	check(r.getPath() == "/search", "path is everything before '?'");
	check(r.getQuery() == "q=42", "query is everything after '?'");

	Request r2;
	r2.parseFromBuffer("GET /a? HTTP/1.1\r\n");
	check(r2.getPath() == "/a" && r2.getQuery() == "", "trailing '?' -> empty query");

	Request r3;
	r3.parseFromBuffer("GET /a?b?c HTTP/1.1\r\n");
	check(r3.getQuery() == "b?c", "only the FIRST '?' splits");
}

// helper for the table of bad request lines
static void expectLineError(const std::string& line, int code,
                            const std::string& name) {
	Request r;
	r.parseFromBuffer(line);
	check(r.hasError() && r.getErrorCode() == code, name);
}

static void test_request_line_errors() {
	expectLineError("GET  /x HTTP/1.1\r\n", 400, "double space -> 400");
	expectLineError("get /x HTTP/1.1\r\n", 400, "lowercase method -> 400");
	expectLineError("GET x HTTP/1.1\r\n", 400, "URI without leading '/' -> 400");
	expectLineError("GET /x\r\n", 400, "2 parts -> 400");
	expectLineError("GET /x FOO/1.1\r\n", 400, "bad version grammar -> 400");
	expectLineError("GET /x http/1.1\r\n", 400, "lowercase 'http' -> 400");
	expectLineError("GET /x HTTP/1.1x\r\n", 400, "trailing junk on version -> 400");
	expectLineError("GET /x HTTP/2.0\r\n", 505, "well-formed unsupported version -> 505");
}

static void test_leading_empty_lines() {
	// RFC 7230 3.5: servers SHOULD skip empty line(s) before the request
	// line -- some clients send a stray CRLF after a previous body.
	Request r;
	r.parseFromBuffer("\r\n\r\nGET /x HTTP/1.1\r\n");
	check(!r.hasError(), "leading CRLFs skipped");
	check(r.getMethod() == "GET", "request line parsed after empty lines");
}

static void test_incomplete_fragment() {
	Request r;
	std::size_t consumed = r.parseFromBuffer("GET / HT");
	check(consumed == 8, "fragment accepted into the buffer");
	check(!r.isComplete() && !r.hasError(), "no full line yet -> just waiting");
	check(r.getMethod().empty(), "nothing parsed from a partial line");
}

int main() {
	test_initial_state();
	test_request_line_happy();
	test_query_split();
	test_request_line_errors();
	test_leading_empty_lines();
	test_incomplete_fragment();
	(void)SIMPLE_GET; // used from Task 3 on

	std::cout << g_passed << " passed, " << g_failed << " failed"
	          << std::endl;
	return g_failed == 0 ? 0 : 1;
}
