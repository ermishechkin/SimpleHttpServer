#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>

#include "httparser.h"
#include "network.h"
#include "config.h"

bool has_endline(Buffer &buffer, std::string &line)
{
    if (!line.empty() && line.back() == '\r' &&
        buffer.size > 0 && buffer.data[0] == '\n') {
        buffer.data++;
        buffer.size--;
        line.resize(line.size()-1);
        return true;
    }
    for (size_t i = 0; i+1 < buffer.size; i++) {
        if (buffer.data[i] == '\r' && buffer.data[i+1] == '\n') {
            buffer.data[i] = '\0';
            line.insert(line.end(), buffer.data, buffer.data+i);
            buffer.data = buffer.data + i + 2;
            buffer.size -= i + 2;
            return true;
        }
    }
    line.insert(line.end(), buffer.data, buffer.data+buffer.size);
    return false;
}


const char *read_word(const char *str, char det, std::string &res)
{
    size_t begin = 0;
    size_t end = 0;
    const char *c = str;

    while (*c != '\0' && isspace(*c))
        c++;
    if (*c == 0)
        return nullptr;
    begin = c - str;

    while (*c != '\0' && *c != det && !isspace(*c)) {
        c++;
    }
    end = c - str;
    bool det_found = *c == det;
    if (det_found && *c != '\0')
        c++;

    if (!det_found) {
        while (*c != '\0' && *c != det && isspace(*c))
            c++;
        if (*c == det && *c != '\0')
            c++;
    }

    res = std::string(str+begin, end-begin);
    return c;
}

const char *path_parse(const char *path)
{
    char *res = (char *)malloc(strlen(path)+1);
    char *c = res;
    while (*path) {
        if (*path == '%' && isxdigit(path[1]) && isxdigit(path[2])) {
            *c++ =
                ((path[1] > '9') ? (path[1] &~ 0x20) - 'A' + 10: (path[1] - '0')) * 16 +
                ((path[2] > '9') ? (path[2] &~ 0x20) - 'A' + 10: (path[2] - '0'));
            path += 3;
        }
        else
            *c++ = *path++;
        if (*path == '?')
            break;
    }
    *c = '\0';
    return res;
}

const char *file_extension(const char *path)
{
    const char *res = nullptr;
    while (*path) {
        if (*path == '/')
            res = nullptr;
        else if (*path == '.')
            res = path + 1;
        path++;
    }
    return res;
}

struct {
    const char *extension;
    const char *mime;
} mime_types[] = {
    {"html", "text/html"},
    {"css", "text/css"},
    {"js", "text/javascript"},
    {"jpg", "image/jpeg"},
    {"jpeg", "image/jpeg"},
    {"png", "image/png"},
    {"gif", "image/gif"},
    {"swf", "application/x-shockwave-flash"},
};

const char *mime_type(const char *ext)
{
    for (size_t i = 0; i < sizeof(mime_types)/sizeof(mime_types[0]); i++) {
        if (strcmp(ext, mime_types[i].extension) == 0)
            return mime_types[i].mime;
    }
    return "application/octet-stream";
}

std::string get_http_time()
{
    std::string res;
    char *buf = (char *)malloc(256);
    time_t now = time(0);
    struct tm tm = *gmtime(&now);
    strftime(buf, 256, "%a, %d %b %Y %H:%M:%S %Z", &tm);
    res = std::string(buf);
    free(buf);
    return res;
}

bool parse_request_line(const std::string &str, std::string &method,
                        std::string &path, std::string &ver)
{
    const char *t;
    if (!(t = read_word(str.c_str(), ' ', method)))
        return false;
    if (!(t = read_word(t, ' ', path)))
        return false;
    if (!(t = read_word(t, '\0', ver)))
        return false;
    return true;
}

bool parse_header(const std::string &header, std::string &key, std::string &value)
{
    const char *t;
    if (!(t = read_word(header.c_str(), ':', key)))
        return false;
    if (!(t = read_word(t, '\0', value)))
        return false;
    return true;
}

