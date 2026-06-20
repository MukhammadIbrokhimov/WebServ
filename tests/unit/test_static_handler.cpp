#include "../../includes/handler.hpp"
#include <iostream>
#include <fstream>
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

static bool contains(const std::string& hay, const std::string& needle) {
	return hay.find(needle) != std::string::npos;
}

static void writeFile(const std::string& path, const std::string& body) {
	std::ofstream out(path.c_str());
	if (!out) {
		std::cerr << "writeFile: cannot open " << path << std::endl;
		std::exit(1);
	}
	out << body;
}

// --- fixture root, filled in by main() before the tests run ---
static std::string g_root;

static void test_missing_file_is_404() {
	LocationConfig loc;
	Response r = StaticFileHandler::handleGet(makeGet("/nope.html"), g_root, loc);
	std::string out = r.serialize();
	check(out.compare(0, 13, "HTTP/1.1 404 ") == 0, "missing file -> 404");
}

static void test_serves_html_200() {
	LocationConfig loc;
	Response r = StaticFileHandler::handleGet(makeGet("/index.html"), g_root, loc);
	std::string out = r.serialize();
	check(out.compare(0, 13, "HTTP/1.1 200 ") == 0, "existing file -> 200");
	check(contains(out, "Content-Type: text/html\r\n"), ".html -> text/html");
	check(contains(out, "<h1>hello</h1>"), "body is the file content");
}

static void test_content_type_css_js() {
	LocationConfig loc;
	check(contains(StaticFileHandler::handleGet(makeGet("/a.css"), g_root, loc).serialize(),
	               "Content-Type: text/css\r\n"), ".css -> text/css");
	check(contains(StaticFileHandler::handleGet(makeGet("/a.js"), g_root, loc).serialize(),
	               "Content-Type: text/javascript\r\n"), ".js -> text/javascript");
}

static void test_unknown_ext_is_octet_stream() {
	LocationConfig loc;
	check(contains(StaticFileHandler::handleGet(makeGet("/blob.bin"), g_root, loc).serialize(),
	               "Content-Type: application/octet-stream\r\n"),
	      "unknown extension -> octet-stream");
}

static void test_traversal_is_403() {
	LocationConfig loc;
	// The literal-dotdot attack the DoD names. Must never reach /etc/passwd.
	Response r = StaticFileHandler::handleGet(
		makeGet("/../../../../etc/passwd"), g_root, loc);
	check(r.serialize().compare(0, 13, "HTTP/1.1 403 ") == 0,
	      "../ escaping root -> 403");
}

static void test_dotdot_within_root_resolves() {
	LocationConfig loc;
	// "/sub/../index.html" stays inside root and must resolve to /index.html.
	Response r = StaticFileHandler::handleGet(
		makeGet("/sub/../index.html"), g_root, loc);
	std::string out = r.serialize();
	check(out.compare(0, 13, "HTTP/1.1 200 ") == 0, "in-root .. -> 200");
	check(contains(out, "<h1>hello</h1>"), "resolves to /index.html");
}

static void test_dot_segments_ignored() {
	LocationConfig loc;
	Response r = StaticFileHandler::handleGet(
		makeGet("/./index.html"), g_root, loc);
	std::string out = r.serialize();
	check(out.compare(0, 13, "HTTP/1.1 200 ") == 0,
	      "single-dot segment ignored -> 200");
	check(contains(out, "<h1>hello</h1>"), "single-dot segment -> serves index.html");
}

static void test_bare_dotdot_is_403() {
	LocationConfig loc;
	Response r = StaticFileHandler::handleGet(makeGet("/.."), g_root, loc);
	check(r.serialize().compare(0, 13, "HTTP/1.1 403 ") == 0, "/.. -> 403");
}

static void test_partial_escape_is_403() {
	LocationConfig loc;
	Response r = StaticFileHandler::handleGet(makeGet("/foo/../../bar"), g_root, loc);
	check(r.serialize().compare(0, 13, "HTTP/1.1 403 ") == 0,
	      "/foo/../../bar escapes after descent -> 403");
}

int main() {
	// Build the fixture tree under a unique-ish temp dir.
	char tmpl[] = "/tmp/webserv_static_XXXXXX";
	char* dir = mkdtemp(tmpl);
	if (dir == NULL) { std::cerr << "mkdtemp failed" << std::endl; return 1; }
	g_root = dir;

	writeFile(g_root + "/index.html", "<h1>hello</h1>");
	writeFile(g_root + "/a.css", "body{}");
	writeFile(g_root + "/a.js", "var x=1;");
	writeFile(g_root + "/blob.bin", "\x00\x01\x02 raw");

	::mkdir((g_root + "/sub").c_str(), 0755);
	writeFile(g_root + "/sub/page.html", "<h1>sub</h1>");

	test_missing_file_is_404();
	test_serves_html_200();
	test_content_type_css_js();
	test_unknown_ext_is_octet_stream();
	test_traversal_is_403();
	test_dotdot_within_root_resolves();
	test_dot_segments_ignored();
	test_bare_dotdot_is_403();
	test_partial_escape_is_403();

	std::cout << g_passed << " passed, " << g_failed << " failed" << std::endl;
	return g_failed == 0 ? 0 : 1;
}
