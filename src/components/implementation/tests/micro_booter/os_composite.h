
#ifndef SQLITE_COS_OS_COMPOSITE_H
#define SQLITE_COS_OS_COMPOSITE_H

#include "sqlite3.h"

#ifndef SQLITE_COS_PROFILE_VFS
#define SQLITE_COS_PROFILE_VFS 0
#endif

#ifndef SQLITE_COS_PROFILE_MUTEX
#define SQLITE_COS_PROFILE_MUTEX 0
#endif

#ifndef SQLITE_COS_PROFILE_MEMORY
#define SQLITE_COS_PROFILE_MEMORY 0
#endif

#if SQLITE_COS_PROFILE_VFS || SQLITE_COS_PROFILE_MUTEX || SQLITE_COS_PROFILE_MEMORY

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define CTRACE_STRING_DEF(x) const int LEN = x ; \
    char _ch[ LEN+1 ]; \
    do { \
        int i = 0; \
        for( i = 0; i < LEN+1; i++ ) \
            _ch[i] = 0; \
    } while(0)

#define CTRACE_APPEND(...) do { snprintf(&_ch[ strlen(_ch) ], LEN - strlen(_ch), __VA_ARGS__ ); } while(0)
#define CTRACE_PRINT(...) do { fprintf(stderr, "%s\n", &_ch[0]); } while(0)

