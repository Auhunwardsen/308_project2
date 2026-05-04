/*
 * table.c -- Mini-KV hash table
 *
 * Project 2, CprE 3080, Spring 2026
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kv.h"

/* ======================================================================
 * Stage 1: Hash table  (Stage 3 added the rwlock for thread-safety,
 *                       Stage 4 added the expiration check)
 * ====================================================================== */

/* the hash table (Stage 1) shared by all threads */
struct table g_table;

/* simple djb2 hash function */
static unsigned int hash_str(const char *s)
{
    unsigned int h = 5381;
    for (int i = 0; s[i] != '\0'; i++)
    {
        h = h * 33 + (unsigned char)s[i];
    }
    return h;
}

/* set up the table with n buckets, all empty */
void table_init(int n)
{
    g_table.buckets = calloc(n, sizeof(struct entry *));
    g_table.num_buckets = n;
    pthread_rwlock_init(&g_table.lock, NULL);
    atomic_init(&g_table.hits, 0);
    atomic_init(&g_table.misses, 0);
    atomic_init(&g_table.puts, 0);
    atomic_init(&g_table.dels, 0);
    atomic_init(&g_table.keys, 0);
}

/* free everything in the table when the server shuts down */
void table_destroy(void)
{
    for (int i = 0; i < g_table.num_buckets; i++)
    {
        struct entry *e = g_table.buckets[i];
        while (e != NULL)
        {
            struct entry *next = e->next;
            free(e);
            e = next;
        }
    }
    free(g_table.buckets);
    pthread_rwlock_destroy(&g_table.lock);
}

/* look up a key, copy its value into out_val. returns 0 on hit, -1 on miss */
int kv_get(const char *key, char *out_val, size_t out_cap)
{
    /* read lock: many readers can be in here at once */
    pthread_rwlock_rdlock(&g_table.lock);

    int idx = hash_str(key) % g_table.num_buckets;
    time_t now = time(NULL);

    struct entry *e = g_table.buckets[idx];
    while (e != NULL)
    {
        if (strcmp(e->key, key) == 0)
        {
            /* check if it expired (Stage 4) */
            if (e->expire != 0 && now >= e->expire)
            {
                break; /* treat as miss; sweeper will clean it up */
            }
            strncpy(out_val, e->val, out_cap - 1);
            out_val[out_cap - 1] = '\0';
            pthread_rwlock_unlock(&g_table.lock);
            atomic_fetch_add(&g_table.hits, 1);
            return 0;
        }
        e = e->next;
    }

    pthread_rwlock_unlock(&g_table.lock);
    atomic_fetch_add(&g_table.misses, 1);
    return -1;
}

/* insert or update a key. returns 0 on success */
int kv_put(const char *key, const char *val, int ttl_seconds)
{
    /* write lock: only one writer allowed */
    pthread_rwlock_wrlock(&g_table.lock);

    int idx = hash_str(key) % g_table.num_buckets;

    time_t exp = 0;
    if (ttl_seconds > 0)
    {
        exp = time(NULL) + ttl_seconds;
    }

    /* if key already exists, just update it */
    struct entry *e = g_table.buckets[idx];
    while (e != NULL)
    {
        if (strcmp(e->key, key) == 0)
        {
            strncpy(e->val, val, MAX_VAL_LEN - 1);
            e->val[MAX_VAL_LEN - 1] = '\0';
            e->expire = exp;
            pthread_rwlock_unlock(&g_table.lock);
            atomic_fetch_add(&g_table.puts, 1);
            return 0;
        }
        e = e->next;
    }

    /* not found, make a new entry and put it at the head of the chain */
    struct entry *new_e = malloc(sizeof(struct entry));
    if (new_e == NULL)
    {
        pthread_rwlock_unlock(&g_table.lock);
        return -1;
    }
    strncpy(new_e->key, key, MAX_KEY_LEN - 1);
    new_e->key[MAX_KEY_LEN - 1] = '\0';
    strncpy(new_e->val, val, MAX_VAL_LEN - 1);
    new_e->val[MAX_VAL_LEN - 1] = '\0';
    new_e->expire = exp;
    new_e->next = g_table.buckets[idx];
    g_table.buckets[idx] = new_e;

    atomic_fetch_add(&g_table.keys, 1);
    pthread_rwlock_unlock(&g_table.lock);
    atomic_fetch_add(&g_table.puts, 1);
    return 0;
}

/* delete a key. returns 0 if removed, -1 if not found */
int kv_del(const char *key)
{
    pthread_rwlock_wrlock(&g_table.lock);

    int idx = hash_str(key) % g_table.num_buckets;

    struct entry *prev = NULL;
    struct entry *e = g_table.buckets[idx];
    while (e != NULL)
    {
        if (strcmp(e->key, key) == 0)
        {
            if (prev == NULL)
            {
                g_table.buckets[idx] = e->next;
            }
            else
            {
                prev->next = e->next;
            }
            free(e);
            atomic_fetch_sub(&g_table.keys, 1);
            pthread_rwlock_unlock(&g_table.lock);
            atomic_fetch_add(&g_table.dels, 1);
            return 0;
        }
        prev = e;
        e = e->next;
    }

    pthread_rwlock_unlock(&g_table.lock);
    return -1;
}
