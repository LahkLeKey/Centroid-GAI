/** @file fitness_main.c @brief Fixed-budget independent source-candidate scoring. */
#include "fitness.h"
#include <stdio.h>

int main(int argc, char **argv) {
    life_fitness_split split;
    life_fitness_report report;
    if (argc != 3 || !life_fitness_split_parse(argv[1], &split)) {
        fputs("Usage: cgai_life_fitness train|dev|confirm OUTPUT\n", stderr);
        return 2;
    }
    if (!life_fitness_measure(split, &report)) {
        fputs("Fixed collision fitness measurement failed\n", stderr);
        return 1;
    }
    if (!life_fitness_write(argv[2], &report)) {
        fputs("Could not create a new complete fitness report\n", stderr);
        return 1;
    }
    return 0;
}
