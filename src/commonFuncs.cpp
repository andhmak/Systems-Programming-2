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

/* Writes count bytes from buf to fd.
   Doesn't return until either count bytes are written or there's an error.
   Returns 0 in case of success and -1 in case of failure. */
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