/* File: dataServer.cpp */

#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <string>
#include <map>
#include <unistd.h> // for write
#include <pthread.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>

#define PATH "./"

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

int block_size;

std::map<int,int> *tasks_remaining;

pthread_mutex_t map_lock = PTHREAD_MUTEX_INITIALIZER;

void *worker_thread(void *arg) {
    write(1, "Worker starting\n", 16);
    pthread_detach(pthread_self());
    pthread_exit(NULL);
}

int traverse_directory(std::string path, int sock) {
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
            printf("%s", cur_path.data());
            fflush(stdout);
            int fd;
            if ((fd = open(cur_path.data(), O_RDONLY)) < 0) {
                perror("remoteClient: open file");
                return -1;
            }
            printf(" opened file ");
            fflush(stdout);
            cur_path = cur_path + " " + std::to_string(stat_buf.st_size) + " ";
            if (safe_write_bytes(sock, cur_path.data(), cur_path.size()) < 0) {
                perror("remoteClient: write to socket");
                return -1;
            }
            printf(" written header ");
            fflush(stdout);
            char buf[block_size];
            int nread;
            while ((nread = read(fd, buf, block_size)) > 0) {
                if (safe_write_bytes(sock, buf, nread) < 0) {
                    perror("remoteClient: write to socket");
                    return -1;
                }//test<---------------------------------
                if (safe_write_bytes(1, buf, nread) < 0) {
                    perror("remoteClient: write to socket");
                    return -1;
                }
            }
            if (nread < 0) {
                perror("remoteClient: read file");
                return -1;
            }
            close_report(fd);
        }
        else if ((stat_buf.st_mode & S_IFMT) == S_IFDIR) {
            traverse_directory(cur_path, sock);
        }
    }
    return 0;
}

void *communication_thread(void *void_t_socket) {
    /* Turn socket to int */
    int socket = *(int*) void_t_socket;
    write(1, "Communication starting\n", 23);
    /* Read path from client */
    int nread;
    char buf[50];
    int path_size = 0;      // size of the path sent by the client in bytes
    std::string data_read;  // data from socket passed to memory
    while (((nread = read(socket, buf, 50)) > 0) || (errno == EINTR)) {
        /* After reading a chunk, process it character by character in memory */
        for (uint i = 0 ; i < nread ; i++) {
            /* Start reading path after space */
            if (buf[i] == ' ') {
                path_size = atoi(data_read.data());
                data_read.erase();
            }
            else {
                data_read.push_back(buf[i]);
            }
        }
        /* Stop reading if whole path read */
        if ((path_size != 0) && (data_read.size() == path_size)) {
            break;
        }
    }
    /* If read failed, close the thread */
    if (nread == -1) {
        perror("dataServer: read from socket");
        close_report(socket);
        pthread_detach(pthread_self());
        pthread_exit(NULL);
    }
    /* If client closed connection, close the thread */
    if (nread == 0) {
        write(2, "dataServer: client closed socket", 32);
        close_report(socket);
        pthread_detach(pthread_self());
        pthread_exit(NULL);
    }
    data_read = PATH + data_read;
    if (traverse_directory(data_read, socket) == -1) {
        close_report(socket);
        pthread_detach(pthread_self());
        pthread_exit(NULL);
    }
    pthread_detach(pthread_self());
    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
	/* Initialising parameters */
    if (argc != 9) {
        fprintf(stderr, "Invalid number of arguments\n");
        exit(EXIT_FAILURE);
    }
    int port, thread_pool_size, queue_size;
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

    /* Creating worker threads */
    pthread_t *worker_threads;
    if ((worker_threads = (pthread_t *) malloc(thread_pool_size*sizeof(pthread_t))) == NULL) {
        perror("dataServer: malloc");
        exit(EXIT_FAILURE);
    }
    for (int i = 0 ; i < thread_pool_size ; i++) {
        if (pthread_create(worker_threads + i, NULL, worker_thread, NULL) != 0) {
            perror("dataServer: create worker thread");
            exit(EXIT_FAILURE);
        }
    }
    
    /* Create map */
    tasks_remaining = new std::map<int,int>;

    /* Create socket */
    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("dataServer: create socket");
        exit(EXIT_FAILURE);
    }
    
    /* Bind socket to address */
    struct sockaddr_in server;
    server.sin_family = AF_INET;    /* Internet domain */
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);  /* The given port */
    if (bind(sock, (sockaddr*) &server, sizeof(server)) < 0) {
        perror("dataServer: bind socket");
        exit(EXIT_FAILURE);
    }
    /* Listen for connections */
    if (listen(sock, 5) < 0) {
        perror("dataServer: socket listen");
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
            exit(EXIT_FAILURE);
        }
        /* Accept connection */
        if ((*newsock = accept(sock, (sockaddr*) &client, &clientlen)) < 0){
            perror("dataServer: accept connection");
            exit(EXIT_FAILURE);
        }
        /* Find and print client's name */
        struct hostent *rem;
        if ((rem = gethostbyaddr((char *) &client.sin_addr.s_addr, sizeof(client.sin_addr.s_addr), client.sin_family)) == NULL) {
            perror("dataServer: gethostbyaddr");
            exit(EXIT_FAILURE);
        }
        printf("Accepted connection from %s\n", rem->h_name);
        /* Create communication thread for this client */
        pthread_t com_thread_id;
        pthread_create(&com_thread_id, NULL, communication_thread, (void *) newsock);
    }

    free(worker_threads);
    printf("Exiting main");
    fflush(stdout);
    /* Exiting successfully */
    exit(EXIT_SUCCESS);
}