# WebServ Development Kanban
## HTTP/1.1 Non-Blocking Server (C++98) - Team of 3 Developers

**Project Goal:** Build a custom HTTP/1.1 server with non-blocking I/O, achieving a perfect mandatory score + bonus features.

**Developer Roles:**
- **Dev A (Network Architect):** Socket management, poll() loop, connection handling, signal management
- **Dev B (Protocol Specialist):** Config parsing, HTTP request parsing, response construction
- **Dev C (Executor):** File I/O, CGI execution, directory listing, integration testing

---

## PHASE 0: Setup & Standards

### [ completed ] 0.1: Repository Setup & Makefile
**Assignee:** Dev A  
**Description:**  
- Initialize git with proper .gitignore (*.o, *.a, test_*, bin/)
- Create Makefile with targets: `all`, `clean`, `fclean`, `re`
- Compile flags: `-Wall -Wextra -Werror -std=c++98` (no external libraries)

**DoD:**
- [ ] Makefile compiles all .cpp files in src/ recursively
- [ ] No undefined references or compilation warnings
- [ ] `make clean` removes all .o files, `fclean` removes with executable
- [ ] Executable named `webserv`

---

### [ completed ] 0.2: Project Structure & Header Organization
**Assignee:** Dev A  
**Description:**  
- Create includes/server.hpp with forward declarations
- Create includes/http.hpp for Request/Response classes
- Create includes/config.hpp for Config class
- Create includes/socket.hpp for Socket wrapper
- Establish include guards and avoid circular dependencies
- Create src/ subdirectories: socket/, http/, config/, cgi/, utils/

