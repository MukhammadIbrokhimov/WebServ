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
	check(r.getPath() == "/index.html", "query-less uri: path == uri");
	check(r.getQuery() == "", "query-less uri: empty query");
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

static void test_simple_get_complete() {
	// the kanban 2.1 DoD case, verbatim
	Request r;
	std::size_t consumed = r.parseFromBuffer(SIMPLE_GET);
	check(consumed == std::string(SIMPLE_GET).size(), "whole head consumed");
	check(r.isComplete(), "complete at the blank line");
	check(!r.hasError(), "no error");
	check(r.getHeader("Host") == "localhost", "Host readable");
}

static void test_header_case_insensitive() {
	Request r;
	r.parseFromBuffer("GET / HTTP/1.1\r\nHoSt: localhost\r\n\r\n");
	check(r.getHeader("HOST") == "localhost", "stored HoSt, found HOST");
	check(r.getHeader("host") == "localhost", "found host too");
	check(r.hasHeader("hOsT"), "hasHeader case-insensitive");
}

static void test_header_value_with_colon() {
	Request r;
	r.parseFromBuffer("GET / HTTP/1.1\r\nHost: localhost:8080\r\n\r\n");
	check(r.getHeader("host") == "localhost:8080",
	      "split at FIRST colon; value keeps its own");
}

static void test_ows_trimming() {
	Request r;
	r.parseFromBuffer("GET / HTTP/1.1\r\nHost:\t  localhost \t\r\n\r\n");
	check(r.getHeader("host") == "localhost", "OWS (SP/TAB) trimmed both ends");
}

static void test_duplicate_headers_join() {
	Request r;
	r.parseFromBuffer("GET / HTTP/1.1\r\nHost: x\r\nAccept: a\r\nAccept: b\r\n\r\n");
	check(r.getHeader("accept") == "a, b", "duplicates comma-joined (7230 3.2.2)");
}

static void test_duplicate_host_rejected() {
	Request r;
	r.parseFromBuffer("GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n");
	check(r.hasError() && r.getErrorCode() == 400,
	      "two Hosts -> 400, never joined (smuggling vector)");
}

static void test_missing_host() {
	Request r;
	r.parseFromBuffer("GET / HTTP/1.1\r\n\r\n");
	check(r.hasError() && r.getErrorCode() == 400, "1.1 without Host -> 400");
}

static void test_http10_no_host_ok() {
	// Host didn't exist before 1.1 -- a Host-less 1.0 request is valid,
	// and nginx (the subject's reference) accepts it.
	Request r;
	r.parseFromBuffer("GET / HTTP/1.0\r\n\r\n");
	check(r.isComplete() && !r.hasError(), "1.0 without Host is fine");
}

static void test_header_errors() {
	Request r1;
	r1.parseFromBuffer("GET / HTTP/1.1\r\nNoColonHere\r\n\r\n");
	check(r1.hasError() && r1.getErrorCode() == 400, "no colon -> 400");

	Request r2;
	r2.parseFromBuffer("GET / HTTP/1.1\r\n: value\r\n\r\n");
	check(r2.hasError() && r2.getErrorCode() == 400, "empty name -> 400");

	Request r3;
	r3.parseFromBuffer("GET / HTTP/1.1\r\nHost : x\r\n\r\n");
	check(r3.hasError() && r3.getErrorCode() == 400,
	      "space before colon -> 400 (7230 3.2.4)");
}

static void test_bare_lf() {
	Request r;
	r.parseFromBuffer("GET / HTTP/1.1\nHost: x\n\n");
	check(r.isComplete() && !r.hasError(), "bare LF tolerated (7230 3.5)");
	check(r.getHeader("host") == "x", "headers parse with bare LF");
}

static void test_ctl_bytes_rejected() {
	// Task 2's review caught these: a bare CR mid-line and a NUL in the
	// URI both parsed fine. Bare CR is a MUST-reject (RFC 7230 3.5), and
	// an embedded NUL would truncate path_.c_str() the moment 2.3 hands
	// it to open() -- the %00.jpg bypass class without even needing
	// percent-decoding.
	Request r1;
	r1.parseFromBuffer("GET /a\rb HTTP/1.1\r\n");
	check(r1.hasError() && r1.getErrorCode() == 400, "bare CR in line -> 400");

	Request r2;
	r2.parseFromBuffer(std::string("GET /a\0b HTTP/1.1\r\nHost: x\r\n\r\n", 30));
	check(r2.hasError() && r2.getErrorCode() == 400, "NUL in URI -> 400");

	Request r3;
	r3.parseFromBuffer("GET / HTTP/1.1\r\nHost: a\rb\r\n\r\n");
	check(r3.hasError() && r3.getErrorCode() == 400, "bare CR in header -> 400");
}

