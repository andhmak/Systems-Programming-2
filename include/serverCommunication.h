/* File: serverCommunication.h */

/* Function to be executed by communication threads, reading the request, creating the relevant tasks and adding them to the queue */
void *communication_thread(void *void_t_socket);