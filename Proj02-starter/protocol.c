/*
 * protocol.c -- Mini-KV protocol parsing & per-client connection loop
 *
 * Project 2, CprE 3080, Spring 2026
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "kv.h"

/* ======================================================================
 * Stage 1: Protocol / connection handling
 * ====================================================================== */

/* read one line from the socket (up to '\n'). returns length, or -1 on EOF/err */
static int read_line(int fd, char *buf, int cap)
{
    int n = 0;
    while (n < cap - 1)
    {
        char c;
        int r = read(fd, &c, 1);
        if (r < 0)
            return -1;
        if (r == 0)
        {
            if (n == 0)
                return -1;
            break;
        }
        if (c == '\n')
            break;
        if (c != '\r')
        {
            buf[n] = c;
            n++;
        }
    }
    buf[n] = '\0';
    return n;
}

/* main loop for one client: read commands, run them, write replies */
void handle_client(int conn_fd)
{
    atomic_fetch_add(&g_active_conns, 1);

    char line[MAX_LINE_LEN];
    char key[MAX_KEY_LEN];
    char val[MAX_VAL_LEN];

    while (!g_shutdown)
    {
        /* read one command line, stop if the client disconnected */
        if (read_line(conn_fd, line, sizeof(line)) < 0)
            break;

        /* ---- GET <key> ---- */
        if (strncmp(line, "GET ", 4) == 0)
        {
            if (sscanf(line + 4, "%255s", key) != 1)
            {
                dprintf(conn_fd, "ERROR bad GET\n");
            }
            else
            {
                char out[MAX_VAL_LEN];
                if (kv_get(key, out, sizeof(out)) == 0)
                {
                    dprintf(conn_fd, "VALUE %s\n", out);
                }
                else
                {
                    dprintf(conn_fd, RESP_NOTFOUND);
                }
            }
        }
        /* ---- PUT <key> <val> [ttl] ---- */
        else if (strncmp(line, "PUT ", 4) == 0)
        {
            int ttl = 0;
            int n = sscanf(line + 4, "%255s %255s %d", key, val, &ttl);
            if (n < 2)
            {
                dprintf(conn_fd, "ERROR bad PUT\n");
            }
            else
            {
                if (n == 2)
                    ttl = 0; /* no ttl given -> never expire */
                kv_put(key, val, ttl);
                dprintf(conn_fd, RESP_OK);
            }
        }
        /* ---- DEL <key> ---- */
        else if (strncmp(line, "DEL ", 4) == 0)
        {
            if (sscanf(line + 4, "%255s", key) != 1)
            {
                dprintf(conn_fd, "ERROR bad DEL\n");
            }
            else
            {
                if (kv_del(key) == 0)
                {
                    dprintf(conn_fd, RESP_OK);
                }
                else
                {
                    dprintf(conn_fd, RESP_NOTFOUND);
                }
            }
        }
        /* ---- STATS ---- */
        else if (strcmp(line, "STATS") == 0)
        {
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
        /* ---- QUIT ---- */
        else if (strcmp(line, "QUIT") == 0)
        {
            dprintf(conn_fd, RESP_BYE);
            break;
        }
        /* ---- anything else ---- */
        else if (line[0] != '\0')
        {
            dprintf(conn_fd, "ERROR unknown command\n");
        }
    }

    close(conn_fd);
    atomic_fetch_sub(&g_active_conns, 1);
}
