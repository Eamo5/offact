#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "activation_service.h"
#include "account_service.h"

static pthread_mutex_t activation_mutex = PTHREAD_MUTEX_INITIALIZER;

static int json_get_int(const char *json, const char *key, int *out)
{
    char pat[64];
    const char *p;
    char *end;

    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (!p || !(p = strchr(p, ':')))
        return -1;
    *out = (int)strtol(p + 1, &end, 10);
    return end == p + 1 ? -1 : 0;
}

static int json_get_bool_true(const char *json, const char *key)
{
    char pat[64];
    const char *p;

    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (!p || !(p = strchr(p, ':')))
        return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return strncmp(p, "true", 4) == 0;
}

static int json_get_string_after(const char *json, const char *after,
                                 const char *key, char *out, size_t out_size)
{
    char pat[64];
    const char *p = after ? strstr(json, after) : json;
    size_t n = 0;

    if (!p)
        return -1;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(p, pat);
    if (!p || !(p = strchr(p, ':')) || !(p = strchr(p, '"')))
        return -1;
    p++;
    while (*p && *p != '"' && n + 1 < out_size) {
        if (*p == '\\' && p[1])
            p++;
        out[n++] = *p++;
    }
    out[n] = 0;
    return *p == '"' ? 0 : -1;
}

static int json_get_u64_hex_after(const char *json, const char *after,
                                  const char *key, uint64_t *out)
{
    char tmp[32];
    char *end;

    if (json_get_string_after(json, after, key, tmp, sizeof(tmp)))
        return -1;
    *out = strtoull(tmp, &end, 16);
    return (*tmp && *end == 0) ? 0 : -1;
}

static int snapshots_equal(const struct offact_account_snapshot *a,
                           const struct offact_account_snapshot *b)
{
    return a->populated == b->populated && strcmp(a->name, b->name) == 0 &&
           a->account_id == b->account_id && strcmp(a->type, b->type) == 0 &&
           a->flags == b->flags;
}

static int fail(char *response, size_t response_size, const char *code)
{
    snprintf(response, response_size, "{\"success\":false,\"error\":\"%s\"}", code);
    return -1;
}

int activation_service_activate(const char *body, size_t body_size,
                                char *response, size_t response_size)
{
    struct offact_account_snapshot before[ACCOUNT_NUMB_MAX];
    struct offact_account_snapshot after[ACCOUNT_NUMB_MAX];
    struct offact_account_snapshot expected;
    int slot, flags;
    uint64_t account_id;
    char selected_name[ACCOUNT_NAME_MAX];
    char account_id_hex[32];
    const char *expected_json;
    int changed[ACCOUNT_NUMB_MAX];
    int changed_count = 0;

    (void)body_size;
    if (pthread_mutex_trylock(&activation_mutex) != 0)
        return fail(response, response_size, "ACTIVATION_IN_PROGRESS");

    memset(&expected, 0, sizeof(expected));
    expected_json = strstr(body, "\"expected\"");
    if (json_get_int(body, "slot", &slot) || slot < 1 || slot > ACCOUNT_NUMB_MAX ||
        json_get_string_after(body, "\"expected\"", "name", expected.name, sizeof(expected.name)) ||
        !expected.name[0] ||
        json_get_u64_hex_after(body, "\"expected\"", "accountId", &expected.account_id) ||
        json_get_string_after(body, "\"expected\"", "type", expected.type, sizeof(expected.type)) ||
        !expected_json || json_get_int(expected_json, "flags", &flags) ||
        json_get_string_after(body, "\"confirmation\"", "selectedName", selected_name, sizeof(selected_name)) ||
        !json_get_bool_true(body, "understandsRisk") ||
        json_get_u64_hex_after(body, NULL, "accountId", &account_id)) {
        pthread_mutex_unlock(&activation_mutex);
        return fail(response, response_size, "INVALID_REQUEST");
    }

    expected.slot = slot;
    expected.flags = flags;
    expected.populated = 1;

    account_service_get_all(before);
    if (!before[slot - 1].populated || strcmp(before[slot - 1].name, selected_name) != 0 ||
        strcmp(before[slot - 1].name, expected.name) != 0 ||
        !snapshots_equal(&before[slot - 1], &expected)) {
        pthread_mutex_unlock(&activation_mutex);
        return fail(response, response_size, "SNAPSHOT_MISMATCH");
    }

    if (OffAct_SetAccountId(slot, account_id) ||
        OffAct_SetAccountType(slot, "np") || OffAct_SetAccountFlags(slot, 4098)) {
        pthread_mutex_unlock(&activation_mutex);
        return fail(response, response_size, "WRITE_FAILED");
    }

    account_service_get_all(after);
    for (int i = 0; i < ACCOUNT_NUMB_MAX; i++) {
        if (!snapshots_equal(&before[i], &after[i]))
            changed[changed_count++] = i + 1;
    }

    if (changed_count != 1 || changed[0] != slot) {
        pthread_mutex_unlock(&activation_mutex);
        return fail(response, response_size, "UNEXPECTED_SLOT_CHANGE");
    }

    if (!after[slot - 1].populated || after[slot - 1].account_id != account_id ||
        strcmp(after[slot - 1].type, "np") != 0 || after[slot - 1].flags != 4098) {
        pthread_mutex_unlock(&activation_mutex);
        return fail(response, response_size, "VERIFY_FAILED");
    }

    snprintf(account_id_hex, sizeof(account_id_hex), "%016llx",
             (unsigned long long)after[slot - 1].account_id);
    snprintf(response, response_size,
             "{\"success\":true,\"changedSlots\":[%d],\"accountId\":\"%s\"}",
             slot, account_id_hex);
    pthread_mutex_unlock(&activation_mutex);
    return 0;
}
