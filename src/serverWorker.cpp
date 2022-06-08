/* File: serverWorker.cpp */

#include <string>
#include <queue>
#include <unistd.h>
#include <fcntl.h>
#include "serverWorker.h"
#include "commonFuncs.h"
#include "serverTypes.h"

extern int block_size;  // size of the blocks in which the file contents are transfered to the client in bytes

extern std::queue<task> *tasks; // queue containing all current tasks

/* Variables for synchronisation */
extern pthread_mutex_t queue_lock;
extern pthread_cond_t cond_nonempty, cond_nonfull, cond_done;

/* Bookkeeping after finishing  current_task */
void finish_task(task current_task) {
    /* Unlock the socket's transfer mutex so that another thread can start writing to it */
    pthread_mutex_unlock(current_task.sock_info.lock_data_transfer);

    /* Decrement the number of remaining tasks for the socket */
    pthread_mutex_lock(current_task.sock_info.lock_tasks_remaining);
    (*current_task.sock_info.tasks_remaining)--;
    pthread_mutex_unlock(current_task.sock_info.lock_tasks_remaining);

    /* Notify communication thread that a task was finished */
    pthread_cond_signal(&cond_done);
}

/* Function to be executed by worker threads, doing file transfers found in the tasks queue */
void *worker_thread(void *arg) {
    task current_task;

    /* Main worker loop */
    while (1) {
        /* Wait for an available task to take from the queue */
        pthread_mutex_lock(&queue_lock);
        while (tasks->empty()) {
            pthread_cond_wait(&cond_nonempty, &queue_lock);
        }
        current_task = tasks->front();
        tasks->pop();
        pthread_mutex_unlock(&queue_lock);
        pthread_cond_signal(&cond_nonfull);

        /* Do task */

        pthread_mutex_lock(current_task.sock_info.lock_data_transfer);
        /* Open the file */
        int fd;
        if ((fd = open(current_task.path.data(), O_RDONLY)) < 0) {
            perror("dataServer: open file");
            close_report(current_task.sock_info.sock_id);
            exit(EXIT_FAILURE);
        }

        /* Add the file's size after the name */
        current_task.path.erase(0, current_task.relative_path_size);
        current_task.path.push_back('\0');
        current_task.path.append((const char *) &(current_task.file_size), sizeof(uint32_t));

        /* Send the name and the size to the client */
        if (safe_write_bytes(current_task.sock_info.sock_id, current_task.path.data(), current_task.path.size()) < 0) {
            perror("dataServer: write to socket");
            close_report(fd);
            finish_task(current_task);
            continue;
        }
        
        /* Send the file contents to the client in block_size blocks */
        char buf[block_size];
        int nread;
        while ((nread = read(fd, buf, block_size)) > 0) {
            if (safe_write_bytes(current_task.sock_info.sock_id, buf, nread) < 0) {
                perror("dataServer: write to socket");
                close_report(fd);
                finish_task(current_task);
                continue;
            }
        }
        if (nread < 0) {
            perror("dataServer: read file");
            close_report(current_task.sock_info.sock_id);
            exit(EXIT_FAILURE);
        }

        /* Close the file */
        close_report(fd);
        
        /* End-of-task bookkeeping */
        finish_task(current_task);
    }
}