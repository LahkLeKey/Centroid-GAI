/** @file evolve_runner.c @brief Native builds and immutable fixed-pack evaluation. */
#include "build_config.h"
#include "context_repository.h"
#include "evolve_run.h"
#include <stdlib.h>
#include <string.h>

static char path_character(char value) {
    if (value == '\\')
        return '/';
#ifdef _WIN32
    if (value >= 'A' && value <= 'Z')
        return (char)(value + ('a' - 'A'));
#endif
    return value;
}

static int path_prefix(const char *path, const char *prefix) {
    for (size_t i = 0U; prefix[i] != '\0'; ++i)
        if (path[i] == '\0' || path_character(path[i]) != path_character(prefix[i]))
            return 0;
    return 1;
}

int evolve_output_allowed(const char *path) {
    if (path == NULL || path[0] == '\0')
        return 0;
    const size_t root_length = strlen(EVOLVE_SOURCE_DIRECTORY);
    if (!path_prefix(path, EVOLVE_SOURCE_DIRECTORY))
        return 1;
    const char boundary = path_character(path[root_length]);
    if (boundary != '/' && boundary != '\0')
        return 1;
    return boundary == '/' && path_prefix(path + root_length + 1U, "build") &&
           (path_character(path[root_length + 6U]) == '/' || path[root_length + 6U] == '\0');
}

static int context_sources_unchanged(const evolve_run *run) {
    context_repository_batch *batch = NULL;
    context_repository_report report;
    if (run->source_context_checksum == 0U)
        return 1;
    const int okay = context_repository_scan(EVOLVE_SOURCE_DIRECTORY, &batch, &report) &&
                     report.source_hash == run->source_context_checksum;
    context_repository_destroy(batch);
    return okay;
}

static int external_inputs_unchanged(const evolve_run *run) {
    uint64_t checksum;
    return run->external_checksum == 0U ||
           (evolve_manifest_hash(run->external_manifest, &checksum) &&
            checksum == run->external_checksum);
}

int evolve_inputs_unchanged(const evolve_run *run) {
    uint64_t checksum;
    return evolve_manifest_hash(EVOLVE_INPUT_MANIFEST, &checksum) &&
           checksum == run->inputs_checksum && external_inputs_unchanged(run) &&
           context_sources_unchanged(run);
}

static int source_unchanged(const evolve_work *work) {
    evolve_mutation_source source;
    char *storage = NULL;
    if (!evolve_read_source(work->source, &source, &storage))
        return 0;
    const int okay = source.checksum == work->source_checksum;
    free(storage);
    return okay;
}

