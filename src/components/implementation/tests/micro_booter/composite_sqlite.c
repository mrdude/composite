
#if SQLITE_OS_OTHER

#include <stdlib.h> /* for malloc() and friends */

#include "os_composite.h"

char* _get_region(void* mem_allocation) {
    return ((char*)mem_allocation) - sizeof(size_t);
}

void* _get_memory(char* region_start) {
    return (void*)(region_start + sizeof(size_t));
}

char* _malloc_region(size_t sz) {
    char* region_start = malloc(sz + sizeof(size_t));
    *((int*)region_start) = sz;
    return region_start;
}

void _free_region(char* region) {
    free(region);
}

size_t _get_memory_size(void* mem_allocation) {
    char* region_start = _get_region(mem_allocation);
    size_t sz = *((size_t*)region_start);
    return sz;
}

/* Memory allocation function */
void* cMemMalloc(int sz) {
    char* region_start = _malloc_region(sz);
    if( region_start == 0 ) return 0;

    return _get_memory(region_start);
}

/* Free a prior allocation */
void cMemFree(void* mem) {
    if( mem == 0 ) return;
    
    char* region = _get_region(mem);
    free(region);
}

/* Resize an allocation */
void* cMemRealloc(void* mem, int newSize) {
    char* old_region = _get_region(mem);
    size_t old_sz = _get_memory_size(mem);

    char* new_region = _malloc_region(newSize);
    if( new_region == 0 ) return 0;

    int i = 0;
    for( i = 0; i < old_sz; i++ )
        new_region[i + sizeof(int)] = old_region[i + sizeof(int)];
    
    _free_region(old_region);

    return _get_memory(new_region);
}

/* Return the size of an allocation */
int cMemSize(void* mem) {
    return (int)_get_memory_size(mem);
}

/* Round up request size to allocation size */
int cMemRoundup(int sz) {
    return sz;
}

/* Initialize the memory allocator */
int cMemInit(void* pAppData) {
    return SQLITE_OK;
}

/* Deinitialize the memory allocator */
void cMemShutdown(void* pAppData) {
}

#endif // SQLITE_OS_OTHER

#if SQLITE_OS_OTHER && SQLITE_THREADSAFE

#include "os_composite.h"

int cMutexInit() {
    return SQLITE_OK;
}

int cMutexEnd() {
    return SQLITE_OK;
}

/* creates a mutex of the given type
 * @param mutexType one of SQLITE_MUTEX_*
 * @return a pointer to a mutex, or NULL if it couldn't be created
 */
sqlite3_mutex* cMutexAlloc(int mutexType) {
    return 0;
}

void cMutexFree(sqlite3_mutex *mutex) {
}

/* tries to enter the given mutex.
 * if another thread is in the mutex, this method will block.
 */
void cMutexEnter(sqlite3_mutex *mutex) {
}

/* tries to enter the given mutex.
 * if another thread is in the mutex, this method will return SQLITE_BUSY.
 *
 * "Some systems ... do not support the operation implemented by sqlite3_mutex_try(). On those systems, sqlite3_mutex_try() will always return SQLITE_BUSY.
 *  The SQLite core only ever uses sqlite3_mutex_try() as an optimization so this is acceptable behavior."
 */
int cMutexTry(sqlite3_mutex *mutex) {
    return SQLITE_BUSY;
}

/* exits the given mutex.
 * behavior is undefined if the mutex wasn't entered by the calling thread
 */
void cMutexLeave(sqlite3_mutex *mutex) {
}

/* returns true if this mutex is held by the calling thread
 *
 * this is used only in SQLite assert()'s, so a working
 * implementation isn't really needed; this can just return TRUE
 */
int cMutexHeld(sqlite3_mutex *mutex) {
    return 1;
}

/* returns true if this mutex is NOT held by the calling thread
 *
 * this is used only in SQLite assert()'s, so a working
 * implementation isn't really needed; this can just return TRUE
 */
int cMutexNotheld(sqlite3_mutex *mutex) {
    return 1;
}

#endif // SQLITE_OS_OTHER && SQLITE_THREADSAFE
/* Contains code for an in-memory filesystem implementation */

#if SQLITE_OS_OTHER

/* returns 1 if the given strings are equal, 0 if not */
static int _fs_strequals(const char* s1, const char* s2, const int n) {
    int i = 0;
    for( ; i < n; i++ ) {
        if( s1[i] == 0 && s2[i] == 0 ) {
            return 1;
        }

        if( s1[i] != s2[i] ) {
            return 0;
        }
    }

    return 0;
}

static void _fs_copydata(char* dst, const char* src, int n) {
    int i = 0;
    for( ; i < n; i++ ) {
        dst[i] = src[i];
    }
}

#include "os_composite.h"

#define INITIAL_BUF_DATA_SIZE (FS_SECTOR_SIZE*2)

/* the maximum possible size of a file; the largest value that can be represented with a signed 64-bit int
 * on 32-bit systems, the actual minimum will be much lower.
 */
#define MAX_FILE_LEN ( (int64_t)(1L<<(sizeof(int64_t)-1)) )

#define MIN(type, a, b) ( ((type)(a)) < ((type)(b)) ? (type)(a) : (type)(b) )

static struct fs_file* _fs_file_list = 0;

/* private inmem fs functions */
static void* _FS_MALLOC(int sz) {
    sz = composite_mem_methods.xRoundup(sz);
    return composite_mem_methods.xMalloc(sz);
}

static int _FS_MEMSIZE(void* mem) {
    return composite_mem_methods.xSize(mem);
}

static void* _FS_REALLOC(void* mem, int newSize) {
    return composite_mem_methods.xRealloc(mem, newSize);
}

static void _FS_FREE(void* mem) {
    composite_mem_methods.xFree(mem);
}

static char* _fs_copystring(const char* str, int n) {
    /* get the length of the string */
    int len;
    for( len = 0; str[len] != '\0' && len < n; len++ ) {}

    /* malloc() some memory for our copy */
    char* new_str = _FS_MALLOC(len+1);
    if( new_str == 0 ) {
        return 0;
    }

    /* perform the copy */
    int i = 0;
    for( i = 0; i < len; i++ ) {
        new_str[i] = str[i];
    }
    new_str[len] = 0;

    return new_str;
}

/* adds the given file to _fs_file_list */
static void _fs_file_link(struct fs_file* file) {
    file->next = _fs_file_list->next;
    _fs_file_list->next = file;
}

