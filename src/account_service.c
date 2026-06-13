#include <stdio.h>
#include <string.h>

#include "account_service.h"

int sceRegMgrGetInt(int, int*);
int sceRegMgrGetStr(int, char*, size_t);
int sceRegMgrGetBin(int, void*, size_t);
int sceRegMgrSetInt(int, int);

#define ACCOUNT_ENTITY_BASE 125829632
#define ACCOUNT_ENTITY_STRIDE 65536

static int account_entity_key(int slot, int rel)
{
    return ACCOUNT_ENTITY_BASE + ((slot - 1) * ACCOUNT_ENTITY_STRIDE) + rel;
}

static unsigned long long fnv1a64(const void *data, size_t size)
{
    const unsigned char *p = data;
    unsigned long long h = 1469598103934665603ULL;

    for (size_t i = 0; i < size; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static size_t bounded_strlen(const char *s, size_t max)
{
    size_t n = 0;

    while (n < max && s[n])
        n++;
    return n;
}

void account_service_json_escape(char *dst, size_t dst_size, const char *src)
{
    size_t n = 0;

    if (!dst_size)
        return;

    for (; src && *src && n + 1 < dst_size; src++) {
        if ((*src == '\\' || *src == '"') && n + 2 < dst_size) {
            dst[n++] = '\\';
            dst[n++] = *src;
        } else if ((unsigned char)*src >= 0x20) {
            dst[n++] = *src;
        }
    }
    dst[n] = 0;
}

int account_service_get_snapshot(int slot, struct offact_account_snapshot *out)
{
    if (!out || slot < 1 || slot > ACCOUNT_NUMB_MAX)
        return -1;

    memset(out, 0, sizeof(*out));
    out->slot = slot;

    OffAct_GetAccountName(slot, out->name);
    if (!out->name[0])
        return 0;

    out->populated = 1;
    OffAct_GetAccountId(slot, &out->account_id);
    OffAct_GetAccountType(slot, out->type);
    OffAct_GetAccountFlags(slot, &out->flags);
    out->generated_account_id = OffAct_GenAccountId(out->name);
    out->activated = (out->account_id == out->generated_account_id &&
                      strcmp(out->type, "np") == 0 && out->flags == 4098);
    return 0;
}

int account_service_get_all(struct offact_account_snapshot out[ACCOUNT_NUMB_MAX])
{
    int count = 0;

    for (int slot = 1; slot <= ACCOUNT_NUMB_MAX; slot++) {
        account_service_get_snapshot(slot, &out[slot - 1]);
        if (out[slot - 1].populated)
            count++;
    }
    return count;
}

int account_service_write_snapshot_json(char *buf, size_t size,
                                        const struct offact_account_snapshot *acct)
{
    char name[ACCOUNT_NAME_MAX * 2];
    char type[ACCOUNT_TYPE_MAX * 2];

    account_service_json_escape(name, sizeof(name), acct->name);
    account_service_json_escape(type, sizeof(type), acct->type);

    return snprintf(buf, size,
                    "{\"slot\":%d,\"name\":\"%s\",\"accountId\":\"%016llx\","
                    "\"generatedAccountId\":\"%016llx\",\"type\":\"%s\","
                    "\"flags\":%d,\"activated\":%s,\"snapshot\":{\"name\":\"%s\","
                    "\"accountId\":\"%016llx\",\"type\":\"%s\",\"flags\":%d}}",
                    acct->slot, name, (unsigned long long)acct->account_id,
                    (unsigned long long)acct->generated_account_id, type,
                    acct->flags, acct->activated ? "true" : "false", name,
                    (unsigned long long)acct->account_id, type, acct->flags);
}

int account_service_write_accounts_json(char *buf, size_t size)
{
    struct offact_account_snapshot accounts[ACCOUNT_NUMB_MAX];
    size_t off = 0;
    int count;

    count = account_service_get_all(accounts);
    off += snprintf(buf + off, size - off, "{\"accounts\":[");
    for (int i = 0, emitted = 0; i < ACCOUNT_NUMB_MAX; i++) {
        if (!accounts[i].populated)
            continue;
        if (emitted++)
            off += snprintf(buf + off, size - off, ",");
        off += account_service_write_snapshot_json(buf + off, size - off, &accounts[i]);
    }
    off += snprintf(buf + off, size - off, "],\"count\":%d}", count);
    return (int)off;
}

static int append_account_diagnostics(char *buf, size_t size, size_t *off,
                                      const struct offact_account_snapshot *acct)
{
    char name[ACCOUNT_NAME_MAX * 2];
    char type[ACCOUNT_TYPE_MAX * 2];
    int id_nonzero = acct->account_id != 0;
    int id_matches_generated = acct->account_id == acct->generated_account_id;
    int type_is_np = strcmp(acct->type, "np") == 0;
    int flags_match = acct->flags == 4098;

    account_service_json_escape(name, sizeof(name), acct->name);
    account_service_json_escape(type, sizeof(type), acct->type);

    return snprintf(buf + *off, size - *off,
                    "{\"slot\":%d,\"name\":\"%s\","
                    "\"accountId\":\"%016llx\","
                    "\"generatedAccountId\":\"%016llx\","
                    "\"type\":\"%s\",\"flags\":%d,"
                    "\"checks\":{\"idNonZero\":%s,"
                    "\"idMatchesGenerated\":%s,\"typeIsNp\":%s,"
                    "\"flagsAre4098\":%s,\"offactActivated\":%s}}",
                    acct->slot, name, (unsigned long long)acct->account_id,
                    (unsigned long long)acct->generated_account_id, type,
                    acct->flags, id_nonzero ? "true" : "false",
                    id_matches_generated ? "true" : "false",
                    type_is_np ? "true" : "false",
                    flags_match ? "true" : "false",
                    acct->activated ? "true" : "false");
}

int account_service_write_diagnostics_json(char *buf, size_t size)
{
    struct offact_account_snapshot accounts[ACCOUNT_NUMB_MAX];
    size_t off = 0;
    int count = account_service_get_all(accounts);

    off += snprintf(buf + off, size - off,
                    "{\"diagnosticVersion\":1,"
                    "\"warning\":\"Read-only diagnostics. This endpoint only reports OffAct-known registry fields: name, account id, account type, and flags. If a PSN prompt remains while these match a working account, another registry field is likely involved.\","
                    "\"expectedActivation\":{\"type\":\"np\",\"flags\":4098,\"accountId\":\"non-zero; usually generated from account name when empty\"},"
                    "\"accounts\":[");
    for (int i = 0, emitted = 0; i < ACCOUNT_NUMB_MAX; i++) {
        if (!accounts[i].populated)
            continue;
        if (emitted++)
            off += snprintf(buf + off, size - off, ",");
        off += append_account_diagnostics(buf, size, &off, &accounts[i]);
    }
    off += snprintf(buf + off, size - off, "],\"count\":%d}", count);
    return (int)off;
}

static void append_comma_if_needed(char *buf, size_t size, size_t *off, int *emitted)
{
    if ((*emitted)++)
        *off += snprintf(buf + *off, size - *off, ",");
}

int account_service_write_deepdiff_json(char *buf, size_t size, int from_slot,
                                        int to_slot, int start_rel, int end_rel,
                                        int limit)
{
    size_t off = 0;
    int emitted = 0;
    int truncated = 0;

    if (from_slot < 1 || from_slot > ACCOUNT_NUMB_MAX ||
        to_slot < 1 || to_slot > ACCOUNT_NUMB_MAX || from_slot == to_slot) {
        return snprintf(buf, size,
                        "{\"success\":false,\"error\":\"INVALID_SLOT\"}");
    }
    if (start_rel < 0)
        start_rel = 0;
    if (end_rel < start_rel || end_rel >= ACCOUNT_ENTITY_STRIDE)
        end_rel = ACCOUNT_ENTITY_STRIDE - 1;
    if (limit <= 0 || limit > 1024)
        limit = 256;

    off += snprintf(buf + off, size - off,
                    "{\"success\":true,\"readOnly\":true,"
                    "\"warning\":\"Deep diff scans readable registry values in each selected account slot and reports differences only. String values are redacted to length/hash. No registry writes are performed.\","
                    "\"fromSlot\":%d,\"toSlot\":%d,\"startRel\":%d,\"endRel\":%d,\"diffs\":[",
                    from_slot, to_slot, start_rel, end_rel);

    for (int rel = start_rel; rel <= end_rel; rel++) {
        int key_a = account_entity_key(from_slot, rel);
        int key_b = account_entity_key(to_slot, rel);
        int ia = 0, ib = 0;
        uint64_t ba = 0, bb = 0;
        char sa[128] = {0}, sb[128] = {0};

        if (!sceRegMgrGetInt(key_a, &ia) && !sceRegMgrGetInt(key_b, &ib) && ia != ib) {
            if (emitted >= limit) {
                truncated = 1;
                break;
            }
            append_comma_if_needed(buf, size, &off, &emitted);
            off += snprintf(buf + off, size - off,
                            "{\"rel\":%d,\"kind\":\"int\",\"from\":%d,\"to\":%d}",
                            rel, ia, ib);
        }

        if (!sceRegMgrGetBin(key_a, &ba, sizeof(ba)) &&
            !sceRegMgrGetBin(key_b, &bb, sizeof(bb)) && ba != bb) {
            if (emitted >= limit) {
                truncated = 1;
                break;
            }
            append_comma_if_needed(buf, size, &off, &emitted);
            off += snprintf(buf + off, size - off,
                            "{\"rel\":%d,\"kind\":\"u64\",\"from\":\"%016llx\",\"to\":\"%016llx\"}",
                            rel, (unsigned long long)ba, (unsigned long long)bb);
        }

        if (!sceRegMgrGetStr(key_a, sa, sizeof(sa)) &&
            !sceRegMgrGetStr(key_b, sb, sizeof(sb)) && strcmp(sa, sb) != 0 &&
            (sa[0] || sb[0])) {
            size_t la = bounded_strlen(sa, sizeof(sa));
            size_t lb = bounded_strlen(sb, sizeof(sb));

            if (emitted >= limit) {
                truncated = 1;
                break;
            }
            append_comma_if_needed(buf, size, &off, &emitted);
            off += snprintf(buf + off, size - off,
                            "{\"rel\":%d,\"kind\":\"str\","
                            "\"fromLen\":%llu,\"toLen\":%llu,"
                            "\"fromHash\":\"%016llx\",\"toHash\":\"%016llx\"}",
                            rel, (unsigned long long)la, (unsigned long long)lb,
                            fnv1a64(sa, la), fnv1a64(sb, lb));
        }
    }

    off += snprintf(buf + off, size - off,
                    "],\"diffCount\":%d,\"truncated\":%s}", emitted,
                    truncated ? "true" : "false");
    return (int)off;
}

int account_service_patch_int_json(char *buf, size_t size, int slot, int rel,
                                   int value, const char *confirm)
{
    struct offact_account_snapshot acct;
    int before = 0;
    int after = 0;
    int key;

    if (!confirm || strcmp(confirm, "PATCH_TEST_ACCOUNT") != 0) {
        return snprintf(buf, size,
                        "{\"success\":false,\"error\":\"CONFIRMATION_REQUIRED\"}");
    }
    if (slot < 1 || slot > ACCOUNT_NUMB_MAX || rel < 0 || rel >= ACCOUNT_ENTITY_STRIDE) {
        return snprintf(buf, size,
                        "{\"success\":false,\"error\":\"INVALID_ARGUMENT\"}");
    }
    if (account_service_get_snapshot(slot, &acct) || !acct.populated) {
        return snprintf(buf, size,
                        "{\"success\":false,\"error\":\"EMPTY_OR_UNREADABLE_SLOT\"}");
    }

    key = account_entity_key(slot, rel);
    if (sceRegMgrGetInt(key, &before)) {
        return snprintf(buf, size,
                        "{\"success\":false,\"error\":\"READ_BEFORE_FAILED\",\"slot\":%d,\"rel\":%d}",
                        slot, rel);
    }
    if (sceRegMgrSetInt(key, value)) {
        return snprintf(buf, size,
                        "{\"success\":false,\"error\":\"WRITE_FAILED\",\"slot\":%d,\"rel\":%d,\"before\":%d,\"requested\":%d}",
                        slot, rel, before, value);
    }
    if (sceRegMgrGetInt(key, &after)) {
        return snprintf(buf, size,
                        "{\"success\":false,\"error\":\"READ_AFTER_FAILED\",\"slot\":%d,\"rel\":%d,\"before\":%d,\"requested\":%d}",
                        slot, rel, before, value);
    }

    return snprintf(buf, size,
                    "{\"success\":%s,\"slot\":%d,\"rel\":%d,\"before\":%d,\"requested\":%d,\"after\":%d}",
                    after == value ? "true" : "false", slot, rel, before, value,
                    after);
}
