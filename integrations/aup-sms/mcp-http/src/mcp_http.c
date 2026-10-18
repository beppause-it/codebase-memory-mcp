/* CR-27 native, opt-in machine transport. No Lens/ASMS database dependencies. */
#include "mcp_http.h"
#include "daemon/ipc.h"
#include "foundation/compat.h"
#include "foundation/constants.h"
#include "foundation/compat_fs.h"
#include "foundation/log.h"
#include "foundation/platform.h"
#include "foundation/secure_random.h"
#include "foundation/sha256.h"
#include "foundation/subprocess.h"
#include "mcp/mcp.h"
#include "store/store.h"
#include "ui/httpd.h"
#include "ui/http_server.h"
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <yyjson/yyjson.h>
#include <sqlite3.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>

#define CONTRACT "asms-cbm-http/v1"
#define PROTOCOL "2025-06-18"
#define JSON_HEADERS "Content-Type: application/json\r\nCache-Control: no-store\r\n"
#define CFG_MAX ((size_t)CBM_SZ_1K * CBM_SZ_1K)

enum {
    ASMS_AUDIT_SIZE_COLUMN = 9,
    ASMS_REVISION_DIGITS_MAX = 18,
    ASMS_JSON_DEPTH_MAX = 48,
    ASMS_EXIT_ALLOCATION = 70,
    ASMS_EXIT_INPUT_IO = 71,
    ASMS_EXIT_INPUT_JSON = 72,
    ASMS_EXIT_CONFIG = 73,
    ASMS_EXIT_TOOL = 74,
    ASMS_EXIT_RESPONSE_LIMIT = 75,
    ASMS_IDENTIFIER_MAX = 96,
    ASMS_DEFAULT_RESULTS = 100,
    ASMS_AUDIT_FIXED_BYTES = 160,
    ASMS_HTTP_OK = 200,
    ASMS_HTTP_ACCEPTED = 202,
    ASMS_ENDPOINT_BUFFER = 272,
    ASMS_HTTP_BAD_REQUEST = 400,
    ASMS_HTTP_UNAUTHORIZED = 401,
    ASMS_HTTP_NOT_FOUND = 404,
    ASMS_HTTP_METHOD_NOT_ALLOWED = 405,
    ASMS_HTTP_CONFLICT = 409,
    ASMS_HTTP_PRECONDITION = 412,
    ASMS_HTTP_TOO_LARGE = 413,
    ASMS_HTTP_UNAVAILABLE = 503,
    ASMS_RESULTS_MAX = 1000,
    ASMS_ARTIFACT_PATH_MAX = 4000,
    ASMS_DEFAULT_TIMEOUT_MS = 30000,
    ASMS_RPC_ERROR_MAGNITUDE = 32000,
    ASMS_PORT_MAX = 65535,
    ASMS_TIMEOUT_MAX_MS = 120000,
    ASMS_AUDIT_DEFAULT_RETENTION = 604800,
    ASMS_OUTPUT_TOKEN_MAX = 1000000,
    ASMS_DEFAULT_RESPONSE_BYTES = 1048576,
    ASMS_AUDIT_DEFAULT_QUOTA = 10485760,
    ASMS_RESPONSE_MAX_BYTES = 16777216,
    ASMS_AUDIT_RETENTION_MAX = 31536000,
    ASMS_AUDIT_QUOTA_MAX = 1073741824,
    ASMS_PRIVATE_UMASK = 077,
    ASMS_ACCEPT_POLL_MS = 200,
    ASMS_LENS_PORT = 9749,
};