#define APPEND_ERR_CODE(x) do { \
    switch( x ) { \
        case SQLITE_OK: CTRACE_APPEND("SQLITE_OK"); break; \
        case SQLITE_ERROR: CTRACE_APPEND("SQLITE_ERROR"); break; \
        case SQLITE_INTERNAL: CTRACE_APPEND("SQLITE_INTERNAL"); break; \
        case SQLITE_PERM: CTRACE_APPEND("SQLITE_PERM"); break; \
        case SQLITE_ABORT: CTRACE_APPEND("SQLITE_ABORT"); break; \
        case SQLITE_BUSY: CTRACE_APPEND("SQLITE_BUSY"); break; \
        case SQLITE_LOCKED: CTRACE_APPEND("SQLITE_LOCKED"); break; \
        case SQLITE_NOMEM: CTRACE_APPEND("SQLITE_NOMEM"); break; \
        case SQLITE_READONLY: CTRACE_APPEND("SQLITE_READONLY"); break; \
        case SQLITE_INTERRUPT: CTRACE_APPEND("SQLITE_INTERRUPT"); break; \
        case SQLITE_IOERR: CTRACE_APPEND("SQLITE_IOERR"); break; \
        case SQLITE_CORRUPT: CTRACE_APPEND("SQLITE_CORRUPT"); break; \
        case SQLITE_NOTFOUND: CTRACE_APPEND("SQLITE_NOTFOUND"); break; \
        case SQLITE_FULL: CTRACE_APPEND("SQLITE_FULL"); break; \
        case SQLITE_CANTOPEN: CTRACE_APPEND("SQLITE_CANTOPEN"); break; \
        case SQLITE_PROTOCOL: CTRACE_APPEND("SQLITE_PROTOCOL"); break; \
        case SQLITE_EMPTY: CTRACE_APPEND("SQLITE_EMPTY"); break; \
        case SQLITE_SCHEMA: CTRACE_APPEND("SQLITE_SCHEMA"); break; \
        case SQLITE_TOOBIG: CTRACE_APPEND("SQLITE_TOOBIG"); break; \
        case SQLITE_CONSTRAINT: CTRACE_APPEND("SQLITE_CONSTRAINT"); break; \
        case SQLITE_MISMATCH: CTRACE_APPEND("SQLITE_MISMATCH"); break; \
        case SQLITE_MISUSE: CTRACE_APPEND("SQLITE_MISUSE"); break; \
        case SQLITE_NOLFS: CTRACE_APPEND("SQLITE_NOLFS"); break; \
        case SQLITE_AUTH: CTRACE_APPEND("SQLITE_AUTH"); break; \
        case SQLITE_FORMAT: CTRACE_APPEND("SQLITE_FORMAT"); break; \
        case SQLITE_RANGE: CTRACE_APPEND("SQLITE_RANGE"); break; \
        case SQLITE_NOTADB: CTRACE_APPEND("SQLITE_NOTADB"); break; \
        case SQLITE_NOTICE: CTRACE_APPEND("SQLITE_NOTICE"); break; \
        case SQLITE_WARNING: CTRACE_APPEND("SQLITE_WARNING"); break; \
        case SQLITE_ROW: CTRACE_APPEND("SQLITE_ROW"); break; \
        case SQLITE_DONE: CTRACE_APPEND("SQLITE_DONE"); break; \
        case SQLITE_IOERR_READ: CTRACE_APPEND("SQLITE_"); break; \
        case SQLITE_IOERR_SHORT_READ: CTRACE_APPEND("SQLITE_IOERR_SHORT_READ"); break; \
        case SQLITE_IOERR_WRITE: CTRACE_APPEND("SQLITE_IOERR_WRITE"); break; \
        case SQLITE_IOERR_FSYNC: CTRACE_APPEND("SQLITE_IOERR_FSYNC"); break; \
        case SQLITE_IOERR_DIR_FSYNC: CTRACE_APPEND("SQLITE_IOERR_DIR_FSYNC"); break; \
        case SQLITE_IOERR_TRUNCATE: CTRACE_APPEND("SQLITE_IOERR_TRUNCATE"); break; \
        case SQLITE_IOERR_FSTAT: CTRACE_APPEND("SQLITE_IOERR_FSTAT"); break; \
        case SQLITE_IOERR_UNLOCK: CTRACE_APPEND("SQLITE_IOERR_UNLOCK"); break; \
        case SQLITE_IOERR_RDLOCK: CTRACE_APPEND("SQLITE_IOERR_RDLOCK"); break; \
        case SQLITE_IOERR_DELETE: CTRACE_APPEND("SQLITE_IOERR_DELETE"); break; \
        case SQLITE_IOERR_BLOCKED: CTRACE_APPEND("SQLITE_IOERR_BLOCKED"); break; \
        case SQLITE_IOERR_NOMEM: CTRACE_APPEND("SQLITE_IOERR_NOMEM"); break; \
        case SQLITE_IOERR_ACCESS: CTRACE_APPEND("SQLITE_IOERR_ACCESS"); break; \
        case SQLITE_IOERR_CHECKRESERVEDLOCK: CTRACE_APPEND("SQLITE_IOERR_CHECKRESERVEDLOCK"); break; \
        case SQLITE_IOERR_LOCK: CTRACE_APPEND("SQLITE_IOERR_LOCK"); break; \
        case SQLITE_IOERR_CLOSE: CTRACE_APPEND("SQLITE_IOERR_CLOSE"); break; \
        case SQLITE_IOERR_DIR_CLOSE: CTRACE_APPEND("SQLITE_IOERR_DIR_CLOSE"); break; \
        case SQLITE_IOERR_SHMOPEN: CTRACE_APPEND("SQLITE_IOERR_SHMOPEN"); break; \
        case SQLITE_IOERR_SHMSIZE: CTRACE_APPEND("SQLITE_IOERR_SHMSIZE"); break; \
        case SQLITE_IOERR_SHMLOCK: CTRACE_APPEND("SQLITE_IOERR_SHMLOCK"); break; \
        case SQLITE_IOERR_SHMMAP: CTRACE_APPEND("SQLITE_IOERR_SHMMAP"); break; \
        case SQLITE_IOERR_SEEK: CTRACE_APPEND("SQLITE_IOERR_SEEK"); break; \
        case SQLITE_IOERR_DELETE_NOENT: CTRACE_APPEND("SQLITE_IOERR_DELETE_NOENT"); break; \
        case SQLITE_IOERR_MMAP: CTRACE_APPEND("SQLITE_IOERR_MMAP"); break; \
        case SQLITE_IOERR_GETTEMPPATH: CTRACE_APPEND("SQLITE_IOERR_GETTEMPPATH"); break; \
        case SQLITE_IOERR_CONVPATH: CTRACE_APPEND("SQLITE_IOERR_CONVPATH"); break; \
        case SQLITE_IOERR_VNODE: CTRACE_APPEND("SQLITE_IOERR_VNODE"); break; \
        case SQLITE_IOERR_AUTH: CTRACE_APPEND("SQLITE_IOERR_AUTH"); break; \
        case SQLITE_LOCKED_SHAREDCACHE: CTRACE_APPEND("SQLITE_LOCKED_SHAREDCACHE"); break; \
        case SQLITE_BUSY_RECOVERY: CTRACE_APPEND("SQLITE_BUSY_RECOVERY"); break; \
        case SQLITE_BUSY_SNAPSHOT: CTRACE_APPEND("SQLITE_BUSY_SNAPSHOT"); break; \
        case SQLITE_CANTOPEN_NOTEMPDIR: CTRACE_APPEND("SQLITE_CANTOPEN_NOTEMPDIR"); break; \
        case SQLITE_CANTOPEN_ISDIR: CTRACE_APPEND("SQLITE_CANTOPEN_ISDIR"); break; \
        case SQLITE_CANTOPEN_FULLPATH: CTRACE_APPEND("SQLITE_CANTOPEN_FULLPATH"); break; \
        case SQLITE_CANTOPEN_CONVPATH: CTRACE_APPEND("SQLITE_CANTOPEN_CONVPATH"); break; \
        case SQLITE_CORRUPT_VTAB: CTRACE_APPEND("SQLITE_CORRUPT_VTAB"); break; \
        case SQLITE_READONLY_RECOVERY: CTRACE_APPEND("SQLITE_READONLY_RECOVERY"); break; \
        case SQLITE_READONLY_CANTLOCK: CTRACE_APPEND("SQLITE_READONLY_CANTLOCK"); break; \
        case SQLITE_READONLY_ROLLBACK: CTRACE_APPEND("SQLITE_READONLY_ROLLBACK"); break; \
        case SQLITE_READONLY_DBMOVED: CTRACE_APPEND("SQLITE_READONLY_DBMOVED"); break; \
        case SQLITE_ABORT_ROLLBACK: CTRACE_APPEND("SQLITE_ABORT_ROLLBACK"); break; \
        case SQLITE_CONSTRAINT_CHECK: CTRACE_APPEND("SQLITE_CONSTRAINT_CHECK"); break; \
        case SQLITE_CONSTRAINT_COMMITHOOK: CTRACE_APPEND("SQLITE_CONSTRAINT_COMMITHOOK"); break; \
        case SQLITE_CONSTRAINT_FOREIGNKEY: CTRACE_APPEND("SQLITE_CONSTRAINT_FOREIGNKEY"); break; \
        case SQLITE_CONSTRAINT_FUNCTION: CTRACE_APPEND("SQLITE_CONSTRAINT_FUNCTION"); break; \
        case SQLITE_CONSTRAINT_NOTNULL: CTRACE_APPEND("SQLITE_CONSTRAINT_NOTNULL"); break; \
        case SQLITE_CONSTRAINT_PRIMARYKEY: CTRACE_APPEND("SQLITE_CONSTRAINT_PRIMARYKEY"); break; \
        case SQLITE_CONSTRAINT_TRIGGER: CTRACE_APPEND("SQLITE_CONSTRAINT_TRIGGER"); break; \
        case SQLITE_CONSTRAINT_UNIQUE: CTRACE_APPEND("SQLITE_CONSTRAINT_UNIQUE"); break; \
        case SQLITE_CONSTRAINT_VTAB: CTRACE_APPEND("SQLITE_CONSTRAINT_VTAB"); break; \
        case SQLITE_CONSTRAINT_ROWID: CTRACE_APPEND("SQLITE_CONSTRAINT_ROWID"); break; \
        case SQLITE_NOTICE_RECOVER_WAL: CTRACE_APPEND("SQLITE_NOTICE_RECOVER_WAL"); break; \
        case SQLITE_NOTICE_RECOVER_ROLLBACK: CTRACE_APPEND("SQLITE_NOTICE_RECOVER_ROLLBACK"); break; \
        case SQLITE_WARNING_AUTOINDEX: CTRACE_APPEND("SQLITE_WARNING_AUTOINDEX"); break; \
        case SQLITE_AUTH_USER: CTRACE_APPEND("SQLITE_AUTH_USER"); break; \
        case SQLITE_OK_LOAD_PERMANENTLY: CTRACE_APPEND("SQLITE_OK_LOAD_PERMANENTLY"); break; \
        default: CTRACE_APPEND("Unknown(%d)", x ); break; \
    } \
    } while(0)
