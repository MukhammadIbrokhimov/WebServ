# WebServ

A lightweight HTTP/1.1 web server implemented in C++98 with non-blocking I/O, supporting multiple server configurations, static file serving, and CGI execution.

## Overview

WebServ is a custom HTTP server built from scratch without external libraries. It uses `poll()` for efficient non-blocking event handling, allowing concurrent client connections with minimal resource overhead.

**Key Features:**
- Non-blocking I/O with `poll()` event loop
- HTTP/1.1 protocol compliance
- Static file serving with directory listings
- File upload handling (multipart form data)
- CGI script execution
- Multiple server configurations
- Connection keep-alive support
- Configurable error pages

**Supported HTTP Methods:**
- `GET` - Retrieve static files and directory listings
- `POST` - Submit data and handle file uploads
- `DELETE` - Remove files (configurable per location)

## Build Instructions

### Prerequisites
- C++ compiler with C++98 support (g++, clang++)
- Standard POSIX/Unix environment (macOS, Linux)
- Make

### Compilation

```bash
# Build the executable
make

# Remove object files
make clean

# Remove all generated files
make fclean

# Rebuild from scratch
make re
```

The executable `webserv` will be created in the project root.

## Usage

### Basic Startup

```bash
# Start server with default configuration (default.conf)
./webserv

# Start server with custom configuration
./webserv custom.conf
```

The server will listen on ports defined in the configuration file and accept incoming connections.

### Graceful Shutdown

Press `Ctrl+C` to gracefully shut down the server. All active connections are closed cleanly.

## Configuration

WebServ uses nginx-like configuration files. See `config/default.conf` for examples.

### Basic Config Syntax

```nginx
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

### Configuration Directives

| Directive | Example | Description |
|-----------|---------|-------------|
| `listen` | `8080` | Port to listen on |
| `server_name` | `localhost` | Server name for virtual hosting |
| `root` | `./www` | Root directory for static files |
| `client_body_limit` | `10000` | Max request body size in bytes |
| `location` | `/ { ... }` | Configure a URI path |
| `allowed_methods` | `GET POST DELETE` | HTTP methods allowed |
| `index` | `index.html` | Default file for directories |
| `error_page` | `404 /404.html` | Custom error page |

### Multiple Servers

Define multiple `server` blocks for different ports:

```nginx
server {
  listen 8080;
  server_name localhost;
  root ./www;
}

server {
  listen 8081;
  server_name api.local;
  root ./api;
}
```

## Project Structure

```
WebServ/
├── src/              # Source files
│   ├── main.cpp
│   ├── socket/       # Socket wrapper
│   ├── http/         # HTTP parsing and responses
│   ├── server/       # Main event loop
│   ├── config/       # Configuration parser
│   ├── logger/       # Logging utilities
│   └── cgi/          # CGI execution
├── includes/         # Header files
├── config/           # Configuration examples
├── www/              # Static file root
└── Makefile
```

## Examples

### Serving Static Files

```bash
# Request index.html from root
curl http://localhost:8080/

# Request a specific file
curl http://localhost:8080/style.css
```

### File Upload

```bash
# Upload a file to /upload location
curl -F "file=@document.pdf" http://localhost:8080/upload
```

### CGI Execution

Configure a location for CGI scripts:

```nginx
location /cgi-bin {
  allowed_methods GET POST;
}
```

Then execute:

```bash
curl http://localhost:8080/cgi-bin/script.py\?param\=value
```

## Compiler Standards

The project compiles with strict flags ensuring code quality:

```
-Wall -Wextra -Werror -std=c++98
```

No warnings or external libraries are used.

## HTTP Error Codes

WebServ handles common HTTP errors:

- `400` - Bad Request
- `403` - Forbidden
- `404` - Not Found
- `405` - Method Not Allowed
- `413` - Payload Too Large
- `500` - Internal Server Error

Custom error pages can be configured in the config file.

## Troubleshooting

### Port Already in Use
Change the port in the config file or wait for the OS to release it.

### Permission Denied
Ensure files and directories are readable:
```bash
chmod -R 644 www/
chmod 755 www/
```

### CGI Script Not Executing
Ensure the script is executable:
```bash
chmod +x path/to/script.py
```

## Authors

WebServ Team (42 School)