typedef struct {
    yyjson_doc *doc;
    yyjson_val *root;
    const char *tenant, *project, *runtime, *data, *control, *key;
    int port, request_max, response_max, timeout, depth, results, retention, quota, record_max;
} config_t;
typedef struct {
    sqlite3 *db;
    config_t cfg;
    const char *config_path;
    char binary[CBM_SZ_4K], correlation[(CBM_SHA256_HEX_LEN + SKIP_ONE)];
    int db_error;
    char *db_message;
    yyjson_mut_doc *db_errors;
    const char *audit_code;
    char *verification_failure;
    uint64_t calls, failures;
} runtime_t;
static volatile sig_atomic_t stopping;
static void remember_sqlite(runtime_t *rt) {
    int code = sqlite3_extended_errcode(rt->db);
    const char *message = sqlite3_errmsg(rt->db);
    if (!rt->db_error) {
        rt->db_error = code;
        rt->db_message = cbm_strdup(message);
    }
    if (!rt->db_errors) {
        rt->db_errors = yyjson_mut_doc_new(NULL);
        if (rt->db_errors) {
            yyjson_mut_doc_set_root(rt->db_errors, yyjson_mut_arr(rt->db_errors));
        }
    }
    if (rt->db_errors) {
        yyjson_mut_val *cause = yyjson_mut_obj(rt->db_errors);
        yyjson_mut_obj_add_str(rt->db_errors, cause, "type", "sqlite");
        yyjson_mut_obj_add_int(rt->db_errors, cause, "code", code);
        yyjson_mut_obj_add_strcpy(rt->db_errors, cause, "message", message);
        yyjson_mut_arr_append(yyjson_mut_doc_get_root(rt->db_errors), cause);
    }
}
static void sqlite_causes(yyjson_mut_doc *d, yyjson_mut_val *array, runtime_t *rt) {
    if (!rt->db_errors) {
        return;
    }
    size_t i;
    size_t max;
    yyjson_mut_val *cause;
    yyjson_mut_arr_foreach(yyjson_mut_doc_get_root(rt->db_errors), i, max, cause)
        yyjson_mut_arr_append(array, yyjson_mut_val_mut_copy(d, cause));
}
static void stop_signal(int sig) {
    (void)sig;
    stopping = SKIP_ONE;
}
static const char *str(yyjson_val *v, const char *key) {
    const char *s = yyjson_get_str(yyjson_obj_get(v, key));
    return s ? s : "";
}
static bool ident(const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (!n || n > ASMS_IDENTIFIER_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}
static bool hex64(const char *s) {
    if (!s || strlen(s) != CBM_SZ_64) {
        return false;
    }
    for (int i = 0; i < CBM_SZ_64; i++) {
        if (!strchr("0123456789abcdef", s[i])) {
            return false;
        }
    }
    return true;
}
static bool equal_secret(const char *a, const char *b) {
    if (strlen(a) != CBM_SZ_64 || strlen(b) != CBM_SZ_64) {
        return false;
    }
    unsigned diff = 0;
    for (int i = 0; i < CBM_SZ_64; i++) {
        diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
    }
    return diff == 0;
}
static void digest_hex(const unsigned char digest[CBM_SZ_32],
                       char out[(CBM_SHA256_HEX_LEN + SKIP_ONE)]) {
    for (int i = 0; i < CBM_SZ_32; i++) {
        (void)snprintf(out + (size_t)i * CBM_SZ_2, CBM_SZ_3, "%02x", digest[i]);
    }
}
static uint64_t millis(void) {
    struct timespec t;
    if (cbm_clock_gettime(CLOCK_MONOTONIC, &t) != 0) {
        return 0;
    }
    return (uint64_t)t.tv_sec * ASMS_RESULTS_MAX + (uint64_t)t.tv_nsec / ASMS_OUTPUT_TOKEN_MAX;
}
static char *read_stream(FILE *f, size_t max) {
    if (!f) {
        return NULL;
    }
    char *s = malloc(max + SKIP_ONE);
    if (!s) {
        (void)fclose(f);
        return NULL;
    }
    size_t n = fread(s, SKIP_ONE, max, f);
    bool ok = !ferror(f) && fgetc(f) == EOF;
    (void)fclose(f);
    if (!ok) {
        free(s);
        return NULL;
    }
    s[n] = 0;
    if (memchr(s, 0, n)) {
        free(s);
        return NULL;
    }
    return s;
}
static char *read_file(const char *path, size_t max) {
    return read_stream(cbm_fopen(path, "rb"), max);
}
static bool file_digest(const char *path, char out[(CBM_SHA256_HEX_LEN + SKIP_ONE)]) {
    cbm_path_info_t info;
    if (cbm_path_info_utf8(path, &info) != CBM_PATH_INFO_OK || !info.is_regular ||
        info.is_symlink) {
        return false;
    }
    FILE *f = cbm_fopen(path, "rb");
    if (!f) {
        return false;
    }
    cbm_sha256_ctx ctx;
    cbm_sha256_init(&ctx);
    unsigned char buf[CBM_SZ_16K];
    unsigned char digest[CBM_SZ_32];
    size_t n;
    while ((n = fread(buf, SKIP_ONE, sizeof(buf), f)) != 0) {
        cbm_sha256_update(&ctx, buf, n);
    }
    bool ok = !ferror(f);
    (void)fclose(f);
    cbm_sha256_final(&ctx, digest);
    digest_hex(digest, out);
    return ok;
}
/* Strict object recursion rejects duplicate keys and embedded NULs, rather than
 * allowing a signer, dispatcher and parser to disagree about field identity. */
static int key_compare(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
static bool object_keys_unique(yyjson_val *v) {
    size_t count = yyjson_obj_size(v);
    if (!count) {
        return true;
    }
    const char **keys = malloc(count * sizeof(*keys));
    if (!keys) {
        return false;
    }
    size_t i;
    size_t max;
    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_foreach(v, i, max, key, value) {
        keys[i] = yyjson_get_str(key);
    }
    qsort(keys, count, sizeof(*keys), key_compare);
    bool unique = true;
    for (i = SKIP_ONE; i < count; i++) {
        if (strcmp(keys[i - SKIP_ONE], keys[i]) == 0) {
            unique = false;
            break;
        }
    }
    free(keys);
    return unique;
}
static bool unique_json(yyjson_val *v, int depth) { // NOLINT(misc-no-recursion)
    if (depth > ASMS_JSON_DEPTH_MAX) {
        return false;
    }
    if (yyjson_is_str(v)) {
        return strlen(yyjson_get_str(v)) == yyjson_get_len(v);
    }
    if (yyjson_is_obj(v)) {
        if (!object_keys_unique(v)) {
            return false;
        }
        size_t i;
        size_t max;
        yyjson_val *key;
        yyjson_val *value;
        yyjson_obj_foreach(v, i, max, key, value) {
            if (!unique_json(key, depth + SKIP_ONE) || !unique_json(value, depth + SKIP_ONE)) {
                return false;
            }
        }
    } else if (yyjson_is_arr(v)) {
        size_t i;
        size_t max;
        yyjson_val *item;
        yyjson_arr_foreach(v, i, max, item) if (!unique_json(item, depth + SKIP_ONE)) {
            return false;
        }
    }
    return true;
}
static yyjson_doc *parse(const char *s) {
    yyjson_doc *d = s ? yyjson_read(s, strlen(s), 0) : NULL;
    if (d && !unique_json(yyjson_doc_get_root(d), 0)) {
        yyjson_doc_free(d);
        return NULL;
    }
    return d;
}
static int limit(yyjson_val *root, const char *key, int def, int max) {
    yyjson_val *v = yyjson_obj_get(root, key);
    if (!v) {
        return def;
    }
    if (!yyjson_is_int(v) || yyjson_get_sint(v) < SKIP_ONE || yyjson_get_sint(v) > max) {
        return -SKIP_ONE;
    }
    return (int)yyjson_get_sint(v);
}
static void config_free(config_t *c) {
    yyjson_doc_free(c->doc);
    memset(c, 0, sizeof(*c));
}
static bool config_read(const char *path, config_t *c) {
    memset(c, 0, sizeof(*c));
    char parent[CBM_SZ_4K];
    if (!path || strlen(path) >= sizeof(parent)) {
        return false;
    }
    size_t path_length = strlen(path);
    memcpy(parent, path, path_length);
    parent[path_length] = 0;
    char *base = strrchr(parent, '/');
    char *backslash = strrchr(parent, '\\');
    if (backslash && (!base || backslash > base)) {
        base = backslash;
    }
    if (!base || base == parent) {
        return false;
    }
    *base++ = 0;
    char *raw = read_stream(cbm_daemon_ipc_private_read_open(parent, base), CFG_MAX);
    c->doc = parse(raw);
    free(raw);
    if (!c->doc) {
        return false;
    }
    c->root = yyjson_doc_get_root(c->doc);
    c->tenant = str(c->root, "tenant_id");
    c->project = str(c->root, "project_scope");
    c->runtime = str(c->root, "runtime_dir");
    c->data = str(c->root, "data_token_sha256");
    c->control = str(c->root, "control_token_sha256");
    c->key = str(c->root, "attestation_key");
    c->port = limit(c->root, "port", -SKIP_ONE, ASMS_PORT_MAX);
    c->request_max = limit(c->root, "request_max_bytes", CBM_SZ_64K, CBM_HTTP_MAX_BODY);
    c->response_max =
        limit(c->root, "response_max_bytes", ASMS_DEFAULT_RESPONSE_BYTES, ASMS_RESPONSE_MAX_BYTES);
    c->timeout = limit(c->root, "timeout_ms", ASMS_DEFAULT_TIMEOUT_MS, ASMS_TIMEOUT_MAX_MS);
    c->depth = limit(c->root, "max_depth", CBM_SZ_8, CBM_SZ_32);
    c->results = limit(c->root, "max_results", ASMS_DEFAULT_RESULTS, ASMS_RESULTS_MAX);
    c->retention = limit(c->root, "audit_retention_seconds", ASMS_AUDIT_DEFAULT_RETENTION,
                         ASMS_AUDIT_RETENTION_MAX);
    c->quota = limit(c->root, "audit_quota_bytes", ASMS_AUDIT_DEFAULT_QUOTA, ASMS_AUDIT_QUOTA_MAX);
    c->record_max = limit(c->root, "audit_record_max_bytes", CBM_SZ_1K, CBM_SZ_4K);
    if (!ident(c->tenant) || !ident(c->project) || !c->runtime[0] || !hex64(c->data) ||
        !hex64(c->control) || equal_secret(c->data, c->control) || strlen(c->key) < CBM_SZ_32 ||
        c->port <= 0 || c->port == ASMS_LENS_PORT || c->request_max < 0 ||
        c->response_max < CBM_SZ_4K || c->timeout < 0 || c->depth < 0 || c->results < 0 ||
        c->retention < 0 || c->quota < 0 || c->record_max < CBM_SZ_512 ||
        !yyjson_is_arr(yyjson_obj_get(c->root, "indexes"))) {
        config_free(c);
        return false;
    }
    return true;
}
static yyjson_mut_val *envelope(yyjson_mut_doc *d, runtime_t *rt, const char *code,
                                const char *message, bool error) {
    yyjson_mut_val *v = yyjson_mut_obj(d);
    yyjson_mut_obj_add_str(d, v, "contract", CONTRACT);
    yyjson_mut_obj_add_str(d, v, "type", error ? "error" : "result");
    yyjson_mut_obj_add_strcpy(d, v, "code", code);
    yyjson_mut_obj_add_strcpy(d, v, "message", message);
    yyjson_mut_obj_add_strcpy(d, v, "request_id", rt->correlation);
    yyjson_mut_obj_add_strcpy(d, v, "correlation_id", rt->correlation);
    yyjson_mut_obj_add_val(d, v, "details", yyjson_mut_obj(d));
    yyjson_mut_obj_add_val(d, v, "causes", yyjson_mut_arr(d));
    if (error && rt->audit_code && !strncmp(code, "AUDIT_", CBM_SZ_6)) {
        yyjson_mut_val *cause = yyjson_mut_obj(d);
        yyjson_mut_obj_add_str(d, cause, "type", "audit_policy");
        yyjson_mut_obj_add_strcpy(d, cause, "code", rt->audit_code);
        yyjson_mut_obj_add_str(d, cause, "message",
                               "Required audit size or quota policy rejected record");
        yyjson_mut_arr_append(yyjson_mut_obj_get(v, "causes"), cause);
    }
    return v;
}
static char *error_json(runtime_t *rt, const char *code, const char *message, yyjson_val *id) {
    yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *env = envelope(d, rt, code, message, true);
    yyjson_mut_val *root = env;
    if (rt->verification_failure) {
        yyjson_doc *failure = parse(rt->verification_failure);
        if (failure) {
            yyjson_mut_arr_append(yyjson_mut_obj_get(env, "causes"),
                                  yyjson_val_mut_copy(d, yyjson_doc_get_root(failure)));
        }
        yyjson_doc_free(failure);
    }
    sqlite_causes(d, yyjson_mut_obj_get(env, "causes"), rt);
    if (id) {
        root = yyjson_mut_obj(d);
        yyjson_mut_obj_add_str(d, root, "jsonrpc", "2.0");
        yyjson_mut_obj_add_val(d, root, "id", yyjson_val_mut_copy(d, id));
        yyjson_mut_val *err = yyjson_mut_obj(d);
        yyjson_mut_obj_add_int(d, err, "code", -ASMS_RPC_ERROR_MAGNITUDE);
        yyjson_mut_obj_add_strcpy(d, err, "message", message);
        yyjson_mut_obj_add_val(d, err, "data", env);
        yyjson_mut_obj_add_val(d, root, "error", err);
    }
    yyjson_mut_doc_set_root(d, root);
    char *out = yyjson_mut_write(d, 0, NULL);
    yyjson_mut_doc_free(d);
    return out;
}
static char *rpc_result(yyjson_val *id, const char *result) {
    yyjson_doc *r = parse(result);
    if (!r) {
        return NULL;
    }
    yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(d);
    yyjson_mut_obj_add_str(d, root, "jsonrpc", "2.0");
    yyjson_mut_obj_add_val(d, root, "id", yyjson_val_mut_copy(d, id));
    yyjson_mut_obj_add_val(d, root, "result", yyjson_val_mut_copy(d, yyjson_doc_get_root(r)));
    yyjson_mut_doc_set_root(d, root);
    char *out = yyjson_mut_write(d, 0, NULL);
    yyjson_mut_doc_free(d);
    yyjson_doc_free(r);
    return out;
}
static bool exec_sql(runtime_t *rt, const char *sql) {
    int rc = sqlite3_exec(rt->db, sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        remember_sqlite(rt);
        return false;
    }
    return true;
}
static sqlite3_stmt *prepare(runtime_t *rt, const char *sql) {
    sqlite3_stmt *s = NULL;
    if (sqlite3_prepare_v2(rt->db, sql, -SKIP_ONE, &s, NULL) != SQLITE_OK) {
        remember_sqlite(rt);
        return NULL;
    }
    return s;
}
static bool done(runtime_t *rt, sqlite3_stmt *s) {
    if (!s) {
        return false;
    }
    int rc = sqlite3_step(s);
    bool ok = rc == SQLITE_DONE;
    if (!ok) {
        remember_sqlite(rt);
    }
    sqlite3_finalize(s);
    return ok;
}
/* Every caller keeps bound text alive until this statement is finalized. */
static void bind_text(sqlite3_stmt *s, int i, const char *v) {
    (void)sqlite3_bind_text(s, i, v, -SKIP_ONE, NULL);
}
/* No caller text enters audit. Columns are closed-vocabulary metadata. */
static bool audit(runtime_t *rt, const char *source, const char *tool, const char *stage,
                  const char *status, uint64_t duration, size_t bytes) {
    rt->audit_code = NULL;
    if (!exec_sql(rt, "BEGIN IMMEDIATE")) {
        return false;
    }
    sqlite3_stmt *s = prepare(rt, "DELETE FROM audit WHERE at < ?");
    if (!s) {
        goto fail;
    }
    sqlite3_bind_int64(s, SKIP_ONE, (sqlite3_int64)time(NULL) - rt->cfg.retention);
    if (!done(rt, s)) {
        goto fail;
    }
    s = prepare(rt, "SELECT coalesce(sum(size),0) FROM audit");
    if (!s) {
        goto fail;
    }
    int rc = sqlite3_step(s);
    int64_t used = rc == SQLITE_ROW ? sqlite3_column_int64(s, 0) : -SKIP_ONE;
    if (rc != SQLITE_ROW) {
        remember_sqlite(rt);
    }
    sqlite3_finalize(s);
    size_t size =
        strlen(source) + strlen(tool) + strlen(stage) + strlen(status) + ASMS_AUDIT_FIXED_BYTES;
    if (used < 0 || size > (size_t)rt->cfg.record_max || used + (int64_t)size > rt->cfg.quota) {
        rt->audit_code = "AUDIT_QUOTA_EXCEEDED";
        if (used < 0) {
            rt->audit_code = "AUDIT_SIZE_SCAN_FAILED";
        } else if (size > (size_t)rt->cfg.record_max) {
            rt->audit_code = "AUDIT_RECORD_LIMIT";
        }
        goto fail;
    }
    s = prepare(rt, "INSERT INTO audit VALUES(?,?,?,?,?,?,?,?,?)");
    if (!s) {
        goto fail;
    }
    bind_text(s, SKIP_ONE, rt->correlation);
    bind_text(s, CBM_SZ_2, source);
    bind_text(s, CBM_SZ_3, tool);
    bind_text(s, CBM_SZ_4, stage);
    bind_text(s, CBM_SZ_5, status);
    sqlite3_bind_int64(s, CBM_SZ_6, (sqlite3_int64)duration);
    sqlite3_bind_int64(s, CBM_SZ_7, (sqlite3_int64)bytes);
    sqlite3_bind_int64(s, CBM_SZ_8, (sqlite3_int64)time(NULL));
    sqlite3_bind_int64(s, ASMS_AUDIT_SIZE_COLUMN, (sqlite3_int64)size);
    if (!done(rt, s) || !exec_sql(rt, "COMMIT")) {
        goto fail;
    }
    return true;
fail:
    (void)exec_sql(rt, "ROLLBACK");
    return false;
}
static bool allowed_tool(const char *name) {
    return strcmp(name, "get_graph_schema") == 0 || strcmp(name, "search_graph") == 0 ||
           strcmp(name, "query_graph") == 0 || strcmp(name, "trace_path") == 0 ||
           strcmp(name, "get_file_outline") == 0 || strcmp(name, "get_architecture") == 0;
}
static yyjson_mut_val *tool_schema(yyjson_mut_doc *d, const char *name) {
    const char *raw = cbm_mcp_tool_input_schema(name);
    yyjson_doc *schema = parse(raw);
    if (!schema) {
        return NULL;
    }
    yyjson_mut_val *s = yyjson_val_mut_copy(d, yyjson_doc_get_root(schema));
    yyjson_doc_free(schema);
    yyjson_mut_val *props = yyjson_mut_obj_get(s, "properties");
    yyjson_mut_obj_remove_key(props, "project");
    /* Account-cache cursors are not source attestations. This profile uses
     * bounded offset paging over immutable artifacts. */
    yyjson_mut_obj_remove_key(props, "cursor");
    if (!strcmp(name, "get_architecture")) {
        const char *disabled[] = {"entry_node_id", "target_node_id", "expected_generation",
                                  "include_behavior_evidence"};
        for (int i = 0; i < CBM_SZ_4; i++) {
            yyjson_mut_obj_remove_key(props, disabled[i]);
        }
        yyjson_mut_val *choices = yyjson_mut_obj_get(
            yyjson_mut_obj_get(yyjson_mut_obj_get(props, "aspects"), "items"), "enum");
        for (size_t i = 0; i < yyjson_mut_arr_size(choices); i++) {
            if (!strcmp(yyjson_mut_get_str(yyjson_mut_arr_get(choices, i)), "system_structure")) {
                yyjson_mut_arr_remove(choices, i);
                break;
            }
        }
    }
    yyjson_mut_val *source = yyjson_mut_obj(d);
    yyjson_mut_obj_add_str(d, source, "type", "string");
    yyjson_mut_obj_add_uint(d, source, "minLength", SKIP_ONE);
    yyjson_mut_obj_add_val(d, props, "source_id", source);
    yyjson_mut_val *required = yyjson_mut_obj_remove_key(s, "required");
    yyjson_mut_val *new_req = yyjson_mut_arr(d);
    size_t i;
    size_t max;
    yyjson_mut_val *item;
    yyjson_mut_arr_foreach(required, i, max, item) {
        if (strcmp(yyjson_mut_get_str(item), "project") != 0) {
            yyjson_mut_arr_add_strcpy(d, new_req, yyjson_mut_get_str(item));
        }
    }
    yyjson_mut_arr_add_str(d, new_req, "source_id");
    yyjson_mut_obj_add_val(d, s, "required", new_req);
    yyjson_mut_obj_remove_key(s, "additionalProperties");
    yyjson_mut_obj_add_bool(d, s, "additionalProperties", false);
    return s;
}
static char *catalog(void) {
    char *native = cbm_mcp_tools_list();
    yyjson_doc *nd = parse(native);
    free(native);
    if (!nd) {
        return NULL;
    }
    yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(d);
    yyjson_mut_val *tools = yyjson_mut_arr(d);
    size_t i;
    size_t max;
    yyjson_val *tool;
    yyjson_arr_foreach(yyjson_obj_get(yyjson_doc_get_root(nd), "tools"), i, max, tool) {
        const char *name = str(tool, "name");
        if (!allowed_tool(name)) {
            continue;
        }
        yyjson_mut_val *copy = yyjson_val_mut_copy(d, tool);
        yyjson_mut_obj_remove_key(copy, "inputSchema");
        yyjson_mut_obj_add_val(d, copy, "inputSchema", tool_schema(d, name));
        yyjson_mut_arr_append(tools, copy);
    }
    yyjson_mut_obj_add_val(d, root, "tools", tools);
    yyjson_mut_doc_set_root(d, root);
    char *out = yyjson_mut_write(d, 0, NULL);
    yyjson_mut_doc_free(d);
    yyjson_doc_free(nd);
    return out;
}
static yyjson_val *find_index(config_t *c, const char *index) {
    yyjson_val *items = yyjson_obj_get(c->root, "indexes");
    yyjson_val *v;
    yyjson_val *found = NULL;
    size_t i;
    size_t max;
    yyjson_arr_foreach(items, i, max, v) {
        if (strcmp(str(v, "repository_index_id"), index) == 0) {
            if (found) {
                return NULL;
            }
            found = v;
        }
    }
    return found;
}
static char *verification_diagnostic(const char *code, const char *message, int os_code) {
    yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(d);
    yyjson_mut_obj_add_strcpy(d, root, "code", code);
    yyjson_mut_obj_add_strcpy(d, root, "message", message);
    yyjson_mut_obj_add_int(d, root, "errno", os_code);
    yyjson_mut_obj_add_strcpy(d, root, "os_message", os_code ? strerror(os_code) : "");
    yyjson_mut_doc_set_root(d, root);
    char *out = yyjson_mut_write(d, 0, NULL);
    yyjson_mut_doc_free(d);
    return out;
}
static bool verify_index(yyjson_val *index, char **diagnostic) {
    const char *path = str(index, "path");
    const char *project = str(index, "cbm_project");
    char actual[(CBM_SHA256_HEX_LEN + SKIP_ONE)];
    char sidecar[CBM_SZ_4K];
    char generation[CBM_SZ_128];
    *diagnostic = NULL;
    if (!path[0] || strlen(path) > ASMS_ARTIFACT_PATH_MAX ||
        !str(index, "repository_index_id")[0] ||
        strlen(str(index, "repository_index_id")) > CBM_SZ_256 || !ident(project) ||
        !hex64(str(index, "artifact_sha256"))) {
        *diagnostic = verification_diagnostic("INDEX_CONFIG_INVALID",
                                              "Trusted native artifact identity is invalid", 0);
        return false;
    }
    const char *suffixes[] = {"-wal", "-journal"};
    for (int i = 0; i < CBM_SZ_2; i++) {
        (void)snprintf(sidecar, sizeof(sidecar), "%s%s", path, suffixes[i]);
        if (cbm_file_exists(sidecar)) {
            *diagnostic =
                verification_diagnostic("ARTIFACT_NOT_SEALED", "Mutable SQLite sidecar exists", 0);
            return false;
        }
    }
    errno = 0;
    if (!file_digest(path, actual)) {
        *diagnostic = verification_diagnostic("ARTIFACT_READ_FAILED",
                                              "Native artifact cannot be hashed", errno);
        return false;
    }
    if (!equal_secret(actual, str(index, "artifact_sha256"))) {
        *diagnostic = verification_diagnostic("ARTIFACT_CHECKSUM_MISMATCH",
                                              "Verified artifact bytes changed", 0);
        return false;
    }
    cbm_store_t *store = cbm_store_open_path_query(path);
    if (!store) {
        sqlite3 *probe = NULL;
        int rc = sqlite3_open_v2(path, &probe, SQLITE_OPEN_READONLY, NULL);
        if (rc == SQLITE_OK) {
            rc = sqlite3_exec(probe, "SELECT 1 FROM sqlite_master LIMIT 1", NULL, NULL, NULL);
        }
        *diagnostic = verification_diagnostic(
            "ARTIFACT_OPEN_FAILED", probe ? sqlite3_errmsg(probe) : sqlite3_errstr(rc), errno);
        yyjson_doc *original = parse(*diagnostic);
        if (original) {
            yyjson_mut_doc *md = yyjson_doc_mut_copy(original, NULL);
            yyjson_mut_obj_add_int(md, yyjson_mut_doc_get_root(md), "sqlite_code", rc);
            free(*diagnostic);
            *diagnostic = yyjson_mut_write(md, 0, NULL);
            yyjson_mut_doc_free(md);
            yyjson_doc_free(original);
        }
        sqlite3_close(probe);
        return false;
    }
    cbm_project_t p = {0};
    cbm_project_t *projects = NULL;
    int count = 0;
    bool ok = cbm_store_list_projects(store, &projects, &count) == CBM_STORE_OK &&
              count == SKIP_ONE && cbm_store_get_project(store, project, &p) == CBM_STORE_OK &&
              cbm_store_generation(store, generation, sizeof(generation)) == CBM_STORE_OK &&
              strcmp(generation, str(index, "generation")) == 0 &&
              cbm_store_check_integrity_verdict(store) == CBM_INTEGRITY_OK;
    cbm_store_free_projects(projects, count);
    if (!ok) {
        const char *original = cbm_store_error(store);
        cbm_store_diagnostic_t sql = cbm_store_diagnostic(store);
        *diagnostic = verification_diagnostic(
            "ARTIFACT_IDENTITY_OR_INTEGRITY_FAILED",
            original && original[0] ? original
                                    : "Single native project, generation or integrity check failed",
            0);
        yyjson_doc *original_doc = parse(*diagnostic);
        if (original_doc) {
            yyjson_mut_doc *md = yyjson_doc_mut_copy(original_doc, NULL);
            yyjson_mut_val *root = yyjson_mut_doc_get_root(md);
            yyjson_mut_obj_add_int(md, root, "sqlite_code", sql.sqlite_code);
            yyjson_mut_obj_add_strcpy(md, root, "sqlite_message", sql.sqlite_message);
            free(*diagnostic);
            *diagnostic = yyjson_mut_write(md, 0, NULL);
            yyjson_mut_doc_free(md);
            yyjson_doc_free(original_doc);
        }
    }
    cbm_project_free_fields(&p);
    cbm_store_close(store);
    return ok;
}
static bool verify_bounded(runtime_t *rt, yyjson_val *body, const char *source);
static bool attested_index(config_t *cfg, yyjson_val *body, yyjson_val **index) {
    static const char *const fields_allowed[] = {
        "binding_version", "tenant_id",  "project_scope",   "state",        "repository_index_id",
        "cbm_project",     "generation", "artifact_sha256", "activated_at", "expires_at"};
    if (!yyjson_is_obj(body)) {
        return false;
    }
    size_t bi;
    size_t bn;
    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_foreach(body, bi, bn, key, value) {
        (void)value;
        bool allowed = false;
        for (size_t i = 0; i < sizeof(fields_allowed) / sizeof(fields_allowed[0]); i++) {
            if (!strcmp(yyjson_get_str(key), fields_allowed[i])) {
                allowed = true;
            }
        }
        if (!allowed) {
            return false;
        }
    }
    *index = find_index(cfg, str(body, "repository_index_id"));
    yyjson_val *version = yyjson_obj_get(body, "binding_version");
    if (!*index || !yyjson_is_int(version) || yyjson_get_sint(version) != SKIP_ONE ||
        strcmp(str(body, "tenant_id"), cfg->tenant) != 0 ||
        strcmp(str(body, "project_scope"), cfg->project) != 0 ||
        strcmp(str(body, "state"), "READY") != 0) {
        return false;
    }
    const char *fields[] = {"cbm_project", "generation", "artifact_sha256"};
    for (int i = 0; i < CBM_SZ_3; i++) {
        if (strcmp(str(body, fields[i]), str(*index, fields[i])) != 0) {
            return false;
        }
    }
    yyjson_val *start = yyjson_obj_get(body, "activated_at");
    yyjson_val *end = yyjson_obj_get(body, "expires_at");
    int64_t now = (int64_t)time(NULL);
    return yyjson_is_int(start) && yyjson_is_int(end) && yyjson_get_sint(start) <= now &&
           yyjson_get_sint(start) > 0 && yyjson_get_sint(end) > now;
}
static bool authorized(config_t *cfg, const char *header, bool control) {
    if (strncmp(header, "Bearer ", CBM_SZ_7) != 0 || strlen(header + CBM_SZ_7) < CBM_SZ_32) {
        return false;
    }
    char prefix[CBM_SZ_256];
    (void)snprintf(prefix, sizeof(prefix), "asms1.%s.%s.%s.", cfg->tenant, cfg->project,
                   control ? "control" : "data");
    if (strncmp(header + CBM_SZ_7, prefix, strlen(prefix)) != 0 ||
        strlen(header + CBM_SZ_7 + strlen(prefix)) < CBM_SZ_32) {
        return false;
    }
    char digest[(CBM_SHA256_HEX_LEN + SKIP_ONE)];
    cbm_sha256_hex(header + CBM_SZ_7, strlen(header + CBM_SZ_7), digest);
    return equal_secret(digest, control ? cfg->control : cfg->data);
}
static bool signed_request(config_t *cfg, cbm_http_req_t *req,
                           char fingerprint[(CBM_SHA256_HEX_LEN + SKIP_ONE)]) {
    size_t n = strlen(req->method) + strlen(req->path) + strlen(req->idempotency_key) +
               strlen(req->if_match) + req->body_len + CBM_SZ_5;
    char *bytes = malloc(n);
    if (!bytes) {
        return false;
    }
    int size = snprintf(bytes, n, "%s\n%s\n%s\n%s\n%s", req->method, req->path,
                        req->idempotency_key, req->if_match, req->body ? req->body : "");
    unsigned char digest[CBM_SZ_32];
    char signature[(CBM_SHA256_HEX_LEN + SKIP_ONE)];
    cbm_hmac_sha256(cfg->key, strlen(cfg->key), bytes, (size_t)size, digest);
    digest_hex(digest, signature);
    cbm_sha256_hex(bytes, (size_t)size, fingerprint);
    free(bytes);
    return equal_secret(signature, req->asms_signature);
}
static bool revision_parse(const char *text, int64_t *out) {
    if (!text[0] || (text[0] == '0' && text[SKIP_ONE]) || strlen(text) > ASMS_REVISION_DIGITS_MAX) {
        return false;
    }
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    *out = strtoll(text, NULL, CBM_DECIMAL_BASE);
    return *out >= 0;
}
static char *binding_read(runtime_t *rt, const char *source, int64_t *revision, bool *revoked) {
    *revision = 0;
    *revoked = true;
    sqlite3_stmt *s = prepare(rt, "SELECT revision,revoked,body FROM bindings WHERE source=?");
    if (!s) {
        return NULL;
    }
    bind_text(s, SKIP_ONE, source);
    int rc = sqlite3_step(s);
    char *out = NULL;
    if (rc == SQLITE_ROW) {
        *revision = sqlite3_column_int64(s, 0);
        *revoked = sqlite3_column_int(s, SKIP_ONE) != 0;
        out = cbm_strdup((const char *)sqlite3_column_text(s, CBM_SZ_2));
    } else if (rc != SQLITE_DONE) {
        remember_sqlite(rt);
    }
    sqlite3_finalize(s);
    return out;
}
static char *mutation_result(runtime_t *rt, const char *source, int64_t revision, bool revoked) {
    yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *env = envelope(d, rt, "BINDING_UPDATED", "Binding revision persisted", false);
    yyjson_mut_val *details = yyjson_mut_obj_get(env, "details");
    yyjson_mut_obj_add_strcpy(d, details, "source_id", source);
    yyjson_mut_obj_add_sint(d, details, "revision", revision);
    yyjson_mut_obj_add_str(d, details, "state", revoked ? "REVOKED" : "ACTIVE");
    yyjson_mut_doc_set_root(d, env);
    char *out = yyjson_mut_write(d, 0, NULL);
    yyjson_mut_doc_free(d);
    return out;
}

static char *replay_storage_failure(runtime_t *rt, int *status) {
    (void)exec_sql(rt, "ROLLBACK");
    *status = ASMS_HTTP_UNAVAILABLE;
    return error_json(rt, "CONTROL_STORAGE_FAILED", "Control transaction failed", NULL);
}
static char *replay_read(runtime_t *rt, const char *key, const char *fingerprint, int64_t *revision,
                         int *status, bool *handled) {
    *handled = true;
    sqlite3_stmt *statement =
        prepare(rt, "SELECT fingerprint,response,revision FROM replays WHERE key=?");
    if (!statement) {
        return replay_storage_failure(rt, status);
    }
    bind_text(statement, SKIP_ONE, key);
    int rc = sqlite3_step(statement);
    if (rc != SQLITE_ROW) {
        if (rc != SQLITE_DONE) {
            remember_sqlite(rt);
        }
        sqlite3_finalize(statement);
        if (rc != SQLITE_DONE) {
            return replay_storage_failure(rt, status);
        }
        *handled = false;
        return NULL;
    }
    bool same = equal_secret(fingerprint, (const char *)sqlite3_column_text(statement, 0));
    char *response =
        same ? cbm_strdup((const char *)sqlite3_column_text(statement, SKIP_ONE)) : NULL;
    *revision = sqlite3_column_int64(statement, CBM_SZ_2);
    sqlite3_finalize(statement);
    if (!exec_sql(rt, "ROLLBACK")) {
        free(response);
        *status = ASMS_HTTP_UNAVAILABLE;
        return error_json(rt, "CONTROL_STORAGE_FAILED", "Control transaction failed", NULL);
    }
    if (same && response) {
        *status = ASMS_HTTP_OK;
        return response;
    }
    *status = ASMS_HTTP_CONFLICT;
    return error_json(rt, "IDEMPOTENCY_CONFLICT", "Key already identifies different bytes", NULL);
}
static bool mutation_valid(runtime_t *rt, cbm_http_req_t *req, const char *source, bool revoke) {
    if (revoke) {
        return true;
    }
    yyjson_doc *body = parse(req->body);
    yyjson_val *index = NULL;
    bool valid = body && attested_index(&rt->cfg, yyjson_doc_get_root(body), &index) &&
                 verify_bounded(rt, yyjson_doc_get_root(body), source);
    yyjson_doc_free(body);
    return valid;
}
static char *control_mutate(runtime_t *rt, cbm_http_req_t *req, const char *source, int *status,
                            int64_t *revision) {
    char fingerprint[(CBM_SHA256_HEX_LEN + SKIP_ONE)];
    int64_t expected;
    bool revoke = strcmp(req->method, "DELETE") == 0;
    const char *code = "CONTROL_INVALID";
    const char *message = "Signed mutation, idempotency key and revision required";
    *status = ASMS_HTTP_BAD_REQUEST;
    if (!ident(req->idempotency_key) || !revision_parse(req->if_match, &expected) ||
        !signed_request(&rt->cfg, req, fingerprint) || (revoke && req->body_len)) {
        goto error;
    }
    if (!exec_sql(rt, "BEGIN IMMEDIATE")) {
        goto database;
    }
    bool replayed;
    char *replay = replay_read(rt, req->idempotency_key, fingerprint, revision, status, &replayed);
    if (replayed) {
        return replay;
    }
    sqlite3_stmt *s;
    bool was_revoked;
    char *previous = binding_read(rt, source, revision, &was_revoked);
    free(previous);
    if (rt->db_error) {
        goto rollback;
    }
    if (*revision != expected) {
        (void)exec_sql(rt, "ROLLBACK");
        *status = ASMS_HTTP_PRECONDITION;
        code = "REVISION_CONFLICT";
        message = "If-Match does not match current revision";
        goto error;
    }
    bool valid = mutation_valid(rt, req, source, revoke);
    if (!valid) {
        (void)exec_sql(rt, "ROLLBACK");
        *status = ASMS_HTTP_CONFLICT;
        code = "ATTESTATION_UNVERIFIABLE";
        message = "READY attestation, validity or exact local artifact verification failed";
        goto error;
    }
    (*revision)++;
    s = prepare(rt, "INSERT INTO bindings VALUES(?,?,?,?) ON CONFLICT(source) DO UPDATE SET "
                    "revision=excluded.revision,revoked=excluded.revoked,body=excluded.body");
    if (!s) {
        goto rollback;
    }
    bind_text(s, SKIP_ONE, source);
    sqlite3_bind_int64(s, CBM_SZ_2, *revision);
    sqlite3_bind_int(s, CBM_SZ_3, revoke);
    bind_text(s, CBM_SZ_4, revoke ? "{}" : req->body);
    if (!done(rt, s)) {
        goto rollback;
    }
    char *out = mutation_result(rt, source, *revision, revoke);
    s = prepare(rt, "INSERT INTO replays VALUES(?,?,?,?)");
    if (!s || !out) {
        sqlite3_finalize(s);
        free(out);
        goto rollback;
    }
    bind_text(s, SKIP_ONE, req->idempotency_key);
    bind_text(s, CBM_SZ_2, fingerprint);
    bind_text(s, CBM_SZ_3, out);
    sqlite3_bind_int64(s, CBM_SZ_4, *revision);
    if (!done(rt, s) || !exec_sql(rt, "COMMIT")) {
        free(out);
        goto rollback;
    }
    *status = ASMS_HTTP_OK;
    return out;
rollback:
    (void)exec_sql(rt, "ROLLBACK");
database:
    *status = ASMS_HTTP_UNAVAILABLE;
    code = "CONTROL_STORAGE_FAILED";
    message = "Control transaction failed";
error:
    return error_json(rt, code, message, NULL);
}

static bool relative_path_valid(const char *path) {
    size_t n = strlen(path);
    return path[0] && path[0] != '/' && path[0] != '\\' && !strchr(path, ':') &&
           !strchr(path, '\\') && strcmp(path, "..") != 0 && strncmp(path, "../", CBM_SZ_3) != 0 &&
           !strstr(path, "/../") && !(n >= CBM_SZ_3 && strcmp(path + n - CBM_SZ_3, "/..") == 0);
}
static bool architecture_arg_valid(const char *key, yyjson_val *value) {
    if (strcmp(key, "entry_node_id") == 0 || strcmp(key, "target_node_id") == 0 ||
        strcmp(key, "expected_generation") == 0 || strcmp(key, "include_behavior_evidence") == 0) {
        return false;
    }
    if (strcmp(key, "aspects") != 0) {
        return true;
    }
    size_t i;
    size_t n;
    yyjson_val *aspect;
    yyjson_arr_foreach(value, i, n, aspect) {
        if (!yyjson_is_str(aspect) || strcmp(yyjson_get_str(aspect), "system_structure") == 0) {
            return false;
        }
    }
    return true;
}
static bool schema_value_valid(yyjson_val *spec, yyjson_val *value) {
    const char *type = str(spec, "type");
    if ((strcmp(type, "string") == 0 && !yyjson_is_str(value)) ||
        (strcmp(type, "integer") == 0 && !yyjson_is_int(value)) ||
        (strcmp(type, "boolean") == 0 && !yyjson_is_bool(value)) ||
        (strcmp(type, "array") == 0 && !yyjson_is_arr(value))) {
        return false;
    }
    yyjson_val *choices = yyjson_obj_get(spec, "enum");
    if (choices) {
        size_t i;
        size_t n;
        yyjson_val *choice;
        bool matched = false;
        yyjson_arr_foreach(choices, i, n, choice) {
            if (yyjson_equals(choice, value)) {
                matched = true;
            }
        }
        if (!matched) {
            return false;
        }
    }
    if (!yyjson_is_int(value)) {
        return true;
    }
    int64_t number = yyjson_get_sint(value);
    yyjson_val *minimum = yyjson_obj_get(spec, "minimum");
    yyjson_val *maximum = yyjson_obj_get(spec, "maximum");
    return (!minimum || number >= yyjson_get_sint(minimum)) &&
           (!maximum || number <= yyjson_get_sint(maximum));
}
static bool integer_ceiling_valid(config_t *c, const char *key, yyjson_val *value) {
    if (!yyjson_is_int(value)) {
        return true;
    }
    int64_t n = yyjson_get_sint(value);
    if (strcmp(key, "limit") == 0 || strcmp(key, "max_rows") == 0 ||
        strcmp(key, "semantic_limit") == 0) {
        return n > 0 && n <= c->results;
    }
    if (strcmp(key, "depth") == 0) {
        return n >= SKIP_ONE && n <= c->depth;
    }
    return strcmp(key, "max_output_tokens") != 0 || n <= c->response_max / CBM_SZ_4;
}
static bool argument_valid(config_t *c, const char *name, const char *key, yyjson_val *value,
                           yyjson_val *props) {
    if (strcmp(key, "source_id") == 0) {
        return true;
    }
    if (strcmp(key, "project") == 0 || strcmp(key, "cursor") == 0) {
        return false;
    }
    yyjson_val *spec = yyjson_obj_get(props, key);
    if (!spec || !schema_value_valid(spec, value) || !integer_ceiling_valid(c, key, value)) {
        return false;
    }
    if ((strcmp(key, "file_path") == 0 || strcmp(key, "path") == 0) && yyjson_is_str(value) &&
        !relative_path_valid(yyjson_get_str(value))) {
        return false;
    }
    return strcmp(name, "get_architecture") != 0 || architecture_arg_valid(key, value);
}
static bool args_valid(config_t *c, const char *name, yyjson_val *args) {
    if (!yyjson_is_obj(args) || !ident(str(args, "source_id"))) {
        return false;
    }
    yyjson_doc *schema = parse(cbm_mcp_tool_input_schema(name));
    if (!schema) {
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(schema);
    yyjson_val *props = yyjson_obj_get(root, "properties");
    size_t i;
    size_t n;
    yyjson_val *key;
    yyjson_val *value;
    bool ok = true;
    yyjson_obj_foreach(args, i, n, key, value) {
        if (!argument_valid(c, name, yyjson_get_str(key), value, props)) {
            ok = false;
            break;
        }
    }
    yyjson_arr_foreach(yyjson_obj_get(root, "required"), i, n, value) {
        const char *required = yyjson_get_str(value);
        if (strcmp(required, "project") != 0 && !yyjson_obj_get(args, required)) {
            ok = false;
        }
    }
    yyjson_doc_free(schema);
    return ok;
}

static char *native_args(config_t *c, yyjson_val *args, const char *project, const char *tool) {
    yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *v = yyjson_val_mut_copy(d, args);
    yyjson_mut_obj_remove_key(v, "source_id");
    yyjson_mut_obj_add_strcpy(d, v, "project", project);
    const char *limit_key = strcmp(tool, "query_graph") == 0 ? "max_rows" : "limit";
    yyjson_doc *schema = parse(cbm_mcp_tool_input_schema(tool));
    yyjson_val *spec =
        yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(schema), "properties"), limit_key);
    if (spec && !yyjson_mut_obj_get(v, limit_key)) {
        int native_default = yyjson_get_int(yyjson_obj_get(spec, "default"));
        int native_max = yyjson_get_int(yyjson_obj_get(spec, "maximum"));
        int selected =
            native_default > 0 && native_default < c->results ? native_default : c->results;
        if (native_max > 0 && selected > native_max) {
            selected = native_max;
        }
        yyjson_mut_obj_add_int(d, v, limit_key, selected);
    }
    yyjson_doc_free(schema);
    if (!strcmp(tool, "trace_path") && !yyjson_mut_obj_get(v, "depth")) {
        yyjson_mut_obj_add_int(d, v, "depth", c->depth < CBM_SZ_3 ? c->depth : CBM_SZ_3);
    }
    if (strcmp(tool, "get_architecture") != 0 && strcmp(tool, "get_graph_schema") != 0 &&
        strcmp(tool, "get_file_outline") != 0 && !yyjson_mut_obj_get(v, "max_output_tokens")) {
        yyjson_mut_obj_add_int(d, v, "max_output_tokens",
                               c->response_max / CBM_SZ_4 > ASMS_OUTPUT_TOKEN_MAX
                                   ? ASMS_OUTPUT_TOKEN_MAX
                                   : c->response_max / CBM_SZ_4);
    }
    yyjson_mut_doc_set_root(d, v);
    char *out = yyjson_mut_write(d, 0, NULL);
    yyjson_mut_doc_free(d);
    return out;
}
static char *replace_private(const char *text, const char *private_value,
                             const char *public_value) {
    if (!private_value[0]) {
        return cbm_strdup(text);
    }
    size_t a = strlen(private_value);
    size_t b = strlen(public_value);
    size_t count = 0;
    const char *p = text;
    while ((p = strstr(p, private_value)) != NULL) {
        count++;
        p += a;
    }
    size_t length = strlen(text) + (b > a ? count * (b - a) : 0) + SKIP_ONE;
    char *out = malloc(length);
    char *dst = out;
    if (!out) {
        return NULL;
    }
    p = text;
    const char *hit;
    while ((hit = strstr(p, private_value)) != NULL) {
        size_t n = (size_t)(hit - p);
        memcpy(dst, p, n);
        dst += n;
        memcpy(dst, public_value, b);
        dst += b;
        p = hit + a;
    }
    memmove(dst, p, strlen(p) + SKIP_ONE);
    return out;
}
static yyjson_mut_val *public_text(yyjson_mut_doc *d, yyjson_val *v, yyjson_val *index,
                                   const char *source) {
    char *s = cbm_strdup(yyjson_get_str(v));
    const char *fields[] = {"path", "artifact_sha256", "repository_index_id", "cbm_project"};
    for (int i = 0; i < CBM_SZ_4 && s; i++) {
        char *next =
            replace_private(s, str(index, fields[i]), i == CBM_SZ_3 ? source : "[redacted]");
        free(s);
        s = next;
    }
    yyjson_mut_val *out = yyjson_mut_strcpy(d, s ? s : "");
    free(s);
    return out;
}
static yyjson_mut_val *public_copy( // NOLINT(misc-no-recursion)
    yyjson_mut_doc *d, yyjson_val *v, yyjson_val *index, const char *source, int depth) {
    if (depth > CBM_SZ_64) {
        return yyjson_mut_null(d);
    }
    if (yyjson_is_obj(v)) {
        yyjson_mut_val *out = yyjson_mut_obj(d);
        size_t i;
        size_t max;
        yyjson_val *key;
        yyjson_val *val;
        yyjson_obj_foreach(v, i, max, key, val) {
            const char *k = yyjson_get_str(key);
            if (!strcmp(k, "cursor") || !strcmp(k, "next_cursor") || !strcmp(k, "root_path") ||
                !strcmp(k, "db_path") || !strcmp(k, "artifact_sha256") ||
                !strcmp(k, "repository_index_id")) {
                continue;
            }
            yyjson_mut_obj_add(out, yyjson_mut_strcpy(d, k),
                               public_copy(d, val, index, source, depth + SKIP_ONE));
        }
        return out;
    }
    if (yyjson_is_arr(v)) {
        yyjson_mut_val *out = yyjson_mut_arr(d);
        size_t i;
        size_t max;
        yyjson_val *item;
        yyjson_arr_foreach(v, i, max, item)
            yyjson_mut_arr_append(out, public_copy(d, item, index, source, depth + SKIP_ONE));
        return out;
    }
    if (yyjson_is_str(v)) {
        /* MCP text may itself contain a serialized native JSON payload. */
        yyjson_doc *nested = depth < CBM_DECIMAL_BASE ? parse(yyjson_get_str(v)) : NULL;
        if (nested && (yyjson_is_obj(yyjson_doc_get_root(nested)) ||
                       yyjson_is_arr(yyjson_doc_get_root(nested)))) {
            yyjson_mut_doc *nd = yyjson_mut_doc_new(NULL);
            yyjson_mut_doc_set_root(
                nd, public_copy(nd, yyjson_doc_get_root(nested), index, source, depth + SKIP_ONE));
            char *text = yyjson_mut_write(nd, 0, NULL);
            yyjson_mut_val *out = yyjson_mut_strcpy(d, text ? text : "");
            free(text);
            yyjson_mut_doc_free(nd);
            yyjson_doc_free(nested);
            return out;
        }
        yyjson_doc_free(nested);
        return public_text(d, v, index, source);
    }
    return yyjson_val_mut_copy(d, v);
}
static void discard_log(const char *line) {
    (void)line;
}
static int worker_failure(int status, const char *code, const char *message) {
    char *cause = verification_diagnostic(code, message, errno);
    if (cause) {
        (void)fprintf(stderr, "%s\n", cause);
        free(cause);
    }
    return status;
}
static int worker(void) {
    cbm_log_set_level(CBM_LOG_NONE);
    cbm_log_set_sink_ex(discard_log, CBM_LOG_SINK_REPLACE);
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    char *input = malloc(CFG_MAX + SKIP_ONE);
    if (!input) {
        return worker_failure(ASMS_EXIT_ALLOCATION, "WORKER_ALLOCATION_FAILED",
                              "Worker input allocation failed");
    }
    size_t n = fread(input, SKIP_ONE, CFG_MAX, stdin);
    if (ferror(stdin) || fgetc(stdin) != EOF) {
        free(input);
        return worker_failure(ASMS_EXIT_INPUT_IO, "WORKER_INPUT_IO_FAILED",
                              "Worker input incomplete or oversized");
    }
    input[n] = 0;
    yyjson_doc *d = parse(input);
    free(input);
    if (!d) {
        return worker_failure(ASMS_EXIT_INPUT_JSON, "WORKER_INPUT_JSON_INVALID",
                              "Worker input is not unambiguous JSON");
    }
    yyjson_val *root = yyjson_doc_get_root(d);
    config_t cfg;
    if (!config_read(str(root, "config_path"), &cfg)) {
        yyjson_doc_free(d);
        return worker_failure(ASMS_EXIT_CONFIG, "WORKER_CONFIG_UNAVAILABLE",
                              "Trusted worker configuration read or schema validation failed");
    }
    yyjson_val *body = yyjson_obj_get(root, "attestation");
    yyjson_val *index = NULL;
    const char *tool = str(root, "tool");
    yyjson_val *args = yyjson_obj_get(root, "arguments");
    bool verify_only = !strcmp(tool, "__verify");
    char *diagnostic = NULL;
    bool valid = (verify_only || (allowed_tool(tool) && args_valid(&cfg, tool, args))) &&
                 attested_index(&cfg, body, &index) && verify_index(index, &diagnostic);
    char *result = NULL;
    if (valid && verify_only) {
        result = cbm_strdup("{\"verified\":true,\"isError\":false}");
    } else if (valid) {
        cbm_mcp_server_t *srv = cbm_mcp_server_new(NULL);
        char *native = native_args(&cfg, args, str(index, "cbm_project"), tool);
        if (srv && native &&
            cbm_mcp_server_bind_index(srv, str(index, "path"), str(index, "cbm_project"))) {
            cbm_mcp_server_set_tool_profile(srv, CBM_MCP_TOOL_PROFILE_ANALYSIS);
            result = cbm_mcp_handle_tool(srv, tool, native);
        }
        free(native);
        cbm_mcp_server_free(srv);
    }
    if (!result) {
        if (!diagnostic) {
            diagnostic = verification_diagnostic(
                "ATTESTATION_OR_DISPATCH_FAILED",
                "Exact READY attestation or native dispatch could not be established", errno);
        }
        result = cbm_mcp_text_result(diagnostic, true);
    }
    free(diagnostic);
    yyjson_doc *rd = parse(result);
    free(result);
    int exit_status = ASMS_EXIT_TOOL;
    if (rd) {
        yyjson_mut_doc *out = yyjson_mut_doc_new(NULL);
        yyjson_mut_doc_set_root(
            out, public_copy(out, yyjson_doc_get_root(rd), index, str(args, "source_id"), 0));
        char *text = yyjson_mut_write(out, 0, NULL);
        if (text && strlen(text) <= (size_t)cfg.response_max && fputs(text, stdout) >= 0 &&
            fflush(stdout) == 0) {
            exit_status = 0;
        } else if (text && strlen(text) > (size_t)cfg.response_max) {
            exit_status = ASMS_EXIT_RESPONSE_LIMIT;
        }
        free(text);
        yyjson_mut_doc_free(out);
        yyjson_doc_free(rd);
    }
    config_free(&cfg);
    yyjson_doc_free(d);
    return exit_status;
}
typedef struct {
    cbm_proc_result_t process;
    int result;
    int spawn_errno;
    bool timed_out;
    bool capture_limit;
} supervision_t;
static supervision_t supervise(runtime_t *rt, const cbm_proc_opts_t *opts, const char *log,
                               const char *out) {
    cbm_proc_result_t pr = {.outcome = CBM_PROC_SPAWN_FAILED, .tree_quiesced = true};
    cbm_subprocess_t *process = NULL;
    uint64_t started = millis();
    int rc = cbm_subprocess_spawn(opts, &process);
    int spawn_errno = rc < 0 ? errno : 0;
    bool timed_out = false, capture_limit = false;
    if (rc == 0) {
        pr.tree_quiesced = false;
        cbm_proc_poll_t polled;
        while ((polled = cbm_subprocess_poll(process, &pr)) == CBM_PROC_POLL_RUNNING) {
            if (millis() - started >= (uint64_t)rt->cfg.timeout) {
                timed_out = true;
            }
            if (cbm_file_size(log) > rt->cfg.response_max ||
                cbm_file_size(out) > rt->cfg.response_max) {
                capture_limit = true;
            }
            if (timed_out || capture_limit || stopping) {
                (void)cbm_subprocess_request_cancel(process);
            }
            cbm_usleep(CBM_USEC_PER_SEC / CBM_MSEC_PER_SEC);
        }
        if (polled == CBM_PROC_POLL_ERROR) {
            rc = -SKIP_ONE;
        } else {
            cbm_subprocess_destroy(process);
        }
    }
    return (supervision_t){.process = pr,
                           .result = rc,
                           .spawn_errno = spawn_errno,
                           .timed_out = timed_out,
                           .capture_limit = capture_limit};
}
static char *worker_stderr(runtime_t *rt, const char *log, yyjson_val *attestation,
                           yyjson_val *args) {
    char *stderr_text = read_file(log, (size_t)rt->cfg.response_max);
    if (stderr_text) {
        yyjson_val *index = find_index(&rt->cfg, str(attestation, "repository_index_id"));
        const char *fields[] = {"path", "artifact_sha256", "repository_index_id", "cbm_project"};
        for (int i = 0; i < CBM_SZ_4 && stderr_text; i++) {
            char *redacted = replace_private(stderr_text, str(index, fields[i]),
                                             i == CBM_SZ_3 ? str(args, "source_id") : "[redacted]");
            free(stderr_text);
            stderr_text = redacted;
        }
    }
    return stderr_text;
}
static char *worker_success(runtime_t *rt, yyjson_val *attestation, yyjson_val *args,
                            yyjson_val *id, const char *result, const char *stderr_text) {
    char *response = rpc_result(id, result);
    if (response) {
        if (stderr_text && stderr_text[0]) {
            yyjson_doc *rd = parse(response);
            yyjson_doc *sd = NULL;
            yyjson_mut_doc *md = rd ? yyjson_doc_mut_copy(rd, NULL) : NULL;
            if (md) {
                yyjson_mut_val *target = yyjson_mut_obj_get(yyjson_mut_doc_get_root(md), "result");
                yyjson_mut_val *meta = yyjson_mut_obj(md);
                yyjson_mut_doc *textdoc = yyjson_mut_doc_new(NULL);
                yyjson_mut_doc_set_root(textdoc, yyjson_mut_strcpy(textdoc, stderr_text));
                char *serialized = yyjson_mut_write(textdoc, 0, NULL);
                sd = parse(serialized);
                free(serialized);
                yyjson_mut_doc_free(textdoc);
                yyjson_val *index = find_index(&rt->cfg, str(attestation, "repository_index_id"));
                if (sd) {
                    yyjson_mut_obj_add_val(
                        md, meta, "stderr",
                        public_copy(md, yyjson_doc_get_root(sd), index, str(args, "source_id"), 0));
                }
                yyjson_mut_obj_add_val(md, target, "_meta", meta);
                free(response);
                response = yyjson_mut_write(md, 0, NULL);
                yyjson_mut_doc_free(md);
            }
            yyjson_doc_free(rd);
            yyjson_doc_free(sd);
        }
        return response;
    }
    return NULL;
}
static char *worker_call(runtime_t *rt, yyjson_val *attestation, const char *tool, yyjson_val *args,
                         yyjson_val *id) {
    char in[CBM_SZ_4K];
    char out[CBM_SZ_4K];
    char log[CBM_SZ_4K];
    if (snprintf(in, sizeof(in), "%s/%s.in", rt->cfg.runtime, rt->correlation) >= (int)sizeof(in) ||
        snprintf(out, sizeof(out), "%s/%s.out", rt->cfg.runtime, rt->correlation) >=
            (int)sizeof(out)) {
        return error_json(rt, "CAPTURE_PATH_INVALID", "Private worker capture path too long", id);
    }
    (void)snprintf(log, sizeof(log), "%s/%s.err", rt->cfg.runtime, rt->correlation);
    yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(d);
    yyjson_mut_obj_add_strcpy(d, root, "config_path", rt->config_path);
    yyjson_mut_obj_add_strcpy(d, root, "tool", tool);
    yyjson_mut_obj_add_val(d, root, "attestation", yyjson_val_mut_copy(d, attestation));
    yyjson_mut_obj_add_val(d, root, "arguments", yyjson_val_mut_copy(d, args));
    yyjson_mut_doc_set_root(d, root);
    char *input = yyjson_mut_write(d, 0, NULL);
    yyjson_mut_doc_free(d);
    FILE *f = cbm_fopen(in, "wbx");
    bool written = f && input && fwrite(input, SKIP_ONE, strlen(input), f) == strlen(input);
    if (f && fclose(f) != 0) {
        written = false;
    }
    free(input);
    if (!written) {
        cbm_unlink(in);
        return error_json(rt, "CAPTURE_IO_FAILED", "Private worker input could not be written", id);
    }
    const char *argv[] = {rt->binary, "asms-mcp-worker", NULL};
    cbm_proc_opts_t opts = {.bin = rt->binary,
                            .argv = argv,
                            .stdin_file = in,
                            .stdout_file = out,
                            .log_file = log,
                            .quiet_timeout_ms = rt->cfg.timeout};
    supervision_t supervision = supervise(rt, &opts, log, out);
    cbm_proc_result_t pr = supervision.process;
    int rc = supervision.result;
    int spawn_errno = supervision.spawn_errno;
    bool timed_out = supervision.timed_out;
    bool capture_limit = supervision.capture_limit;
    char *stderr_text = worker_stderr(rt, log, attestation, args);
    int64_t stderr_bytes = cbm_file_size(log);
    if (stderr_bytes > rt->cfg.response_max) {
        capture_limit = true;
    }
    char *result = rc == 0 && pr.outcome == CBM_PROC_CLEAN && pr.tree_quiesced
                       ? read_file(out, (size_t)rt->cfg.response_max)
                       : NULL;
    bool retained = !pr.tree_quiesced;
    if (!retained) {
        cbm_unlink(in);
        cbm_unlink(out);
        cbm_unlink(log);
    } else {
        /* Preserve stdin_file lifetime when containment is not established. */
        stopping = SKIP_ONE;
    }
    if (result && !capture_limit && !timed_out) {
        char *response = worker_success(rt, attestation, args, id, result, stderr_text);
        free(result);
        if (response) {
            free(stderr_text);
            return response;
        }
    } else {
        free(result);
    }
    const char *code = "WORKER_FAILED";
    if (timed_out) {
        code = "WORKER_TIMEOUT";
    }
    if (pr.exit_code == ASMS_EXIT_RESPONSE_LIMIT || capture_limit) {
        code = "RESPONSE_LIMIT";
    }
    char *err = error_json(rt, code, "Native worker did not return a complete bounded result", id);
    yyjson_doc *ed = parse(err);
    free(err);
    if (!ed) {
        free(stderr_text);
        return NULL;
    }
    yyjson_mut_doc *md = yyjson_doc_mut_copy(ed, NULL);
    yyjson_doc_free(ed);
    yyjson_mut_val *env =
        yyjson_mut_obj_get(yyjson_mut_obj_get(yyjson_mut_doc_get_root(md), "error"), "data");
    yyjson_mut_val *cause = yyjson_mut_obj(md);
    yyjson_mut_obj_add_str(md, cause, "type", "native_subprocess");
    yyjson_mut_obj_add_int(md, cause, "outcome", pr.outcome);
    yyjson_mut_obj_add_int(md, cause, "exit_code", pr.exit_code);
    yyjson_mut_obj_add_int(md, cause, "term_signal", pr.term_signal);
    yyjson_mut_obj_add_int(md, cause, "supervisor_return", rc);
    yyjson_mut_obj_add_bool(md, cause, "deadline_exceeded", timed_out);
    yyjson_mut_obj_add_bool(md, cause, "capture_limit_exceeded", capture_limit);
    yyjson_mut_obj_add_int(md, cause, "errno", spawn_errno);
    yyjson_mut_obj_add_strcpy(md, cause, "os_message", spawn_errno ? strerror(spawn_errno) : "");
    yyjson_mut_obj_add_strcpy(md, cause, "stderr", stderr_text ? stderr_text : "");
    yyjson_mut_obj_add_sint(md, cause, "stderr_bytes", stderr_bytes);
    yyjson_mut_obj_add_bool(md, cause, "diagnostic_limit_exceeded",
                            stderr_bytes > rt->cfg.response_max);
    free(stderr_text);
    yyjson_mut_obj_add_bool(md, cause, "forced", pr.forced);
    yyjson_mut_obj_add_bool(md, cause, "tree_quiesced", pr.tree_quiesced);
    yyjson_mut_obj_add_bool(md, cause, "supervision_failed", pr.supervision_failed);
    yyjson_mut_obj_add_bool(md, cause, "captures_retained", retained);
    yyjson_mut_arr_append(yyjson_mut_obj_get(env, "causes"), cause);
    err = yyjson_mut_write(md, 0, NULL);
    yyjson_mut_doc_free(md);
    return err;
}
static bool verify_bounded(runtime_t *rt, yyjson_val *body, const char *source) {
    yyjson_doc *args = parse("{\"source_id\":\"verify\"}");
    yyjson_doc *id = parse("0");
    (void)source;
    char *response =
        worker_call(rt, body, "__verify", yyjson_doc_get_root(args), yyjson_doc_get_root(id));
    yyjson_doc *d = parse(response);
    bool ok = d && yyjson_is_true(yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(d), "result"),
                                                 "verified"));
    if (!ok) {
        free(rt->verification_failure);
        rt->verification_failure = response;
        response = NULL;
    }
    free(response);
    yyjson_doc_free(args);
    yyjson_doc_free(id);
    yyjson_doc_free(d);
    return ok;
}
static char *annotate(runtime_t *rt, char *response, bool audit_failed) {
    yyjson_doc *rd = parse(response);
    if (!rd) {
        return response;
    }
    yyjson_val *r = yyjson_doc_get_root(rd);
    yyjson_val *result = yyjson_obj_get(r, "result");
    bool tool_failed = yyjson_is_true(yyjson_obj_get(result, "isError"));
    bool failed = yyjson_obj_get(r, "error") || tool_failed;
    if (audit_failed && !failed) {
        char *err = error_json(rt, "AUDIT_POSTFLIGHT_FAILED", "Required completion audit failed",
                               yyjson_obj_get(r, "id"));
        yyjson_doc *failure = parse(err);
        if (failure) {
            yyjson_mut_doc *fd = yyjson_doc_mut_copy(failure, NULL);
            yyjson_mut_val *details = yyjson_mut_obj_get(
                yyjson_mut_obj_get(yyjson_mut_obj_get(yyjson_mut_doc_get_root(fd), "error"),
                                   "data"),
                "details");
            yyjson_mut_obj_add_str(fd, details, "original_tool_outcome", "SUCCEEDED");
            free(err);
            err = yyjson_mut_write(fd, 0, NULL);
            yyjson_mut_doc_free(fd);
            yyjson_doc_free(failure);
        }
        yyjson_doc_free(rd);
        free(response);
        return err;
    }
    yyjson_mut_doc *d = yyjson_doc_mut_copy(rd, NULL);
    yyjson_doc_free(rd);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(d);
    yyjson_mut_val *target = yyjson_mut_obj_get(root, "result");
    if (!target) {
        target = yyjson_mut_obj_get(yyjson_mut_obj_get(root, "error"), "data");
    }
    if (target) {
        yyjson_mut_val *meta =
            envelope(d, rt, failed ? "NATIVE_TOOL_FAILED" : "OK",
                     failed ? "Original native failure retained" : "Completed", failed);
        yyjson_mut_val *native_meta = yyjson_mut_obj_remove_key(target, "_meta");
        if (native_meta) {
            yyjson_mut_obj_add_val(d, meta, "native", native_meta);
        }
        if (audit_failed) {
            yyjson_mut_val *cause = envelope(d, rt, "AUDIT_POSTFLIGHT_FAILED",
                                             "Required completion audit failed", true);
            sqlite_causes(d, yyjson_mut_obj_get(cause, "causes"), rt);
            yyjson_mut_arr_append(yyjson_mut_obj_get(meta, "causes"), cause);
        }
        yyjson_mut_obj_add_val(d, target, "_meta", meta);
    }
    char *out = yyjson_mut_write(d, 0, NULL);
    yyjson_mut_doc_free(d);
    free(response);
    return out;
}