#endif

/* in-mem FS variables */
#define FS_SECTOR_SIZE 4096 /* sqlite will attempt to before filesystem I/O in blocks of this size */
#define MAX_PATHNAME 512

/* API structs */
extern struct sqlite3_io_methods composite_io_methods;
extern struct composite_vfs_data composite_vfs_app_data;
extern const sqlite3_mem_methods composite_mem_methods;

/* cFile */
struct cFile {
    struct sqlite3_io_methods* composite_io_methods;
    const char* zName;
    void* fd;
};

struct composite_vfs_data {
    sqlite3_uint64 prng_state;
};

/* inmem fs structs */
struct fs_data {
    char* buf; /* a pointer to the buffer containing the file's data */
    sqlite3_int64 len; /* the portion of this buffer contains valid data */
};

struct fs_file {
    struct composite_vfs_data* cVfs;
    struct fs_file* next; /* the next file in the list */

    const char* zName; /* the name of the file */
    struct fs_data data;
    int ref; /* the number of open cFile's the file has */
    int deleteOnClose; /* if 1, then this file should be deleted once it's reference count reaches 0 */
};

/* methods for the in-memory FS used by composite */
void fs_init();
void fs_deinit();
struct fs_file* fs_open(sqlite3_vfs* vfs, const char* zName);
void fs_close(struct fs_file* file);
int fs_read(struct fs_file* file, sqlite3_int64 offset, int len, void* buf);
int fs_write(struct fs_file* file, sqlite3_int64 offset, int len, const void* buf);
int fs_truncate(struct fs_file* file, sqlite3_int64 size);
void fs_size_hint(struct fs_file* file, sqlite3_int64 size);
int fs_exists(sqlite3_vfs* vfs, const char *zName);
int fs_delete(sqlite3_vfs* vfs, const char *zName);

