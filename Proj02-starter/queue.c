/*
 * queue.c -- Mini-KV bounded work queue
 *
 * Project 2, CprE 3080, Spring 2026
 */

#include <stdlib.h>
#include <unistd.h>

#include "kv.h"

/* ======================================================================
 * Stage 2: Bounded work queue (producer / consumer)
 *
 * The main thread is the producer (it accepts new clients and pushes
 * their fds in here). Worker threads are consumers -- they pull fds
 * out and run handle_client() on them.
 * ====================================================================== */

/* the work queue (Stage 2) shared by all threads */
struct queue g_queue;

void queue_init(int cap)
{
    g_queue.fds = malloc(cap * sizeof(int));
    g_queue.cap = cap;
    g_queue.head = 0;
    g_queue.tail = 0;
    g_queue.count = 0;
    g_queue.shutdown = 0;
    pthread_mutex_init(&g_queue.mutex, NULL);
    pthread_cond_init(&g_queue.not_full, NULL);
    pthread_cond_init(&g_queue.not_empty, NULL);
}

void queue_destroy(void)
{
    free(g_queue.fds);
    pthread_mutex_destroy(&g_queue.mutex);
    pthread_cond_destroy(&g_queue.not_full);
    pthread_cond_destroy(&g_queue.not_empty);
}

/* producer: main thread puts a new client fd in the queue */
void queue_put(int fd)
{
    pthread_mutex_lock(&g_queue.mutex);

    /* wait if the queue is full */
    while (g_queue.count == g_queue.cap && !g_queue.shutdown)
    {
        pthread_cond_wait(&g_queue.not_full, &g_queue.mutex);
    }

    if (g_queue.shutdown)
    {
        close(fd);
    }
    else
    {
        g_queue.fds[g_queue.tail] = fd;
        g_queue.tail = (g_queue.tail + 1) % g_queue.cap;
        g_queue.count++;
        pthread_cond_signal(&g_queue.not_empty);
    }

    pthread_mutex_unlock(&g_queue.mutex);
}

/* consumer: a worker pulls a client fd out of the queue */
int queue_get(void)
{
    pthread_mutex_lock(&g_queue.mutex);

    while (g_queue.count == 0 && !g_queue.shutdown)
    {
        pthread_cond_wait(&g_queue.not_empty, &g_queue.mutex);
    }

    if (g_queue.count == 0)
    {
        pthread_mutex_unlock(&g_queue.mutex);
        return -1;
    }

    int fd = g_queue.fds[g_queue.head];
    g_queue.head = (g_queue.head + 1) % g_queue.cap;
    g_queue.count--;
    pthread_cond_signal(&g_queue.not_full);

    pthread_mutex_unlock(&g_queue.mutex);
    return fd;
}

/* tell all the threads waiting on the queue to wake up and quit */
void queue_stop(void)
{
    pthread_mutex_lock(&g_queue.mutex);
    g_queue.shutdown = 1;
    pthread_cond_broadcast(&g_queue.not_full);
    pthread_cond_broadcast(&g_queue.not_empty);
    pthread_mutex_unlock(&g_queue.mutex);
}