/* removes the given file from _fs_file_list */
static void _fs_file_unlink(struct fs_file* file) {
    struct fs_file* prev = _fs_file_list;
    struct fs_file* next = _fs_file_list->next;

    while( next != 0 ) {
        if( next == file ) {
            prev->next = next->next;
            file->next = 0;
            break;
        }

        prev = next;
        next = next->next;
    }
}

/* searches _fs_file_list for the file with the given name, or 0 if it doesn't exist */
static struct fs_file* _fs_find_file(sqlite3_vfs* vfs, const char* zName) {
    struct fs_file* file;
    for( file = _fs_file_list->next; file != 0; file = file->next ) {
        if( _fs_strequals(file->zName, zName, MAX_PATHNAME) ) {
            return file;
        }
    }

    return 0;
}

static struct fs_file* _fs_file_alloc(sqlite3_vfs* vfs, const char *zName) {
    struct composite_vfs_data* cVfs = (struct composite_vfs_data*)(vfs->pAppData);
    struct fs_file* file = _FS_MALLOC( sizeof(struct fs_file) );
    if( file == 0 )
        return 0;
    
    char* buf = _FS_MALLOC( INITIAL_BUF_DATA_SIZE );
    if( buf == 0 ) {
        _FS_FREE(file);
        return 0;
    }

    char* zNameCopy = _fs_copystring(zName, MAX_PATHNAME);
    if( zNameCopy == 0 ) {
        _FS_FREE(file);
        _FS_FREE(buf);
        return 0;
    }
    
    file->cVfs = cVfs;
    file->next = 0;
    file->zName = zNameCopy;
    file->data.buf = buf;
    file->data.len = 0;
    file->ref = 0;
    file->deleteOnClose = 0;

    _fs_file_link(file);

    return file;
}

/* free the file and all of its blocks */
static void _fs_file_free(struct fs_file* file) {
    if( file->zName ) {
        _FS_FREE( (char*)file->zName );
        file->zName = 0;
    }

    if( file->data.buf ) {
        _FS_FREE( file->data.buf );
        file->data.buf = 0;
    }

    _FS_FREE( file );
}

/* makes sure that there is sz bytes of space in file's data buffer
 * returns 1 on success, 0 on failure
 */
static int _fs_data_ensure_capacity(struct fs_file* file, sqlite3_int64 sz) {
    int old_size = _FS_MEMSIZE(file->data.buf);
    if( old_size < sz ) {
        int new_size = old_size * 2;
        if( new_size < sz ) {
            new_size = sz;
        }

        void* new_buf = _FS_REALLOC(file->data.buf, new_size);
        if( new_buf == 0 ) {
            return 0;
        } else {
            file->data.buf = new_buf;
        }
    }

    return 1;
}

/* inmem fs functions */
void fs_init() {
    _fs_file_list = _FS_MALLOC( sizeof(struct fs_file) );
    _fs_file_list->zName = 0;
    _fs_file_list->data.buf = 0;
    _fs_file_list->next = 0;
}

void fs_deinit() {
    /* free all files from memory */
    struct fs_file* file = _fs_file_list->next;
    for( ; file != 0; ) {
        void* next = file->next;
        _fs_file_free(file);
        file = next;
    }

    /* clear the file list */
    _FS_FREE( _fs_file_list );
    _fs_file_list = 0;
}

struct fs_file* fs_open(sqlite3_vfs* vfs, const char* zName) {
    //TODO make this atomic
    //atomic {
    struct fs_file* file = _fs_find_file(vfs, zName);
    if( file == 0 ) {
        file = _fs_file_alloc(vfs, zName);
    }
    //}
    if( file == 0 ) {
        return 0;
    }

    file->ref++;
    return file;
}

void fs_close(struct fs_file* file) {
    //TODO make this threadsafe
    file->ref--;
    if( file->ref == 0 && file->deleteOnClose ) { /* have we been waiting to delete this file? */
        _fs_file_unlink(file); //unlink the file from the list of files
        _fs_file_free(file); //free the memory the file used
    }
}

/* returns the number of bytes read, or -1 if an error occurred. short reads are allowed. */
int fs_read(struct fs_file* file, sqlite3_int64 offset, int len, void* buf) {
    //TODO check locks

    /* perform sanity checks on offset and len */
    if( offset < 0 || len < 0 ) {
        return -1;
    }
    
    /* determine the number of bytes to read */
    sqlite3_int64 end_offset = offset + (sqlite3_int64)len;
    if( end_offset > file->data.len ) end_offset = file->data.len;
    int bytes_read = (int)(end_offset - offset);

    /* copy the bytes into the buffer */
    if( bytes_read > 0 ) {
        _fs_copydata( (char*)buf, (const char*)(&file->data.buf[offset]), bytes_read);
    }

    return bytes_read;
}

/* returns the number of bytes written, or -1 if an error occurred. partial writes are not allowed. */
int fs_write(struct fs_file* file, sqlite3_int64 offset, int len, const void* buf) {
    //TODO check locks -- this should occur atomically

    /* perform sanity checks on offset and len */
    if( offset < 0 || len < 0 ) {
        return -1;
    }

    /* ensure that our buffer is large enough to perform the write */
    sqlite3_int64 end_offset = offset + (sqlite3_int64)len;
    if( _fs_data_ensure_capacity(file, end_offset) == 0 ) {
        return -1; /* we don't have enough memory to perform the write */
    }

    /* perform the write */
    _fs_copydata( (char*)(&file->data.buf[offset]), (const char*)buf, len );

    /* adjust file->data.len */
    file->data.len = end_offset;

    return len;
}

/* returns 1 on success, 0 on failure */
int fs_truncate(struct fs_file* file, sqlite3_int64 size) {
    if( size < file->data.len ) {
        file->data.len = size; //TODO reclaim this memory
    }
    return 1;
}

void fs_size_hint(struct fs_file* file, sqlite3_int64 size) {
    _fs_data_ensure_capacity(file, size);
}

/* returns 1 if the given file exists, 0 if it doesn't */
int fs_exists(sqlite3_vfs* vfs, const char *zName) {
    struct fs_file* file = _fs_find_file(vfs, zName);
    return (file != 0);
}

/* returns 1 on success, 0 on failure */
int fs_delete(sqlite3_vfs* vfs, const char *zName) {
    struct fs_file* file = _fs_find_file(vfs, zName);
    if( file == 0 ) { /* the file doesn't exist */
        return 1;
    }

    if( file->ref == 0 ) { /* no one has this file open currently */
        _fs_file_unlink(file);
        _fs_file_free(file);
    } else { /* the file is open somewhere */
        file->deleteOnClose = 1; /* when this file is closed, it will be deleted */
    }

    return 1;
}

#endif //SQLITE_OS_OTHER

