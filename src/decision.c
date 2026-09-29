#include "decision.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

static const char header[] = "# Decision Log\nFormat: simple-decision/1\n";

static int invalid_log(const char *path, size_t line, const char *format, ...)
{
    va_list args;
    fprintf(stderr, "simple-decision: invalid log %s", path);
    if (line != 0) fprintf(stderr, ":%zu", line);
    fprintf(stderr, ": ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    return SD_INVALID_LOG;
}

static int valid_utf8(const unsigned char *s)
{
    while (*s != 0) {
        if (*s < 0x80) {
            if (*s < 0x20 || *s == 0x7f) return 0;
            s++;
            continue;
        }
        unsigned int code;
        size_t extra;
        if (*s >= 0xc2 && *s <= 0xdf) {
            code = *s & 0x1f;
            extra = 1;
        } else if (*s >= 0xe0 && *s <= 0xef) {
            code = *s & 0x0f;
            extra = 2;
        } else if (*s >= 0xf0 && *s <= 0xf4) {
            code = *s & 0x07;
            extra = 3;
        } else {
            return 0;
        }
        s++;
        for (size_t i = 0; i < extra; i++) {
            if ((s[i] & 0xc0) != 0x80) return 0;
            code = (code << 6) | (s[i] & 0x3f);
        }
        if ((extra == 1 && code < 0x80) ||
            (extra == 2 && code < 0x800) ||
            (extra == 3 && code < 0x10000) ||
            (code >= 0xd800 && code <= 0xdfff) || code > 0x10ffff) {
            return 0;
        }
        s += extra;
    }
    return 1;
}

int sd_validate_text(const char *value, size_t maximum, int allow_empty,
                     const char *name)
{
    if (value == NULL) {
        fprintf(stderr, "simple-decision: %s is missing\n", name);
        return SD_BAD_ARGS;
    }
    size_t length = strlen(value);
    if ((!allow_empty && length == 0) || length > maximum) {
        fprintf(stderr, "simple-decision: %s must contain %s1..%zu bytes\n",
                name, allow_empty ? "0.." : "", maximum);
        return SD_BAD_ARGS;
    }
    if (!valid_utf8((const unsigned char *)value)) {
        fprintf(stderr,
                "simple-decision: %s must be valid UTF-8 without control characters\n",
                name);
        return SD_BAD_ARGS;
    }
    return SD_OK;
}

int sd_valid_status(const char *status)
{
    return strcmp(status, "decided") == 0 ||
           strcmp(status, "accepted-risk") == 0 ||
           strcmp(status, "mitigated") == 0 ||
           strcmp(status, "reversed") == 0;
}

int sd_parse_id(const char *text, uint64_t *result)
{
    if (text == NULL || result == NULL || text[0] != 'D' || text[1] != '-')
        return 0;
    const char *digits = text + 2;
    if (*digits < '1' || *digits > '9') return 0;
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(digits, &end, 10);
    if (errno == ERANGE || end == digits || *end != '\0' || value == 0)
        return 0;
    *result = (uint64_t)value;
    return 1;
}

static int copy_field(char *destination, size_t capacity, const char *source,
                      int allow_empty, const char *name)
{
    size_t maximum = capacity - 1;
    if (sd_validate_text(source, maximum, allow_empty, name) != SD_OK)
        return 0;
    memcpy(destination, source, strlen(source) + 1);
    return 1;
}

static char *next_line(char **cursor, size_t *line_number)
{
    if (**cursor == '\0') return NULL;
    char *line = *cursor;
    char *newline = strchr(line, '\n');
    if (newline == NULL) return NULL;
    *newline = '\0';
    *cursor = newline + 1;
    (*line_number)++;
    return line;
}

static const char *field_value(const char *line, const char *prefix)
{
    size_t length = strlen(prefix);
    return strncmp(line, prefix, length) == 0 ? line + length : NULL;
}

static int append_loaded_entry(sd_log *log, const sd_entry *entry)
{
    if (log->count == SIZE_MAX / sizeof(*log->entries)) return 0;
    sd_entry *grown = realloc(log->entries,
                              (log->count + 1) * sizeof(*log->entries));
    if (grown == NULL) return 0;
    log->entries = grown;
    log->entries[log->count++] = *entry;
    return 1;
}

static int parse_log(const char *path, sd_log *log)
{
    char *cursor = log->raw;
    size_t line_number = 0;
    char *line = next_line(&cursor, &line_number);
    if (line == NULL || strcmp(line, "# Decision Log") != 0)
        return invalid_log(path, line_number, "expected '# Decision Log'");
    line = next_line(&cursor, &line_number);
    if (line == NULL || strcmp(line, "Format: simple-decision/1") != 0)
        return invalid_log(path, line_number, "unsupported or missing format");

    uint64_t previous = 0;
    while (*cursor != '\0') {
        line = next_line(&cursor, &line_number);
        if (line == NULL || line[0] != '\0')
            return invalid_log(path, line_number, "expected a blank line");
        line = next_line(&cursor, &line_number);
        if (line == NULL || strcmp(line, "---") != 0)
            return invalid_log(path, line_number, "expected entry separator");
        line = next_line(&cursor, &line_number);
        if (line == NULL || strncmp(line, "### D-", 6) != 0)
            return invalid_log(path, line_number, "expected decision heading");

        char *space = strchr(line + 6, ' ');
        if (space == NULL || space[1] == '\0')
            return invalid_log(path, line_number, "decision summary is missing");
        *space = '\0';
        char id_text[32];
        int written = snprintf(id_text, sizeof(id_text), "D-%s", line + 6);
        sd_entry entry;
        memset(&entry, 0, sizeof(entry));
        if (written < 0 || (size_t)written >= sizeof(id_text) ||
            !sd_parse_id(id_text, &entry.id))
            return invalid_log(path, line_number, "invalid decision ID");
        if (entry.id <= previous)
            return invalid_log(path, line_number,
                               "IDs must increase strictly in file order");
        previous = entry.id;
        if (!copy_field(entry.summary, sizeof(entry.summary), space + 1, 0,
                        "summary"))
            return invalid_log(path, line_number, "invalid summary");

        const char *prefixes[] = {
            "- **Participants:** ", "- **Status:** ", "- **Supersedes:** ",
            "- **Risk tags:** ", "- **Artefacts:** ", "- **Rationale:** "
        };
        char *destinations[] = {
            entry.participants, entry.status, entry.supersedes,
            entry.risk_tags, entry.artefacts, entry.rationale
        };
        const size_t capacities[] = {
            sizeof(entry.participants), sizeof(entry.status),
            sizeof(entry.supersedes), sizeof(entry.risk_tags),
            sizeof(entry.artefacts), sizeof(entry.rationale)
        };
        for (size_t i = 0; i < 6; i++) {
            line = next_line(&cursor, &line_number);
            const char *value = line == NULL ? NULL : field_value(line, prefixes[i]);
            if (value == NULL ||
                !copy_field(destinations[i], capacities[i], value, 0, prefixes[i]))
                return invalid_log(path, line_number, "missing or invalid field");
        }
        if (!sd_valid_status(entry.status))
            return invalid_log(path, line_number - 4, "unknown status '%s'",
                               entry.status);
        if (strcmp(entry.supersedes, "none") != 0) {
            uint64_t linked;
            if (!sd_parse_id(entry.supersedes, &linked))
                return invalid_log(path, line_number - 3,
                                   "invalid supersedes ID");
            int found = 0;
            for (size_t i = 0; i < log->count; i++) {
                if (log->entries[i].id == linked) found = 1;
            }
            if (!found)
                return invalid_log(path, line_number - 3,
                                   "supersedes does not name an earlier entry");
        }
        if (!append_loaded_entry(log, &entry)) {
            fprintf(stderr, "simple-decision: out of memory parsing %s\n", path);
            return SD_ERROR;
        }
    }
    return SD_OK;
}

static int read_all(int fd, char *buffer, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        ssize_t count = read(fd, buffer + offset, length - offset);
        if (count > 0) {
            offset += (size_t)count;
        } else if (count == 0) {
            return 0;
        } else if (errno != EINTR) {
            return 0;
        }
    }
    return 1;
}

