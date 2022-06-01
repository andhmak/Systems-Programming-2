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
        close(sock);
        exit(EXIT_FAILURE);
    }

    /* Process the results */
    //
    
    /* Close socket and exit */
    close_report(sock);
    exit(EXIT_SUCCESS);
}