/* File: serverTypes.h */

#ifndef SERVER_TYPES
#define SERVER_TYPES
#include <string>

/* Struct holding everything a worker thread needs to know about a socket */
typedef struct {
    int sock_id;                            // id of the socket
    pthread_mutex_t *lock_data_transfer;    // mutex guarding data transfer to the socket
    pthread_mutex_t *lock_tasks_remaining;  // mutex guarding access to tasks_remaining
    int *tasks_remaining;                   // number of tasks still remaining on the socket
} sock_info_t;

/* Struct specifying a file transfer task in the queue */
typedef struct {
    int relative_path_size; // Length of the relative part to the requested folder, not including the folder itself
    std::string path;       // The path to the folder
    uint32_t file_size;     // The size of the file to be transfered
    sock_info_t sock_info;  // Information about the socket to which the file should be transfered
} task;

#endif