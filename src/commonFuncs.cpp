/* File: commonFuncs.cpp */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include "commonFuncs.h"

/* Closes file fd points to and calls perror in case of error, retrying if interrupted */
void close_report(int fd) {
    while (close(fd) == -1) {
        if (errno != EINTR) {
            perror("close");
            break;
        }
    }
}

/* Closes directory fd points to and calls perror in case of error, retrying if interrupted */
void closedir_report(DIR *dir) {
    while (closedir(dir) == -1) {
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