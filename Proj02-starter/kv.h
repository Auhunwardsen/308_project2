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

#define MAX_KEY_LEN 256
#define MAX_VAL_LEN 256
#define MAX_LINE_LEN (MAX_KEY_LEN + MAX_VAL_LEN + 64) /* + command + ttl */

/* Response strings. Each response is one line ending in '\n'. */
#define RESP_OK "OK\n"
#define RESP_BYE "BYE\n"
#define RESP_NOTFOUND "NOT_FOUND\n"

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

/* ===== Stage 1: hash table entry ===== */
/* this is one node in the linked list at each bucket */
struct entry
{
    /* the key string */
    char key[MAX_KEY_LEN];
    /* the value string */
    char val[MAX_VAL_LEN];

    /* Stage 4: when does this entry expire? */
    /* 0 means it never expires */
    time_t expire;

    /* pointer to next entry in the bucket (linked list) */
    struct entry *next;
};

/* ===== Stage 1: the hash table ===== */
/* Stage 3 added the rwlock so threads dont step on each other */
struct table
{
    /* array of bucket pointers (each bucket is a linked list) */
    struct entry **buckets;
    /* how many buckets we have */
    int num_buckets;

    /* Stage 3: reader/writer lock for thread-safety */
    pthread_rwlock_t lock;

    /* counters for the STATS command */
    atomic_long hits;   /* number of successful GETs */
    atomic_long misses; /* number of failed GETs */
    atomic_long puts;   /* number of PUTs */
    atomic_long dels;   /* number of DELs */
    atomic_long keys;   /* current number of keys in the table */
};

/* ===== Stage 2: bounded work queue ===== */
/* the acceptor thread puts client fds in here, */
/* and worker threads take them out to handle */
struct queue
{
    /* the array that holds the client fds */
    int *fds;
    /* total capacity of the fds array */
    int cap;
    /* index where we take the next fd from */
    int head;
    /* index where we put the next fd */
    int tail;
    /* how many fds are currently sitting in the queue */
    int count;
    /* set to 1 when the server is shutting down */
    int shutdown;

    /* lock to protect all the fields above */
    pthread_mutex_t mutex;
    /* signaled when there is room to add another fd */
    pthread_cond_t not_full;
    /* signaled when there is at least one fd available to take */
    pthread_cond_t not_empty;
};

/* -------- Function prototypes you will likely want ---------------------- */

/* Protocol / connection handling (Stage 1) */
void handle_client(int conn_fd); /* loop: read line, parse, reply */

/* Hash-table operations (Stage 1, made thread-safe in Stage 3) */
/*   Return 0 on success, -1 on not-found / error. */
/*   You design the full signatures -- these are just suggestions. */
/* int  kv_get(const char *key, char *out_val, size_t out_cap); */
/* int  kv_put(const char *key, const char *val, int ttl_seconds); */
/* int  kv_del(const char *key); */

int kv_get(const char *key, char *out_val, size_t out_cap);
int kv_put(const char *key, const char *val, int ttl_seconds);
int kv_del(const char *key);

/* Sweeper thread (Stage 4) */
void *sweeper_thread(void *arg);

#endif /* KV_H */
