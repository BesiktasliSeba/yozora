#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "yozora_store.h"

#include <errno.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct yozora_store {
    sqlite3 *db;
    int last_error;
    int poisoned;
};

static yozora_status sql_status(yozora_store *store, int code) {
    store->last_error = code;
    switch (code & 0xff) {
        case SQLITE_OK: return YOZORA_OK;
        case SQLITE_BUSY:
        case SQLITE_LOCKED: return YOZORA_BUSY;
        case SQLITE_NOMEM: return YOZORA_NOMEM;
        case SQLITE_CORRUPT:
        case SQLITE_NOTADB: return YOZORA_CORRUPT;
        case SQLITE_TOOBIG:
        case SQLITE_FULL: return YOZORA_LIMIT;
        case SQLITE_CANTOPEN:
        case SQLITE_IOERR:
        case SQLITE_READONLY: return YOZORA_IO;
        default: return YOZORA_STORAGE;
    }
}

static yozora_status execute(yozora_store *store, const char *sql) {
    return sql_status(store, sqlite3_exec(store->db, sql, NULL, NULL, NULL));
}

static yozora_status finalize(yozora_store *store, sqlite3_stmt *statement, int code) {
    int final_code = sqlite3_finalize(statement);
    if (code == SQLITE_DONE || code == SQLITE_ROW) code = SQLITE_OK;
    return sql_status(store, code == SQLITE_OK ? final_code : code);
}

static yozora_status prepare(yozora_store *store, const char *sql, sqlite3_stmt **statement) {
    *statement = NULL;
    return sql_status(store, sqlite3_prepare_v2(store->db, sql, -1, statement, NULL));
}

static yozora_status scalar(yozora_store *store, const char *sql, int64_t *value) {
    sqlite3_stmt *statement = NULL;
    yozora_status status = prepare(store, sql, &statement);
    int code;
    *value = 0;
    if (status != YOZORA_OK) return status;
    code = sqlite3_step(statement);
    if (code == SQLITE_ROW && sqlite3_column_type(statement, 0) == SQLITE_INTEGER)
        *value = sqlite3_column_int64(statement, 0);
    else if (code == SQLITE_ROW || code == SQLITE_DONE)
        code = SQLITE_CORRUPT;
    return finalize(store, statement, code);
}

static yozora_status rollback(yozora_store *store, yozora_status failure) {
    int previous = store->last_error;
    yozora_status status;
    if (sqlite3_get_autocommit(store->db)) return failure;
    status = execute(store, "ROLLBACK");
    if (status != YOZORA_OK) {
        store->poisoned = 1;
        return status;
    }
    store->last_error = previous;
    return failure;
}

static yozora_status commit(yozora_store *store) {
    yozora_status status = execute(store, "COMMIT");
    return status == YOZORA_OK ? YOZORA_OK : rollback(store, status);
}

