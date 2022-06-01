/* File: commonFuncs.cpp */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include "commonFuncs.h"

void close_report(int fd) {
    while (close(fd) == -1) {
        if (errno != EINTR) {
            perror("close");
            break;
        }
    }
}

int safe_write_bytes(int fd, const char *buf, size_t count) {
    int written;
    while ((written = write(fd, buf, count)) < count) {
        if ((written < 0) && (errno != EINTR)) {
            return -1;
        }
        buf += count;
        count -= written;
    }
    return 0;
}