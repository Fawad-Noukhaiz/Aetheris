#ifndef IPC_PROTOCOL_H
#define IPC_PROTOCOL_H

#include "ipc.h"

typedef enum {
    REQ_REGISTER      = 1,
    REQ_UNREGISTER    = 2,
    REQ_SEND          = 3,
    REQ_RECV          = 4,
    REQ_TRY_RECV      = 5,
    REQ_RECV_TIMEOUT  = 6,
    REQ_BROADCAST     = 7,
    REQ_LOOKUP        = 8,
    REQ_TRY_SEND      = 9
} request_type_t;

typedef struct {
    request_type_t type;
    int            sender_id;
    char           process_name[MAX_NAME_LEN];
    unsigned int   caps;
    ipc_message_t  msg;
    int            timeout_ms;
} ipc_request_t;

typedef struct {
    ipc_error_t   status;
    int           assigned_id;
    ipc_message_t msg;
} ipc_response_t;

typedef enum {
    MON_QUERY_STATS = 1
} monitor_query_t;

typedef struct {
    int       active_count;
    struct {
        int          id;
        char         name[MAX_NAME_LEN];
        int          queue_len;
        unsigned int caps;
    } processes[MAX_PROCESSES];
    long long total_sent;
    long long total_recv;
    long long total_errors;
    long long uptime_sec;
} monitor_stats_t;

#endif