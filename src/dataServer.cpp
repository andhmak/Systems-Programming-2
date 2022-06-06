/* File: dataServer.cpp */

#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <string>
#include <map>
#include <queue>
#include <unistd.h> // for write
#include <pthread.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include "commonFuncs.h"

#define PATH "./"

typedef struct {
    int sock_id;
    pthread_mutex_t *lock_data_transfer;
    pthread_mutex_t *lock_tasks_remaining;
    int *tasks_remaining;
} sock_info_t;

typedef struct {
    int relative_path_size;
    std::string path;
    uint32_t file_size;
    sock_info_t sock_info;
} task;

int block_size, queue_size;

std::queue<task> *tasks;

pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_nonempty = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_nonfull = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_done = PTHREAD_COND_INITIALIZER;

void *worker_thread(void *arg) {
    write(1, "Worker starting\n", 16);
    task current_task;
    /* Main worker loop */
    while (1) {
        /* Wait for an available task */
        //write(1, "Worker trying to acquire queue lock\n", 37);
        pthread_mutex_lock(&queue_lock);
        //write(1, "Worker acquired queue lock\n", 28);
        while (tasks->empty()) {
            write(1, "Worker waiting for tasks...\n", 28);
            pthread_cond_wait(&cond_nonempty, &queue_lock);
            write(1, "Worker woke up\n", 16);
        }
        //write(1, "Worker found task\n", 19);
        current_task = tasks->front();
        tasks->pop();
        //write(1, "Worker unlocking queue lock\n", 29);
        pthread_mutex_unlock(&queue_lock);
        //write(1, "Worker signaling nonfull\n", 26);
        pthread_cond_signal(&cond_nonfull);

        //printf("Worker locks %d, %d, %d\n", current_task.sock_info.lock_data_transfer, current_task.sock_info.lock_tasks_remaining, current_task.sock_info.tasks_remaining);
        //fflush(stdout);
        /* Do task */
        //write(1, "Worker trying to acquire socket lock\n", 38);
        pthread_mutex_lock(current_task.sock_info.lock_data_transfer);
        //write(1, "Worker acquired socket lock\n", 29);
        int fd;
        if ((fd = open(current_task.path.data(), O_RDONLY)) < 0) {
            perror("dataServer: open file");
            close_report(current_task.sock_info.sock_id);
            pthread_detach(pthread_self());
            pthread_exit(NULL);
        }
        current_task.path.erase(0, current_task.relative_path_size);
        current_task.path.push_back('\0');
        //for (int i = 0 ; i < current_task.path.size() ; i++) {
        //    printf("%c: %d\n", current_task.path[i], current_task.path[i]);
        //    fflush(stdout);
        //}
        printf("socket: %d\n", current_task.sock_info.sock_id);
        fflush(stdout);
        if (safe_write_bytes(current_task.sock_info.sock_id, current_task.path.data(), current_task.path.size()) < 0) {
            perror("dataServer: write to socket");
            close_report(current_task.sock_info.sock_id);
            pthread_detach(pthread_self());
            pthread_exit(NULL);
        }
        if (safe_write_bytes(current_task.sock_info.sock_id, (const char *) &(current_task.file_size), sizeof(uint32_t)) < 0) {
            perror("dataServer: write to socket");
            close_report(current_task.sock_info.sock_id);
            pthread_detach(pthread_self());
            pthread_exit(NULL);
        }
        //printf("%d\n", ntohl(current_task.size));
        //fflush(stdout);
        char buf[block_size];
        int nread;
        while ((nread = read(fd, buf, block_size)) > 0) {
            if (safe_write_bytes(current_task.sock_info.sock_id, buf, nread) < 0) {
                perror("dataServer: write to socket");
                close_report(current_task.sock_info.sock_id);
                pthread_detach(pthread_self());
                pthread_exit(NULL);
            }
        }
        if (nread < 0) {
            perror("dataServer: read file");
            close_report(current_task.sock_info.sock_id);
            pthread_detach(pthread_self());
            pthread_exit(NULL);
        }
        close_report(fd);
        //write(1, "Worker unlocking socket lock\n", 30);
        pthread_mutex_unlock(current_task.sock_info.lock_data_transfer);
        //write(1, "Worker trying to acquire tasks_remaining lock\n", 47);
        pthread_mutex_lock(current_task.sock_info.lock_tasks_remaining);
        //write(1, "Worker acquired tasks_remaining lock\n", 38);
        (*current_task.sock_info.tasks_remaining)--;
        //write(1, "Worker unlocking tasks_remaining lock\n", 39);
        pthread_mutex_unlock(current_task.sock_info.lock_tasks_remaining);
        //write(1, "Worker signaling done\n", 23);
        pthread_cond_signal(&cond_done);
    }
}

