/* File: serverCommunication.cpp */

#include <cstring>
#include <queue>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string>
#include "serverCommunication.h"
#include "serverTypes.h"
#include "commonFuncs.h"

extern int queue_size;  // maximum size of the tasks queue

extern std::queue<task> *tasks; // queue containing all current tasks

/* Variables for synchronisation */
extern pthread_mutex_t queue_lock;
extern pthread_cond_t cond_nonempty, cond_nonfull, cond_done;

/* Initialises a sock_info_t variable referring to the socket specified by void_t_socket_id */
int initialise_sock_info(sock_info_t &sock_info, void *void_t_socket_id) {

    /* Save socket id and free the argument */
    sock_info.sock_id = *(int*) void_t_socket_id;
    free(void_t_socket_id);

    /* Create socket-specific mutexes */
    if ((sock_info.lock_data_transfer = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t))) == NULL) {
        perror("dataServer: malloc");
        close_report(sock_info.sock_id);
        return -1;
    }
    pthread_mutex_init(sock_info.lock_data_transfer, 0);
    if ((sock_info.lock_tasks_remaining = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t))) == NULL) {
        perror("dataServer: malloc");
        free(sock_info.lock_data_transfer);
        close_report(sock_info.sock_id);
        return -1;
    }
    pthread_mutex_init(sock_info.lock_tasks_remaining, 0);

    /* Create counter for remaining tasks */
    if ((sock_info.tasks_remaining = (int *) malloc(sizeof(int))) == NULL) {
        perror("dataServer: malloc");
        free(sock_info.lock_data_transfer);
        free(sock_info.lock_tasks_remaining);
        close_report(sock_info.sock_id);
        return -1;
        pthread_exit(NULL);
    }
    *sock_info.tasks_remaining = 0;

    return 0;
}

/* Frees all data in the sock_info struct and closes the socket */
void free_socket(sock_info_t sock_info) {
    free(sock_info.lock_data_transfer);
    free(sock_info.lock_tasks_remaining);
    free(sock_info.tasks_remaining);
    close_report(sock_info.sock_id);
}

/* Recursively traverses the directory in path, creating a file transfer task bound for sock_info for each file, and put the task in the queue  */
int traverse_directory(std::string path, sock_info_t sock_info, int relative_path_size) {
    /* Open the directory */
    DIR *cur_dir;
    if ((cur_dir = opendir(path.data())) == NULL) {
        perror("dataServer: opendir");
        if (errno == EACCES) {
            return 0;
        }
        return -1;
    }

    /* For each file in the directory */
    struct dirent *cur_file;
    while ((cur_file = readdir(cur_dir)) != NULL) {
        /* If the file doesn't exist, or refers to the current or parent directories, ignore it */
        if ((cur_file->d_ino == 0) || !strcmp(cur_file->d_name, ".") || !strcmp(cur_file->d_name, "..")) {
            continue;
        }

        /* Check what the file is */
        std::string cur_path = path + "/" + cur_file->d_name;
        struct stat stat_buf;
        if (stat(cur_path.data(), &stat_buf) < 0) {
            perror("dataServer: stat");
            return -1;
        }

        /* If it is a regular file, make a task for it and put it in the queue */
        if ((stat_buf.st_mode & S_IFMT) == S_IFREG) {
            /* Increment remaining tasks */
            pthread_mutex_lock(sock_info.lock_tasks_remaining);
            (*sock_info.tasks_remaining)++;
            pthread_mutex_unlock(sock_info.lock_tasks_remaining);

            /* Make new task */
            task new_task;
            new_task.path = cur_path;
            new_task.relative_path_size = relative_path_size;
            new_task.file_size = htonl(stat_buf.st_size);
            new_task.sock_info = sock_info;

            /* Push it to the queue when there's space */
            pthread_mutex_lock(&queue_lock);
            while (tasks->size() >= queue_size) {
                pthread_cond_wait(&cond_nonfull, &queue_lock);
            }
            tasks->push(new_task);
            pthread_mutex_unlock(&queue_lock);
            pthread_cond_signal(&cond_nonempty);
        }

        /* If it is a directory, recursively call yourself on it */
        else if ((stat_buf.st_mode & S_IFMT) == S_IFDIR) {
            if (traverse_directory(cur_path, sock_info, relative_path_size) == -1) {
                return -1;
            }
        }
        /* Ignore everthing else */
    }

    /* Close the directory */
    closedir(cur_dir);

    /* Return successfully */
    return 0;
}

/* Function to be executed by communication threads, reading the request, creating the relevant tasks and adding them to the queue */
void *communication_thread(void *void_t_socket_id) {

    /* Initialise info about the socket */
    sock_info_t sock_info;
    if (initialise_sock_info(sock_info, void_t_socket_id) != 0) {
        pthread_exit(NULL);
    }
    
    /* Read path from client */
    int nread;
    char buf[50];
    int i;
    int relative_path_size = 0; // size of the relative path. without the final directory
    std::string path;           // path requested by client
    while (((nread = read(sock_info.sock_id, buf, 50)) > 0) || (errno == EINTR)) {
        /* After reading a block, process it character by character in memory */
        for (i = 0 ; i < nread ; i++) {
            /* Nul ends the message */
            if (buf[i] == '\0') {
                break;
            }
            /* Add character to path */
            path.push_back(buf[i]);
            /* Check for the final slash to know what part of the request only
            refers to the position of the directory and isn't to be transfered */
            if (buf[i] == '/') {
                relative_path_size = path.size();
            }
        }
        /* If break, break */
        if (i < nread) {
            break;
        }
    }
    /* If read failed, close the thread */
    if (nread == -1) {
        perror("dataServer: read from socket");
        free_socket(sock_info);
        pthread_exit(NULL);
    }
    /* If client closed connection, close the thread */
    if (nread == 0) {
        write(2, "dataServer: client closed socket\n", 33);
        free_socket(sock_info);
        pthread_exit(NULL);
    }

    /* Check if directory exists and is accessible */
    DIR *requested_dir;
    struct stat stat_buf;
    if ((requested_dir = opendir(path.data())) == NULL) {
        perror("dataServer: opendir");
        free_socket(sock_info);
        pthread_exit(NULL);
    }
    closedir_report(requested_dir);

    /* Traverse directory adding all files to tasks queue */
    if (traverse_directory(path, sock_info, relative_path_size) != 0) {
        free_socket(sock_info);
        exit(EXIT_FAILURE);
    }

    /* Wait for all tasks to end */
    pthread_mutex_lock(sock_info.lock_tasks_remaining);
    while ((*sock_info.tasks_remaining) > 0) {
        pthread_cond_wait(&cond_done, sock_info.lock_tasks_remaining);
    }
    pthread_mutex_unlock(sock_info.lock_tasks_remaining);

    /* Notify the client that we're done */
    if (safe_write_bytes(sock_info.sock_id, "", 1) < 0) {
        perror("dataServer: write to socket");
        free_socket(sock_info);
        pthread_exit(NULL);
    }
    
    /* Free socket and close thread */
    free_socket(sock_info);
    pthread_exit(NULL);
}