static void test_header_ctl_bytes_rejected() {
	// Same hardening as the URI got, same reason: header names/values
	// become CGI env vars in 3.3, and a NUL truncates the env name there.
	Request r1;
	r1.parseFromBuffer(std::string("GET / HTTP/1.1\r\nX-\0Y: v\r\nHost: x\r\n\r\n", 36));
	check(r1.hasError() && r1.getErrorCode() == 400, "NUL in header name -> 400");

	Request r2;
	r2.parseFromBuffer(std::string("GET / HTTP/1.1\r\nX-Y: a\0b\r\nHost: x\r\n\r\n", 37));
	check(r2.hasError() && r2.getErrorCode() == 400, "NUL in header value -> 400");

	Request r3;
	r3.parseFromBuffer("GET / HTTP/1.1\r\nX-Y: a\033b\r\nHost: x\r\n\r\n");
	check(r3.hasError() && r3.getErrorCode() == 400, "ESC in header value -> 400");

	Request r4;
	r4.parseFromBuffer("GET / HTTP/1.1\r\nHost\t: x\r\n\r\n");
	check(r4.hasError() && r4.getErrorCode() == 400, "tab in header name -> 400");
}

static void test_empty_and_ows_values() {
	Request r1;
	r1.parseFromBuffer("GET / HTTP/1.1\r\nHost: x\r\nX-Empty:\r\n\r\n");
	check(r1.isComplete() && r1.hasHeader("x-empty")
	      && r1.getHeader("x-empty") == "", "empty value: present, empty string");

	Request r2;
	r2.parseFromBuffer("GET / HTTP/1.1\r\nHost: x\r\nX-Ows: \t \r\n\r\n");
	check(r2.isComplete() && r2.getHeader("x-ows") == "",
	      "all-OWS value trims to empty");
}

static void test_empty_host_rejected() {
	// nginx (the subject's reference) 400s a zero-length Host; matching
	// it means 2.5's vhost matcher never sees an empty host.
	Request r;
	r.parseFromBuffer("GET / HTTP/1.1\r\nHost:\r\n\r\n");
	check(r.hasError() && r.getErrorCode() == 400, "empty Host value -> 400");
}

static void test_obs_fold_rejected() {
	// Continuation lines (obs-fold) start with SP/TAB and have no colon
	// before the colon of the folded value... in practice ours dies on
	// the no-colon-at-position>0 path or SP-in-name path. RFC 7230 3.2.4
	// says a server MAY reject -- we do. Pin it so a refactor can't
	// silently start accepting folds.
	Request r;
	r.parseFromBuffer("GET / HTTP/1.1\r\nHost: x\r\n folded\r\n\r\n");
	check(r.hasError() && r.getErrorCode() == 400, "obs-fold line -> 400");
}

static void test_pipelining_consumed_count() {
	Request r;
	const std::string two = std::string(SIMPLE_GET) + "GET /next HTTP/1.1\r\n";
	std::size_t consumed = r.parseFromBuffer(two);
	check(consumed == std::string(SIMPLE_GET).size(),
	      "consumption stops exactly at end of request #1");
	check(r.isComplete(), "request #1 complete");
	check(r.getPath() == "/", "request #1 fields untouched by trailing bytes");
}

static void test_pipelining_split_mid_request() {
	// the worked example from the design doc: tail bytes provably arrived
	// in the second call, so the subtraction can't underflow
	Request r;
	std::size_t c1 = r.parseFromBuffer("GE");
	check(c1 == 2, "partial fragment fully counted");
	const std::string rest = "T / HTTP/1.1\r\nHost: x\r\n\r\nGET /next";
	std::size_t c2 = r.parseFromBuffer(rest);
	check(c2 == rest.size() - 9, "9 tail bytes ('GET /next') excluded");
	check(r.isComplete(), "complete after second fragment");
}

