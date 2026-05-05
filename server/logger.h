#ifndef LOGGER_H
#define LOGGER_H

#include "../include/ipc.h"

void      logger_init(const char *filepath);
void      logger_shutdown(void);
void      logger_log(const char *event, int sender, int receiver,
                     int msg_id, msg_priority_t priority, size_t size);

long long logger_total_sent(void);
long long logger_total_recv(void);
long long logger_total_errors(void);

#endif