static const char *effective_state(runtime_t *rt, yyjson_val *body, bool revoked,
                                   yyjson_val **index) {
    *index = NULL;
    if (revoked) {
        return "REVOKED";
    }
    if (!body) {
        return "FAILED";
    }
    if (yyjson_get_sint(yyjson_obj_get(body, "expires_at")) <= (int64_t)time(NULL)) {
        return "EXPIRED";
    }
    if (!attested_index(&rt->cfg, body, index)) {
        return "FAILED";
    }
    return "ACTIVE";
}
static const char *health_verify(runtime_t *rt, yyjson_doc *body, const char *source,
                                 uint64_t started) {
    const char *state = "ACTIVE";
    uint64_t elapsed = millis() - started;
    if (elapsed >= (uint64_t)rt->cfg.timeout) {
        return "VERIFYING";
    }
    int saved = rt->cfg.timeout;
    rt->cfg.timeout -= (int)elapsed;
    if (!verify_bounded(rt, yyjson_doc_get_root(body), source)) {
        state = "FAILED";
    }
    rt->cfg.timeout = saved;
    return state;
}
static char *health(runtime_t *rt) {
    uint64_t started = millis();
    yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *env = envelope(d, rt, "HEALTH", "Native project transport health", false);
    yyjson_mut_val *details = yyjson_mut_obj_get(env, "details");
    yyjson_mut_val *sources = yyjson_mut_arr(d);
    sqlite3_stmt *s =
        prepare(rt, "SELECT source,revision,revoked,body FROM bindings ORDER BY source");
    if (!s) {
        yyjson_mut_doc_free(d);
        return error_json(rt, "HEALTH_STORAGE_FAILED", "Health state unavailable", NULL);
    }
    int rc;
    bool degraded = false;
    while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
        const char *source = (const char *)sqlite3_column_text(s, 0);
        yyjson_doc *body = parse((const char *)sqlite3_column_text(s, CBM_SZ_3));
        yyjson_val *index;
        const char *state = effective_state(rt, body ? yyjson_doc_get_root(body) : NULL,
                                            sqlite3_column_int(s, CBM_SZ_2) != 0, &index);
        if (!strcmp(state, "ACTIVE")) {
            state = health_verify(rt, body, source, started);
        }
        yyjson_doc_free(body);
        yyjson_mut_val *v = yyjson_mut_obj(d);
        yyjson_mut_obj_add_strcpy(d, v, "source_id", source);
        yyjson_mut_obj_add_sint(d, v, "revision", sqlite3_column_int64(s, SKIP_ONE));
        yyjson_mut_obj_add_str(d, v, "state", state);
        if (!strcmp(state, "FAILED") && rt->verification_failure) {
            yyjson_doc *failure = parse(rt->verification_failure);
            if (failure) {
                yyjson_mut_obj_add_val(d, v, "diagnostic",
                                       yyjson_val_mut_copy(d, yyjson_doc_get_root(failure)));
            }
            yyjson_doc_free(failure);
        }
        yyjson_mut_arr_append(sources, v);
        if (!strcmp(state, "FAILED")) {
            degraded = true;
        }
    }
    sqlite3_finalize(s);
    if (rc != SQLITE_DONE) {
        yyjson_mut_doc_free(d);
        return error_json(rt, "HEALTH_STORAGE_FAILED", "Health scan failed", NULL);
    }
    const char *state = "ACTIVE";
    if (degraded) {
        state = "DEGRADED";
    }
    if (stopping) {
        state = "REVOKED";
    }
    yyjson_mut_obj_add_str(d, details, "state", state);
    yyjson_mut_obj_add_str(d, details, "protocol_version", PROTOCOL);
    yyjson_mut_obj_add_strcpy(d, details, "tenant_id", rt->cfg.tenant);
    yyjson_mut_obj_add_strcpy(d, details, "project_scope", rt->cfg.project);
    yyjson_mut_obj_add_val(d, details, "sources", sources);
    yyjson_mut_doc_set_root(d, env);
    char *out = yyjson_mut_write(d, 0, NULL);
    yyjson_mut_doc_free(d);
    return out;
}
static char *execute_tool_call(runtime_t *rt, yyjson_val *root, yyjson_val *id, int *status) {
    const char *code;
    const char *message;
    yyjson_val *params = yyjson_obj_get(root, "params");
    yyjson_val *args = yyjson_obj_get(params, "arguments");
    const char *name = str(params, "name");
    const char *source = str(args, "source_id");
    if (!allowed_tool(name) || !args_valid(&rt->cfg, name, args)) {
        code = "TOOL_ARGUMENTS_REJECTED";
        message = "Read-only native tool, source_id and bounded native schema arguments required";
        return error_json(rt, code, message, id);
    }
    int64_t revision;
    bool revoked;
    char *raw = binding_read(rt, source, &revision, &revoked);
    yyjson_doc *body = parse(raw);
    free(raw);
    yyjson_val *index;
    const char *state =
        effective_state(rt, body ? yyjson_doc_get_root(body) : NULL, revoked, &index);
    if (!body || strcmp(state, "ACTIVE") != 0) {
        yyjson_doc_free(body);
        code = "SOURCE_NOT_ACTIVE";
        message = "Exact source binding is absent, revoked, expired or unverifiable";
        return error_json(rt, code, message, id);
    }
    if (!audit(rt, source, name, "begin", "ACCEPTED", 0, 0)) {
        yyjson_doc_free(body);
        *status = ASMS_HTTP_UNAVAILABLE;
        code = "AUDIT_PREFLIGHT_FAILED";
        message = "Required audit unavailable before native execution";
        return error_json(rt, code, message, id);
    }
    uint64_t start = millis();
    rt->calls++;
    char *out = worker_call(rt, yyjson_doc_get_root(body), name, args, id);
    yyjson_doc_free(body);
    yyjson_doc *rd = parse(out);
    yyjson_val *response = rd ? yyjson_doc_get_root(rd) : NULL;
    bool failed = !rd || yyjson_obj_get(response, "error") ||
                  yyjson_is_true(yyjson_obj_get(yyjson_obj_get(response, "result"), "isError"));
    yyjson_doc_free(rd);
    if (failed) {
        rt->failures++;
    }
    bool logged = audit(rt, source, name, "end", failed ? "FAILED" : "OK", millis() - start,
                        out ? strlen(out) : 0);
    if (out) {
        out = annotate(rt, out, !logged);
    }
    *status = ASMS_HTTP_OK;
    return out;
}
static char *protocol_method(runtime_t *rt, cbm_http_req_t *req, yyjson_val *root, yyjson_val *id,
                             int *status, bool *handled) {
    const char *method = str(root, "method");
    const char *code;
    const char *message;
    char *out = NULL;
    *handled = true;
    if (!strcmp(method, "initialize")) {
        yyjson_val *params = yyjson_obj_get(root, "params");
        if (!str(params, "protocolVersion")[0]) {
            return error_json(rt, "INVALID_REQUEST", "A single valid JSON-RPC object is required",
                              id);
        }
        out =
            rpc_result(id, "{\"protocolVersion\":\"" PROTOCOL
                           "\",\"capabilities\":{\"tools\":{\"listChanged\":false}},\"serverInfo\":"
                           "{\"name\":\"cbm-aup-sms\",\"version\":\"0.1.0-cr27\"}}");
        *status = ASMS_HTTP_OK;
        return out;
    }
    if (strcmp(req->mcp_protocol_version, PROTOCOL) != 0) {
        code = "PROTOCOL_REQUIRED";
        message = "Negotiated MCP-Protocol-Version header required";
        return error_json(rt, code, message, id);
    }
    if (!strcmp(method, "ping")) {
        out = rpc_result(id, "{}");
        *status = ASMS_HTTP_OK;
        return out;
    }
    if (!strcmp(method, "tools/list")) {
        char *list = catalog();
        if (list) {
            out = rpc_result(id, list);
        }
        free(list);
        *status = ASMS_HTTP_OK;
        return out;
    }
    if (strcmp(method, "tools/call") != 0) {
        code = "METHOD_NOT_AVAILABLE";
        message = "Method not available on project transport";
        return error_json(rt, code, message, id);
    }
    *handled = false;
    return NULL;
}
static char *mcp_request(runtime_t *rt, cbm_http_req_t *req, int *status) {
    *status = ASMS_HTTP_BAD_REQUEST;
    yyjson_doc *d = parse(req->body);
    yyjson_val *root = d ? yyjson_doc_get_root(d) : NULL;
    yyjson_val *id = yyjson_obj_get(root, "id");
    const char *method = str(root, "method");
    const char *code = "INVALID_REQUEST";
    const char *message = "A single valid JSON-RPC object is required";
    char *out = NULL;
    if (!yyjson_is_obj(root) || strcmp(str(root, "jsonrpc"), "2.0") != 0 ||
        (id && !yyjson_is_str(id) && !yyjson_is_int(id) && !yyjson_is_null(id))) {
        goto fail;
    }
    if (!method[0] && (yyjson_obj_get(root, "result") || yyjson_obj_get(root, "error"))) {
        *status = ASMS_HTTP_ACCEPTED;
        goto end;
    }
    if (!id) {
        if (!strcmp(method, "notifications/initialized") ||
            !strcmp(method, "notifications/cancelled")) {
            *status = ASMS_HTTP_ACCEPTED;
            goto end;
        }
        goto fail;
    }
    bool handled;
    out = protocol_method(rt, req, root, id, status, &handled);
    if (handled) {
        goto end;
    }
    out = execute_tool_call(rt, root, id, status);
    goto end;
fail:
    out = error_json(rt, code, message, id);
end:
    yyjson_doc_free(d);
    return out;
}
static bool reload(runtime_t *rt) {
    config_t next;
    if (!config_read(rt->config_path, &next)) {
        return false;
    }
    bool same = !strcmp(next.tenant, rt->cfg.tenant) && !strcmp(next.project, rt->cfg.project) &&
                !strcmp(next.runtime, rt->cfg.runtime) && next.port == rt->cfg.port;
    if (!same) {
        config_free(&next);
        return false;
    }
    config_free(&rt->cfg);
    rt->cfg = next;
    return true;
}