int traverse_directory(std::string path, sock_info_t sock_info, int relative_path_size) {
    //printf("Communication locks %d, %d, %d\n", sock_info.lock_data_transfer, sock_info.lock_tasks_remaining, sock_info.tasks_remaining);
    //fflush(stdout);
    DIR *cur_dir;
    struct dirent *cur_file;
    std::string cur_path;
    struct stat stat_buf;
    if ((cur_dir = opendir(path.data())) == NULL) {
        perror("dataServer: opendir");
        return -1;
    }
    if (stat(path.data(), &stat_buf) < 0) {
        perror("dataServer: stat");
        return -1;
    }
    if ((stat_buf.st_mode & S_IFMT) != S_IFDIR) {
        perror("dataServer: not a directory");
        return -1;
    }
    while ((cur_file = readdir(cur_dir)) != NULL) {
        if ((cur_file->d_ino == 0) || !strcmp(cur_file->d_name, ".") || !strcmp(cur_file->d_name, "..")) {
            continue;
        }
        cur_path = path + "/" + cur_file->d_name;
        if (stat(cur_path.data(), &stat_buf) < 0) {
            perror("dataServer: stat");
            return -1;
        }
        if ((stat_buf.st_mode & S_IFMT) == S_IFREG) {
            //write(1, "Communication found new file\n", 30);
            //write(1, "Communication trying to acquire tasks_remaining lock\n", 54);
            pthread_mutex_lock(sock_info.lock_tasks_remaining);
            (*sock_info.tasks_remaining)++;
            //write(1, "Communication unlocking tasks_remaining lock\n", 46);
            pthread_mutex_unlock(sock_info.lock_tasks_remaining);

            task new_task;
            new_task.path = cur_path;
            new_task.relative_path_size = relative_path_size;
            new_task.file_size = htonl(stat_buf.st_size);
            new_task.sock_info = sock_info;
            //write(1, "Communication trying to acquire queue lock\n", 44);
            pthread_mutex_lock(&queue_lock);
            while (tasks->size() >= queue_size) {
                write(1, "Communication waiting for queue to have space...\n", 50);
                pthread_cond_wait(&cond_nonfull, &queue_lock);
            }
            //write(1, "Communication found space\n", 27);
            tasks->push(new_task);
            //write(1, "Communication unlocking queue lock\n", 36);
            pthread_mutex_unlock(&queue_lock);
            //write(1, "Communication signaling nonempty\n", 34);
            pthread_cond_signal(&cond_nonempty);
        }
        else if ((stat_buf.st_mode & S_IFMT) == S_IFDIR) {
            traverse_directory(cur_path, sock_info, relative_path_size);
        }
    }
    closedir(cur_dir);
    return 0;
}