int sd_load(const char *path, int allow_missing, sd_log *log)
{
    memset(log, 0, sizeof(*log));
    struct stat lst;
    if (lstat(path, &lst) != 0) {
        if (errno == ENOENT && allow_missing) return SD_OK;
        fprintf(stderr, "simple-decision: cannot inspect %s: %s\n",
                path, strerror(errno));
        return SD_ERROR;
    }
    if (S_ISLNK(lst.st_mode)) {
        fprintf(stderr, "simple-decision: refusing symbolic-link log %s\n", path);
        return SD_ERROR;
    }
    if (!S_ISREG(lst.st_mode)) {
        fprintf(stderr, "simple-decision: %s is not a regular file\n", path);
        return SD_ERROR;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        fprintf(stderr, "simple-decision: cannot open %s: %s\n",
                path, strerror(errno));
        return SD_ERROR;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        fprintf(stderr, "simple-decision: %s is not a regular file\n", path);
        close(fd);
        return SD_ERROR;
    }
    if (st.st_size < 0 || (uintmax_t)st.st_size > SD_MAX_LOG_BYTES) {
        fprintf(stderr, "simple-decision: %s exceeds the 64 MiB limit\n", path);
        close(fd);
        return SD_ERROR;
    }
    log->raw_len = (size_t)st.st_size;
    log->raw = malloc(log->raw_len + 1);
    if (log->raw == NULL) {
        fprintf(stderr, "simple-decision: out of memory reading %s\n", path);
        close(fd);
        return SD_ERROR;
    }
    if (!read_all(fd, log->raw, log->raw_len)) {
        fprintf(stderr, "simple-decision: cannot read %s: %s\n",
                path, strerror(errno));
        close(fd);
        sd_log_free(log);
        return SD_ERROR;
    }
    if (close(fd) != 0) {
        fprintf(stderr, "simple-decision: cannot close %s: %s\n",
                path, strerror(errno));
        sd_log_free(log);
        return SD_ERROR;
    }
    log->raw[log->raw_len] = '\0';
    if (log->raw_len == 0) {
        int result = invalid_log(path, 0, "empty files are not decision logs");
        sd_log_free(log);
        return result;
    }
    if (memchr(log->raw, '\0', log->raw_len) != NULL) {
        int result = invalid_log(path, 0, "NUL byte is not allowed");
        sd_log_free(log);
        return result;
    }
    /* The parser inserts NUL terminators while tokenising. Parse a copy so
     * transactional append can preserve every byte of the existing log. */
    char *original = log->raw;
    char *working = malloc(log->raw_len + 1);
    if (working == NULL) {
        fprintf(stderr, "simple-decision: out of memory parsing %s\n", path);
        sd_log_free(log);
        return SD_ERROR;
    }
    memcpy(working, original, log->raw_len + 1);
    log->raw = working;
    int result = parse_log(path, log);
    free(working);
    log->raw = original;
    if (result != SD_OK) {
        sd_log_free(log);
        return result;
    }
    return SD_OK;
}

