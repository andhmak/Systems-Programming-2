/* File: dataServer.cpp */

#include <cstring>
#include <queue>
#include <netdb.h>
#include <signal.h>
#include "commonFuncs.h"
#include "serverTypes.h"
#include "serverCommunication.h"
#include "serverWorker.h"

/* Global variables that need to be visible to other threads */

/* Command line arguments */
int block_size, queue_size;

/* Queue containing all current tasks */
std::queue<task> *tasks;

/* Variables for synchronisation */
pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;     // Mutex guarding access to the tasks queue
pthread_cond_t cond_nonempty = PTHREAD_COND_INITIALIZER;    // Condition variable to wait for/signal a non-empty tasks queue
pthread_cond_t cond_nonfull = PTHREAD_COND_INITIALIZER;     // Condition variable to wait for/signal a non-full tasks queue
pthread_cond_t cond_done = PTHREAD_COND_INITIALIZER;        // Condition variable to wait for/signal when a task is completed

int main(int argc, char* argv[]) {
    /* Ignore SIGPIPE (socket errors handled explicitly in threads) */
    static struct sigaction act;
    act.sa_flags = SA_RESTART;
    act.sa_handler = SIG_IGN;
    sigfillset(&(act.sa_mask));
    sigaction(SIGPIPE, &act, NULL);

	/* Initialising parameters */
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
    //printf("Server’s parameters are:\n");
    //printf("port: %d\n", port);
    //printf("thread_pool_size: %d\n", thread_pool_size);
    //printf("queue_size: %d\n", queue_size);
    //printf("Block_size: %d\n", block_size);
    
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
        //struct hostent *rem;
        //if ((rem = gethostbyaddr((char *) &client.sin_addr.s_addr, sizeof(client.sin_addr.s_addr), client.sin_family)) == NULL) {
        //    perror("dataServer: gethostbyaddr");
        //    delete tasks;
        //    close_report(sock);
        //    exit(EXIT_FAILURE);
        //}
        //printf("Accepted connection from %s\n", rem->h_name);
        /* Create communication thread for this client */
        pthread_t com_thread_id;
        if (pthread_create(&com_thread_id, NULL, communication_thread, (void *) newsock) != 0) {
            perror("dataServer: create communication thread");
            exit(EXIT_FAILURE);
        }
        if (pthread_detach(com_thread_id) != 0) {
            perror("dataServer: detach communication thread");
            exit(EXIT_FAILURE);
        }
    }

    /* Exiting successfully (assuming it never happens) */
    delete tasks;
    close_report(sock);
    exit(EXIT_SUCCESS);
}