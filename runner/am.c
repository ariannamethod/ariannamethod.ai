// am.c — standalone AML runner (CLI wrapper around libaml).
//
// Usage: aml <program.aml>
//
// Reads a .aml file and executes it via am_exec_file() from libaml.a.
// Trivial entry point — exists so that .aml programs can be invoked
// directly from the shell instead of needing a custom C host each time.
//
// Built and installed into /opt/homebrew/bin/aml as part of the
// system-wide AML/notorch baseline on Mac Neo (2026-04-25).

#include <stdio.h>
#include <string.h>
#include "ariannamethod.h"

#define AML_RUNNER_VERSION "0.1.0"

static int print_help(const char *argv0) {
    fprintf(stderr,
        "aml — Arianna Method Language runner (v%s)\n"
        "\n"
        "Usage:\n"
        "  %s <program.aml>      Execute an AML program\n"
        "  %s --version          Print runner version\n"
        "  %s --help             Show this help\n",
        AML_RUNNER_VERSION, argv0, argv0, argv0);
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) return print_help(argv[0]);

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)
        return print_help(argv[0]);

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("aml runner %s (libaml linked)\n", AML_RUNNER_VERSION);
        return 0;
    }

#ifdef AML_WITH_NOTORCH
    am_use_notorch();
#endif
    int rc = am_exec_file(argv[1]);
    if (rc != 0)
        fprintf(stderr, "aml: exec failed (rc=%d): %s\n", rc, am_get_error());
    return rc;
}
