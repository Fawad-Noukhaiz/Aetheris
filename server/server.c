#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "../include/ipc.h"
#include "../include/ipc_protocol.h"
#include "registry.h"
#include "queue.h"
#include "logger.h"

static int          g_server_fd  = -1;
static int          g_monitor_fd = -1;
static volatile int g_running    = 1;
static time_t       g_start_time;

static int             g_msg_counter = 0;
static pthread_mutex_t g_msg_id_mutex = PTHREAD_MUTEX_INITIALIZER;

static int safe_read(int fd, void *buf, size_t n) {
    size_t total = 0;
    char  *p     = (char *)buf;
    while (total < n) {
        ssize_t r = read(fd, p + total, n - total);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;
        total += (size_t)r;
    }
    return 0;
}

static int safe_write(int fd, const void *buf, size_t n) {
    size_t       total = 0;
    const char  *p     = (const char *)buf;
    while (total < n) {
        ssize_t w = write(fd, p + total, n - total);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) return -1;
        total += (size_t)w;
    }
    return 0;
}

static int next_msg_id(void) {
    pthread_mutex_lock(&g_msg_id_mutex);
    int id = ++g_msg_counter;
    pthread_mutex_unlock(&g_msg_id_mutex);
    return id;
}

static int has_cap(int client_id, unsigned int required_cap) {
    unsigned int caps = registry_get_caps(client_id);
    return (caps & required_cap) != 0;
}