void *communication_thread(void *void_t_socket) {
    write(1, "Communication starting\n", 23);
    /* Initialise info about the socket */
    sock_info_t sock_info;
    /* Save socket id and free the argument */
    sock_info.sock_id = *(int*) void_t_socket;
    free(void_t_socket);
    /* Create socket-specific mutexes */
    if ((sock_info.lock_data_transfer = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t))) == NULL) {
        perror("dataServer: malloc");
        close_report(sock_info.sock_id);
        pthread_detach(pthread_self());
        pthread_exit(NULL);
    }
    pthread_mutex_init(sock_info.lock_data_transfer, 0);
    if ((sock_info.lock_tasks_remaining = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t))) == NULL) {
        perror("dataServer: malloc");
        close_report(sock_info.sock_id);
        pthread_detach(pthread_self());
        pthread_exit(NULL);
    }
    pthread_mutex_init(sock_info.lock_tasks_remaining, 0);
    /* Create counter for remaining tasks */
    if ((sock_info.tasks_remaining = (int *) malloc(sizeof(int))) == NULL) {
        perror("dataServer: malloc");
        close_report(sock_info.sock_id);
        pthread_detach(pthread_self());
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
        /* After reading a chunk, process it character by character in memory */
        for (int i = 0 ; i < nread ; i++) {
            if (state < (sizeof(uint32_t) - 1)) {
                printf("reading\n");
                fflush(stdout);
                state++;
                data_read.push_back(buf[i]);
            }
            else if (state == (sizeof(uint32_t) - 1)) {
                state++;
                data_read.push_back(buf[i]);
                memcpy(&path_size, data_read.data(), sizeof(uint32_t));
                path_size = ntohl(path_size);
                printf("path_size: %d\n", path_size);
                fflush(stdout);
                data_read.erase();
            }
            else {
                printf("%c", buf[i]);
                fflush(stdout);
                data_read.push_back(buf[i]);
                if (buf[i] == '/') {
                    relative_path_size = data_read.size();
                }
            }
        }
        printf("path size: %d\n", path_size);
        fflush(stdout);
        printf("data_read.size(): %d\n", data_read.size());
        fflush(stdout);
        /* Stop reading if whole path read */
        if ((path_size != 0) && (data_read.size() == path_size)) {
            break;
        }
    }
    /* If read failed, close the thread */
    if (nread == -1) {
        perror("dataServer: read from socket");
        close_report(sock_info.sock_id);
        pthread_detach(pthread_self());
        pthread_exit(NULL);
    }
    /* If client closed connection, close the thread */
    if (nread == 0) {
        write(2, "dataServer: client closed socket", 32);
        close_report(sock_info.sock_id);
        pthread_detach(pthread_self());
        pthread_exit(NULL);
    }
    data_read = PATH + data_read;

    printf("%d\n", relative_path_size);
    printf("%s\n", data_read.data());
    if (traverse_directory(data_read, sock_info, relative_path_size) == -1) {
        close_report(sock_info.sock_id);
        pthread_detach(pthread_self());
        pthread_exit(NULL);
    }

    /* Wait for all tasks to end */
    //write(1, "Communication trying to acquire tasks_remaining lock\n", 54);
    pthread_mutex_lock(sock_info.lock_tasks_remaining);
    //write(1, "Communication acquired tasks_remaining lock\n", 44);
    while (*sock_info.tasks_remaining) {
        write(1, "Communication waiting for tasks to be done...\n", 46);
        pthread_cond_wait(&cond_done, sock_info.lock_tasks_remaining);
    }
    //write(1, "Communication unlocking tasks_remaining lock\n", 45);
    pthread_mutex_unlock(sock_info.lock_tasks_remaining);

    /* Free socket info */
    free(sock_info.lock_data_transfer);
    free(sock_info.lock_tasks_remaining);
    free(sock_info.tasks_remaining);
    /* Close socket and thread */
    close_report(sock_info.sock_id);
    pthread_detach(pthread_self());
    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
	{/* Initialising parameters */
    if (argc != 9) {
        fprintf(stderr, "Invalid number of arguments\n");
        exit(EXIT_FAILURE);
    }
    int port, thread_pool_size;
	for (int i = 1 ; i < 9 ; i += 2) { 
		if (!strcmp(argv[i], "-p")) {
			port = atoi(argv[i + 1]);
		}
        else if (!strcmp(argv[i], "-s")) {
			thread_pool_size = atoi(argv[i + 1]);
        }
        else if (!strcmp(argv[i], "-q")) {
			queue_size = atoi(argv[i + 1]);
        }
        else if (!strcmp(argv[i], "-b")) {
			block_size = atoi(argv[i + 1]);
        }
        else {
            fprintf(stderr, "Invalid passing of arguments\n");
            exit(EXIT_FAILURE);
        }
	}

    /* Print parameters */
    printf("Server’s parameters are:\n");
    printf("port: %d\n", port);
    printf("thread_pool_size: %d\n", thread_pool_size);
    printf("queue_size: %d\n", queue_size);
    printf("Block_size: %d\n", block_size);
    
    /* Create task queue */
    tasks = new std::queue<task>;

    /* Create worker threads */
    pthread_t worker_thread_id;
    for (int i = 0 ; i < thread_pool_size ; i++) {
        if (pthread_create(&worker_thread_id, NULL, worker_thread, NULL) != 0) {
            perror("dataServer: create worker thread");
            exit(EXIT_FAILURE);
        }
        if (pthread_detach(worker_thread_id) != 0) {
            perror("dataServer: detach worker thread");
            exit(EXIT_FAILURE);
        }
    }

    /* Create socket */
    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("dataServer: create socket");
        delete tasks;
        exit(EXIT_FAILURE);
    }
    
    /* Bind socket to address */
    struct sockaddr_in server;
    server.sin_family = AF_INET;    /* Internet domain */
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);  /* The given port */
    if (bind(sock, (sockaddr*) &server, sizeof(server)) < 0) {
        perror("dataServer: bind socket");
        delete tasks;
        close_report(sock);
        exit(EXIT_FAILURE);
    }
    /* Listen for connections */
    if (listen(sock, 5) < 0) {
        perror("dataServer: socket listen");
        delete tasks;
        close_report(sock);
        exit(EXIT_FAILURE);
    }
    printf("Server was successfully initialized...\n");
    printf("Listening for connections to port %d\n", port);
    /* Main server loop */
    while (1) {
        int *newsock;
        struct sockaddr_in client;
        socklen_t clientlen = sizeof(client);
        if ((newsock = (int *) malloc(sizeof(int))) == NULL) {
            perror("dataServer: malloc");
            delete tasks;
            close_report(sock);
            exit(EXIT_FAILURE);
        }
        /* Accept connection */
        if ((*newsock = accept(sock, (sockaddr*) &client, &clientlen)) < 0) {
            perror("dataServer: accept connection");
            delete tasks;
            close_report(sock);
            exit(EXIT_FAILURE);
        }
        /* Find and print client's name */
        struct hostent *rem;
        if ((rem = gethostbyaddr((char *) &client.sin_addr.s_addr, sizeof(client.sin_addr.s_addr), client.sin_family)) == NULL) {
            perror("dataServer: gethostbyaddr");
            delete tasks;
            close_report(sock);
            exit(EXIT_FAILURE);
        }
        printf("Accepted connection from %s\n", rem->h_name);
        /* Create communication thread for this client */
        pthread_t com_thread_id;
        pthread_create(&com_thread_id, NULL, communication_thread, (void *) newsock);
    }

    delete tasks;
    close_report(sock);
    write(1, "Server exiting\n", 15);}
    /* Exiting successfully */
    exit(EXIT_SUCCESS);
}