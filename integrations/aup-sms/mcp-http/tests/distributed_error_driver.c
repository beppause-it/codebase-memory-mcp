/* Standalone native serializer probe; no server, credentials or real artifacts. */
#include "../src/distributed_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson/yyjson.h>

int main(void) {
    size_t capacity = 4096, length = 0;
    char *input = malloc(capacity);
    int ch;
    while ((ch = getchar()) != EOF) {
        if (length + 1 >= capacity) {
            capacity *= 2;
            char *next = realloc(input, capacity);
            if (!next) {
                free(input);
                return 2;
            }
            input = next;
        }
        input[length++] = (char)ch;
    }
    yyjson_doc *doc = yyjson_read(input, length, 0);
    free(input);
    if (!doc)
        return 2;
    yyjson_val *root = yyjson_doc_get_root(doc);
    char *legacy = yyjson_val_write(yyjson_obj_get(root, "response"), 0, NULL);
    yyjson_val *prior = yyjson_obj_get(root, "original");
    char *original = prior ? yyjson_val_write(prior, 0, NULL) : NULL;
    size_t limit = (size_t)yyjson_get_uint(yyjson_obj_get(root, "limit"));
    int status = (int)yyjson_get_int(yyjson_obj_get(root, "status"));
    char *result = cbm_asms_distributed_error(
        legacy, original, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        status ? status : 400, limit ? limit : 65536);
    puts(result ? result : legacy);
    free(result);
    free(original);
    free(legacy);
    yyjson_doc_free(doc);
    return 0;
}
