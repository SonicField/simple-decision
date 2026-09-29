#ifndef SIMPLE_DECISION_DECISION_H
#define SIMPLE_DECISION_DECISION_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SD_MAX_PATH 4096
#define SD_MAX_SUMMARY 1024
#define SD_MAX_FIELD 2048
#define SD_MAX_LOG_BYTES (64u * 1024u * 1024u)

enum {
    SD_OK = 0,
    SD_ERROR = 1,
    SD_INVALID_LOG = 2,
    SD_NOT_FOUND = 3,
    SD_BAD_ARGS = 4
};

typedef struct {
    uint64_t id;
    char summary[SD_MAX_SUMMARY];
    char participants[SD_MAX_FIELD];
    char status[64];
    char supersedes[32];
    char risk_tags[SD_MAX_FIELD];
    char artefacts[SD_MAX_FIELD];
    char rationale[SD_MAX_FIELD];
} sd_entry;

typedef struct {
    char *raw;
    size_t raw_len;
    sd_entry *entries;
    size_t count;
} sd_log;

int sd_validate_text(const char *value, size_t maximum, int allow_empty,
                     const char *name);
int sd_valid_status(const char *status);
int sd_parse_id(const char *text, uint64_t *result);
int sd_load(const char *path, int allow_missing, sd_log *log);
void sd_log_free(sd_log *log);
int sd_add(const char *path, const sd_entry *entry, uint64_t *new_id);
void sd_print_entry(FILE *stream, const sd_entry *entry);

#endif

