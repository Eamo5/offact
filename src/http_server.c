#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <microhttpd.h>

#include "account_service.h"
#include "activation_service.h"
#include "app_installer.h"
#include "assets_index_html.h"
#include "browser.h"
#include "http_server.h"
#include "offactd.h"

#define JSON_BUFFER_SIZE 65536

volatile int http_keep_running = 1;

extern const uint8_t icon0_png[];
extern const size_t icon0_png_size;

struct post_state {
    char *data;
    size_t size;
};

static void add_headers(struct MHD_Response *resp, const char *content_type)
{
    MHD_add_response_header(resp, "Content-Type", content_type);
}

static int is_allowed_host(const char *host)
{
    char expected[64];

    if (!host)
        return 1;
    snprintf(expected, sizeof(expected), "127.0.0.1:%d", OFFACT_PORT);
    if (!strcmp(host, expected) || !strcmp(host, "127.0.0.1") ||
        !strcmp(host, "localhost"))
        return 1;
    snprintf(expected, sizeof(expected), "localhost:%d", OFFACT_PORT);
    return !strcmp(host, expected);
}

static int is_allowed_origin(const char *origin)
{
    char expected[64];

    if (!origin || !origin[0])
        return 1;
    snprintf(expected, sizeof(expected), "http://127.0.0.1:%d", OFFACT_PORT);
    if (!strcmp(origin, expected))
        return 1;
    snprintf(expected, sizeof(expected), "http://localhost:%d", OFFACT_PORT);
    return !strcmp(origin, expected);
}

static int has_json_content_type(struct MHD_Connection *conn)
{
    const char *ct = MHD_lookup_connection_value(conn, MHD_HEADER_KIND,
                                                 "Content-Type");
    return ct && strstr(ct, "application/json") != NULL;
}

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

static int json_get_string(const char *json, const char *key, char *out,
                           size_t out_size)
{
    char pat[64];
    const char *p;
    size_t n = 0;

    if (!out_size)
        return -1;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
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

static enum MHD_Result queue_buffer(struct MHD_Connection *conn, int status,
                                    const void *data, size_t size,
                                    const char *content_type,
                                    enum MHD_ResponseMemoryMode mode)
{
    struct MHD_Response *resp = MHD_create_response_from_buffer(size, (void *)data, mode);
    enum MHD_Result ret;

    if (!resp)
        return MHD_NO;
    add_headers(resp, content_type);
    ret = MHD_queue_response(conn, status, resp);
    MHD_destroy_response(resp);
    return ret;
}

static enum MHD_Result queue_json(struct MHD_Connection *conn, int status, char *json)
{
    return queue_buffer(conn, status, json, strlen(json), "application/json",
                        MHD_RESPMEM_MUST_FREE);
}

static enum MHD_Result queue_simple(struct MHD_Connection *conn, int status,
                                    const char *json)
{
    return queue_buffer(conn, status, json, strlen(json), "application/json",
                        MHD_RESPMEM_PERSISTENT);
}

static enum MHD_Result handle_get(struct MHD_Connection *conn, const char *url)
{
    char *json;

    if (!strcmp(url, OFFACT_ROUTE_INDEX) || !strcmp(url, OFFACT_ROUTE_INDEX_HTML))
        return queue_buffer(conn, MHD_HTTP_OK, assets_index_html, assets_index_html_len,
                            "text/html; charset=utf-8", MHD_RESPMEM_PERSISTENT);
    if (!strcmp(url, OFFACT_ROUTE_ICON))
        return queue_buffer(conn, MHD_HTTP_OK, icon0_png, icon0_png_size,
                            "image/png", MHD_RESPMEM_PERSISTENT);
    if (!strcmp(url, OFFACT_ROUTE_STATUS)) {
        json = malloc(256);
        if (!json)
            return MHD_NO;
        snprintf(json, 256,
                 "{\"app\":\"%s\",\"titleId\":\"%s\",\"version\":\"%s\",\"port\":%d}",
                 OFFACT_APP_NAME, OFFACT_TITLE_ID, OFFACT_VERSION, OFFACT_PORT);
        return queue_json(conn, MHD_HTTP_OK, json);
    }
    if (!strcmp(url, OFFACT_ROUTE_ACCOUNTS)) {
        json = malloc(JSON_BUFFER_SIZE);
        if (!json)
            return MHD_NO;
        account_service_write_accounts_json(json, JSON_BUFFER_SIZE);
        return queue_json(conn, MHD_HTTP_OK, json);
    }
    if (!strcmp(url, OFFACT_ROUTE_DIAGNOSTICS)) {
        json = malloc(JSON_BUFFER_SIZE);
        if (!json)
            return MHD_NO;
        account_service_write_diagnostics_json(json, JSON_BUFFER_SIZE);
        return queue_json(conn, MHD_HTTP_OK, json);
    }
    if (!strcmp(url, OFFACT_ROUTE_DEEPDIFF)) {
        const char *from = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "from");
        const char *to = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "to");
        const char *start = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "start");
        const char *end = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "end");
        const char *limit = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "limit");
        json = malloc(JSON_BUFFER_SIZE);
        if (!json)
            return MHD_NO;
        account_service_write_deepdiff_json(json, JSON_BUFFER_SIZE,
                                            from ? atoi(from) : 1,
                                            to ? atoi(to) : 4,
                                            start ? atoi(start) : 0,
                                            end ? atoi(end) : 65535,
                                            limit ? atoi(limit) : 256);
        return queue_json(conn, MHD_HTTP_OK, json);
    }
    return queue_simple(conn, MHD_HTTP_NOT_FOUND, "{\"error\":\"NOT_FOUND\"}");
}

