/** @file test_reference.c @brief Own-history comparator replay and incomplete-capture
 * rejection. */
#include "internal/file_utils.h"
#include "npc_reference.h"
#include "test_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Canonical development capture retained only in the test working directory. */
static const char capture_path[] = "npc-v3-reference-capture.tsv";
/** Deliberately altered raw capture. */
static const char altered_path[] = "npc-v3-reference-altered.tsv";
/** Original authoritative comparison report. */
static const char expected_path[] = "npc-v3-reference-expected.tsv";
/** Independently replayed authoritative report. */
static const char actual_path[] = "npc-v3-reference-actual.tsv";
/** Pinned descriptor identity fixture; provider identity belongs to wrapper tests. */
static const char identity[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

/** @brief Read a complete trusted fixture and require successful bounded access.
 * @param path Existing fixture path.
 * @param size Writable complete byte count.
 * @return Owned complete bytes, released by the caller. */
static uint8_t *read_fixture(const char *path, size_t *size) {
    uint8_t *bytes = NULL;
    TEST_CHECK(cgai_file_read_all(path, &bytes, size), cgai_last_error());
    return bytes;
}

/** @brief Write one exact altered capture without trusting supplied outcomes.
 * @param bytes Borrowed fixture bytes.
 * @param size Exact retained byte count.
 * @param extra Optional trailing row exercising complete-set rejection. */
static void write_fixture(const uint8_t *bytes, size_t size, const char *extra) {
    FILE *stream = fopen(altered_path, "wb");
    TEST_CHECK(stream != NULL, "could not open altered comparator fixture");
    TEST_CHECK(fwrite(bytes, 1U, size, stream) == size, "incomplete comparator fixture");
    if (extra != NULL)
        TEST_CHECK(fputs(extra, stream) >= 0, "could not append extra comparator row");
    TEST_CHECK(fclose(stream) == 0, "could not close comparator fixture");
}

/** @brief Locate a canonical first-decision field inside a trusted complete template.
 * @param bytes Writable NUL-terminated template copy.
 * @param field Zero-based provenance/action/input field, zero through19.
 * @return Pointer to the requested field's first digit. */
static char *decision_field(char *bytes, size_t field) {
    char *cursor = strstr(bytes, "decision\t");
    TEST_CHECK(cursor != NULL, "missing own-history template decisions");
    cursor += 9U;
    for (size_t index = 0U; index < field; ++index) {
        cursor = strchr(cursor, '\t');
        TEST_CHECK(cursor != NULL, "incomplete trusted decision fixture");
        ++cursor;
    }
    return cursor;
}

/** @brief Require replayed aggregate bytes to match independent template execution exactly. */
static void exact_reference_replay(void) {
    TEST_CHECK(npc_reference_template(capture_path, expected_path, NPC_DEV, identity),
               cgai_last_error());
    TEST_CHECK(npc_reference_files(capture_path, actual_path, NPC_DEV, identity),
               cgai_last_error());
    size_t expected_size = 0U;
    size_t actual_size = 0U;
    uint8_t *expected = read_fixture(expected_path, &expected_size);
    uint8_t *actual = read_fixture(actual_path, &actual_size);
    TEST_CHECK(expected_size == actual_size && memcmp(expected, actual, actual_size) == 0,
               "host replay changed complete comparator outcomes");
    TEST_CHECK(strstr((const char *)actual, "summary\treference\t1152\t1152\t1152\t0\t0\t") != NULL,
               "planner template omitted complete successful development episodes");
    free(expected);
    free(actual);
}

/** @brief Reject falsified observations and provenance without publishing replacement outcomes.
 * @param bytes Writable complete trusted template copy.
 * @param size Complete trusted byte count.
 * @param field Decision field to mutate; observation fields begin at4. */
static void reject_changed_field(uint8_t *bytes, size_t size, size_t field) {
    char *digit = decision_field((char *)bytes, field);
    const char previous = *digit;
    *digit = previous == '0' ? '1' : '0';
    write_fixture(bytes, size, NULL);
    TEST_CHECK(!npc_reference_files(altered_path, actual_path, NPC_DEV, identity),
               "falsified own-history observation or selected-step provenance was accepted");
    *digit = previous;
}

/** @brief Exercise complete-set, descriptor, partition and action-domain admission rules. */
static void rejected_reference_captures(void) {
    size_t size = 0U;
    uint8_t *bytes = read_fixture(capture_path, &size);
    reject_changed_field(bytes, size, 2U);
    reject_changed_field(bytes, size, 4U);
    reject_changed_field(bytes, size, 12U);
    char *action = decision_field((char *)bytes, 3U);
    const char previous = *action;
    *action = '7';
    write_fixture(bytes, size, NULL);
    TEST_CHECK(!npc_reference_files(altered_path, actual_path, NPC_DEV, identity),
               "out-of-domain comparator action was accepted");
    *action = previous;
    write_fixture(bytes, size, "unknown\trow\n");
    TEST_CHECK(!npc_reference_files(altered_path, actual_path, NPC_DEV, identity),
               "extra comparator rows were ignored");
    write_fixture(bytes, (size_t)(strstr((char *)bytes, "decision\t") - (char *)bytes), NULL);
    TEST_CHECK(!npc_reference_files(altered_path, actual_path, NPC_DEV, identity),
               "missing episodes were omitted from comparator scoring");
    free(bytes);
}

/** @brief Reject unauthorized partition reinterpretation and malformed comparator identities. */
static void rejected_reference_arguments(void) {
    TEST_CHECK(!npc_reference_files(capture_path, actual_path, NPC_AUDIT, identity),
               "development capture was reinterpreted as reserved audit outcomes");
    TEST_CHECK(!npc_reference_template(capture_path, actual_path, NPC_AUDIT, identity),
               "reference template consulted reserved audit outcomes");
    TEST_CHECK(!npc_reference_files(capture_path, actual_path, NPC_TRAIN, identity),
               "training outcomes entered comparator evaluation");
    TEST_CHECK(!npc_reference_files(capture_path, capture_path, NPC_DEV, identity),
               "reference report could overwrite its own capture");
    TEST_CHECK(
        !npc_reference_files(capture_path, "./npc-v3-reference-capture.tsv", NPC_DEV, identity),
        "an aliased report path could truncate the capture");
    TEST_CHECK(!npc_reference_files(capture_path, actual_path, NPC_DEV, "invalid"),
               "malformed comparator descriptor identity was accepted");
    TEST_CHECK(
        !npc_reference_files(capture_path, actual_path, NPC_DEV,
                             "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"),
        "capture was rebound to a different comparator");
}

/** @brief Run deterministic comparator admission tests and remove local raw fixtures.
 * @return Zero after all own-history and incomplete-capture checks pass. */
int main(void) {
    (void)remove(capture_path);
    (void)remove(altered_path);
    (void)remove(expected_path);
    (void)remove(actual_path);
    exact_reference_replay();
    rejected_reference_captures();
    rejected_reference_arguments();
    (void)remove(capture_path);
    (void)remove(altered_path);
    (void)remove(expected_path);
    (void)remove(actual_path);
    return 0;
}
