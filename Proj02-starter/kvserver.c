/*
 * kvserver.c -- Mini-KV server entry point
 *
 * Project 2, CprE 3080, Spring 2026
 *
 * Starter scaffolding: this file gives you a working TCP listener and an
 * argument parser. Everything else -- accept loop, protocol, hash table,
 * thread pool, RW locking, TTL sweeper -- is yours to write.
 *
 * Build: run `make` in this directory. See the provided Makefile.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "kv.h"

/* -------- Globals ------------------------------------------------------- */

static volatile sig_atomic_t g_shutdown = 0;

static void sigint_handler(int sig) {
    (void)sig;
    g_shutdown = 1;
}

/* the one big hash table and the work queue used by all threads */
static struct table g_table;
static struct queue g_queue;

/* used by STATS */
static time_t      g_start_time;
static atomic_long g_active_conns;

/* -------- Hash table (Stage 1, made thread-safe in Stage 3) ------------- */

/* simple djb2 hash function */
static unsigned int hash_str(const char *s) {
    unsigned int h = 5381;
    while (*s) {
        h = h * 33 + (unsigned char)*s;
        s++;
    }
    return h;
}

/* set up the table with n buckets, all empty */
static void table_init(int n) {
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
static void table_destroy(void) {
    for (int i = 0; i < g_table.num_buckets; i++) {
        struct entry *e = g_table.buckets[i];
        while (e != NULL) {
            struct entry *next = e->next;
            free(e);
            e = next;
        }
    }
    free(g_table.buckets);
    pthread_rwlock_destroy(&g_table.lock);
}

/* look up a key, copy its value into out_val. returns 0 on hit, -1 on miss */
int kv_get(const char *key, char *out_val, size_t out_cap) {
    /* read lock: many readers can be in here at once */
    pthread_rwlock_rdlock(&g_table.lock);

    int idx = hash_str(key) % g_table.num_buckets;
    time_t now = time(NULL);

    struct entry *e = g_table.buckets[idx];
    while (e != NULL) {
        if (strcmp(e->key, key) == 0) {
            /* check if it expired (Stage 4) */
            if (e->expire != 0 && now >= e->expire) {
                break;  /* treat as miss; sweeper will clean it up */
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
int kv_put(const char *key, const char *val, int ttl_seconds) {
    /* write lock: only one writer allowed */
    pthread_rwlock_wrlock(&g_table.lock);

    int idx = hash_str(key) % g_table.num_buckets;

    time_t exp = 0;
    if (ttl_seconds > 0) {
        exp = time(NULL) + ttl_seconds;
    }

    /* if key already exists, just update it */
    struct entry *e = g_table.buckets[idx];
    while (e != NULL) {
        if (strcmp(e->key, key) == 0) {
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
    if (new_e == NULL) {
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
int kv_del(const char *key) {
    pthread_rwlock_wrlock(&g_table.lock);

    int idx = hash_str(key) % g_table.num_buckets;

    struct entry *prev = NULL;
    struct entry *e = g_table.buckets[idx];
    while (e != NULL) {
        if (strcmp(e->key, key) == 0) {
            if (prev == NULL) {
                g_table.buckets[idx] = e->next;
            } else {
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

/* -------- Work queue (Stage 2) ----------------------------------------- */

static void queue_init(int cap) {
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

static void queue_destroy(void) {
    free(g_queue.fds);
    pthread_mutex_destroy(&g_queue.mutex);
    pthread_cond_destroy(&g_queue.not_full);
    pthread_cond_destroy(&g_queue.not_empty);
}

/* producer: main thread puts a new client fd in the queue */
static void queue_put(int fd) {
    pthread_mutex_lock(&g_queue.mutex);

    /* wait if the queue is full */
    while (g_queue.count == g_queue.cap && !g_queue.shutdown) {
        pthread_cond_wait(&g_queue.not_full, &g_queue.mutex);
    }

    if (g_queue.shutdown) {
        close(fd);
    } else {
        g_queue.fds[g_queue.tail] = fd;
        g_queue.tail = (g_queue.tail + 1) % g_queue.cap;
        g_queue.count++;
        pthread_cond_signal(&g_queue.not_empty);
    }

    pthread_mutex_unlock(&g_queue.mutex);
}

/* consumer: a worker pulls a client fd out of the queue */
static int queue_get(void) {
    pthread_mutex_lock(&g_queue.mutex);

    while (g_queue.count == 0 && !g_queue.shutdown) {
        pthread_cond_wait(&g_queue.not_empty, &g_queue.mutex);
    }

    if (g_queue.count == 0) {
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
static void queue_stop(void) {
    pthread_mutex_lock(&g_queue.mutex);
    g_queue.shutdown = 1;
    pthread_cond_broadcast(&g_queue.not_full);
    pthread_cond_broadcast(&g_queue.not_empty);
    pthread_mutex_unlock(&g_queue.mutex);
}

/* -------- Socket helpers ------------------------------------------------ */

/* Create a listening TCP socket bound to the given port. Returns fd or -1. */
static int make_listen_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(fd);
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    if (listen(fd, 64) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }
    return fd;
}

/* -------- Protocol / connection handling (Stage 1) ---------------------- */

/* read one line from the socket (up to '\n'). returns length, or -1 on EOF/err */
static int read_line(int fd, char *buf, int cap) {
    int n = 0;
    while (n < cap - 1) {
        char c;
        int r = read(fd, &c, 1);
        if (r < 0) return -1;
        if (r == 0) {
            if (n == 0) return -1;
            break;
        }
        if (c == '\n') break;
        if (c != '\r') {
            buf[n] = c;
            n++;
        }
    }
    buf[n] = '\0';
    return n;
}

/* main loop for one client: read commands, run them, write replies */
void handle_client(int conn_fd) {
    atomic_fetch_add(&g_active_conns, 1);

    char line[MAX_LINE_LEN];
    char key[MAX_KEY_LEN];
    char val[MAX_VAL_LEN];

    while (!g_shutdown) {
        if (read_line(conn_fd, line, sizeof(line)) < 0) break;

        if (strncmp(line, "GET ", 4) == 0) {
            if (sscanf(line + 4, "%255s", key) != 1) {
                dprintf(conn_fd, "ERROR bad GET\n");
            } else {
                char out[MAX_VAL_LEN];
                if (kv_get(key, out, sizeof(out)) == 0) {
                    dprintf(conn_fd, "VALUE %s\n", out);
                } else {
                    dprintf(conn_fd, RESP_NOTFOUND);
                }
            }
        }
        else if (strncmp(line, "PUT ", 4) == 0) {
            int ttl = 0;
            int n = sscanf(line + 4, "%255s %255s %d", key, val, &ttl);
            if (n < 2) {
                dprintf(conn_fd, "ERROR bad PUT\n");
            } else {
                if (n == 2) ttl = 0;
                kv_put(key, val, ttl);
                dprintf(conn_fd, RESP_OK);
            }
        }
        else if (strncmp(line, "DEL ", 4) == 0) {
            if (sscanf(line + 4, "%255s", key) != 1) {
                dprintf(conn_fd, "ERROR bad DEL\n");
            } else {
                if (kv_del(key) == 0) {
                    dprintf(conn_fd, RESP_OK);
                } else {
                    dprintf(conn_fd, RESP_NOTFOUND);
                }
            }
        }
        else if (strcmp(line, "STATS") == 0) {
            dprintf(conn_fd,
                "STATS keys=%ld hits=%ld misses=%ld puts=%ld dels=%ld"
                " active_conns=%ld uptime_s=%ld\n",
                atomic_load(&g_table.keys),
                atomic_load(&g_table.hits),
                atomic_load(&g_table.misses),
                atomic_load(&g_table.puts),
                atomic_load(&g_table.dels),
                atomic_load(&g_active_conns),
                (long)(time(NULL) - g_start_time));
        }
        else if (strcmp(line, "QUIT") == 0) {
            dprintf(conn_fd, RESP_BYE);
            break;
        }
        else if (line[0] != '\0') {
            dprintf(conn_fd, "ERROR unknown command\n");
        }
    }

    close(conn_fd);
    atomic_fetch_sub(&g_active_conns, 1);
}

/* -------- Worker and sweeper threads (Stages 2 and 4) ------------------- */

/* worker thread: keep grabbing clients from the queue and serving them */
static void *worker_thread(void *arg) {
    (void)arg;
    while (1) {
        int fd = queue_get();
        if (fd < 0) break;   /* shutdown */
        handle_client(fd);
    }
    return NULL;
}

/* background sweeper: every so often, walk the table and remove expired keys */
void *sweeper_thread(void *arg) {
    int sleep_ms = *(int *)arg;

    while (!g_shutdown) {
        /* sleep in small chunks so shutdown can stop us quickly */
        int left = sleep_ms;
        while (left > 0 && !g_shutdown) {
            int chunk = 50;
            if (left < chunk) chunk = left;
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = chunk * 1000000L;
            nanosleep(&ts, NULL);
            left -= chunk;
        }
        if (g_shutdown) break;

        time_t now = time(NULL);

        /* sweep one bucket at a time so we don't block readers for too long */
        for (int i = 0; i < g_table.num_buckets && !g_shutdown; i++) {
            pthread_rwlock_wrlock(&g_table.lock);

            struct entry *prev = NULL;
            struct entry *e = g_table.buckets[i];
            while (e != NULL) {
                if (e->expire != 0 && now >= e->expire) {
                    struct entry *dead = e;
                    if (prev == NULL) {
                        g_table.buckets[i] = e->next;
                    } else {
                        prev->next = e->next;
                    }
                    e = e->next;
                    free(dead);
                    atomic_fetch_sub(&g_table.keys, 1);
                } else {
                    prev = e;
                    e = e->next;
                }
            }

            pthread_rwlock_unlock(&g_table.lock);
        }
    }
    return NULL;
}

/* -------- Entry point --------------------------------------------------- */

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s <port> <num_workers> <num_buckets> [sweeper_interval_ms]\n"
        "   port                TCP port to listen on (1-65535)\n"
        "   num_workers         number of worker threads (>=1)\n"
        "   num_buckets         hash-table bucket count (>=1)\n"
        "   sweeper_interval_ms default 500\n",
        prog);
}

int main(int argc, char **argv) {
    if (argc < 4 || argc > 5) {
        usage(argv[0]);
        return 1;
    }
    int port         = atoi(argv[1]);
    int num_workers  = atoi(argv[2]);
    int num_buckets  = atoi(argv[3]);
    int sweeper_ms   = (argc == 5) ? atoi(argv[4]) : 500;

    if (port <= 0 || port > 65535 || num_workers < 1 ||
        num_buckets < 1 || sweeper_ms <= 0) {
        usage(argv[0]);
        return 1;
    }

    /* Install Ctrl-C handler for clean shutdown. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Ignore SIGPIPE: writes to closed sockets should fail with EPIPE, not
     * kill the server. */
    signal(SIGPIPE, SIG_IGN);

    int listen_fd = make_listen_socket(port);
    if (listen_fd < 0) return 1;

    fprintf(stderr,
        "kvserver: listening on port %d "
        "(workers=%d, buckets=%d, sweeper=%dms)\n",
        port, num_workers, num_buckets, sweeper_ms);

    /* ================================================================
     * TODO (Stage 1): Sequential accept loop.
     *   while (!g_shutdown) {
     *       int conn = accept(listen_fd, NULL, NULL);
     *       if (conn < 0) { ...handle EINTR on signal, else perror... }
     *       handle_client(conn);
     *       close(conn);
     *   }
     *
     * TODO (Stage 2): Initialize work queue + spawn worker threads.
     *                 The accept loop now enqueues conn fds instead of
     *                 calling handle_client directly.
     *
     * TODO (Stage 3): Initialize the hash table's rwlock before the accept
     *                 loop starts.
     *
     * TODO (Stage 4): Spawn the sweeper thread; join it on shutdown.
     *
     * TODO (shutdown): drain queue, join all threads, free everything.
     * ================================================================ */

    /* Stage 3: init the table (also creates its rwlock). */
    table_init(num_buckets);

    /* Stage 2: init the work queue. */
    queue_init(num_workers * 2);

    atomic_init(&g_active_conns, 0);
    g_start_time = time(NULL);

    /* Stage 2: spawn worker threads. */
    pthread_t *workers = malloc(num_workers * sizeof(pthread_t));
    for (int i = 0; i < num_workers; i++) {
        pthread_create(&workers[i], NULL, worker_thread, NULL);
    }

    /* Stage 4: spawn sweeper thread. */
    pthread_t sweeper;
    pthread_create(&sweeper, NULL, sweeper_thread, &sweeper_ms);

    /* main thread: accept new clients and put them on the queue */
    while (!g_shutdown) {
        int conn = accept(listen_fd, NULL, NULL);
        if (conn < 0) {
            if (errno == EINTR) continue;
            if (!g_shutdown) perror("accept");
            break;
        }
        queue_put(conn);
    }

    /* shutdown: stop the queue, wait for workers, then clean up */
    queue_stop();
    for (int i = 0; i < num_workers; i++) {
        pthread_join(workers[i], NULL);
    }
    pthread_join(sweeper, NULL);

    free(workers);
    close(listen_fd);
    table_destroy();
    queue_destroy();
    return 0;
}
