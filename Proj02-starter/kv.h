/*
 * kv.h -- Mini-KV server: shared declarations
 *
 * Project 2, CprE 3080, Spring 2026
 *
 * You may modify this file. It is provided as a starting point, not a rigid
 * interface. If your design benefits from additional fields or types, add them.
 */
#ifndef KV_H
#define KV_H

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* -------- Protocol constants (do NOT change) ---------------------------- */

#define MAX_KEY_LEN    256
#define MAX_VAL_LEN    256
#define MAX_LINE_LEN   (MAX_KEY_LEN + MAX_VAL_LEN + 64)  /* + command + ttl */

/* Response strings. Each response is one line ending in '\n'. */
#define RESP_OK        "OK\n"
#define RESP_BYE       "BYE\n"
#define RESP_NOTFOUND  "NOT_FOUND\n"

/* -------- Your types go here -------------------------------------------- */

/*
 * TODO (Stage 1): Define your hash-table entry and bucket types.
 *
 * TODO (Stage 2): Define your work-queue type (bounded FIFO of int fds).
 *
 * TODO (Stage 3): Add an rwlock to your table type.
 *
 * TODO (Stage 4): Add expiration timestamp to entries; declare the sweeper
 *                 thread function.
 */

/* one entry in the hash table (linked list node) */
struct entry {
    char key[MAX_KEY_LEN];
    char val[MAX_VAL_LEN];
    time_t expire;          /* 0 means never expires (Stage 4) */
    struct entry *next;
};

/* the hash table (rwlock added in Stage 3) */
struct table {
    struct entry **buckets;
    int num_buckets;
    pthread_rwlock_t lock;
    /* counters for STATS */
    atomic_long hits;
    atomic_long misses;
    atomic_long puts;
    atomic_long dels;
    atomic_long keys;
};

/* bounded queue of client fds (Stage 2 producer/consumer) */
struct queue {
    int *fds;
    int cap;
    int head;
    int tail;
    int count;
    int shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
};

/* -------- Function prototypes you will likely want ---------------------- */

/* Protocol / connection handling (Stage 1) */
void handle_client(int conn_fd);        /* loop: read line, parse, reply */

/* Hash-table operations (Stage 1, made thread-safe in Stage 3) */
/*   Return 0 on success, -1 on not-found / error. */
/*   You design the full signatures -- these are just suggestions. */
/* int  kv_get(const char *key, char *out_val, size_t out_cap); */
/* int  kv_put(const char *key, const char *val, int ttl_seconds); */
/* int  kv_del(const char *key); */


int  kv_get(const char *key, char *out_val, size_t out_cap);
int  kv_put(const char *key, const char *val, int ttl_seconds);
int  kv_del(const char *key);

/* Sweeper thread (Stage 4) */
void *sweeper_thread(void *arg);

#endif /* KV_H */
