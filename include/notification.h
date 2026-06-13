#pragma once

#include <stddef.h>

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

void offact_notify(const char *fmt, ...);
