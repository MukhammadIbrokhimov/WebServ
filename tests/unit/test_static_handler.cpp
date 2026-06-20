#include "../../includes/handler.hpp"
#include <iostream>
#include <string>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

// Framework-free, like test_response.cpp: a counter and a check(). Unlike the
// Request/Response tests this one touches the disk, so it builds a throwaway
// fixture tree under /tmp first and tears it down at the end.
static int g_failed = 0;
static int g_passed = 0;

static void check(bool cond, const std::string& name) {
	if (cond) { ++g_passed; return; }
	++g_failed;
	std::cerr << "FAIL: " << name << std::endl;
}

// A tiny helper request: we only ever need getPath() to return a chosen path,
// and parseFromBuffer() is the only way to set it, so feed it a minimal GET.
static Request makeGet(const std::string& path) {
	Request r;
	std::string raw = "GET " + path + " HTTP/1.1\r\nHost: x\r\n\r\n";
	r.parseFromBuffer(raw);
	return r;
}

// --- fixture root, filled in by main() before the tests run ---
static std::string g_root;

static void test_missing_file_is_404() {
	LocationConfig loc;
	Response r = StaticFileHandler::handleGet(makeGet("/nope.html"), g_root, loc);
	std::string out = r.serialize();
	check(out.compare(0, 13, "HTTP/1.1 404 ") == 0, "missing file -> 404");
}

int main() {
	// Build the fixture tree under a unique-ish temp dir.
	char tmpl[] = "/tmp/webserv_static_XXXXXX";
	char* dir = mkdtemp(tmpl);
	if (dir == NULL) { std::cerr << "mkdtemp failed" << std::endl; return 1; }
	g_root = dir;

	test_missing_file_is_404();

	std::cout << g_passed << " passed, " << g_failed << " failed" << std::endl;
	return g_failed == 0 ? 0 : 1;
}
