#ifndef NETWORK_H
#define NETWORK_H

#include <list>
#include <vector>
#include <event.h>
#include "httparser.h"

class Client {
public:
    static void on_read(int fd, short, void *arg);
    static void on_write(int fd, short, void *arg);
    static void on_accept(int fd, short, void *);
    void write(const Buffer &buff);
    void close_connection();

private:
    event ev_read;
    event ev_write;
    std::vector<char> buf_write;
    std::list<Client>::iterator it_this;
    HTTParser httparser;
    bool closed = false;
};

#endif // NETWORK_H
