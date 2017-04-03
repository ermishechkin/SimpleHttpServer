#ifndef HTTPARSER_H
#define HTTPARSER_H

#include <map>
#include <string>

struct Buffer {
    char *data;
    size_t size;
};

class Client;


class HTTParser
{
public:
    HTTParser();
    void on_data(Client *client, Buffer &buffer);
    void on_ready(Client *client);

private:
    typedef void (HTTParser::*Handler)(Client *client, Buffer &buffer);
    typedef void (HTTParser::*SendHandler)(Client *client);
    Handler cur_handler;
    SendHandler send_handler;
    std::string cur_line;
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    void read_request_line(Client *, Buffer &);
    void read_headers(Client *, Buffer &);
    void finish_request(Client *, bool check_index=false);
    void send_content(Client *);
    void send_error(Client *, int code, const char *description);

    FILE *output_file;
    void send_status(Client *client, int code, const char *description);
    void send_header(Client *client, const std::string &key, const std::string &value);
    void send_raw(Client *client, const char *data, size_t len);
};

#endif // HTTPARSER_H
