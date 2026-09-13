// Real hand-written HTTP/1.1 client + server - see http.h's own comment
// for scope. Client dispatches over kernel/net/tcp/tcp.h's tcp_stream_*
// or kernel/net/tls/tls.h's tls_* by URL scheme; server sits on the
// existing (previously unused) tcp_listen/tcp_accept/tcp_server_*
// (kernel/net/tcp/tcp.h).

#include "http.h"
#include "../tcp/tcp.h"
#include "../tls/tls.h"
#include "../dns/dns.h"
#include "../../lib/strings.h"
#include "../../isr/isr.h"

static bool append_str(char* out, int* p, int cap, const char* s) {
    int i = 0;
    while (s[i] != '\0') {
        if (*p >= cap) {
            return false;
        }
        out[*p] = s[i];
        *p = *p + 1;
        i = i + 1;
    }
    return true;
}

static bool append_decimal(char* out, int* p, int cap, u64 value) {
    u8 tmp[20];
    int len = format_decimal(value, tmp);
    int i = 0;
    while (i < len) {
        if (*p >= cap) {
            return false;
        }
        out[*p] = (char) tmp[i];
        *p = *p + 1;
        i = i + 1;
    }
    return true;
}

int http_build_request(char* out, int out_capacity, const char* method, const char* host, const char* path) {
    int p = 0;
    bool ok = true;
    ok = ok && append_str(out, &p, out_capacity, method);
    ok = ok && append_str(out, &p, out_capacity, " ");
    ok = ok && append_str(out, &p, out_capacity, path);
    ok = ok && append_str(out, &p, out_capacity, " HTTP/1.1\r\nHost: ");
    ok = ok && append_str(out, &p, out_capacity, host);
    ok = ok && append_str(out, &p, out_capacity, "\r\nConnection: close\r\n\r\n");
    if (!ok) {
        return -1;
    }
    return p;
}

static bool parse_status_line(const u8* raw, int line_len, int* status_code_out) {
    int i = 0;
    while (i < line_len && raw[i] != ' ') {
        i = i + 1;
    }
    if (i >= line_len) {
        return false;
    }
    i = i + 1;
    int code = 0;
    int digits = 0;
    while (i < line_len && raw[i] >= '0' && raw[i] <= '9') {
        code = code * 10 + (raw[i] - '0');
        i = i + 1;
        digits = digits + 1;
    }
    if (digits != 3) {
        return false;
    }
    *status_code_out = code;
    return true;
}

static void parse_header_line(const u8* line, int line_len, http_header_t* h) {
    int colon = find_bytes(line, line_len, ":");
    if (colon < 0) {
        h->name = NULL;
        h->name_len = 0;
        h->value = NULL;
        h->value_len = 0;
        return;
    }
    h->name = (const char*) line;
    h->name_len = colon;
    int vstart = colon + 1;
    while (vstart < line_len && line[vstart] == ' ') {
        vstart = vstart + 1;
    }
    h->value = (const char*) &line[vstart];
    h->value_len = line_len - vstart;
}

const http_header_t* http_find_header(const http_response_t* resp, const char* name) {
    int i = 0;
    while (i < resp->header_count) {
        if (resp->headers[i].name != NULL && streq_ci_n(resp->headers[i].name, name, resp->headers[i].name_len)) {
            return &resp->headers[i];
        }
        i = i + 1;
    }
    return NULL;
}