static void *worker_thread(void *arg) {
    int sock_fd   = *(int *)arg;
    free(arg);
    int client_id = -1;

    struct ucred cred;
    socklen_t cred_len = sizeof(cred);
    int have_cred = 0;
#ifdef SO_PEERCRED
    if (getsockopt(sock_fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) == 0) {
        have_cred = 1;
        printf("[server] Worker: client pid=%d uid=%d gid=%d\n",
               cred.pid, cred.uid, cred.gid);
    }
#endif

    while (g_running) {
        ipc_request_t  req;
        ipc_response_t resp;
        memset(&resp, 0, sizeof(resp));

        if (safe_read(sock_fd, &req, sizeof(req)) < 0)
            break;

        switch (req.type) {

        case REQ_REGISTER: {
            /* optional UID restriction: set IPC_RESTRICT_UID=1 to enable */
            char *restrict_env = getenv("IPC_RESTRICT_UID");
            if (restrict_env && atoi(restrict_env) != 0) {
                if (!have_cred || cred.uid != getuid()) {
                    resp.status      = IPC_ERR_DENIED;
                    resp.assigned_id = -1;
                    safe_write(sock_fd, &resp, sizeof(resp));
                    break;
                }
            }
            unsigned int caps = (req.caps == 0) ? CAP_ALL : req.caps;
            int id = registry_register(req.process_name, sock_fd, caps);
            if (id < 0) {
                resp.status      = IPC_ERR_GENERIC;
                resp.assigned_id = -1;
            } else {
                client_id        = id;
                resp.status      = IPC_OK;
                resp.assigned_id = id;
                printf("[server] Registered '%s' → ID %d  caps=0x%x\n",
                       req.process_name, id, caps);
            }
            safe_write(sock_fd, &resp, sizeof(resp));
            break;
        }

        case REQ_UNREGISTER: {
            if (client_id > 0) {
                registry_unregister(client_id);
                printf("[server] Unregistered ID %d\n", client_id);
                client_id = -1;
            }
            resp.status = IPC_OK;
            safe_write(sock_fd, &resp, sizeof(resp));
            goto done;
        }

        case REQ_SEND: {
            if (!has_cap(client_id, CAP_SEND)) {
                resp.status = IPC_ERR_DENIED;
                logger_log("ERR_DENIED", client_id,
                           req.msg.receiver_id, 0,
                           req.msg.priority, 0);
                safe_write(sock_fd, &resp, sizeof(resp));
                break;
            }
            req.msg.sender_id = client_id;
            req.msg.msg_id    = next_msg_id();
            ipc_error_t err   = queue_enqueue(req.msg.receiver_id,
                                              &req.msg);
            resp.status = err;
            if (err == IPC_OK)
                logger_log("SEND", client_id, req.msg.receiver_id,
                           req.msg.msg_id, req.msg.priority,
                           req.msg.size);
            else
                logger_log("ERR_SEND", client_id, req.msg.receiver_id,
                           0, req.msg.priority, 0);
            safe_write(sock_fd, &resp, sizeof(resp));
            break;
        }

        case REQ_TRY_SEND: {
            if (!has_cap(client_id, CAP_SEND)) {
                resp.status = IPC_ERR_DENIED;
                safe_write(sock_fd, &resp, sizeof(resp));
                break;
            }
            req.msg.sender_id = client_id;
            req.msg.msg_id    = next_msg_id();
            ipc_error_t err   = queue_enqueue(req.msg.receiver_id,
                                              &req.msg);
            resp.status = err;
            if (err == IPC_OK)
                logger_log("SEND", client_id, req.msg.receiver_id,
                           req.msg.msg_id, req.msg.priority,
                           req.msg.size);
            safe_write(sock_fd, &resp, sizeof(resp));
            break;
        }

        case REQ_RECV: {
            if (!has_cap(client_id, CAP_RECV)) {
                resp.status = IPC_ERR_DENIED;
                safe_write(sock_fd, &resp, sizeof(resp));
                break;
            }
            ipc_message_t msg;
            ipc_error_t   err = queue_dequeue(client_id, &msg);
            resp.status = err;
            if (err == IPC_OK) {
                resp.msg = msg;
                logger_log("RECV", msg.sender_id, client_id,
                           msg.msg_id, msg.priority, msg.size);
            }
            safe_write(sock_fd, &resp, sizeof(resp));
            break;
        }

        case REQ_TRY_RECV: {
            if (!has_cap(client_id, CAP_RECV)) {
                resp.status = IPC_ERR_DENIED;
                safe_write(sock_fd, &resp, sizeof(resp));
                break;
            }
            ipc_message_t msg;
            ipc_error_t   err = queue_try_dequeue(client_id, &msg);
            resp.status = err;
            if (err == IPC_OK) {
                resp.msg = msg;
                logger_log("RECV", msg.sender_id, client_id,
                           msg.msg_id, msg.priority, msg.size);
            }
            safe_write(sock_fd, &resp, sizeof(resp));
            break;
        }

        case REQ_RECV_TIMEOUT: {
            if (!has_cap(client_id, CAP_RECV)) {
                resp.status = IPC_ERR_DENIED;
                safe_write(sock_fd, &resp, sizeof(resp));
                break;
            }
            ipc_message_t msg;
            ipc_error_t   err = queue_dequeue_timeout(client_id,
                                    &msg, req.timeout_ms);
            resp.status = err;
            if (err == IPC_OK) {
                resp.msg = msg;
                logger_log("RECV", msg.sender_id, client_id,
                           msg.msg_id, msg.priority, msg.size);
            } else if (err == IPC_ERR_TIMEOUT) {
                logger_log("TIMEOUT", client_id, client_id,
                           0, MSG_PRIORITY_NORMAL, 0);
            }
            safe_write(sock_fd, &resp, sizeof(resp));
            break;
        }

        case REQ_BROADCAST: {
            if (!has_cap(client_id, CAP_BROADCAST)) {
                resp.status = IPC_ERR_DENIED;
                logger_log("ERR_DENIED", client_id, 0, 0,
                           req.msg.priority, 0);
                safe_write(sock_fd, &resp, sizeof(resp));
                break;
            }
            int ids[MAX_PROCESSES];
            int n = registry_get_all_active(ids, MAX_PROCESSES);
            req.msg.sender_id = client_id;
            int ok_count = 0;
            for (int i = 0; i < n; i++) {
                if (ids[i] == client_id) continue;
                req.msg.receiver_id = ids[i];
                req.msg.msg_id      = next_msg_id();
                if (queue_enqueue(ids[i], &req.msg) == IPC_OK) {
                    logger_log("BCAST", client_id, ids[i],
                               req.msg.msg_id, req.msg.priority,
                               req.msg.size);
                    ok_count++;
                }
            }
            resp.status      = IPC_OK;
            resp.assigned_id = ok_count;
            safe_write(sock_fd, &resp, sizeof(resp));
            break;
        }

        case REQ_LOOKUP: {
            int id = registry_lookup_by_name(req.process_name);
            resp.status      = (id > 0) ? IPC_OK : IPC_ERR_INVALID_DST;
            resp.assigned_id = id;
            safe_write(sock_fd, &resp, sizeof(resp));
            break;
        }

        default:
            resp.status = IPC_ERR_GENERIC;
            safe_write(sock_fd, &resp, sizeof(resp));
            break;
        }
    }

done:
    if (client_id > 0) {
        queue_clear(client_id);
        registry_unregister(client_id);
    }
    close(sock_fd);
    printf("[server] Worker exited (was ID %d)\n", client_id);
    return NULL;
}

