/** @file bark_main.c @brief C11 specialist initialization, training, replay and reporting CLI. */
#include "bark_tool.h"
#include "internal/error.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Parse a bounded positive additional epoch count.
 * @param text Borrowed decimal token.
 * @param epochs Writable count, unchanged on failure.
 * @return Nonzero for decimal 1..10000. */
static int parse_epochs(const char *text, size_t *epochs) {
    /* Step 1: Reject signs and separators before the standard checked conversion. */
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return 0;
    errno = 0;
    char *end = NULL;
    const unsigned long long parsed = strtoull(text, &end, 10);
    /* Step 2: Publish only supported complete-epoch counts. */
    if (errno != 0 || *end != '\0' || parsed == 0U || parsed > 10000U)
        return 0;
    *epochs = (size_t)parsed;
    return 1;
}

/** @brief Parse a finite supported Adam learning rate.
 * @param text Borrowed decimal token.
 * @param rate Writable finite rate, unchanged on failure.
 * @return Nonzero for a positive rate at most one. */
static int parse_rate(const char *text, double *rate) {
    /* Step 1: Require a numeric token with no surrounding whitespace. */
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return 0;
    errno = 0;
    char *end = NULL;
    const double parsed = strtod(text, &end);
    /* Step 2: Reject underflow, overflow, nonfinite values and trailing input. */
    if (errno != 0 || *end != '\0' || !isfinite(parsed) || parsed <= 0.0 || parsed > 1.0)
        return 0;
    *rate = parsed;
    return 1;
}

/** @brief Parse commands requiring numeric optimization settings.
 * @param argc Complete argument count.
 * @param argv Borrowed complete arguments.
 * @return Native operation status, or ERROR for invalid command arguments. */
static cgai_status train_command(int argc, char **argv) {
    /* Step 1: Parse before loading or writing any checkpoint. */
    double rate = 0.0;
    size_t epochs = 0U;
    if (argc == 6 && strcmp(argv[1], "step") == 0 && parse_epochs(argv[4], &epochs) &&
        parse_rate(argv[5], &rate))
        return bark_tool_step(argv[2], argv[3], epochs, rate);
    /* Step 2: Replay derives total epochs from the reference counters. */
    if (argc == 5 && strcmp(argv[1], "replay") == 0 && parse_rate(argv[4], &rate))
        return bark_tool_replay(argv[2], argv[3], rate);
    return cgai_fail("invalid bark training or replay arguments");
}

/** @brief Dispatch bounded native commands; numerical learning stays entirely in C11.
 * @param argc Complete argument count.
 * @param argv Borrowed complete arguments.
 * @return Native operation status, or ERROR for invalid input. */
static cgai_status run_command(int argc, char **argv) {
    /* Step 1: Keep inference exports and source-free reports separate from optimizer updates. */
    if (argc == 3 && strcmp(argv[1], "init") == 0)
        return bark_tool_init(argv[2]);
    if (argc == 4 && strcmp(argv[1], "export") == 0)
        return bark_tool_export(argv[2], argv[3]);
    if (argc == 6 && strcmp(argv[1], "report") == 0)
        return bark_tool_report(argv[2], argv[3], argv[4], argv[5]);
    if (argc == 3 && strcmp(argv[1], "score") == 0)
        return bark_tool_score(argv[2]);
    /* Step 2: Training and exact replay share strict scalar parsing. */
    return train_command(argc, argv);
}

/** @brief Execute one offline specialist operation using trusted local paths.
 * @param argc Complete argument count.
 * @param argv Borrowed complete arguments.
 * @return Zero on completion; nonzero on malformed input, model or I/O failure. */
int main(int argc, char **argv) {
    /* Step 1: Provide a compact command inventory without accessing any artifacts. */
    if (argc < 3) {
        fprintf(stderr, "usage: cgai_bark_tool init CHECKPOINT | step INPUT OUTPUT EPOCHS RATE | "
                        "export INPUT MODEL | replay INPUT OUTPUT RATE | "
                        "report CANDIDATE INCUMBENT REPORT HARDWARE | score MODEL\n");
        return 2;
    }
    /* Step 2: Report native diagnostics and preserve rejected model state at the caller. */
    if (!run_command(argc, argv)) {
        fprintf(stderr, "%s\n", cgai_last_error());
        return 1;
    }
    return 0;
}
