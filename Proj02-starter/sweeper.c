/*
 * sweeper.c -- Mini-KV background TTL sweeper
 *
 * Project 2, CprE 3080, Spring 2026
 */

#include <stdlib.h>
#include <time.h>

#include "kv.h"

/* ======================================================================
 * Stage 4: Sweeper thread (background cleanup of expired keys)
 * ====================================================================== */

/* sleep for `ms` milliseconds, but wake every 50ms to peek at g_shutdown */
/* so the thread can quit fast when the user hits Ctrl-C */
static void sleep_ms_interruptible(int ms)
{
    while (ms > 0 && !g_shutdown)
    {
        /* sleep at most 50ms at a time */
        int step = 50;
        if (ms < 50)
            step = ms;

        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = step * 1000000;
        nanosleep(&ts, NULL);
        ms -= step;
    }
}

/* walk one bucket and drop any entry whose expire time has passed */
static void sweep_bucket(int i, time_t now)
{
    pthread_rwlock_wrlock(&g_table.lock);

    struct entry *prev = NULL;
    struct entry *e = g_table.buckets[i];
    while (e != NULL)
    {
        if (e->expire != 0 && now >= e->expire)
        {
            /* unlink this entry from the chain and free it */
            struct entry *dead = e;
            if (prev == NULL)
                g_table.buckets[i] = e->next;
            else
                prev->next = e->next;
            e = e->next;
            free(dead);
            atomic_fetch_sub(&g_table.keys, 1);
        }
        else
        {
            prev = e;
            e = e->next;
        }
    }

    pthread_rwlock_unlock(&g_table.lock);
}

/* background sweeper: every so often, walk the table and remove expired keys */
void *sweeper_thread(void *arg)
{
    int sleep_ms = *(int *)arg;

    while (!g_shutdown)
    {
        /* 1. wait for the next sweep tick */
        sleep_ms_interruptible(sleep_ms);
        if (g_shutdown)
            break;

        /* 2. sweep all buckets (one at a time so readers arent blocked long) */
        time_t now = time(NULL);
        for (int i = 0; i < g_table.num_buckets && !g_shutdown; i++)
        {
            sweep_bucket(i, now);
        }
    }
    return NULL;
}
