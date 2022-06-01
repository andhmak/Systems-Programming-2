/* File: remoteClient.cpp */

#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <string>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include "commonFuncs.h"

#define OUTPUT "./output/"

int main(int argc, char* argv[]) {

	/* Initialising parameters */
    if (argc != 7) {
        fprintf(stderr, "Invalid number of arguments\n");
        exit(EXIT_FAILURE);
    }
    in_addr_t server_ip;
    in_port_t server_port;
    char *directory, *server_ip_name;
	for (int i = 1 ; i < 7 ; i += 2) { 
		if (!strcmp(argv[i], "-i")) {
            server_ip_name = argv[i + 1];
			inet_pton(AF_INET, server_ip_name, &server_ip);
		}
        else if (!strcmp(argv[i], "-p")) {
			server_port = atoi(argv[i + 1]);
        }
        else if (!strcmp(argv[i], "-d")) {
			directory = argv[i + 1];
        }
        else {
            fprintf(stderr, "Invalid passing of arguments\n");
            exit(EXIT_FAILURE);
        }
	}

    /* Print parameters  */
    printf("Client’s parameters are:\n");
    printf("serverIP: %s\n", server_ip_name);
    printf("port: %d\n", server_port);
    printf("directory: %s\n", directory);

    /* Create socket */
    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("remoteClient: create socket");
        exit(EXIT_FAILURE);
    }

    /* Initiate connection */
    printf("Connecting to %s on port %d\n", server_ip_name, server_port);
    struct sockaddr_in server;
    server.sin_family = AF_INET;            /* Internet domain */
    server.sin_addr.s_addr = server_ip;
    server.sin_port = htons(server_port);   /* Server port */
    if (connect(sock, (struct sockaddr*) &server, sizeof(server)) < 0) {
        perror("remoteClient: connect to server");
        exit(EXIT_FAILURE);
    }

    /* Communicate with server */

    /* Send the desired path */
    std::string message = std::to_string(strlen(directory)) + " " + directory;
    if (safe_write_bytes(sock, message.data(), message.size()) < 0) {
        perror("remoteClient: write to socket");
        close_report(sock);
        exit(EXIT_FAILURE);
    }

    /* Process the results */
    int nread;
    char buf[50];
    int fd;                 // file descriptor of the newly copied file
    std::string file_name;  // name (path) of the file sent by the server
    int file_size = 0;      // size of the file sent by the server in bytes
    std::string data_read;  // data from socket passed to memory
    char state = 0;         // specifying how the current bytes read should be interpreted
    while (((nread = read(sock, buf, 50)) > 0) || (errno == EINTR)) {
        /* After reading a chunk from the header, process it character by character in memory */
        for (int i = 0 ; i < nread ; i++) {
            /* If reading the header */
            if (state != 2) {
                /* Spaces act as message boundaries */
                if (buf[i] == '\0') {
                    state++;
                    /* The first part is the file name */
                    if (state == 1) {
                        file_name = OUTPUT + data_read;
                    }
                    /* The second is the file size */
                    else if (state == 2) {
                        file_size = atoi(data_read.data());
                        /* Create file, creating parent directories if they don't exist */
                        for (int j = 0 ; j < file_name.size() ; j++) {
                            if (file_name[j] == '/') {
                                std::string dir_path = file_name.substr(0,j);
                                while (file_name[j+1] == '/') {
                                    j++;
                                }
                                DIR* dir = opendir(dir_path.data());
                                /* If directory exists, close it */
                                if (dir) {
                                    closedir(dir);
                                }
                                /* If not, create it */
                                else if (ENOENT == errno) {
                                    if (mkdir(dir_path.data(), 0755) < 0) {
                                        perror("remoteClient: opendir");
                                        close_report(sock);
                                        exit(EXIT_FAILURE);
                                    }
                                }
                                else {
                                    perror("remoteClient: opendir");
                                    close_report(sock);
                                    exit(EXIT_FAILURE);
                                }
                            }
                        }
                        //printf("%s",file_name.data());
                        if ((fd = creat(file_name.data(), 0644)) < 0) {
                            perror("remoteClient: create file");
                            close_report(sock);
                            exit(EXIT_FAILURE);
                        }
                    }
                    data_read.erase();
                }
                else {
                    data_read.push_back(buf[i]);
                }
            }
            /* If reading the third part of the message (file data) add it to the file in chunks (not byte by byte) */
            else {
                int to_write = (nread - i) < file_size ? nread - i : file_size;
                if (safe_write_bytes(fd, buf + i, to_write) < 0) {
                    perror("remoteClient: write to file");
                    close_report(sock);
                    exit(EXIT_FAILURE);
                }
                /* Update characters processed */
                i += to_write - 1;
                /* Updating remaining file bytes */
                file_size -= to_write;
                /* If file done, move to the next message */
                if (file_size == 0) {
                    close_report(fd);
                    state = 0;
                }
            }
        }
    }
    /* If read failed, exit with failure */
    if (nread == -1) {
        perror("remoteClient: read from socket");
        close_report(sock);
        exit(EXIT_FAILURE);
    }
    
    /* Close socket and exit successfully */
    close_report(sock);
    exit(EXIT_SUCCESS);
}