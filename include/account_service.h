#pragma once

#include <stddef.h>
#include <stdint.h>

#include "offact.h"

struct offact_account_snapshot {
    int populated;
    int slot;
    char name[ACCOUNT_NAME_MAX];
    uint64_t account_id;
    uint64_t generated_account_id;
    char type[ACCOUNT_TYPE_MAX];
    int flags;
    int activated;
};

int account_service_get_snapshot(int slot, struct offact_account_snapshot *out);
int account_service_get_all(struct offact_account_snapshot out[ACCOUNT_NUMB_MAX]);
int account_service_write_accounts_json(char *buf, size_t size);
int account_service_write_diagnostics_json(char *buf, size_t size);
int account_service_write_deepdiff_json(char *buf, size_t size, int from_slot,
                                        int to_slot, int start_rel, int end_rel,
                                        int limit);
int account_service_patch_int_json(char *buf, size_t size, int slot, int rel,
                                   int value, const char *confirm);
int account_service_write_snapshot_json(char *buf, size_t size,
                                        const struct offact_account_snapshot *acct);
void account_service_json_escape(char *dst, size_t dst_size, const char *src);
