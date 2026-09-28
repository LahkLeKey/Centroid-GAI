/** @file json_writer.c @brief Checked JSON allocation, formatting, and string escaping. */
#include "internal/json_writer.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Grow the owned JSON buffer while checking allocation capacity.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param needed Required allocation capacity in bytes.
 * @return One on success, or zero on validation/allocation failure.
 */
static int reserve(cgai_json_writer *writer, size_t needed) {
    if (needed <= writer->capacity)
        return 1;
    size_t capacity = writer->capacity ? writer->capacity : 1024U;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = needed;
            break;
        }
        capacity *= 2U;
    }
    char *data = (char *)realloc(writer->data, capacity);
    if (!data)
        return 0;
    writer->data = data;
    writer->capacity = capacity;
    return 1;
}

/**
 * @brief Append formatted JSON while keeping both variadic traversals local and balanced.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param format Borrowed printf-compatible format string.
 */
void cgai_json_append(cgai_json_writer *writer, const char *format, ...) {
    if (writer->failed)
        return;
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (length < 0 || (size_t)length > SIZE_MAX - writer->size - 1U ||
        !reserve(writer, writer->size + (size_t)length + 1U)) {
        writer->failed = 1;
    } else {
        va_start(args, format);
        (void)vsnprintf(writer->data + writer->size, writer->capacity - writer->size, format, args);
        va_end(args);
        writer->size += (size_t)length;
    }
}

/**
 * @brief Append literal UTF-8 bytes without variadic argument handling.
 * @param writer Mutable JSON output buffer.
 * @param value Borrowed NUL-terminated text to copy.
 */
void cgai_json_text(cgai_json_writer *writer, const char *value) {
    if (writer->failed)
        return;
    const size_t length = strlen(value);
    if (length > SIZE_MAX - writer->size - 1U || !reserve(writer, writer->size + length + 1U)) {
        writer->failed = 1;
        return;
    }
    memcpy(writer->data + writer->size, value, length + 1U);
    writer->size += length;
}

/**
 * @brief Append a quoted JSON string, escaping control bytes and delimiters.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param value Borrowed input value to validate or serialize.
 */
void cgai_json_string(cgai_json_writer *writer, const char *value) {
    cgai_json_text(writer, "\"");
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '"' || *p == '\\')
            cgai_json_append(writer, "\\%c", (int)*p);
        else if (*p < 32U)
            cgai_json_append(writer, "\\u%04x", (unsigned int)*p);
        else
            cgai_json_append(writer, "%c", (int)*p);
    }
    cgai_json_text(writer, "\"");
}