/* sqlite_io function prototypes */
int cClose(sqlite3_file* file);
int cRead(sqlite3_file* file, void* buf, int iAmt, sqlite3_int64 iOfst);
int cWrite(sqlite3_file* file, const void* buf, int iAmt, sqlite3_int64 iOfst);
int cTruncate(sqlite3_file* file, sqlite3_int64 size);
int cSync(sqlite3_file* file, int flags);
int cFileSize(sqlite3_file* file, sqlite3_int64 *pSize);
int cLock(sqlite3_file* file, int i);
int cUnlock(sqlite3_file* file, int i);
int cCheckReservedLock(sqlite3_file* file, int *pResOut);
int cFileControl(sqlite3_file* file, int op, void *pArg);
int cSectorSize(sqlite3_file* file);
int cDeviceCharacteristics(sqlite3_file* file);
int cShmMap(sqlite3_file* file, int iPg, int pgsz, int i, void volatile** v);
int cShmLock(sqlite3_file* file, int offset, int n, int flags);
void cShmBarrier(sqlite3_file* file);
int cShmUnmap(sqlite3_file* file, int deleteFlag);
int cFetch(sqlite3_file* file, sqlite3_int64 iOfst, int iAmt, void **pp);
int cUnfetch(sqlite3_file* file, sqlite3_int64 iOfst, void *p);

/* sqlite_vfs function prototypes */
int cOpen(sqlite3_vfs* vfs, const char *zName, sqlite3_file* baseFile, int flags, int *pOutFlags);
int cDelete(sqlite3_vfs* vfs, const char *zName, int syncDir);
int cAccess(sqlite3_vfs* vfs, const char *zName, int flags, int *pResOut);
int cFullPathname(sqlite3_vfs* vfs, const char *zName, int nOut, char *zOut);
int cRandomness(sqlite3_vfs* vfs, int nByte, char *zOut);
int cSleep(sqlite3_vfs* vfs, int microseconds);
int cGetLastError(sqlite3_vfs* vfs, int i, char *ch);
int cCurrentTime(sqlite3_vfs* vfs, double* time);
int cCurrentTimeInt64(sqlite3_vfs* vfs, sqlite3_int64* time);
void cVfsInit();
void cVfsDeinit();

/* sqlite_mutex function prototypes */
int cMutexInit(void);
int cMutexEnd(void);
sqlite3_mutex* cMutexAlloc(int mutexType);
void cMutexFree(sqlite3_mutex *mutex);
void cMutexEnter(sqlite3_mutex *mutex);
int cMutexTry(sqlite3_mutex *mutex);
void cMutexLeave(sqlite3_mutex *mutex);
int cMutexHeld(sqlite3_mutex *mutex);
int cMutexNotheld(sqlite3_mutex *mutex);

/* sqlite_mem function prototypes */
void *cMemMalloc(int sz);         /* Memory allocation function */
void cMemFree(void* mem);          /* Free a prior allocation */
void *cMemRealloc(void* mem, int newSize);  /* Resize an allocation */
int cMemSize(void* mem);           /* Return the size of an allocation */
int cMemRoundup(int sz);          /* Round up request size to allocation size */
int cMemInit(void* pAppData);           /* Initialize the memory allocator */
void cMemShutdown(void* pAppData);      /* Deinitialize the memory allocator */

#endif
