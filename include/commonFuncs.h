/* File: commonFuncs.h */
#include <dirent.h>

/* Closes file fd points to and calls perror in case of error, retrying if interrupted */
void close_report(int fd);

/* Closes directory fd points to and calls perror in case of error, retrying if interrupted */
void closedir_report(DIR *dir);

/* Writes count bytes from buf to fd.
   Doesn't return until either count bytes are written or there's an error.
   Returns 0 in case of success and -1 in case of failure. */
int safe_write_bytes(int fd, const char *buf, size_t count);