**DoD:**
- [ ] All headers have proper include guards (#ifndef, #define, #endif)
- [ ] No circular include dependencies (validate with grep)
- [ ] Directory structure matches design
- [ ] Headers compile without errors in isolation

---

### [ completed ] 0.3: Basic Logging & Error Handling Utility
**Assignee:** Dev A  
**Description:**  
- Create includes/logger.hpp with macros: LOG_INFO, LOG_ERROR, LOG_DEBUG
- Implement to stderr
- Create a global exception hierarchy: WebServException, ConfigException, IOException -- not done

**DoD:**
- [ ] Logging system works with std::cerr
- [ ] Exception classes are constructible and throwable -- not done
- [ ] No external dependencies (std::string only)

---

### [ ] 0.4: README.md Documentation
**Assignee:** Dev C  
**Description:**  
- Document project overview and architecture
- Usage: `./webserv [config_file]` (default: default.conf)
- Build instructions: `make` (should compile without flags warnings)
- List supported HTTP methods: GET, POST, DELETE
- Document CGI support and configuration file syntax
- Include examples of config file usage
- Note: Must be present and complete per subject requirements

**DoD:**
- [ ] README explains what webserv does
- [ ] Build and run instructions are clear
- [ ] Lists all supported features (GET, POST, DELETE, CGI, multiple servers, etc.)
- [ ] Config file examples provided
- [ ] Markdown formatted and well-organized

---

## PHASE 1: The Skeleton - Core Loop & Config

### [ ] 1.1: Socket Wrapper Class (Non-Blocking Setup)
**Assignee:** Dev A  
**Description:**  
- Create Socket class in includes/socket.hpp and src/socket/socket.cpp
- Implement methods:
  - `Socket(int domain, int type)` - constructor (AF_INET, SOCK_STREAM)
  - `bind(int port, const std::string& host = "127.0.0.1")`
  - `listen(int backlog = SOMAXCONN)`
  - `accept()` - returns new Socket for client
  - `setNonBlocking()` - use fcntl() with O_NONBLOCK
  - `close()` - graceful socket closure
  - `getFileDescriptor()` - return fd
  - Destructor handles cleanup
- Error handling: throw exceptions on failures
- No edge cases ignored (EINTR handling, etc.)

**DoD:**
- [ ] Socket creates, binds, and listens on port without errors
- [ ] Accepts incoming connections
- [ ] Non-blocking flag verified with fcntl()
- [ ] Compiles with -std=c++98 -Wall -Wextra -Werror
- [ ] No memory leaks (validate with valgrind)
- [ ] Destructor called properly on scope exit

---

### [ ] 1.2: Poll-Based Main Event Loop
**Assignee:** Dev A  
**Description:**  
- Create Server class in includes/server.hpp and src/server/server.cpp
- Implement poll() loop in `run()` method:
  - Maintain std::vector<struct pollfd> for all sockets (listening + client)
  - **CRITICAL:** Check both POLLIN (read-ready) and POLLOUT (write-ready)
  - Handle POLLERR and POLLHUP
  - Timeout: 5000ms (configurable)
  - On POLLIN for listening socket: call accept() and add to pollfd list
  - On POLLIN for client socket: mark as ready for reading
  - On POLLOUT for client socket: mark as ready for writing
  - Gracefully remove closed client sockets from poll list
- Rough pseudocode:
  ```
  while (!shutdown) {
    poll(fds, timeout)
    for each fd with POLLIN: handle_read()
    for each fd with POLLOUT: handle_write()
    cleanup closed sockets
  }
  ```

**DoD:**
- [ ] Event loop runs without blocking
- [ ] Multiple clients can connect simultaneously
- [ ] POLLIN and POLLOUT are both checked per poll() call
- [ ] No busy-waiting (poll() blocks until event or timeout)
- [ ] Stress test: 10+ concurrent connections work

---

### [ ] 1.3: Signal Handling (SIGINT, SIGTERM)
**Assignee:** Dev A  
**Description:**  
- Install signal handlers for SIGINT (Ctrl+C) and SIGTERM
- Use global volatile sig_atomic_t flag `g_shutdown`
- Handler: set flag, don't use stdio (per POSIX)
- Check flag in main loop: if true, gracefully close all sockets and exit
- No hanging processes on shutdown

**DoD:**
- [ ] Ctrl+C gracefully shuts down server
- [ ] All client connections closed before exit
- [ ] No zombie processes
- [ ] ps aux shows no webserv processes after shutdown

---

### [ ] 1.4: Config File Parser (Basic Structure)
**Assignee:** Dev B  
**Description:**  
- Create Config class in includes/config.hpp and src/config/config.cpp
- Parse nginx-like config file:
  ```
  server {
    listen       8080;
    server_name  localhost;
    root         ./www;
    client_body_limit 10000;
    
    location / {
      allowed_methods GET POST DELETE;
      index index.html;
    }
    
    location /upload {
      allowed_methods POST;
    }
    
    error_page 404 /404.html;
    error_page 500 /500.html;
  }
  ```
- Parse multiple server blocks
- Store in structs/vectors (no STL maps initially if simplicity preferred)
- Validate: all required fields present
- Handle comments (#) and whitespace

**DoD:**
- [ ] Parses valid config file without errors
- [ ] Extracts: listen port, server_name, root, client_body_limit, locations
- [ ] Throws ConfigException on invalid syntax
- [ ] Returns data structure with server info
- [ ] Test files: default.conf parses correctly

---

### [ ] 1.5: Load Config & Initialize Server on Startup
**Assignee:** Dev B  
**Description:**  
- Implement `main(int argc, char** argv)`
- If no argument: load `default.conf`
- If argument: load specified config file
- Parse config
- Create Server instance from config data
- Initialize listening sockets from config (support multiple servers)
- Graceful error handling: print to stderr and exit(1) on config error

**DoD:**
- [ ] `./webserv` loads default.conf
- [ ] `./webserv custom.conf` loads custom config
- [ ] All listening ports open successfully
- [ ] Error messages clear on config failure

---

## PHASE 2: The Core - Basic GET & Static Files

### [ ] 2.1: HTTP Request Parsing (GET, Headers, Body Stub)
**Assignee:** Dev B  
**Description:**  
- Create Request class in includes/http.hpp and src/http/request.cpp
- Implement request parser:
  - Parse request line: method, URI, HTTP version
  - Parse headers: store in map<string, string> (case-insensitive comparison)
  - Separate headers from body with \r\n\r\n
  - Handle incomplete requests (buffer partially received)
  - Validate HTTP/1.1 compliance
  - Extract query string from URI (store separately)
- Methods:
  - `parseFromBuffer(const std::string& buffer)` - returns bytes consumed or -1 (incomplete)
  - `getMethod()`, `getUri()`, `getHeader(key)`, `getBody()`
  - `isComplete()` - returns true if full request received

**DoD:**
- [ ] Parses simple GET request from buffer
- [ ] Extracts method, URI, HTTP version
- [ ] Reads all headers correctly
- [ ] Handles incomplete buffers (doesn't crash)
- [ ] Test: parse "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"

---

### [ ] 2.2: HTTP Response Construction
**Assignee:** Dev B  
**Description:**  
- Create Response class in includes/http.hpp and src/http/response.cpp
- Build HTTP/1.1 compliant response:
  - Status line: "HTTP/1.1 {code} {reason}\r\n"
  - Headers: Content-Type, Content-Length, Date (RFC 1123 format), Connection
  - Blank line: \r\n
  - Body: file content or error page
- Methods:
  - `setStatusCode(int code, const std::string& reason)`
  - `setHeader(const std::string& key, const std::string& value)`
  - `setBody(const std::string& body)`
  - `serialize()` - returns complete HTTP response as string
  - `getContentLength()` - calculated from body
- Auto-set Content-Length and Date headers

**DoD:**
- [ ] Constructs valid HTTP response with status, headers, body
- [ ] Response line correct: "HTTP/1.1 200 OK\r\n"
- [ ] Headers properly formatted with \r\n
- [ ] Body appended after blank line
- [ ] serialize() produces valid HTTP response

---

### [ ] 2.3: File I/O & Static File Serving
**Assignee:** Dev C  
**Description:**  
- Implement StaticFileHandler class in src/http/static_handler.cpp
- Methods:
  - `handleGetRequest(const Request& req, const Config& cfg)` - returns Response
  - `serveFile(const std::string& filepath)` - reads file and sets Response body
  - `serveDirectoryListing(const std::string& dirpath)` - generates HTML list (optional for Phase 2, mandatory Phase 3)
  - `findResourcePath(const std::string& uri, const Config& cfg)` - maps URI to file path
- Logic:
  - Check if file exists: use stat() or access()
  - Check if within root directory (prevent directory traversal)
  - Read file content (handle large files: consider chunking for later)
  - Set Content-Type based on file extension (.html, .css, .js, .txt, etc.)
  - Return 404 if not found, 403 if permission denied
- Error codes: 200, 404, 403

**DoD:**
- [ ] Serves index.html from root without errors
- [ ] Reads arbitrary .html, .css, .js files
- [ ] Returns 404 for missing files
- [ ] Returns 403 for permission denied
- [ ] Content-Type header correct for file types
- [ ] No path traversal vulnerabilities (../../../etc/passwd fails)
- [ ] File paths respect root directory from config

---

### [ ] 2.4: Request Routing (Match URI to Location Block)
**Assignee:** Dev B  
**Description:**  
- Implement Router class to match URIs to config locations
- Logic:
  - Iterate through location blocks in order
  - Match URI against location path (exact or prefix)
  - Return best match (longest prefix match)
  - Extract location config: allowed_methods, index file, etc.
- Methods:
  - `matchLocation(const std::string& uri, const Config& cfg)` - returns location config
- Validation:
  - Check if method (GET, POST, DELETE) is allowed for location
  - Return 405 Method Not Allowed if not

**DoD:**
- [ ] Correctly matches URI to location blocks
- [ ] Returns 405 for disallowed methods
- [ ] Prefix matching works correctly
- [ ] Index file rules applied

---

### [ ] 2.5: Integration: GET Request End-to-End Flow
**Assignee:** Dev A  
**Description:**  
- Connect Request parsing → Router → StaticFileHandler → Response → Client write
- Flow:
  1. On POLLIN for client socket: read data into buffer
  2. Parse buffer with Request::parseFromBuffer()
  3. If incomplete: wait for more data
  4. If complete: process request
  5. Route URI using Router
  6. Check allowed methods
  7. Call StaticFileHandler::handleGetRequest()
  8. Queue response for writing
  9. On POLLOUT: send response via write()
  10. After response sent: close or keep-alive (Connection header)
- Handle connection persistence (HTTP/1.1)

**DoD:**
- [ ] GET / returns index.html
- [ ] GET /notfound.html returns 404
- [ ] GET /forbidden returns 403
- [ ] Response headers include Content-Length and Content-Type
- [ ] Multiple sequential requests on same connection work
- [ ] Stress test: 5+ concurrent GET requests succeed

---

## PHASE 3: The Heavy Lifting - POST, Uploads, CGI

### [ ] 3.1: HTTP Request Parsing (POST with Body)
**Assignee:** Dev B  
**Description:**  
- Extend Request parsing to handle request bodies
- Parse Content-Length header
- Read body based on Content-Length (don't rely on EOF)
- Handle chunked transfer encoding (optional for mandatory, consider for bonus)
- Store body in Request object
- Validate: body size doesn't exceed client_body_limit from config

**DoD:**
- [ ] Parses POST request with Content-Length
- [ ] Reads correct number of bytes for body
- [ ] Rejects body larger than client_body_limit (return 413)
- [ ] Handles incomplete body (buffers and waits)
- [ ] Does not process request until full body received

---

### [ ] 3.2: File Upload Handler (Multipart Form Data)
**Assignee:** Dev C  
**Description:**  
- Implement UploadHandler in src/cgi/upload_handler.cpp
- Parse multipart/form-data:
  - Extract boundary from Content-Type header
  - Parse body sections by boundary
  - Extract filename and file data
  - Save files to upload directory (www/upload/)
- Validation:
  - Check filename (sanitize: no ../, only alphanumeric + dots)
  - Verify upload directory exists
  - Check file doesn't already exist (overwrite or reject)
- Response: 201 Created on success, 400 Bad Request on error

**DoD:**
- [ ] Parses multipart/form-data requests
- [ ] Extracts filename and content correctly
- [ ] Saves file to www/upload/
- [ ] Sanitizes filenames (no path traversal)
- [ ] Returns 201 on success
- [ ] Returns 400 on malformed data
- [ ] Test with curl: `curl -F "file=@test.txt" localhost:8080/upload`

---

### [ ] 3.3: CGI Execution (fork/execve/pipes)
**Assignee:** Dev C  
**Description:**  
- Implement CGICaller class in src/cgi/cgi_caller.cpp
- Execution flow:
  1. Check if URI matches CGI location (e.g., /cgi-bin/*.py)
  2. Construct full script path from root + URI
  3. Set environment variables:
     - REQUEST_METHOD (GET, POST, etc.)
     - SCRIPT_NAME, SCRIPT_FILENAME
     - QUERY_STRING (from URI)
     - CONTENT_LENGTH, CONTENT_TYPE
     - REMOTE_ADDR, SERVER_NAME, SERVER_PORT
     - HTTP_* headers (convert to CGI format)
  4. Fork child process
  5. In child: dup2() stdin/stdout to pipes, execve() script
  6. In parent: write POST body to stdin, read stdout into buffer
  7. Wait for child: collect exit code, handle timeouts
  8. Return script output as response body
- Error handling:
  - Script not found: 404
  - Script execution error: 500
  - Timeout (2 seconds): 504 Gateway Timeout

**DoD:**
- [ ] Forks and executes CGI script
- [ ] Passes environment variables correctly
- [ ] Reads script output into response
- [ ] Handles timeouts (2-second default)
- [ ] No zombie processes (proper waitpid() with WNOHANG)
- [ ] Test: simple Python/Bash script in cgi-bin/ runs and outputs received
- [ ] POST body passed to CGI stdin
- [ ] No memory leaks during fork/exec cycles

---

### [ ] 3.4: DELETE Method Handler
**Assignee:** Dev C  
**Description:**  
- Implement delete request handling:
  - Check if DELETE allowed in location config
  - Verify file exists and is within root
  - Call unlink() to delete file
  - Return 204 No Content on success
  - Return 404 if file not found
  - Return 403 if permission denied
- Do NOT allow deletion of directories (return 400)

**DoD:**
- [ ] DELETE /file.txt successfully deletes file
- [ ] Returns 204 No Content
- [ ] Returns 404 for missing files
- [ ] Returns 403 for permission denied
- [ ] Prevents directory deletion
- [ ] Test: `curl -X DELETE localhost:8080/file.txt`

---

### [ ] 3.5: Error Page Handling
**Assignee:** Dev B  
**Description:**  
- When HTTP error (4xx, 5xx) occurs, serve custom error page
- Read error_page mapping from config:
  ```
  error_page 404 /404.html;
  error_page 500 /500.html;
  ```
- If custom page configured: serve it with error code
- If not configured: generate default error page (simple HTML)
- Store error pages in www/ or config/

**DoD:**
- [ ] 404 errors serve custom 404.html if configured
- [ ] 500 errors serve custom 500.html if configured
- [ ] Default error pages generated if not configured
- [ ] Error pages properly formatted HTML
- [ ] Status code in response header correct

---

### [ ] 3.6: Integration: POST & CGI End-to-End
**Assignee:** Dev A  
**Description:**  
- Connect POST request → CGI or Upload handling → Response
- Flow:
  1. Request parsed with body
  2. Route checks location allowed_methods
  3. If location is /cgi-bin: call CGICaller
  4. If location is /upload: call UploadHandler
  5. Queue response for writing
  6. Send response via poll/write
- Test scenarios:
  - POST form to /cgi-bin/script.py
  - Upload file to /upload
  - POST to non-CGI location (404)

**DoD:**
- [ ] POST to CGI script executes and returns output
- [ ] File upload succeeds and file saved
- [ ] Error cases handled correctly (404, 500, etc.)
- [ ] No zombie processes after CGI execution
- [ ] Concurrent POST requests work

---

## PHASE 4: Resilience & Polish - Error Handling & Stress Testing

### [ ] 4.1: Connection Management (Keep-Alive & Timeouts)
**Assignee:** Dev A  
**Description:**  
- Implement connection timeout tracking:
  - Track last activity time for each client
  - Close idle connections after 30 seconds (configurable)
  - Support HTTP/1.1 Keep-Alive and Connection: close
- Logic:
  - Read "Connection: close" header → close after response
  - No Connection header → keep alive (default)
  - Idle timeout: close socket if no activity > 30s
- Use time() or clock_gettime() to track inactivity

**DoD:**
- [ ] Keep-Alive connections remain open for multiple requests
- [ ] Connection: close header closes socket after response
- [ ] Idle connections close after 30 seconds
- [ ] No hanging sockets consuming file descriptors
- [ ] Stress test: 100 keep-alive connections stable

---

### [ ] 4.2: Buffer Management & Partial Reads/Writes
**Assignee:** Dev A  
**Description:**  
- Implement read/write buffers for each client:
  - Read buffer: accumulates incoming data until complete request
  - Write buffer: queues response data for transmission
  - Handle incomplete writes: resume on next POLLOUT
- Logic:
  - On POLLIN: read() up to buffer size, append to read buffer
  - On complete request: process and queue response to write buffer
  - On POLLOUT: write() as much as possible from write buffer
  - Continue writing on next POLLOUT until buffer empty
- Handle edge cases:
  - read() returns 0 (client closed connection)
  - read() returns -1 with EAGAIN (would block)
  - write() returns < bytes_to_write (partial write)

**DoD:**
- [ ] Partial reads buffered correctly
- [ ] Partial writes resumed on next POLLOUT
- [ ] Large responses (>4KB) sent correctly over multiple writes
- [ ] No data loss
- [ ] Stress test: send 1MB file, verify integrity

---

### [ ] 4.3: Memory Leak Detection & Valgrind
**Assignee:** Dev A  
**Description:**  
- Run valgrind on complete server:
  - `valgrind --leak-check=full --show-leak-kinds=all ./webserv`
  - Generate load: multiple clients, uploads, CGI, etc.
  - Fix all memory leaks and invalid accesses
- Focus areas:
  - Socket resource cleanup
  - Client connection data structures
  - Request/Response objects
  - Config parsing allocations
  - CGI process cleanup (waitpid returns)

**DoD:**
- [ ] Valgrind reports no memory leaks
- [ ] Valgrind reports no invalid memory access
- [ ] No errors under heavy load (100+ requests)
- [ ] All resources freed on shutdown

---

### [ ] 4.4: Stress Testing & Performance
**Assignee:** Dev C  
**Description:**  
- Create stress test script (src/stress_test.sh or similar):
  - Concurrent clients: `ab -n 1000 -c 50 http://localhost:8080/`
  - Mixed requests: GET, POST, DELETE
  - File uploads under load
  - CGI execution under load
  - Monitor: CPU, memory, file descriptors (lsof)
- Acceptance criteria:
  - Handle 50+ concurrent connections
  - Respond to all requests (no timeouts)
  - Memory stable (no leaks under load)
  - No crashes or segfaults

**DoD:**
- [ ] Apache Bench (ab): 1000 requests, 50 concurrency, all succeed
- [ ] Memory doesn't grow unbounded (valgrind check)
- [ ] No file descriptor leaks (lsof count stable)
- [ ] Response times reasonable (<100ms typical)
- [ ] No segfaults or crashes

---

### [ ] 4.5: Error Handling & Edge Cases
**Assignee:** Dev B  
**Description:**  
- Comprehensive error handling for:
  - Malformed HTTP requests: 400 Bad Request
  - Large headers: 431 Request Header Fields Too Large
  - Unsupported HTTP version: 505 HTTP Version Not Supported
  - Method not implemented: 501 Not Implemented
  - Server errors: 500 Internal Server Error
  - Connection errors (read/write fails): log and close gracefully
- Test scenarios:
  - Send garbage data to server
  - Send extremely large headers
  - Send incomplete requests then close
  - Send request with invalid method
- All should close connection or send error without crashing

**DoD:**
- [ ] Malformed requests generate 400 response
- [ ] Server doesn't crash on bad input
- [ ] Error messages logged appropriately
- [ ] Connections closed gracefully on error
- [ ] Error response sent before closing (if possible)

---

### [ ] 4.6: Makefile Verification & Compilation Standards
**Assignee:** Dev A  
**Description:**  
- Ensure Makefile meets all requirements:
  - Compiles without any warnings (even with -Wall -Wextra -Werror)
  - All .cpp files in src/ compiled recursively
  - Object files in .o or similar directory (not mixed with src/)
  - Executable: webserv in project root
  - `make clean`: removes .o files
  - `make fclean`: removes .o and webserv
  - `make re`: clean + all
  - No hardcoded paths, support relative paths
- Test on clean clone:
  - Clone repo
  - `make` - should compile without errors
  - `./webserv` - should start server
  - `make fclean` - cleanup
  - Verify no .o files remain

**DoD:**
- [ ] `make` compiles without warnings or errors
- [ ] `make clean` removes only .o files
- [ ] `make fclean` removes .o and executable
- [ ] `make re` clean + build works
- [ ] Executable is ./webserv
- [ ] Compiles on clean checkout without modification

---

### [ ] 4.7: Code Review & Documentation
**Assignee:** Dev B  
**Description:**  
- Add code comments for complex logic
- Document public class methods (parameters, return values)
- Comment non-obvious decision points (e.g., poll() timeout rationale)
- Review for:
  - Consistency of naming (camelCase vs snake_case)
  - Code duplication (refactor if > 3 occurrences)
  - Complexity (functions > 50 lines reviewed)
- Ensure README.md updated with build and usage info

**DoD:**
- [ ] All public methods documented
- [ ] Complex algorithms commented
- [ ] No copy-paste code duplications
- [ ] Naming conventions consistent
- [ ] README complete and accurate

---

## PHASE 5: Bonus Features (Optional - Scope TBD)

### [ ] 5.1: HTTP/1.1 Chunked Transfer Encoding
**Assignee:** Dev B  
**Description:**  
- Support chunked transfer encoding:
  - Parse Transfer-Encoding: chunked
  - Read chunks: size (hex) \r\n, data, \r\n
  - End marker: 0 \r\n \r\n
  - Useful for streaming or dynamic content
- Modify Request parser to handle chunked bodies

**DoD:**
- [ ] Parses chunked requests correctly
- [ ] Assembles full body from chunks
- [ ] Can stream responses with chunked encoding (optional)

---

### [ ] 5.2: Cookie & Session Management
**Assignee:** Dev B  
**Description:**  
- Parse Set-Cookie and Cookie headers
- Store session data (simple: username, login time)
- Validate session cookies on subsequent requests
- Example:
  - POST /login → Set-Cookie: sessionid=abc123
  - GET / with Cookie: sessionid=abc123 → recognize user
- Simple in-memory storage (no database)

**DoD:**
- [ ] Set-Cookie header set on login
- [ ] Cookie sent with subsequent requests
- [ ] Server recognizes session
- [ ] Session data persists across requests

---

### [ ] 5.3: Directory Listing (Automatic Index Generation)
**Assignee:** Dev C  
**Description:**  
- When no index file in directory: generate HTML listing
- List files and subdirectories with sizes
- Clickable links to navigate
- Optional: sorting by name, size, date

**DoD:**
- [ ] GET /uploads/ generates directory listing
- [ ] Links to files clickable
- [ ] File sizes displayed
- [ ] Proper HTML formatting

---

### [ ] 5.4: Compression (Gzip)
**Assignee:** Dev B  
**Description:**  
- Support Accept-Encoding: gzip
- Compress response bodies if client supports
- Add Content-Encoding: gzip header
- Use zlib library (or system gzip process)

**DoD:**
- [ ] Detects Accept-Encoding header
- [ ] Compresses if supported
- [ ] Adds Content-Encoding header
- [ ] Client can decompress response

---

### [ ] 5.5: HTTPS/SSL Support (Advanced Bonus)
**Assignee:** Dev A  
**Description:**  
- Add OpenSSL or similar for HTTPS
- Listen on 443 with TLS
- Generate self-signed certificate for testing
- Backward compatible: HTTP still works

**DoD:**
- [ ] Server listens on 443
- [ ] TLS handshake succeeds
- [ ] Browser accepts connection (with self-signed warning)
- [ ] HTTPS requests handled correctly

---

### [ ] 5.6: Load Balancing / Multiple Server Support
**Assignee:** Dev A  
**Description:**  
- Already in config (multiple server blocks)
- Verify all servers listen on different ports
- Route requests based on Host header to correct server
- Implement virtual hosting

**DoD:**
- [ ] Multiple server blocks in config parse
- [ ] Each server listens on configured port
- [ ] Host header routing works
- [ ] Stress test: multiple servers handle simultaneous requests

---

## Testing & Quality Checklist

### [ ] Unit Tests for Key Components (Optional but Recommended)
**Assignee:** Dev C  
**Description:**  
- Test Request parser with various inputs
- Test Config parser with valid/invalid configs
- Test Router URI matching
- Test static file handler path traversal prevention

**DoD:**
- [ ] All parser unit tests pass
- [ ] Edge cases covered
- [ ] Tests document expected behavior

---

### [ ] Integration Test Suite
**Assignee:** Dev C  
**Description:**  
- Bash script testing common scenarios:
  - `GET /index.html` → 200 with content
  - `GET /notfound` → 404
  - `POST /cgi-bin/script.py` with data → executes
  - `DELETE /file.txt` → 204 or 404
  - Concurrent requests
- Script generates report: pass/fail

**DoD:**
- [ ] All integration tests pass
- [ ] Test coverage includes happy path + error cases
- [ ] Can be run with `make test` or similar

---

### [ ] Final Verification (Before Submission)
**Assignee:** Dev A  
**Description:**  
- Checklist:
  - [ ] Code compiles with `make` (no warnings)
  - [ ] README.md complete and accurate
  - [ ] Makefile has all targets (all, clean, fclean, re)
  - [ ] No memory leaks (valgrind clean)
  - [ ] Server handles 50+ concurrent connections
  - [ ] All mandatory features working (GET, POST, DELETE, CGI)
  - [ ] No undefined behavior or segfaults
  - [ ] Project structure organized
  - [ ] Git history clean and meaningful commits

**DoD:**
- [ ] All checks pass
- [ ] No warnings or errors in compilation
- [ ] Ready for subject evaluation

---

## Notes & Constraints

### Critical Implementation Rules
1. **C++98 Only:** No C++11, C++17, lambdas, auto, nullptr. Use std::string, std::vector, std::map only from STL.
2. **No External Libraries:** Only standard C++ library and POSIX system calls. No boost, no external HTTP libraries.
3. **Non-Blocking I/O:** All socket operations must be non-blocking. Never use blocking read/write.
4. **Poll Loop:** Single poll() call per iteration checking all file descriptors. Both POLLIN and POLLOUT must be checked.
5. **Memory Management:** Valgrind clean. All allocations freed. No leaks under stress.
6. **HTTP/1.1 Compliance:** Follow HTTP/1.1 specification for response format, status codes, headers.
7. **Config Flexibility:** Support multiple server blocks, locations, error pages, method restrictions.

### Risk Areas
- **File Descriptor Limits:** Monitor with `ulimit -n` and `lsof`. Close idle connections.
- **Zombie Processes:** Use waitpid(pid, NULL, WNOHANG) after fork/exec.
- **Signal Safety:** Signal handlers must only set flags, not call unsafe functions.
- **Buffer Overflows:** Validate all input. Use size-checked operations.
- **Path Traversal:** Validate URI paths, prevent access outside root directory.

### Deliverables
1. **Source Code:** Well-organized src/, includes/ directories
2. **Makefile:** Compiles to webserv executable
3. **README.md:** Project documentation
4. **Config File:** example default.conf showing all features
5. **Git History:** Meaningful commits per feature

---

**Last Updated:** 2025-01-27  
**Status:** Kanban ready for team sprint planning
