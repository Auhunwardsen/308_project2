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

/* ======================================================================
 * Globals
 * ====================================================================== */

/* set to 1 when the user hits Ctrl-C, so all loops can exit cleanly */
volatile sig_atomic_t g_shutdown = 0;

static void sigint_handler(int sig)
{
    (void)sig;
    g_shutdown = 1;
}

/* extra bookkeeping used by the STATS command */
time_t g_start_time;        /* when the server started up */
atomic_long g_active_conns; /* how many clients are connected right now */

/* ======================================================================
 * Stage 1: Socket helpers
 * ====================================================================== */

/* Create a listening TCP socket bound to the given port. Returns fd or -1. */
static int make_listen_socket(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        perror("socket");
        return -1;
    }
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt");
        close(fd);
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(fd);
        return -1;
    }
    if (listen(fd, 64) < 0)
    {
        perror("listen");
        close(fd);
        return -1;
    }
    return fd;
}

/* ======================================================================
 * Stage 2: Worker threads
 * ====================================================================== */

/* worker thread: keep grabbing clients from the queue and serving them */
static void *worker_thread(void *arg)
{
    (void)arg;
    while (1)
    {
        int fd = queue_get();
        if (fd < 0)
            break; /* queue was shut down */
        handle_client(fd);
    }
    return NULL;
}




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

/* ======================================================================
 * Entry point: main()
 * ====================================================================== */

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s <port> <num_workers> <num_buckets> [sweeper_interval_ms]\n"
            "   port                TCP port to listen on (1-65535)\n"
            "   num_workers         number of worker threads (>=1)\n"
            "   num_buckets         hash-table bucket count (>=1)\n"
            "   sweeper_interval_ms default 500\n",
            prog);
}

int main(int argc, char **argv)
{
    /* ---- 0. parse and validate command-line args ---- */
    if (argc < 4 || argc > 5)
    {
        usage(argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    int num_workers = atoi(argv[2]);
    int num_buckets = atoi(argv[3]);
    int sweeper_ms = (argc == 5) ? atoi(argv[4]) : 500;

    if (port <= 0 || port > 65535 || num_workers < 1 ||
        num_buckets < 1 || sweeper_ms <= 0)
    {
        usage(argv[0]);
        return 1;
    }

    /* ---- 1. install signal handlers for clean shutdown ---- */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Ignore SIGPIPE: writes to closed sockets should fail with EPIPE, not
     * kill the server. */
    signal(SIGPIPE, SIG_IGN);

    /* ---- 2. open the listening socket (Stage 1) ---- */
    int listen_fd = make_listen_socket(port);
    if (listen_fd < 0)
        return 1;

    fprintf(stderr,
            "kvserver: listening on port %d "
            "(workers=%d, buckets=%d, sweeper=%dms)\n",
            port, num_workers, num_buckets, sweeper_ms);


    /* ---- 3. init shared state (Stages 1, 2, 3) ---- */
    table_init(num_buckets);     /* Stage 1 + Stage 3 (rwlock) */
    queue_init(num_workers * 2); /* Stage 2 */
    atomic_init(&g_active_conns, 0);
    g_start_time = time(NULL);

    /* ---- 4. spawn worker threads (Stage 2) ---- */
    pthread_t *workers = malloc(num_workers * sizeof(pthread_t));
    for (int i = 0; i < num_workers; i++)
    {
        pthread_create(&workers[i], NULL, worker_thread, NULL);
    }

    /* ---- 5. spawn the sweeper thread (Stage 4) ---- */
    pthread_t sweeper;
    pthread_create(&sweeper, NULL, sweeper_thread, &sweeper_ms);

    /* ---- 6. accept loop: hand new clients off to the workers ---- */
    while (!g_shutdown)
    {
        int conn = accept(listen_fd, NULL, NULL);
        if (conn < 0)
        {
            if (errno == EINTR)
                continue; /* signal, just retry */
            if (!g_shutdown)
                perror("accept");
            break;
        }
        queue_put(conn);
    }

    /* ---- 7. shutdown: stop queue, join threads, free everything ---- */
    queue_stop();
    for (int i = 0; i < num_workers; i++)
    {
        pthread_join(workers[i], NULL);
    }
    pthread_join(sweeper, NULL);

    free(workers);
    close(listen_fd);
    table_destroy();
    queue_destroy();
    return 0;
}
