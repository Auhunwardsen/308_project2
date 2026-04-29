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

/* simple struct for threads */
struct thread_data {
    const char *host;
    int port;
    int ops;
    int read_pct;
};

/* thread function */
void *client_func(void *arg) {
    struct thread_data *data = (struct thread_data *)arg;

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(data->port);
    inet_pton(AF_INET, data->host, &server.sin_addr);

    connect(sock, (struct sockaddr*)&server, sizeof(server));

    char buffer[256];

    for (int i = 0; i < data->ops; i++) {
        int r = rand() % 100;

        if (r < data->read_pct) {
            snprintf(buffer, sizeof(buffer), "GET key%d\n", rand() % 1000);
        } else {
            snprintf(buffer, sizeof(buffer), "PUT key%d val%d\n",
                     rand() % 1000, rand() % 1000);
        }

        write(sock, buffer, strlen(buffer));
        read(sock, buffer, sizeof(buffer)); /* ignore reply */
    }

    close(sock);
    return NULL;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s <host> <port> <num_clients> <ops_per_client> <read_pct>\n",
        prog);
}

int main(int argc, char **argv) {
    if (argc != 6) {
        usage(argv[0]);
        return 1;
    }

    const char *host      = argv[1];
    int port              = atoi(argv[2]);
    int num_clients       = atoi(argv[3]);
    int ops_per_client    = atoi(argv[4]);
    int read_pct          = atoi(argv[5]);

    if (port <= 0 || num_clients < 1 || ops_per_client < 1 ||
        read_pct < 0 || read_pct > 100) {
        usage(argv[0]);
        return 1;
    }

    (void)host;  /* silence warnings until you implement */

    /* TODO:
     *   1. Spawn num_clients pthreads.
     *   2. Each thread: connect to <host>:<port>, run ops_per_client
     *      operations mixing GETs and PUTs per read_pct.
     *   3. Join all threads.
     *   4. Compute and print total elapsed time and total ops/sec.
     */

    pthread_t threads[num_clients];

    struct thread_data data;
    data.host = host;
    data.port = port;
    data.ops = ops_per_client;
    data.read_pct = read_pct;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* 1. Spawn threads */
    for (int i = 0; i < num_clients; i++) {
        pthread_create(&threads[i], NULL, client_func, &data);
    }

    /* 3. Join threads */
    for (int i = 0; i < num_clients; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    /* 4. Compute time and throughput */
    double total_time =
        (end.tv_sec - start.tv_sec) +
        (end.tv_nsec - start.tv_nsec) / 1e9;

    int total_ops = num_clients * ops_per_client;

    fprintf(stderr, "Time: %.2f sec\n", total_time);
    fprintf(stderr, "Throughput: %.2f ops/sec\n", total_ops / total_time);

    return 0;
}