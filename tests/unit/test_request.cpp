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

int main() {
	test_initial_state();
	(void)SIMPLE_GET; // used from Task 3 on

	std::cout << g_passed << " passed, " << g_failed << " failed"
	          << std::endl;
	return g_failed == 0 ? 0 : 1;
}
