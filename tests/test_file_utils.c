/** @file test_file_utils.c @brief Unit tests for private whole-file helpers. */

#include "internal/file_utils.h"
#include "test_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Check binary file round trips, sentinel bytes, and invalid paths.
 *
 * The fixture contains both an embedded zero and 0xff, so comparing raw bytes verifies that file
 * I/O does not treat the payload as a text string. The returned extra NUL is checked separately
 * from the reported payload size. The helper owns the read buffer and removes its temporary file.
 *
 * @return Zero after all checks pass; failed assertions terminate the executable.
 */
int test_file_utils(void) {
    /* Step 1: Define a temporary binary fixture and initialize read-buffer ownership. */
    const char *path = "centroid_gai_file_utils_test.bin";
    const uint8_t input[] = {'C', 'G', 'A', 'I', 0U, 0xffU};
    uint8_t *output = NULL;
    size_t size = 0U;

    /* Step 2: Check invalid input, then write/read the complete raw byte fixture. */
    TEST_CHECK(cgai_file_read_all(NULL, &output, &size) == CGAI_STATUS_ERROR,
               "NULL file path was accepted");
    TEST_CHECK(cgai_file_write_all(path, input, sizeof(input)) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_file_read_all(path, &output, &size) == CGAI_STATUS_OK, cgai_last_error());
    /* Step 3: Compare byte count, payload contents, and the separate text sentinel. */
    TEST_CHECK(size == sizeof(input), "file size changed during round trip");
    TEST_CHECK(memcmp(output, input, sizeof(input)) == 0, "file bytes changed during round trip");
    TEST_CHECK(output[size] == '\0', "text sentinel was not appended");

    /* Step 4: Free read storage and remove the file before checking remaining error paths. */
    free(output);
    (void)remove(path);
    TEST_CHECK(cgai_file_write_all(NULL, input, sizeof(input)) == CGAI_STATUS_ERROR,
               "NULL write path was accepted");
    TEST_CHECK(cgai_file_read_all("missing-centroid-gai-file.bin", &output, &size) ==
                   CGAI_STATUS_ERROR,
               "missing file was accepted");
    return 0;
}