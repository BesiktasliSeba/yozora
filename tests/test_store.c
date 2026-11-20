#define _POSIX_C_SOURCE 200809L
#include "yozora_store.h"

#include <assert.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void sql(sqlite3 *db, const char *text) {
    int code = sqlite3_exec(db, text, NULL, NULL, NULL);
    if (code != SQLITE_OK) fprintf(stderr, "sqlite fixture: %s\n", sqlite3_errmsg(db));
    assert(code == SQLITE_OK);
}

static void memory_store(void) {
    yozora_store *store = NULL, *other = NULL;
    yozora_record records[2];
    size_t count;
    char url[100], title[YOZORA_TITLE_CAP + 1];
    unsigned i;
    assert(yozora_store_open_private(&store) == YOZORA_OK);
    assert(yozora_store_history(store, 0, records, 2, &count) == YOZORA_OK && count == 0);
    assert(yozora_store_visit(store, "https://example.test/a", "明かり", 1) == YOZORA_OK);
    assert(yozora_store_visit(store, "https://example.test/a", "Акари", 2) == YOZORA_OK);
    assert(yozora_store_visit(store, "https://example.test/a", "clock rollback", 0) == YOZORA_OK);
    assert(yozora_store_history(store, 0, records, 2, &count) == YOZORA_OK && count == 1);
    assert(records[0].visits == 3 && records[0].time_ms == 2 && strcmp(records[0].title, "clock rollback") == 0);
    assert(yozora_store_visit(store, "https://example.test/b", "quote ' and \" and \n", 2) == YOZORA_OK);
    assert(yozora_store_history(store, 1, records, 2, &count) == YOZORA_OK && count == 1);
    assert(strcmp(records[0].url, "https://example.test/b") == 0);
    assert(yozora_store_bookmark(store, "https://example.test/a", "bookmark", 1) == YOZORA_OK);
    assert(yozora_store_bookmark(store, "https://example.test/a", "updated", 3) == YOZORA_OK);
    assert(yozora_store_clear_history(store) == YOZORA_OK);
    assert(yozora_store_bookmarks(store, 0, records, 2, &count) == YOZORA_OK && count == 1);
    assert(strcmp(records[0].title, "updated") == 0);
    assert(yozora_store_open_private(&other) == YOZORA_OK);
    assert(yozora_store_bookmarks(other, 0, records, 2, &count) == YOZORA_OK && count == 0);
    assert(yozora_store_close(other) == YOZORA_OK);

    memset(title, 'x', sizeof title);
    title[YOZORA_TITLE_CAP] = '\0';
    assert(yozora_store_visit(store, "https://example.test/long", title, 1) == YOZORA_LIMIT);
    assert(yozora_store_visit(store, "", "", 1) == YOZORA_INVALID);
    assert(yozora_store_visit(store, "https://example.test/", "\xed\xa0\x80", 1) == YOZORA_INVALID);
    assert(yozora_store_visit(store, "https://example.test/", "", -1) == YOZORA_INVALID);
    assert(yozora_store_history(store, 0, records, YOZORA_STORE_PAGE_LIMIT + 1, &count) == YOZORA_LIMIT && count == 0);
    assert(yozora_store_history(store, YOZORA_HISTORY_LIMIT + 1, records, 2, &count) == YOZORA_LIMIT && count == 0);

    for (i = 0; i < YOZORA_HISTORY_LIMIT + 2; ++i) {
        snprintf(url, sizeof url, "https://example.test/history/%04u", i);
        assert(yozora_store_visit(store, url, "entry", i) == YOZORA_OK);
    }
    assert(yozora_store_history(store, YOZORA_HISTORY_LIMIT - 1, records, 2, &count) == YOZORA_OK && count == 1);
    assert(strcmp(records[0].url, "https://example.test/history/0002") == 0);
    assert(yozora_store_remove_history(store, "https://example.test/history/0000") == YOZORA_NOT_FOUND);
    assert(yozora_store_remove_history(store, "https://example.test/history/0002") == YOZORA_OK);

    for (i = 1; i < YOZORA_BOOKMARK_LIMIT; ++i) {
        snprintf(url, sizeof url, "https://example.test/bookmark/%04u", i);
        assert(yozora_store_bookmark(store, url, "entry", i) == YOZORA_OK);
    }
    assert(yozora_store_bookmark(store, "https://example.test/overflow", "", 10) == YOZORA_LIMIT);
    assert(yozora_store_bookmark(store, "https://example.test/a", "still editable", 9000) == YOZORA_OK);
    assert(yozora_store_remove_bookmark(store, "https://example.test/a") == YOZORA_OK);
    assert(yozora_store_remove_bookmark(store, "https://example.test/a") == YOZORA_NOT_FOUND);
    assert(yozora_store_bookmark(store, "https://example.test/replacement", "", 9001) == YOZORA_OK);
    assert(yozora_store_close(store) == YOZORA_OK);
    assert(yozora_store_open_private(&store) == YOZORA_OK);
    assert(yozora_store_history(store, 0, records, 2, &count) == YOZORA_OK && count == 0);
    assert(yozora_store_close(store) == YOZORA_OK);
}

