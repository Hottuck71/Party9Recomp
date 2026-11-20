/* The graphics library's shader cache (pipeline_cache.db in the --cache folder) is a SQLite database. Some copies
   come out of a play session in a state that its SQLite reads as "database disk image is malformed", though a
   newer SQLite finds nothing wrong, and the graphics library then crashes at start. The same rows written out
   afresh (VACUUM INTO) read fine. So before the window opens, this reads the cache the way the library does, with
   the same SQLite, and if it can't, rebuilds it that way, or moves it aside so the game starts with an empty one
   (shaders are then built again as they're needed, which only costs a little stutter). */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include "cache_check.h"

extern FILE* report;

/* The few SQLite calls needed, from the libsqlite3-0.dll the graphics library already uses. */
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
int sqlite3_open_v2(const char* filename, sqlite3** db, int flags, const char* vfs);
int sqlite3_close(sqlite3* db);
int sqlite3_prepare_v2(sqlite3* db, const char* sql, int bytes, sqlite3_stmt** stmt, const char** tail);
int sqlite3_step(sqlite3_stmt* stmt);
int sqlite3_finalize(sqlite3_stmt* stmt);
int sqlite3_bind_int64(sqlite3_stmt* stmt, int index, long long value);
long long sqlite3_column_int64(sqlite3_stmt* stmt, int column);
const void* sqlite3_column_blob(sqlite3_stmt* stmt, int column);
int sqlite3_column_bytes(sqlite3_stmt* stmt, int column);
const unsigned char* sqlite3_column_text(sqlite3_stmt* stmt, int column);
int sqlite3_exec(sqlite3* db, const char* sql, void* callback, void* arg, char** error);
const char* sqlite3_errmsg(sqlite3* db);
#define SQLITE_OK 0
#define SQLITE_ROW 100
#define SQLITE_DONE 101
#define SQLITE_OPEN_READONLY 1
#define SQLITE_OPEN_READWRITE 2

/* Runs a query to its end, touching every column of every row. Returns 1 if it got there. */
static int read_all(sqlite3* db, const char* sql, long long a, long long b, int binds, char* why, size_t why_size) {
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        snprintf(why, why_size, "%s", sqlite3_errmsg(db));
        return 0;
    }
    if (binds >= 1) sqlite3_bind_int64(stmt, 1, a);
    if (binds >= 2) sqlite3_bind_int64(stmt, 2, b);
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        sqlite3_column_int64(stmt, 0);
        sqlite3_column_blob(stmt, 1);
        sqlite3_column_bytes(stmt, 1);
    }
    if (rc != SQLITE_DONE) snprintf(why, why_size, "%s", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

/* Reads the cache as the library does: a check, every row, and each kind of row in load order. */
static int readable(const char* path, char* why, size_t why_size) {
    sqlite3* db = NULL;
    int ok = sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK;
    if (!ok) snprintf(why, why_size, "%s", db ? sqlite3_errmsg(db) : "can't open");
    if (ok) {
        sqlite3_stmt* check = NULL;
        if (sqlite3_prepare_v2(db, "PRAGMA quick_check", -1, &check, NULL) == SQLITE_OK) {
            if (sqlite3_step(check) == SQLITE_ROW) {
                const unsigned char* answer = sqlite3_column_text(check, 0);
                if (!answer || strcmp((const char*)answer, "ok")) {
                    ok = 0;
                    snprintf(why, why_size, "quick_check: %s", answer ? (const char*)answer : "no answer");
                }
            }
            sqlite3_finalize(check);
        }
    }
    if (ok) ok = read_all(db, "SELECT hash, config FROM pipeline_cache", 0, 0, 0, why, why_size);
    if (ok) {
        sqlite3_stmt* kinds = NULL;
        if (sqlite3_prepare_v2(db, "SELECT DISTINCT type, config_version FROM pipeline_cache", -1, &kinds, NULL) ==
            SQLITE_OK) {
            while (ok && sqlite3_step(kinds) == SQLITE_ROW)
                ok = read_all(db,
                              "SELECT hash, config FROM pipeline_cache WHERE type = ? AND config_version = ? "
                              "ORDER BY first_frame_used",
                              sqlite3_column_int64(kinds, 0), sqlite3_column_int64(kinds, 1), 2, why, why_size);
            sqlite3_finalize(kinds);
        }
    }
    if (db) sqlite3_close(db);
    return ok;
}

void cache_check(const char* folder) {
    char path[MAX_PATH], rebuilt[MAX_PATH], aside[MAX_PATH], why[256] = "";
    snprintf(path, sizeof path, "%s\\pipeline_cache.db", folder);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return; /* none yet */
    if (readable(path, why, sizeof why)) return;
    fprintf(report, "Shader cache: %s can't be read (%s); rebuilding it\n", path, why);
    snprintf(rebuilt, sizeof rebuilt, "%s\\pipeline_cache.rebuilt.db", folder);
    snprintf(aside, sizeof aside, "%s\\pipeline_cache.damaged-%lld.db", folder, (long long)time(NULL));
    DeleteFileA(rebuilt);
    sqlite3* db = NULL;
    int copied = 0;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK) {
        char sql[MAX_PATH + 32], quoted[MAX_PATH * 2];
        size_t n = 0;
        for (const char* c = rebuilt; *c && n + 2 < sizeof quoted; c++) { /* a quote in the path doubles */
            if (*c == '\'') quoted[n++] = '\'';
            quoted[n++] = *c;
        }
        quoted[n] = 0;
        snprintf(sql, sizeof sql, "VACUUM INTO '%s'", quoted);
        copied = sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK;
    }
    if (db) sqlite3_close(db);
    copied = copied && readable(rebuilt, why, sizeof why);
    if (!MoveFileA(path, aside)) {
        fprintf(report, "Shader cache: couldn't move it aside; the game may not start\n");
        return;
    }
    if (copied && MoveFileA(rebuilt, path)) {
        fprintf(report, "Shader cache: rebuilt, every shader kept; the old file is %s\n", aside);
    } else {
        DeleteFileA(rebuilt);
        fprintf(report, "Shader cache: couldn't rebuild it, so it starts empty; the old file is %s\n", aside);
    }
    fflush(report);
}