static enum MHD_Result handle_post(struct MHD_Connection *conn, const char *url,
                                   const char *body, size_t body_size)
{
    char *json = malloc(JSON_BUFFER_SIZE);
    int rc;

    if (!json)
        return MHD_NO;
    if (!strcmp(url, OFFACT_ROUTE_ACTIVATE)) {
        if (!has_json_content_type(conn)) {
            snprintf(json, JSON_BUFFER_SIZE,
                     "{\"success\":false,\"error\":\"JSON_REQUIRED\"}");
            return queue_json(conn, MHD_HTTP_UNSUPPORTED_MEDIA_TYPE, json);
        }
        rc = activation_service_activate(body ? body : "", body_size, json, JSON_BUFFER_SIZE);
        return queue_json(conn, rc ? MHD_HTTP_BAD_REQUEST : MHD_HTTP_OK, json);
    }
    if (!strcmp(url, OFFACT_ROUTE_DEBUG_PATCH_INT)) {
        int slot, rel, value;
        char confirm[64];

        if (!has_json_content_type(conn)) {
            snprintf(json, JSON_BUFFER_SIZE,
                     "{\"success\":false,\"error\":\"JSON_REQUIRED\"}");
            return queue_json(conn, MHD_HTTP_UNSUPPORTED_MEDIA_TYPE, json);
        }
        if (json_get_int(body ? body : "", "slot", &slot) ||
            json_get_int(body ? body : "", "rel", &rel) ||
            json_get_int(body ? body : "", "value", &value) ||
            json_get_string(body ? body : "", "confirm", confirm, sizeof(confirm))) {
            snprintf(json, JSON_BUFFER_SIZE,
                     "{\"success\":false,\"error\":\"INVALID_REQUEST\"}");
            return queue_json(conn, MHD_HTTP_BAD_REQUEST, json);
        }
        account_service_patch_int_json(json, JSON_BUFFER_SIZE, slot, rel, value,
                                       confirm);
        return queue_json(conn, strstr(json, "\"success\":true") ? MHD_HTTP_OK : MHD_HTTP_BAD_REQUEST, json);
    }
    if (!strcmp(url, OFFACT_ROUTE_LAUNCHER_INSTALL)) {
        rc = app_installer_install_if_needed();
        snprintf(json, JSON_BUFFER_SIZE, "{\"success\":%s}", rc ? "false" : "true");
        return queue_json(conn, rc ? MHD_HTTP_INTERNAL_SERVER_ERROR : MHD_HTTP_OK, json);
    }
    if (!strcmp(url, OFFACT_ROUTE_BROWSER_OPEN)) {
        rc = browser_open_dashboard();
        snprintf(json, JSON_BUFFER_SIZE, "{\"success\":%s}", rc ? "false" : "true");
        return queue_json(conn, rc ? MHD_HTTP_INTERNAL_SERVER_ERROR : MHD_HTTP_OK, json);
    }
    if (!strcmp(url, OFFACT_ROUTE_SHUTDOWN)) {
        http_keep_running = 0;
        snprintf(json, JSON_BUFFER_SIZE, "{\"success\":true}");
        return queue_json(conn, MHD_HTTP_OK, json);
    }
    free(json);
    return queue_simple(conn, MHD_HTTP_NOT_FOUND, "{\"error\":\"NOT_FOUND\"}");
}

enum MHD_Result http_on_request(void *cls, struct MHD_Connection *conn,
                                const char *url, const char *method,
                                const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls)
{
    struct post_state *st = *con_cls;
    (void)cls;
    (void)version;

    if (!strcmp(method, "OPTIONS"))
        return queue_simple(conn, MHD_HTTP_FORBIDDEN, "{\"error\":\"CORS_DISABLED\"}");

    if (!strcmp(method, "GET"))
        return handle_get(conn, url);

    if (strcmp(method, "POST"))
        return queue_simple(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "{\"error\":\"METHOD_NOT_ALLOWED\"}");

    if (!is_allowed_host(MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Host")))
        return queue_simple(conn, MHD_HTTP_FORBIDDEN, "{\"error\":\"FORBIDDEN_HOST\"}");

    if (!is_allowed_origin(MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Origin")))
        return queue_simple(conn, MHD_HTTP_FORBIDDEN, "{\"error\":\"FORBIDDEN_ORIGIN\"}");

    if (!st) {
        st = calloc(1, sizeof(*st));
        if (!st)
            return MHD_NO;
        *con_cls = st;
        return MHD_YES;
    }
    if (*upload_data_size) {
        char *next;
        if (st->size + *upload_data_size > JSON_BUFFER_SIZE - 1)
            return MHD_NO;
        next = realloc(st->data, st->size + *upload_data_size + 1);
        if (!next)
            return MHD_NO;
        st->data = next;
        memcpy(st->data + st->size, upload_data, *upload_data_size);
        st->size += *upload_data_size;
        st->data[st->size] = 0;
        *upload_data_size = 0;
        return MHD_YES;
    }

    enum MHD_Result ret = handle_post(conn, url, st->data, st->size);
    free(st->data);
    free(st);
    *con_cls = NULL;
    return ret;
}