static char *route_error(runtime_t *rt, int *status, int http_status, const char *code,
                         const char *message) {
    *status = http_status;
    return error_json(rt, code, message, NULL);
}
static char *data_route(runtime_t *rt, cbm_http_req_t *req, int *status) {
    if (!authorized(&rt->cfg, req->authorization, false)) {
        return route_error(rt, status, ASMS_HTTP_UNAUTHORIZED, "DATA_UNAUTHORIZED",
                           "Project data credential required");
    }
    if (strcmp(req->method, "POST") != 0) {
        return route_error(rt, status, ASMS_HTTP_METHOD_NOT_ALLOWED, "METHOD_NOT_ALLOWED",
                           "Stateless MCP endpoint accepts POST; no SSE stream is offered");
    }
    if (strcmp(req->content_type, "application/json") != 0 ||
        !strstr(req->accept, "application/json") || !strstr(req->accept, "text/event-stream")) {
        return route_error(rt, status, ASMS_HTTP_BAD_REQUEST, "REQUEST_REJECTED",
                           "Request does not match machine transport policy");
    }
    return mcp_request(rt, req, status);
}
static char *control_route(runtime_t *rt, cbm_http_req_t *req, int *status, int64_t *revision,
                           const char *health_path, const char *sources) {
    if (!authorized(&rt->cfg, req->authorization, true)) {
        return route_error(rt, status, ASMS_HTTP_UNAUTHORIZED, "CONTROL_UNAUTHORIZED",
                           "Separate project control credential required");
    }
    if (strcmp(req->path, health_path) == 0 && strcmp(req->method, "GET") == 0) {
        char *out = health(rt);
        *status = ASMS_HTTP_OK;
        yyjson_doc *document = parse(out);
        if (document && strcmp(str(yyjson_doc_get_root(document), "type"), "error") == 0) {
            *status = ASMS_HTTP_UNAVAILABLE;
        }
        yyjson_doc_free(document);
        return out;
    }
    const char *source = req->path + strlen(sources);
    if (strncmp(req->path, sources, strlen(sources)) == 0 && ident(source) &&
        (strcmp(req->method, "PUT") == 0 || strcmp(req->method, "DELETE") == 0)) {
        if (strcmp(req->method, "PUT") == 0 && strcmp(req->content_type, "application/json") != 0) {
            return route_error(rt, status, ASMS_HTTP_BAD_REQUEST, "REQUEST_REJECTED",
                               "Request does not match machine transport policy");
        }
        if (!audit(rt, source, "", "control", "ACCEPTED", 0, 0)) {
            return route_error(rt, status, ASMS_HTTP_UNAVAILABLE, "AUDIT_PREFLIGHT_FAILED",
                               "Required audit unavailable before control mutation");
        }
        return control_mutate(rt, req, source, status, revision);
    }
    return route_error(rt, status, ASMS_HTTP_NOT_FOUND, "ENDPOINT_NOT_FOUND",
                       "No endpoint in this attested project");
}
static bool request_policy_valid(runtime_t *rt, cbm_http_req_t *req) {
    char host[CBM_SZ_64];
    (void)snprintf(host, sizeof(host), "127.0.0.1:%d", rt->cfg.port);
    if (strcmp(req->host, host) != 0 || req->origin[0] || req->query[0] ||
        req->body_len > (size_t)rt->cfg.request_max ||
        (req->body && memchr(req->body, 0, req->body_len))) {
        return false;
    }
    return !req->mcp_protocol_version[0] || strcmp(req->mcp_protocol_version, PROTOCOL) == 0;
}
static char *route_request(runtime_t *rt, cbm_http_req_t *req, int *status, int64_t *revision) {
    if (!reload(rt)) {
        return route_error(rt, status, ASMS_HTTP_UNAVAILABLE, "CONFIG_UNAVAILABLE",
                           "Trusted credential configuration unavailable");
    }
    if (!request_policy_valid(rt, req)) {
        return route_error(rt, status, ASMS_HTTP_BAD_REQUEST, "REQUEST_REJECTED",
                           "Request does not match machine transport policy");
    }
    char base[CBM_SZ_256];
    char mcp[ASMS_ENDPOINT_BUFFER];
    char health_path[ASMS_ENDPOINT_BUFFER];
    char sources[ASMS_ENDPOINT_BUFFER];
    (void)snprintf(base, sizeof(base), "/integrations/aup-sms/v1/projects/%s", rt->cfg.project);
    (void)snprintf(mcp, sizeof(mcp), "%s/mcp", base);
    (void)snprintf(health_path, sizeof(health_path), "%s/health", base);
    (void)snprintf(sources, sizeof(sources), "%s/sources/", base);
    if (strcmp(req->path, mcp) == 0) {
        return data_route(rt, req, status);
    }
    return control_route(rt, req, status, revision, health_path, sources);
}
static void serve_request(runtime_t *rt, cbm_http_conn_t *conn, cbm_http_req_t *req) {
    rt->db_error = 0;
    free(rt->db_message);
    rt->db_message = NULL;
    yyjson_mut_doc_free(rt->db_errors);
    rt->db_errors = NULL;
    free(rt->verification_failure);
    rt->verification_failure = NULL;
    rt->audit_code = NULL;
    unsigned char random[CBM_SZ_32];
    if (!cbm_secure_random(random, sizeof(random))) {
        cbm_http_replyf(conn, ASMS_HTTP_UNAVAILABLE, JSON_HEADERS, "{}");
        return;
    }
    digest_hex(random, rt->correlation);
    int status = ASMS_HTTP_BAD_REQUEST;
    char *out = NULL;
    int64_t revision = -SKIP_ONE;
    out = route_request(rt, req, &status, &revision);
    if (!out && status != ASMS_HTTP_ACCEPTED) {
        status = ASMS_HTTP_UNAVAILABLE;
        out =
            error_json(rt, "RESPONSE_UNAVAILABLE", "Response allocation or encoding failed", NULL);
    }
    char headers[CBM_SZ_512];
    if (revision >= 0 && status == ASMS_HTTP_OK) {
        (void)snprintf(headers, sizeof(headers),
                       JSON_HEADERS "X-Request-Id: %s\r\nETag: %" PRId64 "\r\n", rt->correlation,
                       revision);
    } else {
        (void)snprintf(headers, sizeof(headers), JSON_HEADERS "X-Request-Id: %s\r\n",
                       rt->correlation);
    }
    if (out && strlen(out) > (size_t)rt->cfg.response_max) {
        free(out);
        out = error_json(rt, "RESPONSE_LIMIT", "Complete response exceeds configured byte limit",
                         NULL);
        status = ASMS_HTTP_TOO_LARGE;
    }
    cbm_http_reply_buf(conn, status, headers, out ? out : "", out ? strlen(out) : 0);
    free(out);
}
static bool init_state(runtime_t *rt) {
    char path[CBM_SZ_4K];
    if (snprintf(path, sizeof(path), "%s/control.sqlite", rt->cfg.runtime) >= (int)sizeof(path)) {
        return false;
    }
    if (sqlite3_open_v2(path, &rt->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) !=
        SQLITE_OK) {
        remember_sqlite(rt);
        return false;
    }
    sqlite3_busy_timeout(rt->db, ASMS_RESULTS_MAX);
    sqlite3_extended_result_codes(rt->db, SKIP_ONE);
    return exec_sql(rt,
                    "PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; PRAGMA auto_vacuum=FULL;"
                    "CREATE TABLE IF NOT EXISTS identity(tenant TEXT,project TEXT);"
                    "CREATE TABLE IF NOT EXISTS bindings(source TEXT PRIMARY KEY,revision INTEGER "
                    "NOT NULL,revoked INTEGER NOT NULL,body TEXT NOT NULL);"
                    "CREATE TABLE IF NOT EXISTS replays(key TEXT PRIMARY KEY,fingerprint "
                    "TEXT,response TEXT,revision INTEGER);"
                    "CREATE TABLE IF NOT EXISTS audit(correlation TEXT,source TEXT,tool TEXT,stage "
                    "TEXT,status TEXT,duration_ms INTEGER,bytes INTEGER,at INTEGER,size INTEGER);");
}
static bool state_identity(runtime_t *rt) {
    sqlite3_stmt *s = prepare(rt, "SELECT tenant,project FROM identity");
    if (!s) {
        return false;
    }
    int rc = sqlite3_step(s);
    if (rc == SQLITE_ROW) {
        bool ok = !strcmp((const char *)sqlite3_column_text(s, 0), rt->cfg.tenant) &&
                  !strcmp((const char *)sqlite3_column_text(s, SKIP_ONE), rt->cfg.project);
        sqlite3_finalize(s);
        return ok;
    }
    sqlite3_finalize(s);
    if (rc != SQLITE_DONE) {
        return false;
    }
    s = prepare(rt, "INSERT INTO identity VALUES(?,?)");
    if (!s) {
        return false;
    }
    bind_text(s, SKIP_ONE, rt->cfg.tenant);
    bind_text(s, CBM_SZ_2, rt->cfg.project);
    return done(rt, s);
}
static void startup_error(runtime_t *rt, const char *code, const char *message) {
    int os_error = errno;
    char *raw = error_json(rt, code, message, NULL);
    yyjson_doc *d = parse(raw);
    free(raw);
    if (!d) {
        return;
    }
    yyjson_mut_doc *md = yyjson_doc_mut_copy(d, NULL);
    yyjson_doc_free(d);
    yyjson_mut_val *details = yyjson_mut_obj_get(yyjson_mut_doc_get_root(md), "details");
    yyjson_mut_obj_add_int(md, details, "errno", os_error);
    yyjson_mut_obj_add_strcpy(md, details, "os_message", os_error ? strerror(os_error) : "");
    yyjson_mut_obj_add_strcpy(md, details, "platform_validation",
                              cbm_daemon_ipc_validation_detail());
    raw = yyjson_mut_write(md, 0, NULL);
    if (raw) {
        (void)fprintf(stderr, "%s\n", raw);
        free(raw);
    }
    yyjson_mut_doc_free(md);
}
static void handle_connection(runtime_t *rt, cbm_http_conn_t *conn) {
    cbm_http_req_t req = {0};
    int rc = cbm_httpd_read_request(conn, &req);
    if (rc == 0) {
        serve_request(rt, conn, &req);
    } else if (rc > 0) {
        unsigned char random[CBM_SZ_32];
        if (cbm_secure_random(random, sizeof(random))) {
            digest_hex(random, rt->correlation);
        }
        char *err = error_json(rt, "HTTP_PARSE_FAILED", "HTTP framing or headers rejected", NULL);
        cbm_http_reply_buf(conn, rc, JSON_HEADERS, err ? err : "{}", err ? strlen(err) : CBM_SZ_2);
        free(err);
    }
    cbm_http_req_free(&req);
}
int cbm_asms_mcp_http_entry(int argc, char **argv) {
    if (argc < CBM_SZ_2) {
        return -SKIP_ONE;
    }
    if (!strcmp(argv[SKIP_ONE], "asms-mcp-worker")) {
        return argc == CBM_SZ_2 ? worker() : CBM_SZ_64;
    }
    if (strcmp(argv[SKIP_ONE], "asms-mcp-http") != 0) {
        return -SKIP_ONE;
    }
    runtime_t rt = {0};
    int result = SKIP_ONE;
    cbm_log_set_level(CBM_LOG_NONE);
    cbm_log_set_sink_ex(discard_log, CBM_LOG_SINK_REPLACE);
    unsigned char initial_random[CBM_SZ_32];
    if (!cbm_secure_random(initial_random, sizeof(initial_random))) {
        return ASMS_EXIT_ALLOCATION;
    }
    digest_hex(initial_random, rt.correlation);
    if (argc != CBM_SZ_3 || !config_read(argv[CBM_SZ_2], &rt.cfg)) {
        startup_error(&rt, "CONFIG_INVALID",
                      "Trusted configuration read, JSON or schema validation failed");
        return CBM_SZ_64;
    }
    rt.config_path = argv[CBM_SZ_2];
#ifndef _WIN32
    (void)umask(ASMS_PRIVATE_UMASK);
#endif
    cbm_path_info_t info;
    if (!cbm_daemon_ipc_private_directory_secure(rt.cfg.runtime) ||
        cbm_path_info_utf8(rt.cfg.runtime, &info) != CBM_PATH_INFO_OK || !info.is_directory ||
        info.is_symlink ||
        !cbm_http_server_resolve_binary_path(argv[0], rt.binary, sizeof(rt.binary)) ||
        !init_state(&rt) || !state_identity(&rt)) {
        goto end;
    }
    cbm_httpd_t *listener = cbm_httpd_listen(rt.cfg.port);
    if (!listener) {
        goto end;
    }
    stopping = 0;
    if (signal(SIGINT, stop_signal) == SIG_ERR || signal(SIGTERM, stop_signal) == SIG_ERR) {
        (void)cbm_httpd_close(listener);
        goto end;
    }
    while (!stopping) {
        cbm_http_conn_t *conn = cbm_httpd_accept(listener, ASMS_ACCEPT_POLL_MS);
        if (!conn) {
            continue;
        }
        handle_connection(&rt, conn);
        cbm_httpd_conn_close(conn);
    }
    if (cbm_httpd_close(listener)) {
        result = 0;
    }
end:
    if (result) {
        startup_error(&rt, "STARTUP_FAILED",
                      "Native project transport startup or containment failed");
    }
    sqlite3_close(rt.db);
    free(rt.db_message);
    yyjson_mut_doc_free(rt.db_errors);
    free(rt.verification_failure);
    config_free(&rt.cfg);
    return result;
}
