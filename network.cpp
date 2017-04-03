#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <string.h>

#include "config.h"
#include "network.h"
#include "httparser.h"

std::list<Client> clients;
typedef std::list<Client>::iterator client_it_t;

struct event_base *base;
unsigned short listen_port = 80;

int setnonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        return -1;
    }
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) < 0) {
        return -2;
    }
    return 0;
}

template <typename T> class keks;

void Client::on_read(int fd, short, void *arg)
{
    #define BUF_SIZE 4096

    Client &client = *(Client *)arg;
    char buffer[BUF_SIZE];
    while (1) {
        Buffer buff_data { .data = buffer, .size = BUF_SIZE };
        int len = read(fd, buffer, buff_data.size);
        if (len == 0) {
            event_del(&client.ev_read);
            close(fd);
            clients.erase(client.it_this);
            return;
        }
        else if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            else {
                event_del(&client.ev_read);
                close(fd);
                clients.erase(client.it_this);
                return;
            }
        }
        buff_data.size = len;
        client.httparser.on_data(&client, buff_data);
    }
    event_add(&client.ev_write, NULL);
}

void Client::on_write(int fd, short, void *arg)
{
    Client &client = *(Client *)arg;
    std::vector<char> &buf_write = client.buf_write;
    int len;
    int count;
    if (buf_write.empty()) {
        return;
    }
    len = buf_write.size();
    count = ::write(fd, &buf_write.front(), len);
    if (count == -1) {
        if (errno == EINTR || errno == EAGAIN) {
            event_add(&client.ev_write, NULL);
            return;
        }
        else {
            return;
        }
    }
    else if (count < len) {
        buf_write = std::vector<char>(&buf_write.front()+count, &buf_write.front()+len-count);
        event_add(&client.ev_write, NULL);
    }
    else {
        buf_write.clear();
        client.httparser.on_ready(&client);
    }
    if (client.buf_write.empty() && client.closed) {
        event_del(&client.ev_read);
        event_del(&client.ev_write);
        close(fd);
        clients.erase(client.it_this);
    }
}

void Client::on_accept(int fd, short, void *)
{
    size_t ll = clients.size();
    ll = ll+1;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        return;
    }

    if (setnonblocking(client_fd) < 0) {
        return;
    }

    clients.push_front({});
    client_it_t client = clients.begin();
    client->it_this = client;

    Client *client_ptr = &*client;
    client_ptr = &*client;
    event_set(&client->ev_read, client_fd, EV_READ|EV_PERSIST, on_read, client_ptr);
    event_base_set(base, &client->ev_read);
    event_add(&client->ev_read, NULL);
    event_set(&client->ev_write, client_fd, EV_WRITE, on_write, client_ptr);
    event_base_set(base, &client->ev_write);
}

void Client::write(const Buffer &buff)
{
    if (buff.size == 0)
        return;
    buf_write.insert(buf_write.end(), buff.data, buff.data+buff.size);
    event_add(&ev_write, NULL);
}

void Client::close_connection()
{
    closed = true;
}

int create_listen_socket()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    int reuse_addr_on = 1;
    struct sockaddr_in listen_addr;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr_on, sizeof(reuse_addr_on)) == -1) {
        return -2;
    }
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port = htons(listen_port);
    if (bind(fd, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        return -3;
    }
    if (listen(fd, 100) < 0)  {
        return -4;
    }
    if (setnonblocking(fd) < 0) {
        return -5;
    }
    return fd;
}

int start_network()
{
    int listen_fd;
    struct event ev_accept;

    base = event_base_new();

    if ((listen_fd = create_listen_socket()) < 0) {
        return -1;
    }


    event_set(&ev_accept, listen_fd, EV_READ|EV_PERSIST, Client::on_accept, NULL);
    event_base_set(base, &ev_accept);
    event_add(&ev_accept, NULL);

    int i;
    for (i=0; i < NCPU; i++)
        if (fork() == 0) {
            event_reinit(base);
            break;
        }
    if (i == NCPU) {
        while (true)
            sleep(10);
    }
    event_base_dispatch(base);

    return 0;
}