#if SQLITE_OS_OTHER

#include "os_composite.h"

/* sqlite3_io_methods */
int cClose(sqlite3_file* baseFile) {
    struct cFile* file = (struct cFile*)baseFile;
    
    fs_close((struct fs_file*)file->fd);
    file->fd = 0;

    return SQLITE_OK;
}

int cRead(sqlite3_file* baseFile, void* buf, int iAmt, sqlite3_int64 iOfst) {
    struct cFile* file = (struct cFile*)baseFile;
    struct fs_file* fd = (struct fs_file*)file->fd;

    /* read the bytes */
    int bytesRead = fs_read(fd, iOfst, iAmt, buf);

    /* was there an error? */
    if( bytesRead == -1 ) {
        return SQLITE_IOERR_READ;
    }

    if( bytesRead < iAmt ) {
        /* if we do a short read, we have to fill the rest of the buffer with 0's */
        char* data = buf;
        int i;
        for( i = bytesRead; i < iAmt; i++ )
            data[i] = 0;

        return SQLITE_IOERR_SHORT_READ;
    }

    //assert( bytesRead == iAmt);
    return SQLITE_OK;
}

int cWrite(sqlite3_file* baseFile, const void* buf, int iAmt, sqlite3_int64 iOfst) {
    struct cFile* file = (struct cFile*)baseFile;
    struct fs_file* fd = (struct fs_file*)file->fd;

    int bytesWritten = fs_write(fd, iOfst, iAmt, buf);
    if( bytesWritten == iAmt ) {
        return SQLITE_OK;
    }

    return SQLITE_IOERR_WRITE;
}

int cTruncate(sqlite3_file* baseFile, sqlite3_int64 size) {
    struct cFile* file = (struct cFile*)baseFile;
    struct fs_file* fd = (struct fs_file*)file->fd;
    if( fs_truncate(fd, size) ) {
        return SQLITE_OK;
    } else {
        return SQLITE_IOERR_TRUNCATE;
    }
}

int cSync(sqlite3_file* baseFile, int flags) {
    struct cFile* file = (struct cFile*)baseFile;
    /* this is a NOP -- writes to the in-memory filesystem are atomic */
    return SQLITE_OK;
}

int cFileSize(sqlite3_file* baseFile, sqlite3_int64 *pSize) {
    struct cFile* file = (struct cFile*)baseFile;
    struct fs_file* fd = (struct fs_file*)file->fd;

    *pSize = fd->data.len;
    return SQLITE_OK;
}

/* increases the lock on a file
 * @param lockType one of SQLITE_LOCK_*
 */
int cLock(sqlite3_file* baseFile, int lockType) {
    struct cFile* file = (struct cFile*)baseFile;
    return SQLITE_OK;
}

/* decreases the lock on a file
 * @param lockType one of SQLITE_LOCK_*
 */
int cUnlock(sqlite3_file* baseFile, int lockType) {
    struct cFile* file = (struct cFile*)baseFile;
    return SQLITE_OK;
}

/* returns true if any connection has a RESERVED, PENDING, or EXCLUSIVE lock on this file
 */
int cCheckReservedLock(sqlite3_file* baseFile, int *pResOut) {
    struct cFile* file = (struct cFile*)baseFile;
    if( pResOut ) *pResOut = 0;
    return SQLITE_OK;
}

/* "VFS implementations should return [SQLITE_NOTFOUND] for file control opcodes that they do not recognize."
 */
int cFileControl(sqlite3_file* baseFile, int op, void *pArg) {
    struct cFile* file = (struct cFile*)baseFile;
    struct fs_file* fd;

    switch( op ) {
        case SQLITE_FCNTL_SIZE_HINT:
            /* "[SQLITE_FCNTL_SIZE_HINT] opcode is used by SQLite to give the VFS layer a hint
             * of how large the database file will grow to be during the current transaction...the
             * underlying VFS might choose to preallocate database file space based on this hint..."
             */
             if( file->fd ) {
                 fd = (struct fs_file*)file->fd;
                 int size_hint = *((int *)pArg);
                 fs_size_hint(fd, (sqlite3_int64)size_hint);
             }
             return SQLITE_OK;
        default:
            return SQLITE_NOTFOUND;
    }
}

/* "The xSectorSize() method returns the sector size of the device that underlies the file."
 */
int cSectorSize(sqlite3_file* baseFile) {
    return FS_SECTOR_SIZE;
}

/* "The xDeviceCharacteristics() method returns a bit vector describing behaviors of the underlying device"
 */
int cDeviceCharacteristics(sqlite3_file* baseFile) {
    int flags = 0;
    flags |= SQLITE_IOCAP_ATOMIC; /* "The SQLITE_IOCAP_ATOMIC property means that all writes of any size are atomic." */
    flags |= SQLITE_IOCAP_ATOMIC512; /* "The SQLITE_IOCAP_ATOMICnnn values mean that writes of blocks that are nnn bytes in size and are aligned to an address which is an integer multiple of nnn are atomic." */
    flags |= SQLITE_IOCAP_ATOMIC1K;
    flags |= SQLITE_IOCAP_ATOMIC2K;
    flags |= SQLITE_IOCAP_ATOMIC4K;
    flags |= SQLITE_IOCAP_ATOMIC8K;
    flags |= SQLITE_IOCAP_ATOMIC16K;
    flags |= SQLITE_IOCAP_ATOMIC32K;
    flags |= SQLITE_IOCAP_ATOMIC64K;
    flags |= SQLITE_IOCAP_SAFE_APPEND; /* "The SQLITE_IOCAP_SAFE_APPEND value means that when data is appended to a file, the data is appended first then the size of the file is extended, never the other way around." */
    flags |= SQLITE_IOCAP_SEQUENTIAL; /* The SQLITE_IOCAP_SEQUENTIAL property means that information is written to disk in the same order as calls to xWrite(). */
    return flags;
}

int cShmMap(sqlite3_file* baseFile, int iPg, int pgsz, int i, void volatile** v) {
    return SQLITE_IOERR;
}

int cShmLock(sqlite3_file* baseFile, int offset, int n, int flags) {
    return SQLITE_OK;
}

void cShmBarrier(sqlite3_file* baseFile) {
}

int cShmUnmap(sqlite3_file* baseFile, int deleteFlag) {
    return SQLITE_IOERR;
}

int cFetch(sqlite3_file* baseFile, sqlite3_int64 iOfst, int iAmt, void **pp) {
    return SQLITE_IOERR;
}

int cUnfetch(sqlite3_file* baseFile, sqlite3_int64 iOfst, void *p) {
    return SQLITE_IOERR;
}