static void disk_store(void) {
    char directory[] = "/tmp/yozora-store-test-XXXXXX";
    char database[256], sidecar[280], alias[256], traversal[280];
    yozora_store *store = NULL;
    yozora_record records[2];
    size_t count;
    struct stat attributes;
    sqlite3 *db = NULL;
    int fd;
    assert(mkdtemp(directory));
    snprintf(database, sizeof database, "%s/browser.sqlite3", directory);
    snprintf(sidecar, sizeof sidecar, "%s-journal", database);
    snprintf(alias, sizeof alias, "%s-link", directory);
    assert(yozora_store_open(NULL, &store) == YOZORA_INVALID && store == NULL);
    assert(yozora_store_open("relative", &store) == YOZORA_INVALID && store == NULL);
    snprintf(traversal, sizeof traversal, "%s/../escape", directory);
    assert(yozora_store_open(traversal, &store) == YOZORA_INVALID && store == NULL);
    assert(symlink(directory, alias) == 0);
    assert(yozora_store_open(alias, &store) == YOZORA_IO && store == NULL);
    snprintf(traversal, sizeof traversal, "%s/", alias);
    assert(yozora_store_open(traversal, &store) == YOZORA_INVALID && store == NULL);
    assert(unlink(alias) == 0);
    assert(chmod(directory, 0755) == 0);
    assert(yozora_store_open(directory, &store) == YOZORA_IO && store == NULL);
    assert(chmod(directory, 0700) == 0);
    assert(symlink("/does/not/exist", database) == 0);
    assert(yozora_store_open(directory, &store) == YOZORA_IO && store == NULL);
    assert(unlink(database) == 0);
    assert(mkfifo(database, 0600) == 0);
    assert(yozora_store_open(directory, &store) == YOZORA_IO && store == NULL);
    assert(unlink(database) == 0);
    fd = open(database, O_CREAT | O_EXCL | O_WRONLY, 0644);
    assert(fd >= 0 && close(fd) == 0 && chmod(database, 0644) == 0);
    assert(yozora_store_open(directory, &store) == YOZORA_IO && store == NULL);
    assert(chmod(database, 0600) == 0);
    assert(link(database, alias) == 0);
    assert(yozora_store_open(directory, &store) == YOZORA_IO && store == NULL);
    assert(unlink(alias) == 0);
    assert(symlink("/does/not/exist", sidecar) == 0);
    assert(yozora_store_open(directory, &store) == YOZORA_IO && store == NULL);
    assert(unlink(sidecar) == 0);

    assert(yozora_store_open(directory, &store) == YOZORA_OK);
    assert(stat(database, &attributes) == 0 && (attributes.st_mode & 0777) == 0600);
    assert(yozora_store_visit(store, "https://example.test/a", "saved", 1234567890123LL) == YOZORA_OK);
    assert(yozora_store_bookmark(store, "https://example.test/a", "saved bookmark", 1234567890123LL) == YOZORA_OK);
    assert(yozora_store_close(store) == YOZORA_OK);
    assert(yozora_store_open(directory, &store) == YOZORA_OK);
    assert(yozora_store_history(store, 0, records, 2, &count) == YOZORA_OK && count == 1);
    assert(records[0].time_ms == 1234567890123LL && strcmp(records[0].title, "saved") == 0);

    assert(sqlite3_open(database, &db) == SQLITE_OK);
    sql(db, "BEGIN IMMEDIATE");
    assert(yozora_store_visit(store, "https://example.test/blocked", "", 20) == YOZORA_BUSY);
    assert(yozora_store_sqlite_error(store) == SQLITE_BUSY);
    sql(db, "ROLLBACK");
    sql(db, "CREATE TRIGGER fail_prune BEFORE DELETE ON history BEGIN SELECT RAISE(ABORT,'fixture'); END");
    sql(db, "INSERT INTO history VALUES('https://example.test/prune','old',0,1)");
    /* reach the cap with direct fixture rows so the failing prune exercises rollback */
    {
        sqlite3_stmt *statement = NULL;
        unsigned i;
        char url[100];
        sql(db, "BEGIN IMMEDIATE");
        assert(sqlite3_prepare_v2(db, "INSERT INTO history VALUES(?1,'fixture',1,1)", -1, &statement, NULL) == SQLITE_OK);
        for (i = 2; i < YOZORA_HISTORY_LIMIT; ++i) {
            snprintf(url, sizeof url, "https://example.test/fixture/%u", i);
            assert(sqlite3_bind_text(statement, 1, url, -1, SQLITE_TRANSIENT) == SQLITE_OK);
            assert(sqlite3_step(statement) == SQLITE_DONE);
            assert(sqlite3_reset(statement) == SQLITE_OK);
        }
        assert(sqlite3_finalize(statement) == SQLITE_OK);
        sql(db, "COMMIT");
    }
    assert(yozora_store_visit(store, "https://example.test/rollback", "", 2000000000000LL) == YOZORA_STORAGE);
    assert(yozora_store_history(store, 0, records, 2, &count) == YOZORA_OK);
    assert(strcmp(records[0].url, "https://example.test/a") == 0 && records[0].visits == 1);
    sql(db, "DROP TRIGGER fail_prune");
    assert(yozora_store_clear_history(store) == YOZORA_OK);
    sql(db, "INSERT INTO history VALUES('https://example.test/bad',CAST(X'61620063' AS TEXT),1,1)");
    memset(records, 1, sizeof records);
    assert(yozora_store_history(store, 0, records, 2, &count) == YOZORA_CORRUPT && count == 0);
    assert(records[0].url[0] == '\0' && records[1].url[0] == '\0');
    assert(yozora_store_clear_history(store) == YOZORA_OK);
    assert(yozora_store_bookmarks(store, 0, records, 2, &count) == YOZORA_OK && count == 1);
    assert(yozora_store_close(store) == YOZORA_OK);
    sql(db, "INSERT INTO yozora_metadata VALUES(1)");
    assert(yozora_store_open(directory, &store) == YOZORA_SCHEMA && store == NULL);
    sql(db, "DELETE FROM yozora_metadata WHERE rowid=(SELECT max(rowid) FROM yozora_metadata)");
    sql(db, "PRAGMA user_version=2");
    assert(yozora_store_open(directory, &store) == YOZORA_SCHEMA && store == NULL);
    /* a profile of the old name opens, keeps its rows and gets the new metadata table; both tables at once are not a known schema */
    sql(db, "PRAGMA user_version=1");
    sql(db, "ALTER TABLE yozora_metadata RENAME TO akari_metadata");
    assert(yozora_store_open(directory, &store) == YOZORA_OK && store);
    assert(yozora_store_bookmarks(store, 0, records, 2, &count) == YOZORA_OK && count == 1);
    assert(yozora_store_close(store) == YOZORA_OK);
    sql(db, "SELECT version FROM yozora_metadata");
    sql(db, "CREATE TABLE akari_metadata (version INTEGER NOT NULL)");
    assert(yozora_store_open(directory, &store) == YOZORA_SCHEMA && store == NULL);
    sql(db, "DROP TABLE akari_metadata");
    assert(sqlite3_close(db) == SQLITE_OK);
    assert(unlink(database) == 0);
    assert(sqlite3_open(database, &db) == SQLITE_OK);
    sql(db, "CREATE TABLE sqliteXunrelated(value TEXT); INSERT INTO sqliteXunrelated VALUES('keep')");
    assert(sqlite3_close(db) == SQLITE_OK && chmod(database, 0600) == 0);
    assert(yozora_store_open(directory, &store) == YOZORA_SCHEMA && store == NULL);
    assert(unlink(database) == 0);
    fd = open(database, O_CREAT | O_EXCL | O_WRONLY, 0600);
    assert(fd >= 0 && write(fd, "broken database", 15) == 15 && close(fd) == 0);
    assert(yozora_store_open(directory, &store) == YOZORA_CORRUPT && store == NULL);
    assert(unlink(database) == 0 && rmdir(directory) == 0);
}

int main(void) {
    memory_store();
    disk_store();
    puts("store: persistence, private isolation, limits, permissions, schema rejection, the old metadata table, busy and rollback passed");
    return 0;
}
