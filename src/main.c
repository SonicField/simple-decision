#include "decision.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define SIMPLE_DECISION_VERSION "0.1.0"

static void usage(FILE *stream)
{
    fprintf(stream,
        "Usage:\n"
        "  simple-decision add LOG SUMMARY --participants=TEXT --rationale=TEXT [OPTIONS]\n"
        "  simple-decision list LOG [--status=STATUS]\n"
        "  simple-decision show LOG ID\n"
        "  simple-decision check LOG\n"
        "  simple-decision help\n"
        "  simple-decision version\n\n"
        "Add options:\n"
        "  --artefacts=TEXT\n"
        "  --risk-tags=TEXT\n"
        "  --status=decided|accepted-risk|mitigated|reversed\n"
        "  --supersedes=ID\n");
}

static int set_option(char *destination, size_t capacity, const char *value,
                      const char *name, int *seen)
{
    if (*seen) {
        fprintf(stderr, "simple-decision: duplicate option --%s\n", name);
        return SD_BAD_ARGS;
    }
    *seen = 1;
    int result = sd_validate_text(value, capacity - 1, 0, name);
    if (result != SD_OK) return result;
    memcpy(destination, value, strlen(value) + 1);
    return SD_OK;
}

static int command_add(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr,
                "simple-decision: add requires LOG, SUMMARY, --participants, and --rationale\n");
        return SD_BAD_ARGS;
    }
    const char *path = argv[2];
    if (sd_validate_text(path, SD_MAX_PATH - 1, 0, "log path") != SD_OK ||
        sd_validate_text(argv[3], SD_MAX_SUMMARY - 1, 0, "summary") != SD_OK)
        return SD_BAD_ARGS;

    sd_entry entry;
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.summary, argv[3], strlen(argv[3]) + 1);
    memcpy(entry.status, "decided", sizeof("decided"));
    memcpy(entry.supersedes, "none", sizeof("none"));
    memcpy(entry.risk_tags, "none", sizeof("none"));
    memcpy(entry.artefacts, "none", sizeof("none"));
    int participants_seen = 0, rationale_seen = 0, artefacts_seen = 0;
    int risks_seen = 0, status_seen = 0, supersedes_seen = 0;

    for (int i = 4; i < argc; i++) {
        const char *value;
        int result;
        if (strncmp(argv[i], "--participants=", 15) == 0) {
            value = argv[i] + 15;
            result = set_option(entry.participants, sizeof(entry.participants),
                                value, "participants", &participants_seen);
        } else if (strncmp(argv[i], "--rationale=", 12) == 0) {
            value = argv[i] + 12;
            result = set_option(entry.rationale, sizeof(entry.rationale),
                                value, "rationale", &rationale_seen);
        } else if (strncmp(argv[i], "--artefacts=", 12) == 0) {
            value = argv[i] + 12;
            result = set_option(entry.artefacts, sizeof(entry.artefacts),
                                value, "artefacts", &artefacts_seen);
        } else if (strncmp(argv[i], "--risk-tags=", 12) == 0) {
            value = argv[i] + 12;
            result = set_option(entry.risk_tags, sizeof(entry.risk_tags),
                                value, "risk-tags", &risks_seen);
        } else if (strncmp(argv[i], "--status=", 9) == 0) {
            value = argv[i] + 9;
            result = set_option(entry.status, sizeof(entry.status), value,
                                "status", &status_seen);
            if (result == SD_OK && !sd_valid_status(entry.status)) {
                fprintf(stderr, "simple-decision: unknown status '%s'\n",
                        entry.status);
                result = SD_BAD_ARGS;
            }
        } else if (strncmp(argv[i], "--supersedes=", 13) == 0) {
            value = argv[i] + 13;
            result = set_option(entry.supersedes, sizeof(entry.supersedes),
                                value, "supersedes", &supersedes_seen);
            uint64_t ignored;
            if (result == SD_OK && !sd_parse_id(entry.supersedes, &ignored)) {
                fprintf(stderr, "simple-decision: invalid supersedes ID '%s'\n",
                        entry.supersedes);
                result = SD_BAD_ARGS;
            }
        } else {
            fprintf(stderr, "simple-decision: unknown option '%s'\n", argv[i]);
            return SD_BAD_ARGS;
        }
        if (result != SD_OK) return result;
    }
    if (!participants_seen || !rationale_seen) {
        fprintf(stderr,
                "simple-decision: --participants and --rationale are required\n");
        return SD_BAD_ARGS;
    }
    uint64_t id;
    int result = sd_add(path, &entry, &id);
    if (result == SD_OK && printf("D-%" PRIu64 "\n", id) < 0) {
        fprintf(stderr, "simple-decision: cannot write decision ID\n");
        return SD_ERROR;
    }
    return result;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr);
        return SD_BAD_ARGS;
    }
    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "-h") == 0) {
        usage(stdout);
        return SD_OK;
    }
    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        puts("simple-decision " SIMPLE_DECISION_VERSION);
        return SD_OK;
    }
    if (strcmp(argv[1], "add") == 0) return command_add(argc, argv);
    fprintf(stderr, "simple-decision: unknown command '%s'\n", argv[1]);
    return SD_BAD_ARGS;
}

