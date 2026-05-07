#ifndef REGISTRY_H
#define REGISTRY_H

#include "../include/ipc.h"

typedef struct {
    int          id;
    char         name[MAX_NAME_LEN];
    int          socket_fd;
    int          active;
    unsigned int caps;
} process_entry_t;

void registry_init(void);
int  registry_register(const char *name, int sock_fd, unsigned int caps);
void registry_unregister(int id);
int  registry_lookup_by_name(const char *name);
int  registry_get_fd(int id);
unsigned int registry_get_caps(int id);
int  registry_get_all_active(int *ids_out, int max);
void registry_get_snapshot(process_entry_t *out, int *count_out);

#endif