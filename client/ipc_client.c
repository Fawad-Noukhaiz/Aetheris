#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <errno.h>

#include "../include/ipc.h"
#include "../include/ipc_protocol.h"

static __thread int g_server_fd = -1;
static __thread int g_my_id     = -1;
static __thread char g_saved_name[MAX_NAME_LEN];
static __thread unsigned int g_saved_caps = 0;

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

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
    size_t      total = 0;
    const char *p     = (const char *)buf;
    while (total < n) {
        ssize_t w = send(fd, p + total, n - total, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) return -1;
        total += (size_t)w;
    }
    return 0;
}

static int try_connect_and_register(void) {
    if (g_saved_name[0] == '\0') return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SERVER_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    ipc_request_t req;
    ipc_response_t resp;
    memset(&req, 0, sizeof(req));
    req.type = REQ_REGISTER;
    req.caps = g_saved_caps;
    strncpy(req.process_name, g_saved_name, MAX_NAME_LEN - 1);
    req.process_name[MAX_NAME_LEN - 1] = '\0';

    if (safe_write(fd, &req, sizeof(req)) < 0) { close(fd); return -1; }
    if (safe_read(fd, &resp, sizeof(resp)) < 0)  { close(fd); return -1; }

    if (resp.status != IPC_OK) { close(fd); return -1; }

    g_server_fd = fd;
    g_my_id = resp.assigned_id;
    return 0;
}

static int send_request(ipc_request_t *req, ipc_response_t *resp) {
    int attempt = 0;
retry:
    if (g_server_fd < 0) {
        /* do not auto-register for explicit register requests */
        if (req->type == REQ_REGISTER) return IPC_ERR_SERVER_DOWN;
        if (try_connect_and_register() != 0) return IPC_ERR_SERVER_DOWN;
    }

    /* ensure sender id is current for non-register requests */
    if (req->type != REQ_REGISTER) req->sender_id = g_my_id;

    if (safe_write(g_server_fd, req,  sizeof(*req))  < 0) {
        close(g_server_fd); g_server_fd = -1;
        if (attempt++ == 0) goto retry;
        return IPC_ERR_SERVER_DOWN;
    }
    if (safe_read (g_server_fd, resp, sizeof(*resp)) < 0) {
        close(g_server_fd); g_server_fd = -1;
        if (attempt++ == 0) goto retry;
        return IPC_ERR_SERVER_DOWN;
    }
    return resp->status;
}

static void fill_msg(ipc_message_t *msg, int dest) {
    msg->sender_id    = g_my_id;
    msg->receiver_id  = dest;
    msg->timestamp_ms = now_ms();
    if (msg->size == 0 && msg->data[0] != '\0')
        msg->size = strlen(msg->data);
}

int ipc_init(const char *process_name) {
    return ipc_init_with_caps(process_name, CAP_ALL);
}

int ipc_init_with_caps(const char *process_name, unsigned int caps) {
    /* save name/caps for possible reconnects */
    strncpy(g_saved_name, process_name, MAX_NAME_LEN - 1);
    g_saved_name[MAX_NAME_LEN - 1] = '\0';
    g_saved_caps = caps;

    if (try_connect_and_register() != 0) return IPC_ERR_SERVER_DOWN;
    return g_my_id;
}

void ipc_shutdown(void) {
    if (g_server_fd < 0) return;
    ipc_request_t  req;
    ipc_response_t resp;
    memset(&req, 0, sizeof(req));
    req.type      = REQ_UNREGISTER;
    req.sender_id = g_my_id;
    send_request(&req, &resp);
    close(g_server_fd);
    g_server_fd = -1;
    g_my_id     = -1;
    g_saved_name[0] = '\0';
    g_saved_caps = 0;
}

int ipc_get_my_id(void) { return g_my_id; }

