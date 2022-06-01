/* File: commonFuncs.h */
#include <dirent.h>

/* Closes file fd points to and calls perror in case of error, retrying if interrupted */
void close_report(int fd);

/* Closes directory fd points to and calls perror in case of error, retrying if interrupted */
void closedir_report(DIR *dir);

int safe_write_bytes(int fd, const char *buf, size_t count);