static yozora_status schema(yozora_store *store) {
    int64_t version, tables;
    yozora_status status = execute(store, "BEGIN IMMEDIATE");
    if (status != YOZORA_OK) return status;
    status = scalar(store, "PRAGMA user_version", &version);
    if (status != YOZORA_OK) return rollback(store, status);
    if (version == 0) {
        status = scalar(store, "SELECT count(*) FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'", &tables);
        if (status != YOZORA_OK) return rollback(store, status);
        if (tables) return rollback(store, YOZORA_SCHEMA);
        status = execute(store,
            "CREATE TABLE yozora_metadata (version INTEGER NOT NULL);"
            "INSERT INTO yozora_metadata VALUES (1);"
            "CREATE TABLE history (url TEXT PRIMARY KEY NOT NULL, title TEXT NOT NULL,"
            "time_ms INTEGER NOT NULL CHECK(time_ms>=0), visits INTEGER NOT NULL CHECK(visits>0));"
            "CREATE INDEX history_time ON history(time_ms DESC,url ASC);"
            "CREATE TABLE bookmarks (url TEXT PRIMARY KEY NOT NULL, title TEXT NOT NULL,"
            "time_ms INTEGER NOT NULL CHECK(time_ms>=0));"
            "CREATE INDEX bookmarks_time ON bookmarks(time_ms DESC,url ASC);"
            "PRAGMA user_version=1;");
    } else if (version == 1) {
        /* a profile from before the rename keeps its metadata table as akari_metadata with the same rows, so
           the table takes the new name instead of being refused as another schema */
        status = scalar(store, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='akari_metadata'", &tables);
        if (status == YOZORA_OK && tables == 1) {
            status = scalar(store, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='yozora_metadata'", &tables);
            if (status == YOZORA_OK) status = tables == 0 ? execute(store, "ALTER TABLE akari_metadata RENAME TO yozora_metadata") : YOZORA_SCHEMA;
        }
        if (status != YOZORA_OK) return rollback(store, status);
        status = scalar(store, "SELECT count(*) FROM yozora_metadata", &tables);
        if (status != YOZORA_OK || tables != 1) return rollback(store, YOZORA_SCHEMA);
        status = scalar(store, "SELECT version FROM yozora_metadata", &version);
        if (status != YOZORA_OK || version != 1) return rollback(store, YOZORA_SCHEMA);
        status = scalar(store, "SELECT count(*) FROM history", &tables);
        if (status == YOZORA_OK && tables > YOZORA_HISTORY_LIMIT) status = YOZORA_SCHEMA;
        if (status == YOZORA_OK) status = scalar(store, "SELECT count(*) FROM bookmarks", &tables);
        if (status == YOZORA_OK && tables > YOZORA_BOOKMARK_LIMIT) status = YOZORA_SCHEMA;
    } else {
        status = YOZORA_SCHEMA;
    }
    return status == YOZORA_OK ? commit(store) : rollback(store, status);
}

static yozora_status check_sidecar(const char *path, const char *suffix) {
    char name[4096];
    struct stat attributes;
    int length = snprintf(name, sizeof name, "%s%s", path, suffix);
    if (length < 0 || (size_t)length >= sizeof name) return YOZORA_LIMIT;
    if (lstat(name, &attributes) != 0) return errno == ENOENT ? YOZORA_OK : YOZORA_IO;
    if (!S_ISREG(attributes.st_mode) || attributes.st_uid != geteuid() ||
        (attributes.st_mode & 07777) != 0600 || attributes.st_nlink != 1) return YOZORA_IO;
    return YOZORA_OK;
}

static yozora_status database_path(const char *directory, char *path, size_t capacity) {
    struct stat attributes;
    size_t length;
    int fd, result, close_result;
    yozora_status status = yozora_text_check(directory, 4000, 0);
    if (status != YOZORA_OK) return status;
    if (directory[0] != '/') return YOZORA_INVALID;
    length = strlen(directory);
    /* lstat follows a final directory symlink when the path ends in a slash */
    if (directory[length - 1] == '/' || strstr(directory, "//") ||
        strstr(directory, "/../") || strstr(directory, "/./") ||
        (length >= 3 && strcmp(directory + length - 3, "/..") == 0) ||
        (length >= 2 && strcmp(directory + length - 2, "/.") == 0)) return YOZORA_INVALID;
    if (lstat(directory, &attributes) != 0) return YOZORA_IO;
    if (!S_ISDIR(attributes.st_mode) || attributes.st_uid != geteuid() ||
        (attributes.st_mode & 07777) != 0700) return YOZORA_IO;
    result = snprintf(path, capacity, "%s/browser.sqlite3", directory);
    if (result < 0 || (size_t)result >= capacity) return YOZORA_LIMIT;
    status = check_sidecar(path, "");
    if (status == YOZORA_OK) status = check_sidecar(path, "-journal");
    if (status == YOZORA_OK) status = check_sidecar(path, "-wal");
    if (status == YOZORA_OK) status = check_sidecar(path, "-shm");
    if (status != YOZORA_OK) return status;
    fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (fd < 0) return YOZORA_IO;
    result = fstat(fd, &attributes);
    close_result = close(fd);
    if (result != 0 || close_result != 0 || !S_ISREG(attributes.st_mode) ||
        attributes.st_uid != geteuid() || (attributes.st_mode & 07777) != 0600 ||
        attributes.st_nlink != 1) return YOZORA_IO;
    return YOZORA_OK;
}

static yozora_status open_database(const char *path, yozora_store **out) {
    yozora_store *store;
    yozora_status status;
    int code;
    store = calloc(1, sizeof *store);
    if (!store) return YOZORA_NOMEM;
    code = sqlite3_open_v2(path, &store->db,
                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_PRIVATECACHE, NULL);
    status = sql_status(store, code);
    if (status == YOZORA_OK) status = sql_status(store, sqlite3_busy_timeout(store->db, 250));
    if (status == YOZORA_OK) {
        sqlite3_limit(store->db, SQLITE_LIMIT_LENGTH, 16384);
        /* positive cache_size is measured in pages on ios 5's sqlite */
        status = execute(store, "PRAGMA cache_size=64; PRAGMA temp_store=MEMORY;"
                         "PRAGMA secure_delete=ON; PRAGMA synchronous=FULL;");
    }
    if (status == YOZORA_OK) status = schema(store);
    if (status != YOZORA_OK) {
        if (store->db) {
            code = sqlite3_close(store->db);
            if (code != SQLITE_OK) {
                fprintf(stderr, "yozora: database cleanup failed (%d)\n", code);
            }
        }
        free(store);
        return status;
    }
    *out = store;
    return YOZORA_OK;
}

yozora_status yozora_store_open(const char *directory, yozora_store **out) {
    char path[4096];
    yozora_status status;
    if (!out) return YOZORA_INVALID;
    *out = NULL;
    status = database_path(directory, path, sizeof path);
    if (status != YOZORA_OK) return status;
    return open_database(path, out);
}

yozora_status yozora_store_open_private(yozora_store **out) {
    if (!out) return YOZORA_INVALID;
    *out = NULL;
    return open_database(":memory:", out);
}

yozora_status yozora_store_close(yozora_store *store) {
    yozora_status status;
    if (!store) return YOZORA_INVALID;
    status = sql_status(store, sqlite3_close(store->db));
    if (status == YOZORA_OK) free(store);
    return status;
}

int yozora_store_sqlite_error(const yozora_store *store) {
    return store ? store->last_error : SQLITE_MISUSE;
}

static yozora_status check_record(yozora_store *store, const char *url, const char *title, int64_t time_ms) {
    yozora_status status;
    if (!store) return YOZORA_INVALID;
    if (store->poisoned) return YOZORA_STORAGE;
    if (time_ms < 0) return YOZORA_INVALID;
    status = yozora_url_text_check(url);
    if (status == YOZORA_OK) status = yozora_text_check(title, YOZORA_TITLE_CAP, 1);
    return status;
}

static yozora_status write_record(yozora_store *store, const char *sql,
                                 const char *url, const char *title, int64_t time_ms) {
    sqlite3_stmt *statement = NULL;
    yozora_status status = prepare(store, sql, &statement);
    int code;
    if (status != YOZORA_OK) return status;
    code = sqlite3_bind_text(statement, 1, url, -1, SQLITE_TRANSIENT);
    if (code == SQLITE_OK) code = sqlite3_bind_text(statement, 2, title, -1, SQLITE_TRANSIENT);
    if (code == SQLITE_OK) code = sqlite3_bind_int64(statement, 3, time_ms);
    if (code == SQLITE_OK) code = sqlite3_step(statement);
    return finalize(store, statement, code);
}

static yozora_status prune_history(yozora_store *store) {
    sqlite3_stmt *statement = NULL;
    yozora_status status = prepare(store, "DELETE FROM history WHERE url IN (SELECT url FROM history "
                                  "ORDER BY time_ms DESC,url ASC LIMIT -1 OFFSET ?1)", &statement);
    int code;
    if (status != YOZORA_OK) return status;
    code = sqlite3_bind_int(statement, 1, YOZORA_HISTORY_LIMIT);
    if (code == SQLITE_OK) code = sqlite3_step(statement);
    return finalize(store, statement, code);
}

yozora_status yozora_store_visit(yozora_store *store, const char *url, const char *title, int64_t time_ms) {
    yozora_status status = check_record(store, url, title, time_ms);
    if (status != YOZORA_OK) return status;
    status = execute(store, "BEGIN IMMEDIATE");
    if (status != YOZORA_OK) return status;
    /* ios 5's sqlite has no UPSERT; update and insert share one write transaction */
    status = write_record(store, "UPDATE history SET title=?2, time_ms=max(time_ms,?3),"
                          "visits=CASE WHEN visits<9223372036854775807 THEN visits+1 ELSE visits END WHERE url=?1",
                          url, title, time_ms);
    if (status == YOZORA_OK && sqlite3_changes(store->db) == 0)
        status = write_record(store, "INSERT INTO history VALUES(?1,?2,?3,1)", url, title, time_ms);
    if (status == YOZORA_OK) status = prune_history(store);
    return status == YOZORA_OK ? commit(store) : rollback(store, status);
}

yozora_status yozora_store_bookmark(yozora_store *store, const char *url, const char *title, int64_t time_ms) {
    int64_t count;
    yozora_status status = check_record(store, url, title, time_ms);
    if (status != YOZORA_OK) return status;
    status = execute(store, "BEGIN IMMEDIATE");
    if (status != YOZORA_OK) return status;
    status = write_record(store, "UPDATE bookmarks SET title=?2,time_ms=?3 WHERE url=?1", url, title, time_ms);
    if (status == YOZORA_OK && sqlite3_changes(store->db) == 0) {
        status = scalar(store, "SELECT count(*) FROM bookmarks", &count);
        if (status == YOZORA_OK && count >= YOZORA_BOOKMARK_LIMIT) status = YOZORA_LIMIT;
        if (status == YOZORA_OK)
            status = write_record(store, "INSERT INTO bookmarks VALUES(?1,?2,?3)", url, title, time_ms);
    }
    return status == YOZORA_OK ? commit(store) : rollback(store, status);
}

static yozora_status remove_record(yozora_store *store, const char *sql, const char *url) {
    sqlite3_stmt *statement = NULL;
    yozora_status status;
    int code;
    if (!store) return YOZORA_INVALID;
    if (store->poisoned) return YOZORA_STORAGE;
    status = yozora_url_text_check(url);
    if (status != YOZORA_OK) return status;
    status = prepare(store, sql, &statement);
    if (status != YOZORA_OK) return status;
    code = sqlite3_bind_text(statement, 1, url, -1, SQLITE_TRANSIENT);
    if (code == SQLITE_OK) code = sqlite3_step(statement);
    status = finalize(store, statement, code);
    if (status == YOZORA_OK && sqlite3_changes(store->db) == 0) return YOZORA_NOT_FOUND;
    return status;
}

yozora_status yozora_store_remove_bookmark(yozora_store *store, const char *url) {
    return remove_record(store, "DELETE FROM bookmarks WHERE url=?1", url);
}

yozora_status yozora_store_remove_history(yozora_store *store, const char *url) {
    return remove_record(store, "DELETE FROM history WHERE url=?1", url);
}

yozora_status yozora_store_clear_history(yozora_store *store) {
    if (!store) return YOZORA_INVALID;
    if (store->poisoned) return YOZORA_STORAGE;
    return execute(store, "DELETE FROM history");
}

static yozora_status column_text(sqlite3_stmt *statement, int column, char *text, size_t capacity) {
    const unsigned char *value;
    int bytes;
    if (sqlite3_column_type(statement, column) != SQLITE_TEXT) return YOZORA_CORRUPT;
    value = sqlite3_column_text(statement, column);
    if (!value) return YOZORA_NOMEM;
    bytes = sqlite3_column_bytes(statement, column);
    if (bytes < 0 || (size_t)bytes >= capacity) return YOZORA_CORRUPT;
    if (memchr(value, '\0', (size_t)bytes)) return YOZORA_CORRUPT;
    memcpy(text, value, (size_t)bytes);
    text[bytes] = '\0';
    return yozora_text_check(text, capacity, 1) == YOZORA_OK ? YOZORA_OK : YOZORA_CORRUPT;
}

static yozora_status list_records(yozora_store *store, const char *sql, size_t offset,
                                  yozora_record *records, size_t capacity, size_t *count) {
    sqlite3_stmt *statement = NULL;
    yozora_status status;
    size_t used = 0;
    int code;
    if (count) *count = 0;
    if (records && capacity <= YOZORA_STORE_PAGE_LIMIT) memset(records, 0, capacity * sizeof *records);
    if (!store || !records || !count || !capacity) return YOZORA_INVALID;
    if (capacity > YOZORA_STORE_PAGE_LIMIT || offset > YOZORA_HISTORY_LIMIT) return YOZORA_LIMIT;
    if (store->poisoned) return YOZORA_STORAGE;
    status = prepare(store, sql, &statement);
    if (status != YOZORA_OK) return status;
    code = sqlite3_bind_int64(statement, 1, (sqlite3_int64)capacity);
    if (code == SQLITE_OK) code = sqlite3_bind_int64(statement, 2, (sqlite3_int64)offset);
    while (code == SQLITE_OK && (code = sqlite3_step(statement)) == SQLITE_ROW) {
        yozora_record *record;
        if (used == capacity) { status = YOZORA_STORAGE; break; }
        record = &records[used];
        status = column_text(statement, 0, record->url, sizeof record->url);
        if (status == YOZORA_OK && yozora_url_text_check(record->url) != YOZORA_OK) status = YOZORA_CORRUPT;
        if (status == YOZORA_OK) status = column_text(statement, 1, record->title, sizeof record->title);
        if (status == YOZORA_OK && (sqlite3_column_type(statement, 2) != SQLITE_INTEGER ||
                                   sqlite3_column_type(statement, 3) != SQLITE_INTEGER)) status = YOZORA_CORRUPT;
        if (status != YOZORA_OK) break;
        record->time_ms = sqlite3_column_int64(statement, 2);
        record->visits = sqlite3_column_int64(statement, 3);
        if (record->time_ms < 0 || record->visits < 1) { status = YOZORA_CORRUPT; break; }
        ++used;
        code = SQLITE_OK;
    }
    if (status == YOZORA_OK) status = finalize(store, statement, code);
    else {
        yozora_status cleanup = finalize(store, statement, SQLITE_OK);
        if (cleanup != YOZORA_OK) status = cleanup;
    }
    if (status == YOZORA_OK) *count = used;
    else memset(records, 0, capacity * sizeof *records);
    return status;
}

yozora_status yozora_store_history(yozora_store *store, size_t offset,
                                 yozora_record *records, size_t capacity, size_t *count) {
    return list_records(store, "SELECT url,title,time_ms,visits FROM history "
                        "ORDER BY time_ms DESC,url ASC LIMIT ?1 OFFSET ?2", offset, records, capacity, count);
}

yozora_status yozora_store_bookmarks(yozora_store *store, size_t offset,
                                   yozora_record *records, size_t capacity, size_t *count) {
    return list_records(store, "SELECT url,title,time_ms,1 FROM bookmarks "
                        "ORDER BY time_ms DESC,url ASC LIMIT ?1 OFFSET ?2", offset, records, capacity, count);
}
