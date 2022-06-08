/* File: serverCommunication.cpp */

#include <cstring>
#include <queue>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <dirent.h>
#include "serverCommunication.h"
#include "commonFuncs.h"

/* Default starting path relative to the directory where the server is executed */
#define PATH "./"

extern int queue_size;  // maximum size of the tasks queue

extern std::queue<task> *tasks; // queue containing all current tasks

/* Variables for synchronisation */
extern pthread_mutex_t queue_lock;
extern pthread_cond_t cond_nonempty, cond_nonfull, cond_done;

/* Frees all data in the sock_info struct and closes the socket */
void free_socket(sock_info_t sock_info) {
    free(sock_info.lock_data_transfer);
    free(sock_info.lock_tasks_remaining);
    free(sock_info.tasks_remaining);
    close_report(sock_info.sock_id);
}

/* Recursively traverses the directory in path, creating a file transfer task bound for sock_info for each file, and put the task in the queue  */
int traverse_directory(std::string path, sock_info_t sock_info, int relative_path_size) {
    //printf("Communication locks %d, %d, %d\n", sock_info.lock_data_transfer, sock_info.lock_tasks_remaining, sock_info.tasks_remaining);
    //fflush(stdout);
    /* Open the directory */
    DIR *cur_dir;
    if ((cur_dir = opendir(path.data())) == NULL) {
        perror("dataServer: opendir");
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
            //write(1, "Communication found new file\n", 30);
            //write(1, "Communication trying to acquire tasks_remaining lock\n", 54);
            pthread_mutex_lock(sock_info.lock_tasks_remaining);
            (*sock_info.tasks_remaining)++;
            //write(1, "Communication unlocking tasks_remaining lock\n", 46);
            pthread_mutex_unlock(sock_info.lock_tasks_remaining);

            /* Make new task */
            task new_task;
            new_task.path = cur_path;
            new_task.relative_path_size = relative_path_size;
            new_task.file_size = htonl(stat_buf.st_size);
            new_task.sock_info = sock_info;
            /* Push it to the queue when there's space */
            //write(1, "Communication trying to acquire queue lock\n", 44);
            pthread_mutex_lock(&queue_lock);
            while (tasks->size() >= queue_size) {
                //write(1, "Communication waiting for queue to have space...\n", 50);
                pthread_cond_wait(&cond_nonfull, &queue_lock);
            }
            //write(1, "Communication found space\n", 27);
            tasks->push(new_task);
            //write(1, "Communication unlocking queue lock\n", 36);
            pthread_mutex_unlock(&queue_lock);
            //write(1, "Communication signaling nonempty\n", 34);
            pthread_cond_signal(&cond_nonempty);
        }
        /* If it is a directory, recursively call yourself on it */
        else if ((stat_buf.st_mode & S_IFMT) == S_IFDIR) {
            traverse_directory(cur_path, sock_info, relative_path_size);
        }
        /* Ignore everthing else */
    }

    /* Close the directory */
    closedir(cur_dir);

    /* Return successfully */
    return 0;
}

void *communication_thread(void *void_t_socket) {
    //write(1, "Communication starting\n", 23);
    /* Initialise info about the socket */
    sock_info_t sock_info;
    /* Save socket id and free the argument */
    sock_info.sock_id = *(int*) void_t_socket;
    free(void_t_socket);
    /* Create socket-specific mutexes */
    if ((sock_info.lock_data_transfer = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t))) == NULL) {
        perror("dataServer: malloc");
        close_report(sock_info.sock_id);
        pthread_exit(NULL);
    }
    pthread_mutex_init(sock_info.lock_data_transfer, 0);
    if ((sock_info.lock_tasks_remaining = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t))) == NULL) {
        perror("dataServer: malloc");
        free(sock_info.lock_data_transfer);
        close_report(sock_info.sock_id);
        pthread_exit(NULL);
    }
    pthread_mutex_init(sock_info.lock_tasks_remaining, 0);
    /* Create counter for remaining tasks */
    if ((sock_info.tasks_remaining = (int *) malloc(sizeof(int))) == NULL) {
        perror("dataServer: malloc");
        free(sock_info.lock_data_transfer);
        free(sock_info.lock_tasks_remaining);
        close_report(sock_info.sock_id);
        pthread_exit(NULL);
    }
    *sock_info.tasks_remaining = 0;
    /* Read path from client */
    int nread;
    char buf[50];
    uint32_t path_size = 0;     // size of the path sent by the client in bytes
    int relative_path_size = 0; // size of the relative path. without the final directory
    std::string data_read;      // data from socket passed to memory
    char state = 0;             // specifying how the current bytes read should be interpreted
    while (((nread = read(sock_info.sock_id, buf, 50)) > 0) || (errno == EINTR)) {
        /* After reading a block, process it character by character in memory */
        for (int i = 0 ; i < nread ; i++) {
            /* Add byte to the read data */
            data_read.push_back(buf[i]);
            /* If reading path size */
            if (state <= (sizeof(uint32_t) - 1)) {
                //printf("reading\n");
                //fflush(stdout);
                state++;
                /* If finished reading path size */
                if (state == sizeof(uint32_t)) {
                    /* Transform it */
                    memcpy(&path_size, data_read.data(), sizeof(uint32_t));
                    path_size = ntohl(path_size);
                    //printf("path_size: %d\n", path_size);
                    //fflush(stdout);
                    /* Erase data read */
                    data_read.erase();
                }
            }
            /* If reading the path */
            else {
                //printf("%c", buf[i]);
                //fflush(stdout);
                /* Check for the final slash to know what part of the request only
                   refers to the position of the directory and isn't to be transfered */
                if (buf[i] == '/') {
                    relative_path_size = data_read.size();
                }
            }
        }
        //printf("path size: %d\n", path_size);
        //fflush(stdout);
        //printf("data_read.size(): %d\n", data_read.size());
        //fflush(stdout);
        /* Stop reading if whole path read */
        if ((path_size != 0) && (data_read.size() == path_size)) {
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
    /* Add the default relative path to the client's request */
    data_read = PATH + data_read;

    //printf("%d\n", relative_path_size);
    //printf("%s\n", data_read.data());

    /* Check if directory exists */
    DIR *requested_dir;
    struct stat stat_buf;
    if ((requested_dir = opendir(data_read.data())) == NULL) {
        perror("dataServer: opendir");
        free_socket(sock_info);
        pthread_exit(NULL);
    }
    closedir_report(requested_dir);

    /* Traverse directory adding all files to tasks queue */
    if (traverse_directory(data_read, sock_info, relative_path_size) != 0) {
        free_socket(sock_info);
        exit(EXIT_FAILURE);
    }

    /* Wait for all tasks to end */
    //write(1, "Communication trying to acquire tasks_remaining lock\n", 54);
    pthread_mutex_lock(sock_info.lock_tasks_remaining);
    //write(1, "Communication acquired tasks_remaining lock\n", 44);
    while ((*sock_info.tasks_remaining) > 0) {
        //write(1, "Communication waiting for tasks to be done...\n", 46);
        pthread_cond_wait(&cond_done, sock_info.lock_tasks_remaining);
    }
    //write(1, "Communication unlocking tasks_remaining lock\n", 45);
    pthread_mutex_unlock(sock_info.lock_tasks_remaining);

    /* Free socket and close thread */
    free_socket(sock_info);
    pthread_exit(NULL);
}