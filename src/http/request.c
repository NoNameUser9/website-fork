#include "http/request.h"

#include <assert.h>
#include <stdio.h>
#include <sys/socket.h>

#include "http/headermap.h"

#define BUF_CAP 8192

bool read_request_header_lines(int sd, StringBuilder *header, StringBuilder *body);
void request_trim_cr(StringView *sv);

bool http_request_parse(HttpRequest *request, int fd) {
    StringBuilder header = { .alloc = &request->alloc, 0 };

    if (!read_request_header_lines(fd, &header, &request->body)) return false;
    StringView sv = {
        .items = header.items,
        .count = header.count,
    };

    StringView status_line = sv_chop_by(&sv, '\n');
    request_trim_cr(&status_line);

    StringView method = sv_chop_by(&status_line, ' ');

    if (sv_eq_cstr(method, "GET")) {
        request->method = HTTP_GET;
    } else if (sv_eq_cstr(method, "POST")) {
        request->method = HTTP_POST;
    } else {
        return false;
    }

    request->resource_path = sv_dup(&request->alloc, sv_chop_by(&status_line, ' '));
    request->version = sv_dup(&request->alloc, sv_chop_by(&status_line, '\n'));

    StringView header_line;
    while (true) {
        header_line = sv_chop_by(&sv, '\n');

        if (header_line.count == 0) {
            break;
        }

        request_trim_cr(&header_line);

        StringView key = sv_dup(&request->alloc, sv_chop_by(&header_line, ':'));
        StringView value = sv_dup(&request->alloc, sv_slice_from(header_line, 1));
        http_headermap_insert(&request->headers, (HttpHeader) { key, value });
    }

    StringView resource_path = request->resource_path;
    StringBuilder sb = { .alloc = &request->alloc, 0 };
    StringView path = sv_chop_by(&resource_path, '?');
    if (!http_urldecode(path, &sb)) return false;
    request->path = sb_to_sv(sb);

    request->query_string = resource_path;

    return true;
}

// TODO: time limit
bool read_request_header_lines(int sd, StringBuilder *header, StringBuilder *body) {
    // TODO maybe it's not a good idea to allocate 8Kb at the stack for each request
    char buf[BUF_CAP] = {0};

    size_t count = 0;

    while (true) {
        ssize_t bytes = recv(sd, buf, BUF_CAP, 0);
        if (bytes < 0) {
            return false;
        }

        sb_append_buf(header, buf, bytes);
        sb_append_char(header, '\0');
        header->count--;

        const char *body_ptr = strstr(header->items + count, "\r\n\r\n");
        count = header->count;
        size_t newlines = 4;

        if (body_ptr == NULL) {
            body_ptr = strstr(header->items, "\n\n");
            newlines = 2;
        }

        if (body_ptr != NULL) {
            size_t header_size = (size_t) (body_ptr - header->items);
            sb_append_buf(body, body_ptr + newlines, header->count - header_size - newlines);
            header->count = header_size;
            return true;
        }
    }
}

void request_trim_cr(StringView *sv) {
    assert(sv->count > 0);
    if (sv->items[sv->count - 1] == '\r') sv->count--;
}