int evolve_work_unchanged(const evolve_run *run, const evolve_work *work) {
    char path[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    uint64_t checksum;
    if (!evolve_inputs_unchanged(run) || !source_unchanged(work))
        return 0;
    return !work->configured ||
           (evolve_path_join(path, sizeof(path), work->build, "evolve/inputs.txt") &&
            evolve_manifest_hash(path, &checksum) && checksum == run->inputs_checksum);
}

int evolve_work_create(const evolve_run *run, const char *name, const char *bytes, size_t size,
                       evolve_work *work) {
    work->source_checksum = evolve_bytes_hash(bytes, size);
    work->configured = 0;
    return evolve_path_join(work->directory, sizeof(work->directory), run->output, name) &&
           evolve_create_directory(work->directory) &&
           evolve_path_join(work->source, sizeof(work->source), work->directory,
                            "gameplay_model.c") &&
           evolve_write_exclusive(work->source, bytes, size) &&
           evolve_path_join(work->build, sizeof(work->build), work->directory, "build");
}

static int command(const evolve_run *run, const evolve_work *work, const char *stage,
                   const char *executable, const char *const *arguments) {
    char log[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    int status = -1;
    if (!evolve_work_unchanged(run, work) ||
        !evolve_path_join(log, sizeof(log), work->directory, stage))
        return 0;
    printf("%s: %s\n", work->directory, stage);
    fflush(stdout);
    return evolve_process_run(executable, arguments, log, 1800U, &status) && status == 0 &&
           evolve_work_unchanged(run, work);
}

static size_t generator_arguments(const char **arguments, size_t used) {
    arguments[used++] = "-G";
    arguments[used++] = EVOLVE_CMAKE_GENERATOR;
    if (EVOLVE_CMAKE_PLATFORM[0] != '\0') {
        arguments[used++] = "-A";
        arguments[used++] = EVOLVE_CMAKE_PLATFORM;
    }
    if (EVOLVE_CMAKE_TOOLSET[0] != '\0') {
        arguments[used++] = "-T";
        arguments[used++] = EVOLVE_CMAKE_TOOLSET;
    }
    return used;
}

static int definition(char *output, size_t capacity, const char *key, const char *value) {
    const int size = snprintf(output, capacity, "-D%s=%s", key, value);
    return size >= 0 && (size_t)size < capacity;
}

static int configure(const evolve_run *run, const evolve_work *work) {
    char candidate[EVOLVE_PROCESS_MAX_PATH_BYTES + 64U];
    char compiler[EVOLVE_PROCESS_MAX_PATH_BYTES + 64U];
    char tidy[EVOLVE_PROCESS_MAX_PATH_BYTES + 64U];
    char format[EVOLVE_PROCESS_MAX_PATH_BYTES + 64U];
    const char *arguments[24] = {"-S", EVOLVE_SOURCE_DIRECTORY, "-B", work->build};
    if (!definition(candidate, sizeof(candidate), "CGAI_MUTATION_SOURCE_FILE", work->source) ||
        !definition(compiler, sizeof(compiler), "CMAKE_C_COMPILER", EVOLVE_C_COMPILER) ||
        !definition(tidy, sizeof(tidy), "CLANG_TIDY_EXECUTABLE", EVOLVE_CLANG_TIDY) ||
        !definition(format, sizeof(format), "CLANG_FORMAT_EXECUTABLE", EVOLVE_CLANG_FORMAT))
        return 0;
    size_t used = generator_arguments(arguments, 4U);
    arguments[used++] = "-DCMAKE_BUILD_TYPE=Release";
    arguments[used++] = "-DCGAI_BUILD_TESTS=ON";
    arguments[used++] = "-DCGAI_BUILD_EVOLUTION_TOOLS=ON";
    arguments[used++] =
        EVOLVE_SANITIZERS ? "-DCGAI_ENABLE_SANITIZERS=ON" : "-DCGAI_ENABLE_SANITIZERS=OFF";
    arguments[used++] = candidate;
    arguments[used++] = compiler;
    arguments[used++] = tidy;
    arguments[used++] = format;
    arguments[used] = NULL;
    return command(run, work, "configure.log", EVOLVE_CMAKE_PROGRAM, arguments);
}

static int build(const evolve_run *run, const evolve_work *work) {
    const char *arguments[] = {"--build",    work->build, "--config", "Release",
                               "--parallel", "2",         NULL};
    return command(run, work, "build.log", EVOLVE_CMAKE_PROGRAM, arguments);
}

static int test(const evolve_run *run, const evolve_work *work) {
    const char *arguments[] = {"--test-dir", work->build,           "-C",
                               "Release",    "--output-on-failure", NULL};
    return command(run, work, "tests.log", EVOLVE_CTEST_PROGRAM, arguments);
}

static int lint(const evolve_run *run, const evolve_work *work) {
    const char *arguments[] = {"--build",  work->build, "--config",          "Release",
                               "--target", "life_lint", "life_format_check", NULL};
    return command(run, work, "checks.log", EVOLVE_CMAKE_PROGRAM, arguments);
}

static int read_evaluator_path(const char *path, evolve_work *work) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    char line[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    int okay = fgets(line, sizeof(line), file) != NULL && !ferror(file);
    if (fclose(file) != 0)
        okay = 0;
    if (!okay)
        return 0;
    const size_t size = strcspn(line, "\r\n");
    if (size == 0U || size >= sizeof(line) - 1U)
        return 0;
    line[size] = '\0';
    return evolve_absolute_path(line, work->evaluator, sizeof(work->evaluator));
}

static int evaluator_path(evolve_work *work) {
    char path[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    return evolve_path_join(path, sizeof(path), work->build, "evolve-tools-Release.txt") &&
           read_evaluator_path(path, work);
}

static int measure(const evolve_run *run, const evolve_work *work, life_fitness_split split,
                   life_fitness_report *report) {
    char path[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    static const char *const names[] = {"development.tsv", "confirmation.tsv", "training.tsv"};
    static const char *const logs[] = {"development.log", "confirmation.log", "training.log"};
    const char *name = names[(size_t)split];
    const char *log = logs[(size_t)split];
    if (!evolve_path_join(path, sizeof(path), work->directory, name))
        return 0;
    const char *arguments[] = {life_fitness_split_name(split), path, NULL};
    return command(run, work, log, work->evaluator, arguments) && life_fitness_read(path, report) &&
           report->split == split;
}

static evolve_evaluation_status command_failure(const evolve_run *run, const evolve_work *work,
                                                evolve_evaluation_status status) {
    return evolve_work_unchanged(run, work) ? status : EVOLVE_EVALUATION_INPUTS_CHANGED;
}

evolve_evaluation_status evolve_evaluate(const evolve_run *run, evolve_work *work,
                                         life_fitness_report *training,
                                         life_fitness_report *development,
                                         life_fitness_report *confirmation) {
    if (!configure(run, work))
        return command_failure(run, work, EVOLVE_EVALUATION_CONFIGURE_REJECTED);
    work->configured = 1;
    if (!evolve_work_unchanged(run, work))
        return EVOLVE_EVALUATION_INPUTS_CHANGED;
    if (!build(run, work))
        return command_failure(run, work, EVOLVE_EVALUATION_BUILD_REJECTED);
    if (!test(run, work))
        return command_failure(run, work, EVOLVE_EVALUATION_TEST_REJECTED);
    if (!lint(run, work))
        return command_failure(run, work, EVOLVE_EVALUATION_LINT_REJECTED);
    if (!evaluator_path(work) || !measure(run, work, LIFE_FITNESS_TRAIN, training) ||
        !measure(run, work, LIFE_FITNESS_DEV, development) ||
        !measure(run, work, LIFE_FITNESS_CONFIRM, confirmation))
        return command_failure(run, work, EVOLVE_EVALUATION_FITNESS_REJECTED);
    return evolve_work_unchanged(run, work) ? EVOLVE_EVALUATION_OK
                                            : EVOLVE_EVALUATION_INPUTS_CHANGED;
}

const char *evolve_evaluation_name(evolve_evaluation_status status) {
    static const char *const names[] = {
        "measured",        "configure_rejected",   "build_rejected", "tests_rejected",
        "checks_rejected", "measurement_rejected", "inputs_changed"};
    return (size_t)status < sizeof(names) / sizeof(names[0]) ? names[status] : "invalid";
}
