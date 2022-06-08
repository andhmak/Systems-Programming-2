/* File: serverWorker.h */

/* Function to be executed by worker threads, doing file transfers found in the tasks queue */
void *worker_thread(void *arg);