int ipc_send(int dest_id, ipc_message_t *msg) {
    if (g_my_id < 0) return IPC_ERR_NOT_REG;
    fill_msg(msg, dest_id);
    ipc_request_t  req;
    ipc_response_t resp;
    memset(&req, 0, sizeof(req));
    req.type      = REQ_SEND;
    req.sender_id = g_my_id;
    req.msg       = *msg;
    return send_request(&req, &resp);
}

int ipc_recv(ipc_message_t *msg) {
    if (g_my_id < 0) return IPC_ERR_NOT_REG;
    ipc_request_t  req;
    ipc_response_t resp;
    memset(&req, 0, sizeof(req));
    req.type      = REQ_RECV;
    req.sender_id = g_my_id;
    int rc = send_request(&req, &resp);
    if (rc == IPC_OK) *msg = resp.msg;
    return rc;
}

int ipc_try_send(int dest_id, ipc_message_t *msg) {
    if (g_my_id < 0) return IPC_ERR_NOT_REG;
    fill_msg(msg, dest_id);
    ipc_request_t  req;
    ipc_response_t resp;
    memset(&req, 0, sizeof(req));
    req.type      = REQ_TRY_SEND;
    req.sender_id = g_my_id;
    req.msg       = *msg;
    return send_request(&req, &resp);
}

int ipc_try_recv(ipc_message_t *msg) {
    if (g_my_id < 0) return IPC_ERR_NOT_REG;
    ipc_request_t  req;
    ipc_response_t resp;
    memset(&req, 0, sizeof(req));
    req.type      = REQ_TRY_RECV;
    req.sender_id = g_my_id;
    int rc = send_request(&req, &resp);
    if (rc == IPC_OK) *msg = resp.msg;
    return rc;
}

int ipc_recv_timeout(ipc_message_t *msg, int timeout_ms) {
    if (g_my_id < 0) return IPC_ERR_NOT_REG;
    ipc_request_t  req;
    ipc_response_t resp;
    memset(&req, 0, sizeof(req));
    req.type       = REQ_RECV_TIMEOUT;
    req.sender_id  = g_my_id;
    req.timeout_ms = timeout_ms;
    int rc = send_request(&req, &resp);
    if (rc == IPC_OK) *msg = resp.msg;
    return rc;
}

int ipc_broadcast(ipc_message_t *msg) {
    if (g_my_id < 0) return IPC_ERR_NOT_REG;
    fill_msg(msg, 0);
    ipc_request_t  req;
    ipc_response_t resp;
    memset(&req, 0, sizeof(req));
    req.type      = REQ_BROADCAST;
    req.sender_id = g_my_id;
    req.msg       = *msg;
    return send_request(&req, &resp);
}

int ipc_get_id_by_name(const char *name) {
    if (g_server_fd < 0) return IPC_ERR_SERVER_DOWN;
    ipc_request_t  req;
    ipc_response_t resp;
    memset(&req, 0, sizeof(req));
    req.type = REQ_LOOKUP;
    strncpy(req.process_name, name, MAX_NAME_LEN - 1);
    req.process_name[MAX_NAME_LEN - 1] = '\0';
    int rc = send_request(&req, &resp);
    if (rc == IPC_OK) return resp.assigned_id;
    return rc;
}

const char *ipc_strerror(int err) {
    switch (err) {
        case IPC_OK:              return "Success";
        case IPC_ERR_INVALID_DST: return "Invalid destination";
        case IPC_ERR_QUEUE_FULL:  return "Queue full";
        case IPC_ERR_TIMEOUT:     return "Timeout";
        case IPC_ERR_NO_MSG:      return "No message available";
        case IPC_ERR_NOT_REG:     return "Process not registered";
        case IPC_ERR_SERVER_DOWN: return "Server unavailable";
        case IPC_ERR_DENIED:      return "Permission denied (capability)";
        case IPC_ERR_GENERIC:     return "Generic error";
        default:                  return "Unknown error";
    }
}