static void test_terminal_states_consume_nothing() {
	Request done;
	done.parseFromBuffer(SIMPLE_GET);
	check(done.parseFromBuffer("more bytes") == 0, "complete -> consumes 0");
	check(done.isComplete() && !done.hasError(), "state unchanged");

	Request broken;
	const std::string bad = "garbage here\r\nleftover";
	check(broken.parseFromBuffer(bad) == bad.size(),
	      "erroring call consumes everything (framing unrecoverable)");
	check(broken.hasError(), "garbage errored");
	check(broken.parseFromBuffer("more") == 0, "error -> consumes 0");
}

static void test_reset_reuse() {
	Request r;
	r.parseFromBuffer(SIMPLE_GET);
	check(r.isComplete(), "first parse complete");
	r.reset();
	check(!r.isComplete() && !r.hasError() && r.getMethod().empty()
	      && !r.hasHeader("host"), "reset wipes everything");
	r.parseFromBuffer("GET /again HTTP/1.1\r\nHost: y\r\n\r\n");
	check(r.isComplete() && r.getPath() == "/again"
	      && r.getHeader("host") == "y", "object reusable after reset");
}

static void test_request_line_limits() {
	// Build a line of exactly 8192 bytes including the trailing \r --
	// the boundary must PASS. (The cap counts the line before the \n.)
	const std::size_t cap = 8 * 1024;
	const std::string head = "GET /";
	const std::string tail = " HTTP/1.1\r";
	std::string line = head
		+ std::string(cap - head.size() - tail.size(), 'a') + tail;
	Request ok;
	ok.parseFromBuffer(line + "\nHost: x\r\n\r\n");
	check(ok.isComplete() && !ok.hasError(), "8 KB request line passes");

	Request over;
	over.parseFromBuffer("GET /" + std::string(cap, 'a') + " HTTP/1.1\r\n");
	check(over.hasError() && over.getErrorCode() == 414,
	      "over-cap request line -> 414");
}

static void test_no_newline_flood() {
	// THE case the every-call raw check exists for: no terminator, ever.
	// Without it, buf_ grows until OOM and the subject's no-crash rule
	// takes the grade to 0.
	Request r;
	r.parseFromBuffer(std::string(5 * 1024, 'A'));
	check(!r.hasError(), "5 KB unterminated: still under cap, waiting");
	r.parseFromBuffer(std::string(4 * 1024, 'A'));
	check(r.hasError() && r.getErrorCode() == 414,
	      "9 KB with no newline at all -> 414");
}

static void test_header_line_limit() {
	Request r;
	r.parseFromBuffer("GET / HTTP/1.1\r\nX-Big: "
	                  + std::string(9 * 1024, 'b') + "\r\n\r\n");
	check(r.hasError() && r.getErrorCode() == 431, "9 KB header line -> 431");
}

static std::string numberedHeaders(int n) {
	std::string out;
	for (int i = 0; i < n; ++i) {
		std::ostringstream ss;
		ss << "X-H" << i << ": v\r\n";
		out += ss.str();
	}
	return out;
}

static void test_header_count_limit() {
	Request ok;   // Host + 99 = 100 headers -> boundary passes
	ok.parseFromBuffer("GET / HTTP/1.1\r\nHost: x\r\n"
	                   + numberedHeaders(99) + "\r\n");
	check(ok.isComplete() && !ok.hasError(), "100 headers pass");

	Request over; // Host + 100 = 101 -> 431
	over.parseFromBuffer("GET / HTTP/1.1\r\nHost: x\r\n"
	                     + numberedHeaders(100) + "\r\n");
	check(over.hasError() && over.getErrorCode() == 431, "101 headers -> 431");
}

int main() {
	test_initial_state();
	test_request_line_happy();
	test_query_split();
	test_request_line_errors();
	test_leading_empty_lines();
	test_incomplete_fragment();
	test_simple_get_complete();
	test_header_case_insensitive();
	test_header_value_with_colon();
	test_ows_trimming();
	test_duplicate_headers_join();
	test_duplicate_host_rejected();
	test_missing_host();
	test_http10_no_host_ok();
	test_header_errors();
	test_bare_lf();
	test_ctl_bytes_rejected();
	test_header_ctl_bytes_rejected();
	test_empty_and_ows_values();
	test_empty_host_rejected();
	test_obs_fold_rejected();
	test_pipelining_consumed_count();
	test_pipelining_split_mid_request();
	test_terminal_states_consume_nothing();
	test_reset_reuse();
	test_request_line_limits();
	test_no_newline_flood();
	test_header_line_limit();
	test_header_count_limit();

	std::cout << g_passed << " passed, " << g_failed << " failed"
	          << std::endl;
	return g_failed == 0 ? 0 : 1;
}