static void *monitor_thread(void *arg) {
    (void)arg;

    int mon_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (mon_sock < 0) return NULL;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, MONITOR_SOCK_PATH,
            sizeof(addr.sun_path) - 1);

    unlink(MONITOR_SOCK_PATH);
    if (bind(mon_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("monitor bind");
        close(mon_sock);
        return NULL;
    }
    /* restrict monitor socket to owner only */
    if (chmod(MONITOR_SOCK_PATH, S_IRWXU) < 0) {
        /* non-fatal */
        perror("chmod monitor socket");
    }
    listen(mon_sock, 4);
    g_monitor_fd = mon_sock;

    while (g_running) {
        struct sockaddr_un cli;
        socklen_t len = sizeof(cli);
        int cfd = accept(mon_sock, (struct sockaddr *)&cli, &len);
        if (cfd < 0) break;

        monitor_query_t q;
        while (safe_read(cfd, &q, sizeof(q)) == 0) {
            if (q == MON_QUERY_STATS) {
                monitor_stats_t stats;
                memset(&stats, 0, sizeof(stats));

                process_entry_t entries[MAX_PROCESSES];
                int count = 0;
                registry_get_snapshot(entries, &count);
                stats.active_count = count;

                for (int i = 0; i < count; i++) {
                    stats.processes[i].id        = entries[i].id;
                    stats.processes[i].queue_len =
                        queue_length(entries[i].id);
                    stats.processes[i].caps      = entries[i].caps;
                    strncpy(stats.processes[i].name,
                            entries[i].name,
                            MAX_NAME_LEN - 1);
                    stats.processes[i].name[MAX_NAME_LEN - 1] = '\0';
                }
                stats.total_sent   = logger_total_sent();
                stats.total_recv   = logger_total_recv();
                stats.total_errors = logger_total_errors();
                stats.uptime_sec   =
                    (long long)(time(NULL) - g_start_time);

                safe_write(cfd, &stats, sizeof(stats));
            }
        }
        close(cfd);
    }
    close(mon_sock);
    return NULL;
}

static void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
    if (g_server_fd  >= 0) { close(g_server_fd);  g_server_fd  = -1; }
    if (g_monitor_fd >= 0) { close(g_monitor_fd); g_monitor_fd = -1; }
    unlink(SERVER_SOCK_PATH);
    unlink(MONITOR_SOCK_PATH);
    printf("\n[server] Shutting down cleanly.\n");
    exit(0);
}

int main(void) {
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    /* restrict newly created files/sockets to owner-only by default */
    umask(0077);

    g_start_time = time(NULL);

    system("mkdir -p logs");

    registry_init();
    queue_init();
    logger_init("logs/ipc.log");

    g_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_server_fd < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SERVER_SOCK_PATH,
            sizeof(addr.sun_path) - 1);

    unlink(SERVER_SOCK_PATH);
    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    /* restrict server socket file to owner-only */
    if (chmod(SERVER_SOCK_PATH, S_IRWXU) < 0) {
        perror("chmod server socket");
    }
    if (listen(g_server_fd, 32) < 0) {
        perror("listen"); return 1;
    }

    printf("╔══════════════════════════════════════╗\n");
    printf("║  Microkernel IPC Server  — started   ║\n");
    printf("╚══════════════════════════════════════╝\n");
    printf("[server] Socket : %s\n", SERVER_SOCK_PATH);
    printf("[server] Monitor: %s\n", MONITOR_SOCK_PATH);
    printf("[server] Log    : logs/ipc.log\n\n");

    pthread_t mon_tid;
    pthread_create(&mon_tid, NULL, monitor_thread, NULL);
    pthread_detach(mon_tid);

    while (g_running) {
        struct sockaddr_un cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int cfd = accept(g_server_fd,
                         (struct sockaddr *)&cli_addr, &cli_len);
        if (cfd < 0) {
            if (g_running) perror("accept");
            break;
        }

        int *pfd = malloc(sizeof(int));
        if (!pfd) { close(cfd); continue; }
        *pfd = cfd;

        pthread_t tid;
        pthread_create(&tid, NULL, worker_thread, pfd);
        pthread_detach(tid);
    }

    logger_shutdown();
    return 0;
}