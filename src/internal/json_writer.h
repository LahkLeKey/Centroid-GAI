/** @file json_writer.h @brief Shared owned JSON writer for native ABI responses. */
#ifndef CGAI_JSON_WRITER_H
#define CGAI_JSON_WRITER_H
#include <stddef.h>

/** Owned expandable UTF-8 output with a sticky failure flag. */
typedef struct cgai_json_writer {
    char *data;      /**< Owned allocation; caller frees it or transfers ownership. */
    size_t size;     /**< Written bytes excluding NUL. */
    size_t capacity; /**< Allocated byte capacity. */
    int failed;      /**< Nonzero after allocation or formatting failure. */
} cgai_json_writer;

/** Append printf-formatted text to a zero-initialized or active writer. */
void cgai_json_append(cgai_json_writer *writer, const char *format, ...);
/** Append NUL-terminated literal text. */
void cgai_json_text(cgai_json_writer *writer, const char *value);
/** Append a quoted JSON string with escaped delimiters and control bytes. */
void cgai_json_string(cgai_json_writer *writer, const char *value);
#endif
