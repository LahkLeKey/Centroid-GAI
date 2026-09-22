/** @file test_file_utils.c @brief Unit tests for private whole-file helpers. */

#include "internal/file_utils.h"
#include "test_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int test_file_utils(void) {
    const char *path = "centroid_gai_file_utils_test.bin";
    const uint8_t input[] = {'C', 'G', 'A', 'I', 0U, 0xffU};
    uint8_t *output = NULL;
    size_t size = 0U;

    TEST_CHECK(cgai_file_read_all(NULL, &output, &size) == CGAI_STATUS_ERROR,
               "NULL file path was accepted");
    TEST_CHECK(cgai_file_write_all(path, input, sizeof(input)) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_file_read_all(path, &output, &size) == CGAI_STATUS_OK, cgai_last_error());
    TEST_CHECK(size == sizeof(input), "file size changed during round trip");
    TEST_CHECK(memcmp(output, input, sizeof(input)) == 0, "file bytes changed during round trip");
    TEST_CHECK(output[size] == '\0', "text sentinel was not appended");

    free(output);
    (void)remove(path);
    TEST_CHECK(cgai_file_write_all(NULL, input, sizeof(input)) == CGAI_STATUS_ERROR,
               "NULL write path was accepted");
    TEST_CHECK(cgai_file_read_all("missing-centroid-gai-file.bin", &output, &size) ==
                   CGAI_STATUS_ERROR,
               "missing file was accepted");
    return 0;
}