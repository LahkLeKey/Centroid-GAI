/** @file gameplay_main.c @brief C11 composed training, replay, export and quality CLI. */
#include "gameplay_tool.h"
#include "internal/error.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Parse one supported positive full-pass count.
 * @param text Borrowed scalar token.
 * @param count Writable additional epoch count.
 * @return Nonzero for1..10000 with no trailing characters. */
static int parse_epochs(const char *text, size_t *count) {
    /* Step1: Exclude whitespace and signs before checked conversion. */
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return 0;
    errno = 0;
    char *end = NULL;
    const unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || value < 1U || value > 10000U)
        return 0;
    *count = (size_t)value;
    return 1;
}

/** @brief Parse one supported finite positive learning rate.
 * @param text Borrowed scalar token.
 * @param rate Writable learning rate.
 * @return Nonzero for a finite rate in(0,1]. */
static int parse_rate(const char *text, double *rate) {
    /* Step1: Require a digit-led exact scalar token before any artifact access. */
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return 0;
    errno = 0;
    char *end = NULL;
    const double value = strtod(text, &end);
    if (errno != 0 || *end != '\0' || !isfinite(value) || value <= 0.0 || value > 1.0)
        return 0;
    *rate = value;
    return 1;
}

/** @brief Dispatch commands with numeric optimization settings.
 * @param argc Argument count.
 * @param argv Borrowed arguments.
 * @return Native operation status. */
static cgai_status train_command(int argc, char **argv) {
    /* Step1: Parse optimization scalars before loading or replacing checkpoints. */
    size_t epochs = 0U;
    double rate = 0.0;
    if (argc == 6 && strcmp(argv[1], "step") == 0 && parse_epochs(argv[4], &epochs) &&
        parse_rate(argv[5], &rate))
        return gameplay_tool_step(argv[2], argv[3], epochs, rate);
    if (argc == 5 && strcmp(argv[1], "replay") == 0 && parse_rate(argv[4], &rate))
        return gameplay_tool_replay(argv[2], argv[3], rate);
    return cgai_fail("invalid composed training or replay arguments");
}

/** @brief Dispatch fixed argument arrays to the native task tool.
 * @param argc Complete argument count.
 * @param argv Borrowed complete arguments.
 * @return Native operation status. */
static cgai_status run_command(int argc, char **argv) {
    /* Step1: Pure exports and measurements retain explicit operation identities. */
    if (argc == 3 && strcmp(argv[1], "init") == 0)
        return gameplay_tool_init(argv[2]);
    if (argc == 4 && strcmp(argv[1], "export") == 0)
        return gameplay_tool_export(argv[2], argv[3]);
    if (argc == 6 && strcmp(argv[1], "report") == 0)
        return gameplay_tool_report(argv[2], argv[3], argv[4], argv[5]);
    if (argc == 3 && strcmp(argv[1], "score") == 0)
        return gameplay_tool_score(argv[2]);
    return train_command(argc, argv);
}

/** @brief Run one composed-network operation without an external numerical runtime.
 * @param argc Complete argument count.
 * @param argv Borrowed complete arguments.
 * @return Zero on success, two for usage, one for operation failure. */
int main(int argc, char **argv) {
    /* Step1: Report usage before any artifact access. */
    if (argc < 3) {
        fprintf(stderr,
                "usage: cgai_gameplay_tool init CHECKPOINT | step INPUT OUTPUT EPOCHS RATE | "
                "export INPUT MODEL | replay INPUT OUTPUT RATE | "
                "report CANDIDATE INCUMBENT REPORT HARDWARE | score MODEL\n");
        return 2;
    }
    if (!run_command(argc, argv)) {
        fprintf(stderr, "%s\n", cgai_last_error());
        return 1;
    }
    return 0;
}
