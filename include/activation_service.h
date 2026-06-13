#pragma once

#include <stddef.h>

int activation_service_activate(const char *body, size_t body_size,
                                char *response, size_t response_size);
