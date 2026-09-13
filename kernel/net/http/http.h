#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real hand-written HTTP/1.1 request/response layer (RFC 7230) - built on
// top of kernel/net/tcp/tcp.h's tcp_stream_*/kernel/net/tls/tls.h's tls_*
// (both share an identical open/send/receive/close shape). Replaces the
// old "hardcoded request string + first 4 response bytes == HTTP" checks
// shell/shell/commands/net.c's cmd_tcp/cmd_tlsfetch used before this item.

#define HTTP_MAX_HEADERS 16

typedef struct {
    const char* name;
    int name_len;
    const char* value;
    int value_len;
} http_header_t;

typedef struct {
    int status_code;
    http_header_t headers[HTTP_MAX_HEADERS];
    int header_count;
    const u8* body;
    int body_len;
} http_response_t;

// "METHOD path HTTP/1.1\r\nHost: host\r\nConnection: close\r\n\r\n" -
// returns the byte length written, or -1 if out_capacity is too small.
int http_build_request(char* out, int out_capacity, const char* method, const char* host, const char* path);

// Parses a raw HTTP response already sitting in `raw[0..raw_len)`.
// Handles both `Content-Length`-framed and `Transfer-Encoding: chunked`
// bodies (chunked is dechunked IN PLACE inside `raw` - `out->body` then
// points into `raw` at the dechunked bytes). Returns false if the status
// line is malformed or (for chunked) the final `0\r\n\r\n` chunk never
// arrived within `raw_len`.
bool http_parse_response(u8* raw, int raw_len, http_response_t* out);

// Real lookup, case-insensitive per RFC 7230 3.2 - returns NULL if absent.
const http_header_t* http_find_header(const http_response_t* resp, const char* name);

// Parses "http://host[:port]/path" or "https://host[:port]/path", resolves
// the host (literal IP or DNS), opens a real TCP or TLS connection
// (dispatched by scheme), sends a real GET request, reads until the peer
// closes, and parses the result into `out`. `response_buf`/`max_len` is
// the caller-owned scratch buffer `out`'s pointers end up pointing into -
// it must outlive `out`.
bool http_fetch(const char* url, u8* response_buf, u32 max_len, http_response_t* out);

// --- Server side ---

// Given a connection slot already returned by tcp_accept(), reads one real
// HTTP/1.1 request, real-parses its request line, and sends a real
// response: "/" -> 200 with a small live status body, anything else ->
// 404. Closes the connection itself either way. Returns false only on a
// genuine I/O failure (timeout/malformed request still get a real HTTP
// error response, not a dropped connection).
bool http_serve_request(int conn_slot);

#pragma GCC visibility pop
