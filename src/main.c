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
    if (result == SD_OK) {
        if (printf("D-%" PRIu64 "\n", id) < 0 || fflush(stdout) != 0) {
            fprintf(stderr, "simple-decision: cannot write decision ID\n");
            return SD_ERROR;
        }
    }
    return result;
}

static int validate_log_path(const char *path)
{
    return sd_validate_text(path, SD_MAX_PATH - 1, 0, "log path");
}

static int command_check(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "simple-decision: check requires exactly one LOG\n");
        return SD_BAD_ARGS;
    }
    if (validate_log_path(argv[2]) != SD_OK) return SD_BAD_ARGS;
    sd_log log;
    int result = sd_load(argv[2], 0, &log);
    if (result != SD_OK) return result;
    if (printf("OK: %zu decisions\n", log.count) < 0) result = SD_ERROR;
    sd_log_free(&log);
    if (result != SD_OK)
        fprintf(stderr, "simple-decision: cannot write check result\n");
    return result;
}

static int command_list(int argc, char **argv)
{
    if (argc < 3 || argc > 4) {
        fprintf(stderr,
                "simple-decision: list requires LOG and optional --status=STATUS\n");
        return SD_BAD_ARGS;
    }
    if (validate_log_path(argv[2]) != SD_OK) return SD_BAD_ARGS;
    const char *status = NULL;
    if (argc == 4) {
        if (strncmp(argv[3], "--status=", 9) != 0) {
            fprintf(stderr, "simple-decision: unknown option '%s'\n", argv[3]);
            return SD_BAD_ARGS;
        }
        status = argv[3] + 9;
        if (!sd_valid_status(status)) {
            fprintf(stderr, "simple-decision: unknown status '%s'\n", status);
            return SD_BAD_ARGS;
        }
    }
    sd_log log;
    int result = sd_load(argv[2], 0, &log);
    if (result != SD_OK) return result;
    for (size_t i = 0; i < log.count; i++) {
        sd_entry *entry = &log.entries[i];
        if (status != NULL && strcmp(status, entry->status) != 0) continue;
        if (printf("D-%" PRIu64 "\t%s\t%s\n", entry->id, entry->status,
                   entry->summary) < 0) {
            fprintf(stderr, "simple-decision: cannot write list output\n");
            result = SD_ERROR;
            break;
        }
    }
    sd_log_free(&log);
    return result;
}

static int command_show(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "simple-decision: show requires LOG and ID\n");
        return SD_BAD_ARGS;
    }
    if (validate_log_path(argv[2]) != SD_OK) return SD_BAD_ARGS;
    uint64_t wanted;
    if (!sd_parse_id(argv[3], &wanted)) {
        fprintf(stderr, "simple-decision: invalid decision ID '%s'\n", argv[3]);
        return SD_BAD_ARGS;
    }
    sd_log log;
    int result = sd_load(argv[2], 0, &log);
    if (result != SD_OK) return result;
    for (size_t i = 0; i < log.count; i++) {
        if (log.entries[i].id == wanted) {
            sd_print_entry(stdout, &log.entries[i]);
            if (ferror(stdout)) {
                fprintf(stderr, "simple-decision: cannot write decision output\n");
                result = SD_ERROR;
            }
            sd_log_free(&log);
            return result;
        }
    }
    fprintf(stderr, "simple-decision: decision D-%" PRIu64 " not found\n",
            wanted);
    sd_log_free(&log);
    return SD_NOT_FOUND;
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
    if (strcmp(argv[1], "check") == 0) return command_check(argc, argv);
    if (strcmp(argv[1], "list") == 0) return command_list(argc, argv);
    if (strcmp(argv[1], "show") == 0) return command_show(argc, argv);
    fprintf(stderr, "simple-decision: unknown command '%s'\n", argv[1]);
    return SD_BAD_ARGS;
}