/** sqlite3_vfs methods */
/* opens a file
 * @param vfs
 * @param zName the name of the file to open
 * @param baseFile the struct cFile to fill in
 * @param flags the set of requested OPEN flags; a set of flags from SQLITE_OPEN_*
 * @param pOutFlags the flags that were actually set
 */
int cOpen(sqlite3_vfs* vfs, const char *zName, sqlite3_file* baseFile, int flags, int *pOutFlags) {
    struct cFile* file = (struct cFile*)baseFile;
    file->composite_io_methods = 0;

    if( pOutFlags ) *pOutFlags = flags;

    /* does the file exist? */
    int fileExists = 0;
    cAccess(vfs, zName, SQLITE_ACCESS_EXISTS, &fileExists);

    if( (flags & SQLITE_OPEN_CREATE) && (flags & SQLITE_OPEN_EXCLUSIVE) ) {
        /* These two flags mean "that file should always be created, and that it is an error if it already exists."
         * They are always used together.
         */
         if( fileExists ) {
             return SQLITE_IOERR; //the file already exists -- error!
         }
    }

    void* fd = fs_open(vfs, zName);
    if( fd == 0 ) {
        return SQLITE_IOERR;
    }
    
    file->composite_io_methods = &composite_io_methods;
    file->zName = zName;
    file->fd = fd;
    if( flags & SQLITE_OPEN_DELETEONCLOSE ) {
        fs_delete(vfs, zName); /* the file will be deleted when it's reference count hits 0 */
    }

    return SQLITE_OK;
}

int cDelete(sqlite3_vfs* vfs, const char *zName, int syncDir) {
    if( fs_delete(vfs, zName) ) {
        return SQLITE_OK;
    } else {
        return SQLITE_IOERR_DELETE;
    }
}

/*
 * @param vfs
 * @param zName the name of the file or directory
 * @param flags the type of access check to perform; is SQLITE_ACCESS_EXISTS, _ACCESS_READWRITE, or _ACCESS_READ
 * @param pResOut
 */
int cAccess(sqlite3_vfs* vfs, const char *zName, int flags, int *pResOut) {
    /* all files can be accessed by everyone as long as it exists */
    *pResOut = fs_exists(vfs, zName);
    return SQLITE_OK;
}

int cFullPathname(sqlite3_vfs* vfs, const char *zName, int nOut, char *zOut) {
    const int zNameLen = strnlen(zName, MAX_PATHNAME);
    if( nOut < zNameLen ) /* these isn't enough room to copy the string */
        return SQLITE_CANTOPEN;
    
    int i;
    for( i = 0; i < zNameLen; i++ ) {
        zOut[i] = zName[i];
    }
    zOut[i] = 0;

    return SQLITE_OK;
}

/* xorshift* */
static sqlite3_uint64 get_random(sqlite3_uint64 *state) {
    const sqlite3_uint64 magic = 2685821657736338717L;
    *state ^= *state >> 12;
    *state ^= *state << 25;
    *state ^= *state >> 17;
    return (*state) * magic;
}

/* attempts to return nByte bytes of randomness.
 * @return the actual number of bytes of randomness generated
 */
int cRandomness(sqlite3_vfs* vfs, int nByte, char *zOut) {
    //TODO how to get this from composite?
    /* cRandom uses the prng defined in get_random() to get random bytes
     */
    struct composite_vfs_data *data = (struct composite_vfs_data*)vfs->pAppData;

    int i = 0;
    for( ; i + 8 < nByte; i += 8 ) {
        sqlite3_uint64 rand = get_random( &data->prng_state );
        *((sqlite3_uint64*)&zOut[i]) = rand;
    }

    for( ; i + 4 < nByte; i += 4 ) {
        int rand = (int)get_random( &data->prng_state );
        *((int*)&zOut[i]) = rand;
    }

    for( ; i < nByte; i++ ) {
        char rand = (char)get_random( &data->prng_state );
        zOut[i] = rand;
    }

    /* return the number of bytes generated */
    return nByte;
}

/* sleep for at least the given number of microseconds
 */
int cSleep(sqlite3_vfs* vfs, int microseconds) {
    //TODO
    return SQLITE_OK;
}

int cGetLastError(sqlite3_vfs* vfs, int i, char *ch) {
    return SQLITE_OK;
}

/* "returns a Julian Day Number for the current date and time as a floating point value"
 */
int cCurrentTime(sqlite3_vfs* vfs, double* time) {
    //TODO
}

/* "returns, as an integer, the Julian Day Number multiplied by 86400000 (the number of milliseconds in a 24-hour day)"
 */
int cCurrentTimeInt64(sqlite3_vfs* vfs, sqlite3_int64* time) {
    //TODO
}

void cVfsInit() {
    fs_init();
}

void cVfsDeinit() {
    fs_deinit();
}

#endif // SQLITE_OS_OTHER
/*
** 2004 May 22
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This file contains the VFS implementation for Composite.
*/
#if SQLITE_OS_OTHER

#include "os_composite.h"