void sd_log_free(sd_log *log)
{
    if (log == NULL) return;
    free(log->raw);
    free(log->entries);
    memset(log, 0, sizeof(*log));
}

static int lock_log(const char *path)
{
    char lock_path[SD_MAX_PATH + 6];
    int count = snprintf(lock_path, sizeof(lock_path), "%s.lock", path);
    if (count < 0 || (size_t)count >= sizeof(lock_path)) {
        fprintf(stderr, "simple-decision: lock path is too long\n");
        return -1;
    }
    struct stat st;
    if (lstat(lock_path, &st) == 0 && S_ISLNK(st.st_mode)) {
        fprintf(stderr, "simple-decision: refusing symbolic-link lock %s\n",
                lock_path);
        return -1;
    }
    int fd = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        fprintf(stderr, "simple-decision: cannot open lock %s: %s\n",
                lock_path, strerror(errno));
        return -1;
    }
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        fprintf(stderr, "simple-decision: lock %s is not a regular file\n",
                lock_path);
        close(fd);
        return -1;
    }
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    while (fcntl(fd, F_SETLKW, &lock) != 0) {
        if (errno == EINTR) continue;
        fprintf(stderr, "simple-decision: cannot lock %s: %s\n",
                lock_path, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static void unlock_log(int fd)
{
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    while (fcntl(fd, F_SETLK, &lock) != 0 && errno == EINTR) {}
    close(fd);
}

static int write_all(int fd, const char *data, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        ssize_t count = write(fd, data + offset, length - offset);
        if (count > 0) offset += (size_t)count;
        else if (count < 0 && errno == EINTR) continue;
        else return 0;
    }
    return 1;
}

static int parent_directory(const char *path, char *directory, size_t capacity)
{
    const char *slash = strrchr(path, '/');
    if (slash == NULL) return snprintf(directory, capacity, ".") > 0;
    if (slash == path) return snprintf(directory, capacity, "/") > 0;
    size_t length = (size_t)(slash - path);
    if (length >= capacity) return 0;
    memcpy(directory, path, length);
    directory[length] = '\0';
    return 1;
}

static int commit_log(const char *path, const char *old_data, size_t old_length,
                      const char *addition, size_t addition_length)
{
    char directory[SD_MAX_PATH];
    if (!parent_directory(path, directory, sizeof(directory))) {
        fprintf(stderr, "simple-decision: parent path is too long\n");
        return SD_ERROR;
    }
    char temporary[SD_MAX_PATH + 32];
    int count = snprintf(temporary, sizeof(temporary),
                         "%s/.simple-decision.tmp.XXXXXX", directory);
    if (count < 0 || (size_t)count >= sizeof(temporary)) {
        fprintf(stderr, "simple-decision: temporary path is too long\n");
        return SD_ERROR;
    }
    int fd = mkstemp(temporary);
    if (fd < 0) {
        fprintf(stderr, "simple-decision: cannot create temporary file: %s\n",
                strerror(errno));
        return SD_ERROR;
    }
    mode_t mode = 0644;
    struct stat existing;
    if (lstat(path, &existing) == 0) mode = existing.st_mode & 0777;
    int result = SD_ERROR;
    if (fchmod(fd, mode) != 0 ||
        !write_all(fd, old_data, old_length) ||
        !write_all(fd, addition, addition_length) || fsync(fd) != 0) {
        fprintf(stderr, "simple-decision: cannot write transaction: %s\n",
                strerror(errno));
        goto done;
    }
    if (close(fd) != 0) {
        fd = -1;
        fprintf(stderr, "simple-decision: cannot close transaction: %s\n",
                strerror(errno));
        goto done;
    }
    fd = -1;
#ifdef SIMPLE_DECISION_TESTING
    const char *failure = getenv("SIMPLE_DECISION_TEST_FAIL");
    if (failure != NULL && strcmp(failure, "before-rename") == 0) {
        errno = EIO;
        fprintf(stderr, "simple-decision: injected failure before rename\n");
        goto done;
    }
#endif
    if (rename(temporary, path) != 0) {
        fprintf(stderr, "simple-decision: cannot commit transaction: %s\n",
                strerror(errno));
        goto done;
    }
    temporary[0] = '\0';
#ifdef SIMPLE_DECISION_TESTING
    if (failure != NULL && strcmp(failure, "after-rename") == 0) {
        errno = EIO;
        fprintf(stderr, "simple-decision: injected failure after rename\n");
        return SD_ERROR;
    }
#endif
    int directory_fd = open(directory, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if (directory_fd < 0 || fsync(directory_fd) != 0) {
        fprintf(stderr, "simple-decision: cannot sync parent directory: %s\n",
                strerror(errno));
        if (directory_fd >= 0) close(directory_fd);
        return SD_ERROR;
    }
    if (close(directory_fd) != 0) {
        fprintf(stderr, "simple-decision: cannot close parent directory: %s\n",
                strerror(errno));
        return SD_ERROR;
    }
    result = SD_OK;

done:
    if (fd >= 0) close(fd);
    if (temporary[0] != '\0') unlink(temporary);
    return result;
}

void sd_print_entry(FILE *stream, const sd_entry *entry)
{
    fprintf(stream,
            "### D-%" PRIu64 " %s\n"
            "- **Participants:** %s\n"
            "- **Status:** %s\n"
            "- **Supersedes:** %s\n"
            "- **Risk tags:** %s\n"
            "- **Artefacts:** %s\n"
            "- **Rationale:** %s\n",
            entry->id, entry->summary, entry->participants, entry->status,
            entry->supersedes, entry->risk_tags, entry->artefacts,
            entry->rationale);
}

int sd_add(const char *path, const sd_entry *entry, uint64_t *new_id)
{
    int lock_fd = lock_log(path);
    if (lock_fd < 0) return SD_ERROR;
    sd_log log;
    int result = sd_load(path, 1, &log);
    if (result != SD_OK) {
        unlock_log(lock_fd);
        return result;
    }
    uint64_t id = log.count == 0 ? 1 : log.entries[log.count - 1].id + 1;
    if (id == 0) {
        fprintf(stderr, "simple-decision: decision ID space is exhausted\n");
        sd_log_free(&log);
        unlock_log(lock_fd);
        return SD_ERROR;
    }
    if (strcmp(entry->supersedes, "none") != 0) {
        uint64_t linked;
        int found = sd_parse_id(entry->supersedes, &linked) ? 0 : -1;
        for (size_t i = 0; i < log.count; i++) {
            if (log.entries[i].id == linked) found = 1;
        }
        if (found != 1) {
            fprintf(stderr,
                    "simple-decision: --supersedes must name an earlier decision\n");
            sd_log_free(&log);
            unlock_log(lock_fd);
            return SD_BAD_ARGS;
        }
    }
    sd_entry complete = *entry;
    complete.id = id;
    char addition[SD_MAX_SUMMARY + SD_MAX_FIELD * 4 + 512];
    int length = snprintf(addition, sizeof(addition),
            "\n---\n### D-%" PRIu64 " %s\n"
            "- **Participants:** %s\n"
            "- **Status:** %s\n"
            "- **Supersedes:** %s\n"
            "- **Risk tags:** %s\n"
            "- **Artefacts:** %s\n"
            "- **Rationale:** %s\n",
            complete.id, complete.summary, complete.participants,
            complete.status, complete.supersedes, complete.risk_tags,
            complete.artefacts, complete.rationale);
    if (length < 0 || (size_t)length >= sizeof(addition)) {
        fprintf(stderr, "simple-decision: formatted entry exceeds internal limit\n");
        sd_log_free(&log);
        unlock_log(lock_fd);
        return SD_ERROR;
    }
    const char *base = log.raw == NULL ? header : log.raw;
    size_t base_length = log.raw == NULL ? sizeof(header) - 1 : log.raw_len;
    result = commit_log(path, base, base_length, addition, (size_t)length);
    sd_log_free(&log);
    unlock_log(lock_fd);
    if (result == SD_OK) *new_id = id;
    return result;
}
