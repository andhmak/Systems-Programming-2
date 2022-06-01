/* File: commonFuncs.h */

/* Closes file fd point to and calls perror in case of error, retrying if interrupted */
void close_report(int fd);

int safe_write_bytes(int fd, const char *buf, size_t count);