/* sqlite_io function prototypes */
static int _cClose(sqlite3_file* baseFile) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cClose(file = '%s')", file->zName);
    #endif

    const int res = cClose(baseFile);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cRead(sqlite3_file* baseFile, void* buf, int iAmt, sqlite3_int64 iOfst) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cRead(file = %s, buf = <>, iAmt = %d, iOfst = %" PRIu64 ")", file->zName, iAmt, iOfst);
    #endif

    const int res = cRead(baseFile, buf, iAmt, iOfst);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cWrite(sqlite3_file* baseFile, const void* buf, int iAmt, sqlite3_int64 iOfst) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cWrite(file = %s, buf = <>, iAmt = %d, iOfst = %" PRIu64 ")", file->zName, iAmt, iOfst);
    #endif

    const int res = cWrite(baseFile, buf, iAmt, iOfst);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cTruncate(sqlite3_file* baseFile, sqlite3_int64 size) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cTruncate(file = %s, size = %" PRIu64 ")", file->zName, size);
    #endif

    const int res = cTruncate(baseFile, size);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cSync(sqlite3_file* baseFile, int flags) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(160);
        CTRACE_APPEND("cSync(file = %s, flags = [", file->zName);
        if( flags & SQLITE_SYNC_NORMAL ) CTRACE_APPEND(" SYNC_NORMAL ");
        if( flags & SQLITE_SYNC_FULL ) CTRACE_APPEND(" SYNC_FULL ");
        if( flags & SQLITE_SYNC_DATAONLY ) CTRACE_APPEND(" SYNC_DATAONLY ");
        CTRACE_APPEND("])");
    #endif

    const int res = cSync(baseFile, flags);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cFileSize(sqlite3_file* baseFile, sqlite3_int64 *pSize) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cFileSize(file = %s, pSize = <>)", file->zName);
    #endif

    const int res = cFileSize(baseFile, pSize);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_APPEND(", pSize = %" PRIu64, *pSize);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cLock(sqlite3_file* baseFile, int lockType) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cLock(file = %s, lockType = ", file->zName);
        if( lockType == SQLITE_LOCK_NONE )      CTRACE_APPEND("LOCK_NONE");
        if( lockType == SQLITE_LOCK_SHARED )    CTRACE_APPEND("LOCK_SHARED");
        if( lockType == SQLITE_LOCK_RESERVED )  CTRACE_APPEND("LOCK_RESERVED");
        if( lockType == SQLITE_LOCK_PENDING )   CTRACE_APPEND("LOCK_PENDING");
        if( lockType == SQLITE_LOCK_EXCLUSIVE ) CTRACE_APPEND("LOCK_EXCLUSIVE");
        CTRACE_APPEND(")");
    #endif

    const int res = cLock(baseFile, lockType);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cUnlock(sqlite3_file* baseFile, int lockType) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cUnlock(file = %s, lockType = ", file->zName);
        if( lockType == SQLITE_LOCK_NONE )      CTRACE_APPEND("LOCK_NONE");
        if( lockType == SQLITE_LOCK_SHARED )    CTRACE_APPEND("LOCK_SHARED");
        if( lockType == SQLITE_LOCK_RESERVED )  CTRACE_APPEND("LOCK_RESERVED");
        if( lockType == SQLITE_LOCK_PENDING )   CTRACE_APPEND("LOCK_PENDING");
        if( lockType == SQLITE_LOCK_EXCLUSIVE ) CTRACE_APPEND("LOCK_EXCLUSIVE");
        CTRACE_APPEND(")");
    #endif

    const int res = cUnlock(baseFile, lockType);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cCheckReservedLock(sqlite3_file* baseFile, int *pResOut) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cCheckReservedLock(file = %s)", file->zName);
    #endif

    const int res = cCheckReservedLock(baseFile, pResOut);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_APPEND(", pResOut = %d", pResOut ? *pResOut : -1);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cFileControl(sqlite3_file* baseFile, int op, void *pArg) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cFileControl(file = %s, op = ", file->zName);

        switch(op) {
            case SQLITE_FCNTL_SIZE_HINT: CTRACE_APPEND("SQLITE_FCNTL_SIZE_HINT"); break;
            default: CTRACE_APPEND("Unknown(%d)", op); break;
        }

        CTRACE_APPEND(", pArg = <...>");
    #endif

    const int res = cFileControl(baseFile, op, pArg);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cSectorSize(sqlite3_file* baseFile) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cSectorSize(file = %s)", file->zName);
    #endif

    const int sectorSize = cSectorSize(baseFile);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => %d", sectorSize);
        CTRACE_PRINT();
    #endif

    return sectorSize;
}

static int _cDeviceCharacteristics(sqlite3_file* baseFile) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(160);
        CTRACE_APPEND("cDeviceCharacteristics(file = %s)", file->zName);
    #endif

    const int flags = cDeviceCharacteristics(baseFile);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => flags = [...]");
        /*
        if( flags & SQLITE_IOCAP_ATOMIC ) CTRACE_APPEND(" IOCAP_ATOMIC ");
        if( flags & SQLITE_IOCAP_ATOMIC512 ) CTRACE_APPEND(" IOCAP_ATOMIC512 ");
        if( flags & SQLITE_IOCAP_ATOMIC1K ) CTRACE_APPEND(" IOCAP_ATOMIC1K ");
        if( flags & SQLITE_IOCAP_ATOMIC2K ) CTRACE_APPEND(" IOCAP_ATOMIC2K ");
        if( flags & SQLITE_IOCAP_ATOMIC4K ) CTRACE_APPEND(" IOCAP_ATOMIC4K ");
        if( flags & SQLITE_IOCAP_ATOMIC8K ) CTRACE_APPEND(" IOCAP_ATOMIC8K ");
        if( flags & SQLITE_IOCAP_ATOMIC16K ) CTRACE_APPEND(" IOCAP_ATOMIC16K ");
        if( flags & SQLITE_IOCAP_ATOMIC32K ) CTRACE_APPEND(" IOCAP_ATOMIC32K ");
        if( flags & SQLITE_IOCAP_ATOMIC64K ) CTRACE_APPEND(" IOCAP_ATOMIC64K ");
        if( flags & SQLITE_IOCAP_SAFE_APPEND ) CTRACE_APPEND(" IOCAP_SAFE_APPEND ");
        if( flags & SQLITE_IOCAP_SEQUENTIAL ) CTRACE_APPEND(" IOCAP_SEQUENTIAL ");
        CTRACE_APPEND("]");
        */
        CTRACE_PRINT();
    #endif

    return flags;
}

static int _cShmMap(sqlite3_file* baseFile, int iPg, int pgsz, int i, void volatile** v) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cShmMap(file = %s, iPg = %d, pgsz = %d, i = %d, v = <>)", file->zName, iPg, pgsz, i);
    #endif

    const int res = cShmMap(baseFile, iPg, pgsz, i, v);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cShmLock(sqlite3_file* baseFile, int offset, int n, int flags) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(160);
        CTRACE_APPEND("cShmLock(file = %s, offset = %d, n = %d, flags = [", file->zName, offset, n);

        /* these are the only valid flag combinations */
        if( flags == SQLITE_SHM_LOCK | SQLITE_SHM_SHARED ) CTRACE_APPEND(" SHM_LOCK | SHM_SHARED ");
        if( flags == SQLITE_SHM_LOCK | SQLITE_SHM_EXCLUSIVE ) CTRACE_APPEND(" SHM_LOCK | SHM_EXCLUSIVE ");
        if( flags == SQLITE_SHM_UNLOCK | SQLITE_SHM_SHARED ) CTRACE_APPEND(" SHM_UNLOCK | SHM_SHARED ");
        if( flags == SQLITE_SHM_UNLOCK | SQLITE_SHM_EXCLUSIVE ) CTRACE_APPEND(" SHM_UNLOCK | SHM_EXCLUSIVE ");

        CTRACE_APPEND("])");
    #endif

    const int res = cShmLock(baseFile, offset, n, flags);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static void _cShmBarrier(sqlite3_file* baseFile) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cShmBarrier(file = %s)", file->zName);
    #endif

    cShmBarrier(baseFile);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_PRINT();
    #endif
}