bool http_parse_response(u8* raw, int raw_len, http_response_t* out) {
    out->header_count = 0;
    out->body = NULL;
    out->body_len = 0;

    int header_block_end = find_bytes(raw, raw_len, "\r\n\r\n");
    if (header_block_end < 0) {
        return false;
    }

    int status_line_len = find_bytes(raw, header_block_end, "\r\n");
    if (status_line_len < 0) {
        status_line_len = header_block_end;
    }
    if (!parse_status_line(raw, status_line_len, &out->status_code)) {
        return false;
    }

    int cursor = status_line_len + 2;
    while (cursor < header_block_end) {
        int remaining = header_block_end - cursor;
        int line_len = find_bytes(&raw[cursor], remaining, "\r\n");
        if (line_len < 0) {
            line_len = remaining;
        }
        if (line_len > 0 && out->header_count < HTTP_MAX_HEADERS) {
            parse_header_line(&raw[cursor], line_len, &out->headers[out->header_count]);
            out->header_count = out->header_count + 1;
        }
        cursor = cursor + line_len + 2;
    }

    int body_start = header_block_end + 4;
    int body_available = raw_len - body_start;
    if (body_available < 0) {
        body_available = 0;
    }

    const http_header_t* cl = http_find_header(out, "Content-Length");
    const http_header_t* te = http_find_header(out, "Transfer-Encoding");

    if (te != NULL && te->value_len >= 7 && streq_ci_n(te->value, "chunked", 7)) {
        u8* write_ptr = &raw[body_start];
        int read_pos = body_start;
        int write_len = 0;
        bool done = false;
        while (read_pos < raw_len && !done) {
            int remaining = raw_len - read_pos;
            int size_line_len = find_bytes(&raw[read_pos], remaining, "\r\n");
            if (size_line_len < 0) {
                return false;  // truncated - chunk-size line never completed
            }
            u32 chunk_size = 0;
            int i = 0;
            while (i < size_line_len && raw[read_pos + i] != ';') {
                u8 c = raw[read_pos + i];
                int digit;
                if (c >= '0' && c <= '9') {
                    digit = c - '0';
                } else if (c >= 'a' && c <= 'f') {
                    digit = c - 'a' + 10;
                } else if (c >= 'A' && c <= 'F') {
                    digit = c - 'A' + 10;
                } else {
                    break;
                }
                chunk_size = chunk_size * 16 + (u32) digit;
                i = i + 1;
            }
            read_pos = read_pos + size_line_len + 2;
            if (chunk_size == 0) {
                done = true;  // final chunk - trailers (if any) ignored
            } else {
                if (read_pos + (int) chunk_size + 2 > raw_len) {
                    return false;  // truncated - chunk data never fully arrived
                }
                int k = 0;
                while (k < (int) chunk_size) {
                    write_ptr[write_len + k] = raw[read_pos + k];
                    k = k + 1;
                }
                write_len = write_len + (int) chunk_size;
                read_pos = read_pos + (int) chunk_size + 2;
            }
        }
        if (!done) {
            return false;  // ran out of buffer before the final chunk arrived
        }
        out->body = write_ptr;
        out->body_len = write_len;
    } else if (cl != NULL) {
        int content_length = 0;
        int i = 0;
        while (i < cl->value_len && cl->value[i] >= '0' && cl->value[i] <= '9') {
            content_length = content_length * 10 + (cl->value[i] - '0');
            i = i + 1;
        }
        if (content_length > body_available) {
            content_length = body_available;
        }
        out->body = &raw[body_start];
        out->body_len = content_length;
    } else {
        out->body = &raw[body_start];
        out->body_len = body_available;
    }

    return true;
}

bool http_fetch(const char* url, u8* response_buf, u32 max_len, http_response_t* out) {
    bool use_tls;
    const char* rest;
    if (starts_with(url, "https://")) {
        use_tls = true;
        rest = &url[8];
    } else if (starts_with(url, "http://")) {
        use_tls = false;
        rest = &url[7];
    } else {
        return false;
    }

    char host[128];
    u16 port = use_tls ? 443 : 80;
    int i = 0;
    while (rest[i] != '\0' && rest[i] != '/' && rest[i] != ':' && i < 127) {
        host[i] = rest[i];
        i = i + 1;
    }
    host[i] = '\0';
    int cursor = i;
    if (rest[cursor] == ':') {
        cursor = cursor + 1;
        port = (u16) parse_decimal_u32(&rest[cursor]);
        while (rest[cursor] != '\0' && rest[cursor] != '/') {
            cursor = cursor + 1;
        }
    }

    char path[256];
    if (rest[cursor] == '\0') {
        path[0] = '/';
        path[1] = '\0';
    } else {
        int j = 0;
        while (rest[cursor] != '\0' && j < 255) {
            path[j] = rest[cursor];
            j = j + 1;
            cursor = cursor + 1;
        }
        path[j] = '\0';
    }

    u8 ip[4];
    if (!parse_ip(host, ip)) {
        if (!dns_resolve_a(host, ip)) {
            return false;
        }
    }

    char request[512];
    int request_len = http_build_request(request, (int) sizeof(request), "GET", host, path);
    if (request_len < 0) {
        return false;
    }

    u32 total_received = 0;
    bool sent;

    if (use_tls) {
        tls_conn_t conn;
        if (!tls_connect(ip, port, &conn)) {
            return false;
        }
        sent = tls_send(&conn, (const u8*) request, (u16) request_len);
        if (sent) {
            bool peer_open = true;
            while (peer_open && total_received < max_len) {
                u16 chunk_len;
                if (!tls_receive(&conn, &response_buf[total_received], (u16) (max_len - total_received), 3000, &chunk_len)) {
                    peer_open = false;
                } else {
                    total_received = total_received + chunk_len;
                }
            }
        }
        tls_close(&conn);
    } else {
        tcp_conn_t conn;
        if (!tcp_stream_open(ip, port, &conn)) {
            return false;
        }
        sent = tcp_stream_send(&conn, (const u8*) request, (u16) request_len);
        if (sent) {
            bool peer_open = true;
            while (peer_open && total_received < max_len) {
                u16 chunk_len;
                if (!tcp_stream_receive(&conn, &response_buf[total_received], (u16) (max_len - total_received), 3000, &chunk_len)) {
                    peer_open = false;
                } else {
                    total_received = total_received + chunk_len;
                }
            }
        }
        tcp_stream_close(&conn);
    }

    if (!sent || total_received == 0) {
        return false;
    }

    return http_parse_response(response_buf, (int) total_received, out);
}

