#include "distributed_error.h"
#include "foundation/compat.h"
#include "foundation/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <yyjson/yyjson.h>

#ifndef CBM_VERSION
#define CBM_VERSION "dev"
#endif

typedef struct {
    yyjson_mut_doc *doc;
    yyjson_mut_val *limitations;
    const char *correlation;
    unsigned sequence, nodes, max_nodes;
    size_t detail_limit, message_limit;
} diagnostic_t;

static const char *text(yyjson_val *v, const char *key) {
    const char *s = yyjson_get_str(yyjson_obj_get(v, key));
    return s ? s : "";
}
static void limitation(diagnostic_t *ctx, const char *reason) {
    size_t i, max;
    yyjson_mut_val *value;
    yyjson_mut_arr_foreach(ctx->limitations, i, max, value) {
        if (!strcmp(yyjson_mut_get_str(value), reason))
            return;
    }
    yyjson_mut_arr_add_strcpy(ctx->doc, ctx->limitations, reason);
}
/* Request-derived UUIDv8: distinct identities, no invented trace or exception. */
static yyjson_mut_val *identity(diagnostic_t *ctx) {
    char seed[128], hash[65], uuid[37];
    snprintf(seed, sizeof(seed), "%s:%u", ctx->correlation, ctx->sequence++);
    cbm_sha256_hex(seed, strlen(seed), hash);
    hash[12] = '8';
    hash[16] = "89ab"[(unsigned char)hash[16] % 4];
    snprintf(uuid, sizeof(uuid), "%.8s-%.4s-%.4s-%.4s-%.12s", hash, hash + 8, hash + 12, hash + 16,
             hash + 20);
    return yyjson_mut_strcpy(ctx->doc, uuid);
}
static yyjson_mut_val *stack(diagnostic_t *ctx) {
    yyjson_mut_doc *d = ctx->doc;
    yyjson_mut_val *v = yyjson_mut_obj(d);
    yyjson_mut_obj_add_str(d, v, "availability", "unavailable");
    yyjson_mut_obj_add_str(d, v, "format", "native");
    yyjson_mut_obj_add_null(d, v, "text");
    yyjson_mut_obj_add_val(d, v, "frames", yyjson_mut_arr(d));
    yyjson_mut_obj_add_bool(d, v, "redacted", false);
    yyjson_mut_obj_add_str(d, v, "reason", "Native C returned an error; no stack was captured.");
    return v;
}
static yyjson_val *native_value(yyjson_val *v) {
    yyjson_val *error = yyjson_obj_get(v, "error");
    if (error) {
        yyjson_val *data = yyjson_obj_get(error, "data");
        return yyjson_is_obj(data) ? data : error;
    }
    yyjson_val *result = yyjson_obj_get(v, "result");
    if (result)
        v = result;
    yyjson_val *structured = yyjson_obj_get(v, "structuredContent");
    return yyjson_is_obj(structured) ? structured : v;
}
static bool is_failure(yyjson_val *root) {
    return yyjson_obj_get(root, "error") ||
           yyjson_is_true(yyjson_obj_get(yyjson_obj_get(root, "result"), "isError")) ||
           !strcmp(text(root, "type"), "error");
}
static yyjson_mut_val *node(diagnostic_t *ctx, yyjson_val *raw,
                            unsigned depth) { // NOLINT(misc-no-recursion)
    if (ctx->nodes >= ctx->max_nodes || depth > 6) {
        limitation(ctx, "native_causes_truncated");
        return NULL;
    }
    ctx->nodes++;
    yyjson_val *v = native_value(raw);
    yyjson_mut_doc *d = ctx->doc;
    yyjson_mut_val *out = yyjson_mut_obj(d), *details = yyjson_mut_obj(d);
    const char *kind = text(v, "type");
    yyjson_mut_obj_add_val(d, out, "id", identity(ctx));
    yyjson_mut_obj_add_strcpy(d, out, "type", kind[0] ? kind : "native_returned_error");
    if (!kind[0]) {
        yyjson_mut_obj_add_str(d, details, "representation",
                               "native returned error, not an exception class");
    }
    yyjson_val *code = yyjson_obj_get(v, "code");
    yyjson_mut_obj_add_val(d, out, "code",
                           yyjson_is_str(code) || yyjson_is_int(code) ? yyjson_val_mut_copy(d, code)
                                                                      : yyjson_mut_null(d));
    const char *message = text(v, "message");
    if (!message[0]) {
        yyjson_val *content = yyjson_obj_get(v, "content");
        message = text(yyjson_arr_get(content, 0), "text");
    }
    size_t length = strlen(message),
           shown = length < ctx->message_limit ? length : ctx->message_limit;
    while (shown && ((unsigned char)message[shown] & 0xc0) == 0x80)
        shown--;
    yyjson_mut_obj_add_val(d, out, "message", yyjson_mut_strncpy(d, message, shown));
    if (shown < length) {
        limitation(ctx, "native_message_truncated");
        yyjson_mut_obj_add_uint(d, details, "original_message_bytes", length);
    }
    yyjson_obj_iter iter = yyjson_obj_iter_with(v);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iter))) {
        const char *name = yyjson_get_str(key);
        if (!strcmp(name, "type") || !strcmp(name, "code") || !strcmp(name, "message") ||
            !strcmp(name, "causes") || !strcmp(name, "_meta") || !strcmp(name, "diagnostic"))
            continue;
        yyjson_val *value = yyjson_obj_iter_get_val(key);
        char *encoded = yyjson_val_write(value, 0, NULL);
        if (encoded && strlen(encoded) <= ctx->detail_limit) {
            yyjson_mut_obj_add_val(d, details, name, yyjson_val_mut_copy(d, value));
        } else {
            limitation(ctx, "native_details_truncated");
        }
        free(encoded);
    }
    yyjson_val *native_meta = yyjson_obj_get(yyjson_obj_get(v, "_meta"), "native");
    if (native_meta) {
        char *encoded = yyjson_val_write(native_meta, 0, NULL);
        if (encoded && strlen(encoded) <= ctx->detail_limit)
            yyjson_mut_obj_add_val(d, details, "native_metadata",
                                   yyjson_val_mut_copy(d, native_meta));
        else
            limitation(ctx, "native_details_truncated");
        free(encoded);
    }
    yyjson_mut_obj_add_val(d, out, "details", details);
    yyjson_mut_obj_add_val(d, out, "stack", stack(ctx));
    yyjson_mut_obj_add_null(d, out, "cause");
    yyjson_mut_obj_add_null(d, out, "context");
    yyjson_mut_obj_add_bool(d, out, "context_suppressed", false);
    yyjson_mut_val *children = yyjson_mut_arr(d);
    yyjson_mut_obj_add_val(d, out, "children", children);
    size_t i, max;
    yyjson_val *cause;
    yyjson_arr_foreach(yyjson_obj_get(v, "causes"), i, max, cause) {
        yyjson_mut_val *child = node(ctx, cause, depth + 1);
        if (child)
            yyjson_mut_arr_append(children, child);
    }
    return out;
}
static yyjson_mut_val *diagnostic(diagnostic_t *ctx, yyjson_val *raw, int status,
                                  bool size_failure) {
    yyjson_mut_doc *d = ctx->doc;
    yyjson_mut_val *out = yyjson_mut_obj(d);
    yyjson_mut_obj_add_str(d, out, "protocol", "aup.exception");
    yyjson_mut_obj_add_str(d, out, "version", "1.0");
    yyjson_mut_obj_add_val(d, out, "diagnostic_id", identity(ctx));
    time_t now = time(NULL);
    struct tm utc;
    char timestamp[32];
    if (!cbm_gmtime_r(&now, &utc) ||
        !strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc))
        return NULL;
    yyjson_mut_obj_add_strcpy(d, out, "captured_at", timestamp);
    yyjson_mut_val *origin = yyjson_mut_obj(d);
    yyjson_mut_obj_add_val(d, out, "origin", origin);
    yyjson_mut_obj_add_str(d, origin, "system", "cbm");
    yyjson_mut_obj_add_str(d, origin, "service", "asms-mcp-http");
    yyjson_mut_obj_add_str(d, origin, "language", "c");
    yyjson_mut_obj_add_str(d, origin, "component_version", CBM_VERSION);
    yyjson_mut_obj_add_null(d, origin, "runtime_version");
    yyjson_mut_obj_add_str(d, origin, "operation", "mcp.http");
    yyjson_val *native = native_value(raw);
    const char *request_id = text(native, "request_id"),
               *correlation_id = text(native, "correlation_id");
    yyjson_mut_obj_add_strcpy(d, origin, "request_id",
                              request_id[0] ? request_id : ctx->correlation);
    yyjson_mut_obj_add_strcpy(d, origin, "correlation_id",
                              correlation_id[0] ? correlation_id : ctx->correlation);
    yyjson_mut_obj_add_null(d, origin, "trace_id");
    yyjson_mut_obj_add_null(d, origin, "span_id");
    yyjson_mut_val *ids = yyjson_mut_obj(d);
    yyjson_mut_obj_add_int(d, ids, "http_status", status);
    yyjson_val *rpc_id = yyjson_obj_get(raw, "id");
    if (yyjson_is_str(rpc_id) || yyjson_is_int(rpc_id) || yyjson_is_null(rpc_id))
        yyjson_mut_obj_add_val(d, ids, "jsonrpc_id", yyjson_val_mut_copy(d, rpc_id));
    yyjson_val *rpc_code = yyjson_obj_get(yyjson_obj_get(raw, "error"), "code");
    if (yyjson_is_int(rpc_code))
        yyjson_mut_obj_add_val(d, ids, "jsonrpc_error_code", yyjson_val_mut_copy(d, rpc_code));
    yyjson_mut_obj_add_val(d, origin, "identifiers", ids);
    limitation(ctx, "native_stack_unavailable");
    if (size_failure) {
        limitation(ctx, "response_limit_exceeded");
        limitation(ctx, "full_diagnostic_reference_unavailable");
    }
    yyjson_mut_obj_add_val(d, out, "primary", node(ctx, raw, 0));
    yyjson_mut_val *secondary = yyjson_mut_arr(d);
    yyjson_mut_obj_add_val(d, out, "secondary", secondary);
    yyjson_val *result = yyjson_obj_get(raw, "result");
    yyjson_val *meta = yyjson_obj_get(result ? result : native_value(raw), "_meta");
    size_t i, max;
    yyjson_val *cause;
    yyjson_arr_foreach(yyjson_obj_get(meta, "causes"), i, max, cause) {
        yyjson_mut_val *error = node(ctx, cause, 0);
        if (!error)
            continue;
        yyjson_mut_val *item = yyjson_mut_obj(d);
        yyjson_mut_obj_add_str(d, item, "kind", "persistence");
        yyjson_mut_obj_add_str(d, item, "phase", "postflight");
        yyjson_mut_obj_add_str(d, item, "system", "cbm");
        yyjson_mut_obj_add_strcpy(d, item, "at", timestamp);
        yyjson_mut_obj_add_val(d, item, "exception", error);
        yyjson_mut_arr_append(secondary, item);
    }
    yyjson_mut_obj_add_val(d, out, "hops", yyjson_mut_arr(d));
    yyjson_mut_val *completeness = yyjson_mut_obj(d);
    yyjson_mut_obj_add_bool(d, completeness, "complete", false);
    yyjson_mut_obj_add_val(d, completeness, "limitations", ctx->limitations);
    yyjson_mut_obj_add_null(d, completeness, "diagnostic_reference");
    yyjson_mut_obj_add_val(d, out, "completeness", completeness);
    return out;
}
char *cbm_asms_distributed_error(const char *legacy, const char *original, const char *correlation,
                                 int status, size_t limit) {
    yyjson_doc *input = legacy ? yyjson_read(legacy, strlen(legacy), 0) : NULL;
    if (!input)
        return NULL;
    yyjson_val *root = yyjson_doc_get_root(input);
    yyjson_val *rpc_error = yyjson_obj_get(root, "error");
    yyjson_val *result = yyjson_obj_get(root, "result");
    bool tool_error = yyjson_is_true(yyjson_obj_get(result, "isError"));
    if (!is_failure(root)) {
        yyjson_doc_free(input);
        return NULL;
    }
    yyjson_doc *prior = original ? yyjson_read(original, strlen(original), 0) : NULL;
    char *encoded = NULL;
    for (unsigned attempt = 0; attempt < 2; attempt++) {
        yyjson_mut_doc *d = yyjson_doc_mut_copy(input, NULL);
        yyjson_mut_val *output = yyjson_mut_doc_get_root(d), *target = output;
        if (rpc_error)
            target = yyjson_mut_obj_get(yyjson_mut_obj_get(output, "error"), "data");
        else if (tool_error) {
            yyjson_mut_val *tool = yyjson_mut_obj_get(output, "result");
            target = yyjson_mut_obj_get(tool, "structuredContent");
            if (!target) {
                target = yyjson_mut_obj(d);
                yyjson_mut_obj_add_val(d, tool, "structuredContent", target);
            }
        }
        diagnostic_t ctx = {.doc = d,
                            .limitations = yyjson_mut_arr(d),
                            .correlation = correlation,
                            .max_nodes = attempt ? 4 : 16,
                            .detail_limit = attempt ? 128 : 2048,
                            .message_limit = attempt ? 256 : 2048};
        yyjson_val *preferred =
            prior && is_failure(yyjson_doc_get_root(prior)) ? yyjson_doc_get_root(prior) : root;
        yyjson_mut_val *value = diagnostic(&ctx, preferred, status, original != NULL);
        if (target && value)
            yyjson_mut_obj_add_val(d, target, "diagnostic", value);
        encoded = yyjson_mut_write(d, 0, NULL);
        yyjson_mut_doc_free(d);
        if (encoded && strlen(encoded) <= limit)
            break;
        free(encoded);
        encoded = NULL;
    }
    yyjson_doc_free(prior);
    yyjson_doc_free(input);
    return encoded;
}