static int _cShmUnmap(sqlite3_file* baseFile, int deleteFlag) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cShmUnmap(file = %s, deleteFlag = %d)", file->zName, deleteFlag);
    #endif

    const int res = cShmUnmap(baseFile, deleteFlag);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cFetch(sqlite3_file* baseFile, sqlite3_int64 iOfst, int iAmt, void **pp) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cFetch(file = %s, iOfst = %" PRIu64 ", iAmt = %d, pp = <>)", file->zName, iOfst, iAmt);
    #endif

    const int res = cFetch(baseFile, iOfst, iAmt, pp);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cUnfetch(sqlite3_file* baseFile, sqlite3_int64 iOfst, void *p) {
    #if SQLITE_COS_PROFILE_VFS
        struct cFile* file = (struct cFile*)baseFile;
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cUnfetch(file = %s, iOfst = %" PRIu64 ", pp = <>)", file->zName, iOfst);
    #endif

    const int res = cUnfetch(baseFile, iOfst, p);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

/* sqlite_vfs function prototypes */
static int _cOpen(sqlite3_vfs* vfs, const char *zName, sqlite3_file* baseFile, int flags, int *pOutFlags) {
    #if SQLITE_COS_PROFILE_VFS
        CTRACE_STRING_DEF(320);
        CTRACE_APPEND("cOpen(vfs = <ptr>, zName = '%s', file = <not initialized>, flags = [", zName);
        if( flags & SQLITE_OPEN_MAIN_DB )        CTRACE_APPEND(" OPEN_MAIN_DB ");
        if( flags & SQLITE_OPEN_MAIN_JOURNAL )   CTRACE_APPEND(" OPEN_MAIN_JOURNAL ");
        if( flags & SQLITE_OPEN_TEMP_DB )        CTRACE_APPEND(" OPEN_TEMP_DB ");
        if( flags & SQLITE_OPEN_TEMP_JOURNAL )   CTRACE_APPEND(" OPEN_TEMP_JOURNAL ");
        if( flags & SQLITE_OPEN_TRANSIENT_DB )   CTRACE_APPEND(" OPEN_TRANSIENT_DB ");
        if( flags & SQLITE_OPEN_SUBJOURNAL )     CTRACE_APPEND(" OPEN_SUBJOURNAL ");
        if( flags & SQLITE_OPEN_MASTER_JOURNAL ) CTRACE_APPEND(" OPEN_MASTER_JOURNAL ");
        if( flags & SQLITE_OPEN_WAL )            CTRACE_APPEND(" OPEN_WAL ");
        //
        if( flags & SQLITE_OPEN_READWRITE )      CTRACE_APPEND(" OPEN_READWRITE ");
        if( flags & SQLITE_OPEN_CREATE )         CTRACE_APPEND(" OPEN_CREATE ");
        if( flags & SQLITE_OPEN_READONLY )       CTRACE_APPEND(" OPEN_READONLY ");
        //
        if( flags & SQLITE_OPEN_DELETEONCLOSE )  CTRACE_APPEND(" OPEN_DELETEONCLOSE ");
        if( flags & SQLITE_OPEN_EXCLUSIVE )      CTRACE_APPEND(" OPEN_EXCLUSIVE ");
        CTRACE_APPEND("], pOutFlags = <...>)");
    #endif

    const int res = cOpen(vfs, zName, baseFile, flags, pOutFlags);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_APPEND(", pOutFlags = [");

        if( pOutFlags ) {
            if( *pOutFlags & SQLITE_OPEN_MAIN_DB )        CTRACE_APPEND(" OPEN_MAIN_DB ");
            if( *pOutFlags & SQLITE_OPEN_MAIN_JOURNAL )   CTRACE_APPEND(" OPEN_MAIN_JOURNAL ");
            if( *pOutFlags & SQLITE_OPEN_TEMP_DB )        CTRACE_APPEND(" OPEN_TEMP_DB ");
            if( *pOutFlags & SQLITE_OPEN_TEMP_JOURNAL )   CTRACE_APPEND(" OPEN_TEMP_JOURNAL ");
            if( *pOutFlags & SQLITE_OPEN_TRANSIENT_DB )   CTRACE_APPEND(" OPEN_TRANSIENT_DB ");
            if( *pOutFlags & SQLITE_OPEN_SUBJOURNAL )     CTRACE_APPEND(" OPEN_SUBJOURNAL ");
            if( *pOutFlags & SQLITE_OPEN_MASTER_JOURNAL ) CTRACE_APPEND(" OPEN_MASTER_JOURNAL ");
            if( *pOutFlags & SQLITE_OPEN_WAL )            CTRACE_APPEND(" OPEN_WAL ");
            //
            if( *pOutFlags & SQLITE_OPEN_READWRITE )      CTRACE_APPEND(" OPEN_READWRITE ");
            if( *pOutFlags & SQLITE_OPEN_CREATE )         CTRACE_APPEND(" OPEN_CREATE ");
            if( *pOutFlags & SQLITE_OPEN_READONLY )       CTRACE_APPEND(" OPEN_READONLY ");
            //
            if( *pOutFlags & SQLITE_OPEN_DELETEONCLOSE )  CTRACE_APPEND(" OPEN_DELETEONCLOSE ");
            if( *pOutFlags & SQLITE_OPEN_EXCLUSIVE )      CTRACE_APPEND(" OPEN_EXCLUSIVE ");
        } else {
            CTRACE_APPEND(" ??? ");
        }
        CTRACE_APPEND("]");
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cDelete(sqlite3_vfs* vfs, const char *zName, int syncDir) {
    #if SQLITE_COS_PROFILE_VFS
        CTRACE_STRING_DEF(160);
        CTRACE_APPEND("cDelete(vfs = <ptr>, zName = %s, syncDir = %d)", zName, syncDir);
    #endif

    const int res = cDelete(vfs, zName, syncDir);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cAccess(sqlite3_vfs* vfs, const char *zName, int flags, int *pResOut) {
    #if SQLITE_COS_PROFILE_VFS
        CTRACE_STRING_DEF(160);
        CTRACE_APPEND("cAccess(vfs = <ptr>, zName = %s, flags = [", zName);
        if( flags == SQLITE_ACCESS_EXISTS ) CTRACE_APPEND(" ACCESS_EXISTS ");
        if( flags == SQLITE_ACCESS_READWRITE ) CTRACE_APPEND(" ACCESS_READWRITE ");
        if( flags == SQLITE_ACCESS_READ ) CTRACE_APPEND(" ACCESS_READ ");
        CTRACE_APPEND("], pResOut = <flags>)");
    #endif

    const int res = cAccess(vfs, zName, flags, pResOut);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_APPEND(", pResOut = %d", pResOut ? *pResOut : -1);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cFullPathname(sqlite3_vfs* vfs, const char *zName, int nOut, char *zOut) {
    #if SQLITE_COS_PROFILE_VFS
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cFullPathname(vfs = <ptr>, zName = %s, nOut = %d, zOut = <...>)", zName, nOut);
    #endif

    const int res = cFullPathname(vfs, zName, nOut, zOut);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_APPEND(", zOut = %s\n", zOut);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cRandomness(sqlite3_vfs* vfs, int nByte, char *zOut) {
    #if SQLITE_COS_PROFILE_VFS
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cRandomness(vfs = <ptr>, nByte = %d, zOut = <ptr>)", nByte);
    #endif

    const int bytesOfRandomness = cRandomness(vfs, nByte, zOut);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        CTRACE_APPEND("bytesOfRandom = %d, zOut = <...>\n", bytesOfRandomness);
        CTRACE_PRINT();
    #endif

    return bytesOfRandomness;
}

static int _cSleep(sqlite3_vfs* vfs, int microseconds) {
    #if SQLITE_COS_PROFILE_VFS
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cSleep(vfs = <vfs>, microseconds = %d)\n", microseconds);
    #endif

    const int res = cSleep(vfs, microseconds);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cGetLastError(sqlite3_vfs* vfs, int i, char *ch) {
    #if SQLITE_COS_PROFILE_VFS
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cGetLastError(vfs = <vfs>, i = %d, ch = %s)", i, ch);
    #endif

    const int res = cGetLastError(vfs, i, ch);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cCurrentTime(sqlite3_vfs* vfs, double* time) {
    #if SQLITE_COS_PROFILE_VFS
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cCurrentTime(vfs = <vfs>, time = <...>)");
    #endif

    const int res = cCurrentTime(vfs, time);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_APPEND(", time = %f", *time);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cCurrentTimeInt64(sqlite3_vfs* vfs, sqlite3_int64* time) {
    return cCurrentTimeInt64(vfs, time);
    #if SQLITE_COS_PROFILE_VFS
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cCurrentTimeInt64(vfs = <vfs>, time = <...>)");
    #endif

    const int res = cCurrentTimeInt64(vfs, time);

    #if SQLITE_COS_PROFILE_VFS
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_APPEND(", time = %" PRIu64 "\n", *time);
        CTRACE_PRINT();
    #endif

    return res;
}

/* sqlite_mutex function prototypes */
#if SQLITE_THREADSAFE
static int _cMutexInit(void) {
    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cMutexInit()");
    #endif

    const int res = cMutexInit();

    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cMutexEnd(void) {
    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cMutexEnd()");
    #endif

    const int res = cMutexEnd();

    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static sqlite3_mutex* _cMutexAlloc(int mutexType) {
    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_STRING_DEF(160);
        CTRACE_APPEND("cMutexAlloc(mutexType = ");
        switch( mutexType ) {
            case SQLITE_MUTEX_FAST: CTRACE_APPEND(" MUTEX_FAST "); break;
            case SQLITE_MUTEX_RECURSIVE: CTRACE_APPEND(" MUTEX_RECURSIVE "); break;
            case SQLITE_MUTEX_STATIC_MASTER: CTRACE_APPEND(" MUTEX_STATIC_MASTER "); break;
            case SQLITE_MUTEX_STATIC_MEM: CTRACE_APPEND(" MUTEX_STATIC_MEM "); break;
            case SQLITE_MUTEX_STATIC_OPEN: CTRACE_APPEND(" MUTEX_STATIC_OPEN "); break;
            case SQLITE_MUTEX_STATIC_PRNG: CTRACE_APPEND(" MUTEX_STATIC_PRNG "); break;
            case SQLITE_MUTEX_STATIC_LRU: CTRACE_APPEND(" MUTEX_STATIC_LRU "); break;
            case SQLITE_MUTEX_STATIC_PMEM: CTRACE_APPEND(" MUTEX_STATIC_PMEM "); break;
            case SQLITE_MUTEX_STATIC_APP1: CTRACE_APPEND(" MUTEX_STATIC_APP1 "); break;
            case SQLITE_MUTEX_STATIC_APP2: CTRACE_APPEND(" MUTEX_STATIC_APP2 "); break;
            case SQLITE_MUTEX_STATIC_APP3: CTRACE_APPEND(" MUTEX_STATIC_APP3 "); break;
            case SQLITE_MUTEX_STATIC_VFS1: CTRACE_APPEND(" MUTEX_STATIC_VFS1 "); break;
            case SQLITE_MUTEX_STATIC_VFS2: CTRACE_APPEND(" MUTEX_STATIC_VFS2 "); break;
            case SQLITE_MUTEX_STATIC_VFS3: CTRACE_APPEND(" MUTEX_STATIC_VFS3 "); break; 
        }
        CTRACE_APPEND(")");
    #endif

    sqlite3_mutex* mut = cMutexAlloc(mutexType);

    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_APPEND(" => mutex = %p", mutex);
        CTRACE_PRINT();
    #endif

    return mut;
}

static void _cMutexFree(sqlite3_mutex *mutex) {
    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cMutexFree(mutex = %p)", mutex);
    #endif

    cMutexFree(mutex);

    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_PRINT();
    #endif
}

static void _cMutexEnter(sqlite3_mutex *mutex) {
    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cMutexEnter(mutex = %p)", mutex);
    #endif

    cMutexEnter(mutex);

    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_PRINT();
    #endif
}

static int _cMutexTry(sqlite3_mutex *mutex) {
    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cMutexTry(mutex)");
    #endif

    const int res = cMutexTry(mutex);

    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static void _cMutexLeave(sqlite3_mutex *mutex) {
    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cMutexLeave(mutex = %p)", mutex);
    #endif

    cMutexLeave(mutex);

    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_PRINT();
    #endif
}

static int _cMutexHeld(sqlite3_mutex *mutex) {
    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cMutexHeld(mutex = %p)", mutex);
    #endif

    const int res = cMutexHeld(mutex);

    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_APPEND(" => %s", res ? "TRUE" : "FALSE");
        CTRACE_PRINT();
    #endif

    return res;
}

static int _cMutexNotheld(sqlite3_mutex *mutex) {
    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cMutexNotheld(mutex = %p)", mutex);
    #endif

    const int res = cMutexNotheld(mutex);

    #if SQLITE_COS_PROFILE_MUTEX
        CTRACE_APPEND(" => %s", res ? "TRUE" : "FALSE");
        CTRACE_PRINT();
    #endif

    return res;
}
#endif

/* sqlite_mem function prototypes */
static void* _cMemMalloc(int sz) {
    #if SQLITE_COS_PROFILE_MEMORY
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cMemMalloc(sz = %d)", sz);
    #endif

    void* mem = cMemMalloc(sz);

    #if SQLITE_COS_PROFILE_MEMORY
        CTRACE_APPEND(" => mem = %p", mem);
        CTRACE_PRINT();
    #endif

    return mem;
}

static void _cMemFree(void* mem) {
    #if SQLITE_COS_PROFILE_MEMORY
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cMemFree(mem = %p)", mem);
    #endif

    cMemFree(mem);

    #if SQLITE_COS_PROFILE_MEMORY
        CTRACE_PRINT();
    #endif
}

static void* _cMemRealloc(void* mem, int newSize) {
    #if SQLITE_COS_PROFILE_MEMORY
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cMemRealloc(mem = %p, newSize = %d)", mem, newSize);
    #endif

    void* newPtr = cMemRealloc(mem, newSize);

    #if SQLITE_COS_PROFILE_MEMORY
        CTRACE_PRINT(" => newPtr = %p\n", newPtr);
        CTRACE_PRINT();
    #endif

    return newPtr;
}

static int _cMemSize(void* mem) {
    #if SQLITE_COS_PROFILE_MEMORY
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cMemSize(mem = %p)", mem);
    #endif

    const int sz = cMemSize(mem);

    #if SQLITE_COS_PROFILE_MEMORY
        CTRACE_APPEND("=> sz = %d\n", sz);
        CTRACE_PRINT();
    #endif

    return sz;
}

static int _cMemRoundup(int sz) {
    #if SQLITE_COS_PROFILE_MEMORY
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cMemRoundup(sz = %d)", sz);
    #endif

    const int newSz = cMemRoundup(sz);

    #if SQLITE_COS_PROFILE_MEMORY
        CTRACE_APPEND("newSz = %d", newSz);
        CTRACE_PRINT();
    #endif

    return sz;
}

static int _cMemInit(void* pAppData) {
    #if SQLITE_COS_PROFILE_MEMORY
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cMemInit(pAppData = <>)");
    #endif

    const int res = cMemInit(pAppData);

    #if SQLITE_COS_PROFILE_MEMORY
        CTRACE_APPEND(" => ");
        APPEND_ERR_CODE(res);
        CTRACE_PRINT();
    #endif

    return res;
}

static void _cMemShutdown(void* pAppData) {
    #if SQLITE_COS_PROFILE_MEMORY
        CTRACE_STRING_DEF(80);
        CTRACE_APPEND("cMemShutdown(pAppData = <>)");
    #endif

    cMemShutdown(pAppData);

    #if SQLITE_COS_PROFILE_MEMORY
        CTRACE_PRINT();
    #endif
}

/* API structs */
struct sqlite3_io_methods composite_io_methods = {
    .iVersion = 1,
    .xClose = _cClose,
    .xRead = _cRead,
    .xWrite = _cWrite,
    .xTruncate = _cTruncate,
    .xSync = _cSync,
    .xFileSize = _cFileSize,
    .xLock = _cLock,
    .xUnlock = _cUnlock,
    .xCheckReservedLock = _cCheckReservedLock,
    .xFileControl = _cFileControl,
    .xSectorSize = _cSectorSize,
    .xDeviceCharacteristics = _cDeviceCharacteristics,
    /* everything above is required for version 1 */
    .xShmMap = _cShmMap,
    .xShmLock = _cShmLock,
    .xShmBarrier = _cShmBarrier,
    .xShmUnmap = _cShmUnmap,
    /* everything above is required for version 1-2 */
    .xFetch = _cFetch,
    .xUnfetch = _cUnfetch
};

struct composite_vfs_data composite_vfs_app_data;

static sqlite3_vfs composite_vfs = {
    .iVersion = 2,
    .szOsFile = sizeof(struct cFile),
    .mxPathname = MAX_PATHNAME,
    .pNext = 0,
    .zName = "composite-inmemfs",
    .pAppData = &composite_vfs_app_data,
    .xOpen = _cOpen,
    .xDelete = _cDelete,
    .xAccess = _cAccess,
    .xFullPathname = _cFullPathname,
    .xDlOpen = 0,
    .xDlError = 0,
    .xDlSym = 0,
    .xDlClose = 0,
    .xRandomness = _cRandomness,
    .xSleep = _cSleep,
    .xCurrentTime = _cCurrentTime,
    .xGetLastError = _cGetLastError,
    /* everything above is required in version 1 */
    .xCurrentTimeInt64 = _cCurrentTimeInt64,
    /* everything above is required in versions 1-2 */
    .xSetSystemCall = 0,
    .xGetSystemCall = 0,
    .xNextSystemCall = 0
};

#if SQLITE_THREADSAFE
static const sqlite3_mutex_methods composite_mutex_methods = {
    .xMutexInit = _cMutexInit,
    .xMutexEnd = _cMutexEnd,
    .xMutexAlloc = _cMutexAlloc,
    .xMutexFree = _cMutexFree,
    .xMutexEnter = _cMutexEnter,
    .xMutexTry = _cMutexTry,
    .xMutexLeave = _cMutexLeave,
    .xMutexHeld = _cMutexHeld,
    .xMutexNotheld = _cMutexNotheld
};
#endif

const sqlite3_mem_methods composite_mem_methods = {
    .xMalloc = _cMemMalloc,
    .xFree = _cMemFree,
    .xRealloc = _cMemRealloc,
    .xSize = _cMemSize,
    .xRoundup = _cMemRoundup,
    .xInit = _cMemInit,
    .xShutdown = _cMemShutdown,
    .pAppData = 0
};

/* init the OS interface */
int sqlite3_os_init(void){
  #if SQLITE_THREADSAFE
    sqlite3_config(SQLITE_CONFIG_MUTEX, &composite_mutex_methods);
  #endif
  sqlite3_config(SQLITE_CONFIG_MALLOC, &composite_mem_methods);

  struct composite_vfs_data *data = &composite_vfs_app_data;
  data->prng_state = 4; /* seed the PRNG with a completely random value */
  
  cVfsInit();
  sqlite3_vfs_register(&composite_vfs, 1);

  return SQLITE_OK;
}

/* shutdown the OS interface */
int sqlite3_os_end(void) {
  cVfsDeinit();
  return SQLITE_OK; 
}

#endif
