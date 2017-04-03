#include <getopt.h>
#include <stdlib.h>
#include <string>
#include "config.h"

int start_network();

int main(int argc, char *argv[])
{
    int opt;
    while ((opt = getopt(argc, argv, "r:c:")) != -1) {
        switch (opt) {
        case 'r':
            BASE_PATH = std::string(optarg);
            if (!BASE_PATH.empty() && BASE_PATH.back() != '/')
                BASE_PATH += '/';
            break;
        case 'c':
            NCPU = atoi(optarg);
            if (NCPU <= 0)
                NCPU = 1;
            break;
        default:
            break;
        }
    }
    start_network();
}