HTTParser::HTTParser()
{
    cur_handler = &HTTParser::read_request_line;
    send_handler = nullptr;
}

void HTTParser::on_data(Client *client, Buffer &buffer)
{
    if (cur_handler)
        (this->*cur_handler)(client, buffer);
}

void HTTParser::on_ready(Client *client)
{
    if (send_handler)
        (this->*send_handler)(client);
}

void HTTParser::read_request_line(Client *client, Buffer &buffer)
{
    if (has_endline(buffer, cur_line)) {
        std::string version;
        parse_request_line(cur_line, method, path, version);
        cur_line.clear();
        cur_handler = &HTTParser::read_headers;
        read_headers(client, buffer);
    }
}

void HTTParser::read_headers(Client *client, Buffer &buffer)
{
    while (has_endline(buffer, cur_line)) {
        if (cur_line.empty()) {
            cur_handler = nullptr;
            finish_request(client);
            return;
        }
        else {
            std::string key, value;
            if (parse_header(cur_line, key, value))
                headers.insert(std::make_pair(key, value));
            cur_line.clear();
        }
    }
}

void HTTParser::finish_request(Client *client, bool check_index)
{
    #define check(COND) do {if (!COND) { \
        if (!check_index) send_error(client, 404, "Not found"); \
        else send_error(client, 403, "Forbidden"); \
        return;} } while (0)
    bool need_body;
    if (method == "GET")
        need_body = true;
    else if (method == "HEAD")
        need_body = false;
    else {
        send_error(client, 405, "Method Not Allowed");
        return;
    }

    std::string temp0 = BASE_PATH + path;
    const char *temp = path_parse(temp0.c_str());
    char *rpath = realpath(temp, nullptr);
    free((void *)temp);
    check(rpath);
    check(strncmp(rpath, BASE_PATH.c_str(), BASE_PATH.length()) == 0);

    struct stat sb;
    stat(rpath, &sb);
    if (S_ISREG(sb.st_mode)) {
        output_file = fopen(rpath, "rb");
        const char *mime = mime_type(file_extension(rpath));
        free(rpath); rpath = nullptr;

        check(output_file);
        send_status(client, 200, "OK");
        send_header(client, "Content-Length", std::to_string(sb.st_size));
        send_header(client, "Content-Type", mime);
        send_raw(client, "\r\n", 2);
        if (need_body) {
            send_handler = &HTTParser::send_content;
            send_content(client);
        }
        else
            client->close_connection();
    }
    else if (S_ISDIR(sb.st_mode) && !check_index) {
        path += "/index.html";
        finish_request(client, true);
        free(rpath); rpath = nullptr;
    }
    else {
        check(false);
        free(rpath); rpath = nullptr;
    }
}

void HTTParser::send_content(Client *client)
{
    const size_t n = 10240;
    char data[n];
    size_t len;
    if ((len = fread(data, 1, n, output_file)) > 0) {
        Buffer res { .data = data, .size = len };
        client->write(res);
    }
    if (len < n) {
        fclose(output_file);
        output_file = NULL;
        send_handler = nullptr;
        client->close_connection();
    }
}

void HTTParser::send_error(Client *client, int code, const char *description)
{
    send_status(client, code, description);
    client->close_connection();
}

void HTTParser::send_status(Client *client, int code, const char *description)
{
    std::string res = std::string("HTTP/1.0 ") + std::to_string(code) +
            ' ' + description + "\r\n";
    Buffer buf = {.data = (char *)res.c_str(), .size = res.length()};
    client->write(buf);
    send_header(client, "Server", "SimpleHttpServer");
    send_header(client, "Connection", "Close");
    send_header(client, "Date", get_http_time());
}

void HTTParser::send_header(Client *client, const std::string &key, const std::string &value)
{
    std::string res = key + ": " + value + "\r\n";
    Buffer buf = {.data = (char *)res.c_str(), .size = res.length()};
    client->write(buf);
}

void HTTParser::send_raw(Client *client, const char *data, size_t len)
{
    client->write(Buffer{.data = (char *)data, .size = len});
}
