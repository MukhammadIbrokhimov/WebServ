#pragma once

#include <string>
#include <map>
#include <cstddef>
#include <ctime>

// Request/Response live here per the phase 0.2 layout -- Response joins in
// task 2.2.
//
// Request is an incremental HTTP/1.1 request parser. TCP hands us a byte
// stream with no message boundaries: recv() may deliver a request whole,
// split at any byte, or glued to the next one. So we eat fragments through
// parseFromBuffer() and remember where we were between calls.
//
// Deliberately I/O-free: no sockets, no Logger, no exceptions for bad input.
// A garbage request is normal network weather, not an exceptional condition
// -- we record an HTTP status code instead (4.5 turns it into a response).
// The subject's poll/errno rules never apply here because the server loop
// does the recv()ing; we only ever see bytes. Also what makes the unit
// tests a two-file build.

class Request {
	public:
		Request();

		// Feed raw bytes from the connection. Returns how many bytes of
		// `data` THIS request consumed. Consumption stops at completion --
		// leftover bytes belong to the next request on a keep-alive
		// connection (2.5 re-feeds them to a fresh Request). In a terminal
		// state (complete/error) nothing is consumed: returns 0.
		std::size_t parseFromBuffer(const std::string& data);

		bool isComplete() const;
		bool hasError() const;
		int  getErrorCode() const;   // 400/414/431/505, 0 when healthy

		const std::string& getMethod() const;   // "GET"
		const std::string& getUri() const;      // raw "/a?b=c"
		const std::string& getPath() const;     // "/a"
		const std::string& getQuery() const;    // "b=c"
		const std::string& getVersion() const;  // "HTTP/1.1"
		const std::string& getBody() const;     // "" until 3.1 adds bodies

		// Case-insensitive lookup (HoSt == HOST). Returns "" when absent,
		// which is also a legal value -- hasHeader() tells those apart.
		// By-value return because there's no stored string to reference
		// when the header is missing.
		std::string getHeader(const std::string& name) const;
		bool        hasHeader(const std::string& name) const;

		// Recycle for the next request on a keep-alive connection.
		void reset();

	private:
		// The whole trick of this class. Lines are consumed the moment
		// they're complete, so no TCP split point can change the outcome:
		//
		//   S_REQUEST_LINE --"GET / HTTP/1.1"--> S_HEADERS
		//   S_HEADERS --"Name: value"--> S_HEADERS
		//   S_HEADERS --blank line--> S_COMPLETE
		//   malformed anything, from any state --> S_ERROR
		//
		// S_COMPLETE/S_ERROR are terminal. 3.1 slots S_BODY in after the
		// blank line, 5.1 adds chunk states. Same chassis, more states.
		enum ParseState {
			S_REQUEST_LINE,
			S_HEADERS,
			S_COMPLETE,
			S_ERROR
		};

		ParseState  state_;
		std::string buf_;         // only ever holds the current partial line
		int         error_code_;
		std::size_t header_count_;

		std::string method_;
		std::string uri_;
		std::string path_;
		std::string query_;
		std::string version_;
		std::string body_;
		std::map<std::string, std::string> headers_;   // keys lowercased

		// one grammar production per state, mirroring the machine
		void parseRequestLine(const std::string& line);
		void parseHeaderLine(const std::string& line);
		void setError(int code);
};

// Response is the mirror of Request: where Request decodes a byte stream
// into fields, Response builds fields into a byte stream. serialize() is the
// only thing the server loop (2.5) ever needs -- it hands the bytes to the
// write buffer.
//
// I keep it I/O-free for the same reason Request is: no sockets, no Logger.
// A handler fills in the status/headers/body and serialize() turns it into
// "HTTP/1.1 ...\r\n...\r\n\r\nbody". That also makes the unit test a
// dependency-free two-file build, like the request tests.
//
// A fresh Response is already a valid empty 200 OK -- so a GET handler can
// just setBody() and serialize() without ceremony. Content-Length and Date
// are filled in at serialize() time so they can't drift out of sync with the
// body; the caller never has to remember them.

class Response {
	public:
		Response();

		// Status line pieces. The one-arg form looks the reason phrase up
		// (setStatusCode(404) -> "Not Found") because every error site in
		// 2.3/4.5 would otherwise hand-type the same strings. The two-arg
		// form is the escape hatch for a code we don't name, or a CGI
		// script that dictates its own reason.
		void setStatusCode(int code);
		void setStatusCode(int code, const std::string& reason);

		// Header names are case-insensitive (RFC 7230 3.2), so I key them
		// lowercased like Request does and re-canonicalize on output. That
		// means setHeader("content-type", ...) and a later
		// setHeader("Content-Type", ...) are the SAME header -- the second
		// overwrites, no accidental duplicate.
		void setHeader(const std::string& name, const std::string& value);

		void setBody(const std::string& body);

		// Always the real body length -- serialize() trusts this over
		// anything the caller may have set, so a wrong Content-Length can't
		// desync the framing (a classic smuggling bug).
		std::size_t getContentLength() const;

		// The whole message as one string. const because building bytes
		// shouldn't mutate the object; the auto Date/Content-Length are
		// merged into a local copy of the headers, not stored back.
		std::string serialize() const;

		// RFC 1123 date ("Sun, 06 Nov 1994 08:49:37 GMT"). Static and takes
		// the time explicitly so it's testable without freezing the clock --
		// serialize() feeds it time(NULL). Built by hand instead of
		// strftime("%a"/"%b") because those are locale-dependent and HTTP
		// dates MUST be the C locale's English abbreviations.
		static std::string httpDate(std::time_t t);

		// Canonical reason phrase for the codes this server actually emits,
		// "" for anything we don't name. Kept as a table so the status line
		// and 4.5's error pages read from one source of truth.
		static std::string reasonPhrase(int code);

	private:
		int         status_;
		std::string reason_;
		std::string body_;
		std::map<std::string, std::string> headers_;   // keys lowercased
};