// --- Server side ---

static int build_status_body(char* out, int cap) {
    int p = 0;
    bool ok = true;
    ok = ok && append_str(out, &p, cap, "minic-os HTTP server - uptime_ticks=");
    ok = ok && append_decimal(out, &p, cap, g_tick_count);
    ok = ok && append_str(out, &p, cap, "\n");
    if (!ok) {
        return 0;
    }
    return p;
}

bool http_serve_request(int conn_slot) {
    u8 buf[512];
    u32 total = 0;
    bool got_headers = false;
    while (total < sizeof(buf) - 1 && !got_headers) {
        u32 n = tcp_server_receive(conn_slot, &buf[total], (u32) (sizeof(buf) - 1 - total), 3000);
        if (n == 0) {
            break;
        }
        total = total + n;
        if (find_bytes(buf, (int) total, "\r\n\r\n") >= 0) {
            got_headers = true;
        }
    }
    if (!got_headers) {
        tcp_server_close(conn_slot);
        return false;
    }

    int line_len = find_bytes(buf, (int) total, "\r\n");
    if (line_len < 0) {
        line_len = (int) total;
    }
    char path[256];
    path[0] = '\0';
    int sp1 = find_bytes(buf, line_len, " ");
    if (sp1 >= 0) {
        int path_start = sp1 + 1;
        int sp2 = find_bytes(&buf[path_start], line_len - path_start, " ");
        int path_len = (sp2 >= 0) ? sp2 : (line_len - path_start);
        if (path_len > 255) {
            path_len = 255;
        }
        int i = 0;
        while (i < path_len) {
            path[i] = (char) buf[path_start + i];
            i = i + 1;
        }
        path[path_len] = '\0';
    }

    int status_code;
    const char* status_text;
    char body[128];
    int body_len;
    if (streq(path, "/")) {
        status_code = 200;
        status_text = "OK";
        body_len = build_status_body(body, (int) sizeof(body));
    } else {
        status_code = 404;
        status_text = "Not Found";
        const char* msg = "Not Found\n";
        body_len = strlen_(msg);
        int i = 0;
        while (i < body_len) {
            body[i] = msg[i];
            i = i + 1;
        }
    }

    char response[512];
    int p = 0;
    bool ok = true;
    ok = ok && append_str(response, &p, (int) sizeof(response), "HTTP/1.1 ");
    ok = ok && append_decimal(response, &p, (int) sizeof(response), (u64) status_code);
    ok = ok && append_str(response, &p, (int) sizeof(response), " ");
    ok = ok && append_str(response, &p, (int) sizeof(response), status_text);
    ok = ok && append_str(response, &p, (int) sizeof(response), "\r\nContent-Length: ");
    ok = ok && append_decimal(response, &p, (int) sizeof(response), (u64) body_len);
    ok = ok && append_str(response, &p, (int) sizeof(response), "\r\nConnection: close\r\n\r\n");
    int i = 0;
    while (ok && i < body_len) {
        if (p >= (int) sizeof(response)) {
            ok = false;
        } else {
            response[p] = body[i];
            p = p + 1;
            i = i + 1;
        }
    }

    if (ok) {
        tcp_server_send(conn_slot, (const u8*) response, (u16) p);
    }
    tcp_server_close(conn_slot);
    return ok;
}
