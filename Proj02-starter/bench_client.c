/*
 * bench_client.c -- Mini-KV concurrent benchmark client
 *
 * Project 2, CprE 3080, Spring 2026
 *
 * YOU write this file. The scaffolding here is intentionally minimal:
 * an argument parser and nothing else. Your job is to fill in the rest.
 *
 * Usage:
 *   ./bench_client <host> <port> <num_clients> <ops_per_client> <read_pct>
 *
 * Requirements (from the spec):
 *   - Spawn <num_clients> threads.
 *   - Each thread opens its own TCP connection to <host>:<port>.
 *   - Each thread issues <ops_per_client> operations.
 *   - <read_pct> percent of ops are GETs; the rest are PUTs.
 *   - Keys drawn from a small pool (~1000 keys) so GETs actually hit.
 *   - Report total wall-clock time and overall throughput (ops/sec).
 *
 * Hints:
 *   - Use clock_gettime(CLOCK_MONOTONIC, ...) to measure elapsed time.
 *   - Each thread needs its own rand_r() seed to avoid serialization on
 *     the global rand() lock.
 *   - Read the server's reply line-by-line; don't assume one TCP packet
 *     per command.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

/* ===== struct passed to each worker thread ===== */
/* every thread gets a pointer to one of these so it knows */
/* where to connect and what work to do */
struct thread_data
{
    /* server hostname / IP address */
    const char *host;
    /* server port number */
    int port;
    /* how many operations this thread should run */
    int ops;
    /* percent of operations that should be GETs (the rest are PUTs) */
    int read_pct;
};

/* ===== the function each client thread runs ===== */
/* it opens one connection and fires off `ops` requests */
void *client_func(void *arg)
{
    /* cast the void* back to our struct */
    struct thread_data *data = (struct thread_data *)arg;

    /* --- step 1: make a TCP socket --- */
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    /* --- step 2: fill in the server address --- */
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(data->port);
    inet_pton(AF_INET, data->host, &server.sin_addr);

    /* --- step 3: connect to the server --- */
    connect(sock, (struct sockaddr *)&server, sizeof(server));

    /* buffer used for both sending and receiving */
    char buffer[256];

    /* --- step 4: do the operations in a loop --- */
    for (int i = 0; i < data->ops; i++)
    {
        /* roll a number 0..99 to decide GET vs PUT */
        int r = rand() % 100;

        if (r < data->read_pct)
        {
            /* this op is a GET */
            snprintf(buffer, sizeof(buffer), "GET key%d\n", rand() % 1000);
        }
        else
        {
            /* this op is a PUT */
            snprintf(buffer, sizeof(buffer), "PUT key%d val%d\n",
                     rand() % 1000, rand() % 1000);
        }

        /* send the command to the server */
        write(sock, buffer, strlen(buffer));
        /* read the reply back (we dont actually use it) */
        read(sock, buffer, sizeof(buffer)); /* ignore reply */
    }

    /* --- step 5: clean up --- */
    close(sock);
    return NULL;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s <host> <port> <num_clients> <ops_per_client> <read_pct>\n",
            prog);
}

int main(int argc, char **argv)
{
    if (argc != 6)
    {
        usage(argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    int num_clients = atoi(argv[3]);
    int ops_per_client = atoi(argv[4]);
    int read_pct = atoi(argv[5]);

    if (port <= 0 || num_clients < 1 || ops_per_client < 1 ||
        read_pct < 0 || read_pct > 100)
    {
        usage(argv[0]);
        return 1;
    }

    (void)host; /* silence warnings until you implement */

    /* TODO:
     *   1. Spawn num_clients pthreads.
     *   2. Each thread: connect to <host>:<port>, run ops_per_client
     *      operations mixing GETs and PUTs per read_pct.
     *   3. Join all threads.
     *   4. Compute and print total elapsed time and total ops/sec.
     */

    /* ===== set up threads and shared data ===== */

    /* one pthread_t per client thread */
    pthread_t threads[num_clients];

    /* all threads share the same thread_data (read-only for them) */
    struct thread_data data;
    data.host = host;
    data.port = port;
    data.ops = ops_per_client;
    data.read_pct = read_pct;

    /* timestamps so we can measure how long the whole run takes */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* spawn the client threads */
    for (int i = 0; i < num_clients; i++)
    {
        pthread_create(&threads[i], NULL, client_func, &data);
    }

    /* wait for them all to finish */
    for (int i = 0; i < num_clients; i++)
    {
        pthread_join(threads[i], NULL);
    }

    /* stop the timer now that every thread is done */
    clock_gettime(CLOCK_MONOTONIC, &end);

    /* figure out how long it took in seconds */
    double total_time = (end.tv_sec - start.tv_sec)
                      + (end.tv_nsec - start.tv_nsec) / 1000000000.0;

    /* total ops = clients * ops each */
    int total_ops = num_clients * ops_per_client;

    /* print the results */
    fprintf(stderr, "Time: %.2f sec\n", total_time);
    fprintf(stderr, "Throughput: %.2f ops/sec\n", total_ops / total_time);

    return 0;
}