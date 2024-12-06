/*
 * A type which wraps a semaphore
 *
 * semaphore.c
 *
 * Copyright (c) 2006-2008, R Oudkerk
 * Licensed to PSF under a Contributor Agreement.
 */

#include "multiprocessing.h"

#ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>           // gettimeofday()
#endif

#ifdef HAVE_MP_SEMAPHORE

// These match the values in Lib/multiprocessing/synchronize.py
enum { RECURSIVE_MUTEX, SEMAPHORE };


/*[python input]
class SEM_HANDLE_converter(CConverter):
    type = "SEM_HANDLE"
    format_unit = '"F_SEM_HANDLE"'

[python start generated code]*/
/*[python end generated code: output=da39a3ee5e6b4b0d input=3e0ad43e482d8716]*/

/*[clinic input]
module _multiprocessing
class _multiprocessing.SemLock "SemLockObject *" "&_PyMp_SemLockType"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=935fb41b7d032599]*/

#ifdef MS_WINDOWS

typedef struct {
    PyObject_HEAD
    SEM_HANDLE handle;
    unsigned long last_tid;
    int count;
    int maxvalue;
    int kind;
    char *name;
} SemLockObject;

#include "clinic/semaphore.c.h"

#define ISMINE(o) (o->count > 0 && PyThread_get_thread_ident() == o->last_tid)

/*
 * Windows definitions
 */

#define SEM_FAILED NULL

#define SEM_CLEAR_ERROR() SetLastError(0)
#define SEM_GET_LAST_ERROR() GetLastError()
#define SEM_CREATE(name, val, max) CreateSemaphore(NULL, val, max, NULL)
#define SEM_CLOSE(sem) (CloseHandle(sem) ? 0 : -1)
#define SEM_GETVALUE(sem, pval) _GetSemaphoreValue(sem, pval)
#define SEM_UNLINK(name) 0

static int
_GetSemaphoreValue(HANDLE handle, long *value)
{
    long previous;

    switch (WaitForSingleObjectEx(handle, 0, FALSE)) {
    case WAIT_OBJECT_0:
        if (!ReleaseSemaphore(handle, 1, &previous))
            return MP_STANDARD_ERROR;
        *value = previous + 1;
        return 0;
    case WAIT_TIMEOUT:
        *value = 0;
        return 0;
    default:
        return MP_STANDARD_ERROR;
    }
}

/*[clinic input]
@critical_section
_multiprocessing.SemLock.acquire

    block as blocking: bool = True
    timeout as timeout_obj: object = None

Acquire the semaphore/lock.
[clinic start generated code]*/

static PyObject *
_multiprocessing_SemLock_acquire_impl(SemLockObject *self, int blocking,
                                      PyObject *timeout_obj)
/*[clinic end generated code: output=f9998f0b6b0b0872 input=079ca779975f3ad6]*/
{
    double timeout;
    DWORD res, full_msecs, nhandles;
    HANDLE handles[2], sigint_event;

    /* calculate timeout */
    if (!blocking) {
        full_msecs = 0;
    } else if (timeout_obj == Py_None) {
        full_msecs = INFINITE;
    } else {
        timeout = PyFloat_AsDouble(timeout_obj);
        if (PyErr_Occurred())
            return NULL;
        timeout *= 1000.0;      /* convert to millisecs */
        if (timeout < 0.0) {
            timeout = 0.0;
        } else if (timeout >= 0.5 * INFINITE) { /* 25 days */
            PyErr_SetString(PyExc_OverflowError,
                            "timeout is too large");
            return NULL;
        }
        full_msecs = (DWORD)(timeout + 0.5);
    }

    /* check whether we already own the lock */
    if (self->kind == RECURSIVE_MUTEX && ISMINE(self)) {
        ++self->count;
        Py_RETURN_TRUE;
    }

    /* check whether we can acquire without releasing the GIL and blocking */
    if (WaitForSingleObjectEx(self->handle, 0, FALSE) == WAIT_OBJECT_0) {
        self->last_tid = GetCurrentThreadId();
        ++self->count;
        Py_RETURN_TRUE;
    }

    /* prepare list of handles */
    nhandles = 0;
    handles[nhandles++] = self->handle;
    if (_PyOS_IsMainThread()) {
        sigint_event = _PyOS_SigintEvent();
        assert(sigint_event != NULL);
        handles[nhandles++] = sigint_event;
    }
    else {
        sigint_event = NULL;
    }

    /* do the wait */
    Py_BEGIN_ALLOW_THREADS
    if (sigint_event != NULL)
        ResetEvent(sigint_event);
    res = WaitForMultipleObjectsEx(nhandles, handles, FALSE, full_msecs, FALSE);
    Py_END_ALLOW_THREADS

    /* handle result */
    switch (res) {
    case WAIT_TIMEOUT:
        Py_RETURN_FALSE;
    case WAIT_OBJECT_0 + 0:
        self->last_tid = GetCurrentThreadId();
        ++self->count;
        Py_RETURN_TRUE;
    case WAIT_OBJECT_0 + 1:
        errno = EINTR;
        return PyErr_SetFromErrno(PyExc_OSError);
    case WAIT_FAILED:
        return PyErr_SetFromWindowsErr(0);
    default:
        PyErr_Format(PyExc_RuntimeError, "WaitForSingleObject() or "
                     "WaitForMultipleObjects() gave unrecognized "
                     "value %u", res);
        return NULL;
    }
}

/*[clinic input]
@critical_section
_multiprocessing.SemLock.release

Release the semaphore/lock.
[clinic start generated code]*/

static PyObject *
_multiprocessing_SemLock_release_impl(SemLockObject *self)
/*[clinic end generated code: output=b22f53ba96b0d1db input=9bd62d3645e7a531]*/
{
    if (self->kind == RECURSIVE_MUTEX) {
        if (!ISMINE(self)) {
            PyErr_SetString(PyExc_AssertionError, "attempt to "
                            "release recursive lock not owned "
                            "by thread");
            return NULL;
        }
        if (self->count > 1) {
            --self->count;
            Py_RETURN_NONE;
        }
        assert(self->count == 1);
    }

    if (!ReleaseSemaphore(self->handle, 1, NULL)) {
        if (GetLastError() == ERROR_TOO_MANY_POSTS) {
            PyErr_SetString(PyExc_ValueError, "semaphore or lock "
                            "released too many times");
            return NULL;
        } else {
            return PyErr_SetFromWindowsErr(0);
        }
    }

    --self->count;
    Py_RETURN_NONE;
}

#else /* !MS_WINDOWS */

#ifdef HAVE_BROKEN_SEM_GETVALUE

/*--- workaround shared memory about replacement of `sem_getvalue` function

                   ----- Extandable array of MAX_COUNTERS Counters -----
                 /                                                       \
+----------------+---------------+------/  /-----+------------+------------+
|  Header        |    Counter1   |               | CounterN-1 |  CounterN  |
|----------------|---------------|    ....       |------------|------------|
|  nb_semlocks   | handle        |               |            |            |
|  max_semlocks  | handle_lock   |               |            |            |
|                | current_value |               |            |            |
|                | ref_handle    |               |            |            |
+----------------+---------------+------/  /-----+------------+------------+

---*/

    #define _STR(x) #x
    #define STR(x) _STR(x)
    #pragma message("STRUCT: Line: "STR(__LINE__)" -> HAVE_BROKEN_SEM_GETVALUE = "STR(HAVE_BROKEN_SEM_GETVALUE))

    // ------------- list of structs --------------
    typedef struct {
        int nb_semlocks ; // current number of semaphores. Starts 0.
        int max_semlocks ; // max size of counter array.
    } HeaderObject ;

    typedef struct _co {
        SEM_HANDLE handle ; // Current handle semphore.
        SEM_HANDLE handle_lock ; // Lock to update current_value of semaphore.
        int current_value ; // Update on each acquire/release. Start semaphore value > 1.
        char sem_name[24] ; // name copy of semaphore.
        SEM_HANDLE ref_handle ; // Reference to the primary ref handle.
    } CounterObject ;

    /*-----------
    mmap allocate at least a 4096 bytes block.
    So calculate size of counters array. First 'item' is
    used as header
    -------------*/
    #define MAX_COUNTERS  ((4096/sizeof(CounterObject))-1)

    typedef struct {
        HeaderObject header ;
        CounterObject array_counters[MAX_COUNTERS] ;
    } SharedCounters ;

    typedef int MEMORY_HANDLE;

    typedef struct {
        char *name_shm;
        MEMORY_HANDLE handle_shm; // Memory handle
        size_t size_shm ;
        char *name_gmlock;
        SEM_HANDLE handle_gmlock ; // Global memeory lock to handle shared memory.
        int valid ;
        int _errno ;
        char *origin_errno ;
        SharedCounters *counters ; // data from mmap, shares by all processes.
    }  CountersWorkaround ;

#endif

typedef struct {
    PyObject_HEAD
    SEM_HANDLE handle;
    unsigned long last_tid;
    int count;
    int maxvalue;
    int kind;
    char *name;
#ifdef HAVE_BROKEN_SEM_GETVALUE
    #pragma message("STRUCT SEMLOCK + counter")
    CounterObject *counter ;
#endif
} SemLockObject;

#ifdef HAVE_BROKEN_SEM_GETVALUE
#pragma message("ALL STRUCTS + FUNC + DEFINE")

#define ISMINE(o) (o->count > 0 && PyThread_get_thread_ident() == o->last_tid)
#define ISSEMAPHORE(o) ((o)->maxvalue > 1)

/*------
#define ENTER(output, msg) fprintf(output, "PID:%05d: '%s' l:%d\n", getpid(), msg, __LINE__)
#define ENTER_STR(output, msg, str) fprintf(output, "PID:%05d: '%s' -> '%s' l:%d\n", getpid(), msg, str, __LINE__)
#define SUB_ENTER(output, msg) fprintf(output, "\tPID:%05d: '%s' l:%d\n", getpid(), msg, __LINE__)
#define SUB_ENTER_STR(output, msg, str) fprintf(output, "\tPID:%05d: '%s' -> '%s' l:%d\n", getpid(), msg, str, __LINE__)
-------*/

#define ENTER(output, msg) 0
#define ENTER_STR(output, msg, str) 0
#define SUB_ENTER(output, msg)  0
#define SUB_ENTER_STR(output, msg, str) 0 ;

#include <sys/mman.h>   // shm_open, shm_unlink

#define ACQUIRE_GENERAL_LOCK    (/*printf("\t}}}} GMLOCK ACQ }}}}:%d\n", __LINE__) &&*/ sem_wait(shm_semlock_counters.handle_gmlock) >= 0)
#define RELEASE_GENERAL_LOCK    (/*printf("\t{{{{ GMLOCK REL {{{{:%d\n", __LINE__) &&*/ sem_post(shm_semlock_counters.handle_gmlock) >= 0)

#define ACQUIRE_COUNTER_LOCK(c) (printf("\t\t\t\t>>>> MUTEX ACQ(%d) >>>>:%d\n", (c)->handle_lock, __LINE__) && sem_wait((c)->handle_lock) >= 0)
#define RELEASE_COUNTER_LOCK(c) (printf("\t\t\t\t<<<< MUTEX REL(%d) <<<<:%d\n", (c)->handle_lock, __LINE__) && sem_post((c)->handle_lock) >= 0)

// global data for each process.
CountersWorkaround shm_semlock_counters = {
    .name_shm = "/shm_gh125828",
    .handle_shm = (MEMORY_HANDLE)0,
    .size_shm = (size_t)(sizeof(HeaderObject) + (sizeof(CounterObject)*MAX_COUNTERS)),
    .name_gmlock = "/mp_gh125828",
    .handle_gmlock = (SEM_HANDLE)0,
    .valid = 0,
    ._errno = 0,
    .counters = (SharedCounters *)NULL,
} ;

/*-----------
static char chars[] = "abcdefghijklmnopqrstuvwxyz_9876543210" ;
static int l = sizeof(chars)-1 ; // exclude '\0'
char *make_name(char *buf, const char *prefix, int n) {
    char *ref = buf;
    int i = 0 ;

    if (prefix) {
        strcpy(buf, prefix) ;
        buf += strlen(buf) ;
    }
    while (i++ < n && (*buf++ = chars[rand()%l])) ;
    *buf = 0 ;

    return ref ;
}
--------*/

#define SIZE    9
static char gh_name[SIZE+1] = "_gh125828" ;
static int base_len = SIZE ;

static char *_make_sem_name2(char *buf, const char *name) {
    int base_len = 3 ;
    char *pbuf= buf + base_len ;
    strncpy(buf, name, base_len) ; // The 3 first chars
    name += base_len ;
    for(int i = 0 ; i < SIZE ; i++){ // Mix chars of each name
        *pbuf++ = *name++ ;
        *pbuf++ = gh_name[i] ;
    }
    *pbuf = 0 ;
    return buf ;
}

static char *_make_sem_name(char *buf, const char *name) {
    strcpy(buf, name) ;
    strcat(buf, gh_name) ;

    return buf ;
}

static SEM_HANDLE _create_lock_extend_name(char *name) {
    ENTER(stdout, __func__) ;

    int flags = O_CREAT | O_EXCL ;
    SEM_HANDLE sem = SEM_FAILED ;
    char buf[256], buf1[96] ; // size name is defined in python SemLock class as '/sm-<eight chars>'
    _make_sem_name(buf1, name) ;
    SUB_ENTER_STR(stdout, __func__, buf1) ;

    sem = sem_open(buf1, flags, 0600, 1) ;
    return sem ;
}

static SEM_HANDLE _connect_lock_extend_name(char *name) {
    ENTER(stdout, __func__) ;

    SEM_HANDLE sem = SEM_FAILED ;
    char buf[256], buf1[96] ; // size name is defined in python SemLock class as '/sm-<eight chars>'
    _make_sem_name(buf1, name) ;
    SUB_ENTER_STR(stdout, __func__, buf1) ;

    sem = sem_open(buf1, 0) ;
    return sem ;
}

char *str_counter(char *p, CounterObject *counter){
    sprintf(p, "(%s, H:%d, L:%d, R:%d)", counter->sem_name,
                                                    counter->handle,
                                                    counter->handle_lock,
                                                    counter->ref_handle) ;
    return p ;
}

void dump_shm_semlock_counters(void) {
    char buf[128] ;
    if (shm_semlock_counters.valid) {
        printf("shm: %x\n", shm_semlock_counters.handle_shm) ;
        printf("sem: %x\n", shm_semlock_counters.handle_gmlock) ;
        printf("max_semloks: %d\n", shm_semlock_counters.counters->header.max_semlocks) ;
        printf("nb_semloks: %d\n", shm_semlock_counters.counters->header.nb_semlocks) ;
        printf("counters: %p\n", shm_semlock_counters.counters->array_counters) ;
        CounterObject *counter = shm_semlock_counters.counters->array_counters ;
        for( int i = 0, j = 0 ; i < shm_semlock_counters.counters->header.max_semlocks 
                                && j < shm_semlock_counters.counters->header.nb_semlocks ; i++ ) {
            if (counter->handle_lock != (SEM_HANDLE)0) {
                puts(str_counter(buf, counter)) ;
                ++j ;
                ++counter ;
            }
        }
    }
}

void create_shm_semlock_counters(void) {
    ENTER(stdout, __func__) ;
    int oflag = O_RDWR ;
    int shm = -1 ;
    int res = -1 ;
    int mode = 0x600 ;
    char *action ;
    SEM_HANDLE sem = SEM_FAILED ;
    char buf[128] ;
    int pid = getpid(), ppid = getppid() ;

    // already done
    if (shm_semlock_counters.valid) {
        return ;
    }

    //printf("PID:%d: create_shm_semlock_counters from %d\n", pid, ppid) ;

    // create a locked lock to handle shared memmory
    sem = sem_open(shm_semlock_counters.name_gmlock, O_CREAT | O_EXCL, 0600, 1) ;
    if (errno == EEXIST && sem == SEM_FAILED) {
        shm_semlock_counters._errno = errno ;
        // semaphore exists, just connects
        sem = sem_open(shm_semlock_counters.name_gmlock, 0) ; // , 0600, 1) ;
        action = "SEM_CONNECT" ;
    } else {
        action = "SEM_CREATE" ;
    }
    shm_semlock_counters._errno = 0 ;
    shm_semlock_counters.handle_gmlock = sem ;
    sprintf(buf, "%s, GMLOCK %d", action, sem) ;
    SUB_ENTER_STR(stdout, __func__, buf) ;

    // first time
    // Locks global lock in order to be alone to
    // create shared memory
    if (sem != SEM_FAILED && ACQUIRE_GENERAL_LOCK) {
        if (shm_semlock_counters.handle_shm == (MEMORY_HANDLE)0) {
            shm = shm_open(shm_semlock_counters.name_shm, oflag, 0) ; // S_IRUSR | S_IWUSR) ;
            res = 0 ;
            if (shm == -1) {
                oflag |= O_CREAT ;
                shm = shm_open(shm_semlock_counters.name_shm, oflag, S_IRUSR | S_IWUSR) ;
                // set size.
                res = ftruncate(shm, shm_semlock_counters.size_shm) ;
            }
            // mmap
            if (res >= 0) {
                shm_semlock_counters.handle_shm = shm ;
                shm_semlock_counters.counters = (SharedCounters *)mmap(NULL,
                                                          shm_semlock_counters.size_shm,
                                                          (PROT_WRITE | PROT_READ),
                                                          MAP_SHARED,
                                                          shm_semlock_counters.handle_shm,
                                                          0L);
                shm_semlock_counters._errno = errno ;
                // when just created, init values
                if (shm_semlock_counters.counters != MAP_FAILED && oflag & O_CREAT) {
                    shm_semlock_counters.counters->header.max_semlocks = MAX_COUNTERS ;
                    shm_semlock_counters.counters->header.nb_semlocks = 0 ;
                }
                // set initialization is successful.
                shm_semlock_counters.valid = 1 ;
                shm_semlock_counters._errno = 0 ;
                int pid = getpid() ;
                printf("PID:%d: nb_semloks: %d\n", pid, shm_semlock_counters.counters->header.nb_semlocks) ;
                CounterObject *counter = shm_semlock_counters.counters->array_counters ;
                for( int i = 0, j = 0 ; i < shm_semlock_counters.counters->header.max_semlocks
                                        && j < shm_semlock_counters.counters->header.nb_semlocks ; i++ ) {
                    if (counter->handle_lock != (SEM_HANDLE)0) {
                        printf("PID:%d: sem: %s\n", pid, str_counter(buf, counter)) ;
                        ++j ;
                        ++counter ;
                    }
                }
            }
        }
        RELEASE_GENERAL_LOCK ;
    }
}

void check_shm_semlock_counters(void) {
    SUB_ENTER(stdout, __func__) ;

    if(!shm_semlock_counters.valid) {
        create_shm_semlock_counters() ;
    }
}

void _extend_shm_semlock_counters(void) {
    SUB_ENTER(stdout, __func__) ;
    int pid = getpid() ;

    if (!shm_semlock_counters.valid) {
        return ;
    }
    printf("PID:%d: extend shm_semlock_counters\n", pid) ;

    // global lock must be LOCKED
    int nb_new_counters = (int)(MAX_COUNTERS+1) ;
    long size = sizeof(CounterObject)*nb_new_counters ;
    CounterObject *new = NULL ;

    new = (CounterObject *)mmap(shm_semlock_counters.counters,
                                shm_semlock_counters.size_shm + size,
                                (PROT_WRITE | PROT_READ),
                                MAP_SHARED | MAP_FIXED,
                                shm_semlock_counters.handle_shm,
                                0) ; // to complete
    shm_semlock_counters._errno = errno ;
    if (new != NULL && new != (CounterObject *)-1) {
        shm_semlock_counters.counters->header.max_semlocks += nb_new_counters;
        shm_semlock_counters.size_shm += size ;
        shm_semlock_counters._errno = 0 ;
    }
}

void delete_shm_semlock_counters(void) { // SemLockObject *self) {
    ENTER(stdout, __func__) ;
    int pid = getpid(), ppid = getppid() ;

    // enumerate all opened semaphores from array_counters
    if (shm_semlock_counters.valid) {
        printf("PID:%d: delete shm_semlock_counters from %d\n", pid, ppid) ;
        if (shm_semlock_counters.counters) {
            // close lock and unlink
            sem_close(shm_semlock_counters.handle_gmlock) ;
            shm_semlock_counters._errno = errno ;
            sem_unlink(shm_semlock_counters.name_gmlock) ;
            shm_semlock_counters._errno = errno ;

            // unmmap
            munmap(shm_semlock_counters.counters, shm_semlock_counters.size_shm) ;
            shm_semlock_counters._errno = errno ;
        }
        // close shared mem
        if (shm_semlock_counters.name_shm) {
            shm_unlink(shm_semlock_counters.name_shm) ;
            shm_semlock_counters._errno = errno ;
        }
    }
}

static CounterObject* _search_counter_from_handle(SEM_HANDLE handle) {
    char buf[256] ;
    sprintf(buf, "Search: %d", handle) ;
    ENTER_STR(stdout, __func__, buf) ;

    int i = 0, j = 0 ;
    HeaderObject *header = &shm_semlock_counters.counters->header ;
    CounterObject *counter = shm_semlock_counters.counters->array_counters ;

     // test global lock
    while(i < header->max_semlocks && j < header->nb_semlocks) {
        SUB_ENTER_STR(stdout, __func__, str_counter(buf, counter)) ;
        if(counter->handle == handle) {
            SUB_ENTER_STR(stdout, __func__, "find....") ;
            return counter ;
        }
        ++i ;
        ++counter ;
    }
    return (CounterObject *)NULL ;
}

static CounterObject* search_counter_from_handle(SEM_HANDLE handle) {
    CounterObject *counter = NULL ;

    // lock global lock
    if (ACQUIRE_GENERAL_LOCK) {
        // find an available slot
        counter = _search_counter_from_handle(handle) ;

        // release global lock
        RELEASE_GENERAL_LOCK ;
    }
    return counter ;
}

static CounterObject* _search_counter_free_slot(void) {
    ENTER(stdout, __func__) ;

    char buf[256] ;
    int i = 0 ;
    HeaderObject *header = &shm_semlock_counters.counters->header ;
    CounterObject *counter = shm_semlock_counters.counters->array_counters ;

     // test global lock
    while (i < header->max_semlocks ) {
        SUB_ENTER_STR(stdout, __func__, str_counter(buf, counter)) ;
        if(counter->handle == (SEM_HANDLE)0) {
            SUB_ENTER_STR(stdout, __func__, "find....") ;
            return counter ;
        }
        ++i ;
        ++counter ;
    }
    return (CounterObject *)NULL ;
}

static CounterObject *_new_counter(SemLockObject *self, SEM_HANDLE ref_handle, int value) {
    char buf[256], buf2[128] ;
    ENTER(stdout, __func__) ;
    CounterObject *counter = NULL, *ref_counter = 1 ;
    HeaderObject *header =  &shm_semlock_counters.counters->header ;

    // find an available slot
    counter = _search_counter_free_slot() ;
    if (counter == NULL) {
        _extend_shm_semlock_counters() ;
        counter = _search_counter_free_slot() ;
    }
    if (counter){
        sprintf(buf,"%s %d", str_counter(buf2, counter), value) ;
        SUB_ENTER_STR(stdout, __func__, buf) ;

        // update self counter
        self->counter = counter ;

        // copies from semlock
        counter->handle = self->handle ;
        strcpy(counter->sem_name, self->name) ;
        counter->ref_handle = ref_handle ;

        // create a lock
        if(ref_handle == (SEM_HANDLE)0) {
            counter->handle_lock = _create_lock_extend_name(self->name) ;
            sprintf(buf, "Create Handle lock: %d, ref_handle:%d", counter->handle_lock, counter->ref_handle) ;
        } else {
            counter->handle_lock = _connect_lock_extend_name(self->name) ;
            sprintf(buf, "Connect Handle lock: %d, ref_handle:%d", counter->handle_lock, counter->ref_handle) ;
        }
        SUB_ENTER_STR(stdout, __func__, buf) ;
        if(ref_handle != (SEM_HANDLE)0) {
            ref_counter = _search_counter_from_handle(counter->ref_handle) ;
            if (ref_counter) {
                ref_handle = ref_counter->handle ;
            }
        }
        // save value
        counter->current_value = value ;

        // update header
        ++header->nb_semlocks ;
    } else {
            printf("LINE %d", __LINE__) ;
    }
    SUB_ENTER_STR(stdout, __func__, "OUT") ;
    return counter ;
}

CounterObject *connect_counter(SemLockObject *self, SEM_HANDLE ref_handle) {
    // On MacOSX, two open_sem calls to the same named semaphore return
    // two different handles. So we have to store them in shared mem.
    ENTER(stdout, __func__) ;

    CounterObject *counter = NULL ;
    char buf[256], buf2[128] ;

    if (!shm_semlock_counters.valid) {
        create_shm_semlock_counters() ;
    }
    if (ACQUIRE_GENERAL_LOCK) {
        if (self->handle != ref_handle) {
            counter = _new_counter(self, ref_handle, -1) ;
            sprintf(buf,"%s %d", str_counter(buf2, counter), ref_handle) ;
            SUB_ENTER_STR(stdout, __func__, buf) ;
        }
        RELEASE_GENERAL_LOCK ;
    }
    return counter ;
}

CounterObject *new_counter(SemLockObject *self, int value) {
     ENTER(stdout, __func__) ;

    CounterObject *counter = NULL ;
    char buf[256], buf2[128] ;

    if (!shm_semlock_counters.valid) {
        create_shm_semlock_counters() ;
    }

    // acquire global lock
    if (ACQUIRE_GENERAL_LOCK) {
        counter = _new_counter(self, 0, value) ;
        sprintf(buf, "%s %d", str_counter(buf2, counter), value) ;
        SUB_ENTER_STR(stdout, __func__, buf) ;
        RELEASE_GENERAL_LOCK ;
    }
    return counter ;
}

void unlink_close_counter(SemLockObject *self) {
    char buf[128] ;
    ENTER_STR(stdout, __func__, str_counter(buf, self->counter)) ;

    CounterObject *counter = NULL ;
    HeaderObject *header = &shm_semlock_counters.counters->header ;

    counter = self ? self->counter : NULL ;
    // acquire global lock
    if (counter && (ACQUIRE_GENERAL_LOCK)) {
        // close & unlink lock
        sem_close(counter->handle_lock) ;
        sem_unlink(_make_sem_name(buf, self->name)) ;

        // reset counter members and update hheader
        memset(counter, 0, sizeof(CounterObject)) ;
        self->counter = NULL ;
        --header->nb_semlocks ;

        // release global lock
        RELEASE_GENERAL_LOCK ;
    }
}

int _on_update_counter_value(CounterObject *counter, int incr) {
    char buf[128] ;
    ENTER_STR(stdout, __func__, str_counter(buf, counter)) ;

    int value = -1 ;

    // Counter lock already locked.
    if (counter) {
        while(counter->ref_handle != (SEM_HANDLE)0) {
            counter = _search_counter_from_handle(counter->ref_handle) ;
        }
        counter->current_value += incr ;

        // recopy counter
        return counter->current_value ;
    }
}

int _on_get_counter_value(CounterObject *counter) {
    char buf[128] ;
    ENTER_STR(stdout, __func__, str_counter(buf, counter)) ;
    int value = -1 ;

    // Counter lock already locked.
    if (counter) {
        while(counter->ref_handle != (SEM_HANDLE)0) {
            counter = _search_counter_from_handle(counter->ref_handle) ;
        }
        if (counter) {
            return counter->current_value ;
        }

    }
    return value ;
}

int on_get_counter_value(CounterObject *counter) {
    char buf[256] ;
    ENTER_STR(stdout, __func__, str_counter(buf, counter)) ;

    int value = -1 ;

    // acquire counter lock
    if (counter && ACQUIRE_COUNTER_LOCK(counter)) {
        // recopy counter
        value = _on_get_counter_value(counter);

        // release counter lock
        RELEASE_COUNTER_LOCK(counter) ;
    } else {
        puts("no acquire") ;
    }
    return value ;
}

#endif

#include "clinic/semaphore.c.h"

/*
 * Unix definitions
 */

#define SEM_CLEAR_ERROR()
#define SEM_GET_LAST_ERROR() 0
#define SEM_CREATE(name, val, max) sem_open(name, O_CREAT | O_EXCL, 0600, val)
#define SEM_CLOSE(sem) sem_close(sem)
#define SEM_GETVALUE(sem, pval) sem_getvalue(sem, pval)
#define SEM_UNLINK(name) sem_unlink(name)

/* OS X 10.4 defines SEM_FAILED as -1 instead of (sem_t *)-1;  this gives
   compiler warnings, and (potentially) undefined behaviour. */
#ifdef __APPLE__
#  undef SEM_FAILED
#  define SEM_FAILED ((sem_t *)-1)
#endif

#ifndef HAVE_SEM_UNLINK
#  define sem_unlink(name) 0
#endif

#ifndef HAVE_SEM_TIMEDWAIT
#  define sem_timedwait(sem,deadline) sem_timedwait_save(sem,deadline,_save)

static int
sem_timedwait_save(sem_t *sem, struct timespec *deadline, PyThreadState *_save)
{
    int res;
    unsigned long delay, difference;
    struct timeval now, tvdeadline, tvdelay;

    errno = 0;
    tvdeadline.tv_sec = deadline->tv_sec;
    tvdeadline.tv_usec = deadline->tv_nsec / 1000;

    for (delay = 0 ; ; delay += 1000) {
        /* poll */
        if (sem_trywait(sem) == 0)
            return 0;
        else if (errno != EAGAIN)
            return MP_STANDARD_ERROR;

        /* get current time */
        if (gettimeofday(&now, NULL) < 0)
            return MP_STANDARD_ERROR;

        /* check for timeout */
        if (tvdeadline.tv_sec < now.tv_sec ||
            (tvdeadline.tv_sec == now.tv_sec &&
             tvdeadline.tv_usec <= now.tv_usec)) {
            errno = ETIMEDOUT;
            return MP_STANDARD_ERROR;
        }

        /* calculate how much time is left */
        difference = (tvdeadline.tv_sec - now.tv_sec) * 1000000 +
            (tvdeadline.tv_usec - now.tv_usec);

        /* check delay not too long -- maximum is 20 msecs */
        if (delay > 20000)
            delay = 20000;
        if (delay > difference)
            delay = difference;

        /* sleep */
        tvdelay.tv_sec = delay / 1000000;
        tvdelay.tv_usec = delay % 1000000;
        if (select(0, NULL, NULL, NULL, &tvdelay) < 0)
            return MP_STANDARD_ERROR;

        /* check for signals */
        Py_BLOCK_THREADS
        res = PyErr_CheckSignals();
        Py_UNBLOCK_THREADS

        if (res) {
            errno = EINTR;
            return MP_EXCEPTION_HAS_BEEN_SET;
        }
    }
}

#endif /* !HAVE_SEM_TIMEDWAIT */

/*[clinic input]
@critical_section
_multiprocessing.SemLock.acquire

    block as blocking: bool = True
    timeout as timeout_obj: object = None

Acquire the semaphore/lock.
[clinic start generated code]*/
static PyObject *
_multiprocessing_SemLock_acquire_impl(SemLockObject *self, int blocking,
                                      PyObject *timeout_obj)
/*[clinic end generated code: output=f9998f0b6b0b0872 input=079ca779975f3ad6]*/
{
ENTER(stdout, __func__) ;
    int res, err = 0;
    struct timespec deadline = {0};

    if (self->kind == RECURSIVE_MUTEX && ISMINE(self)) {
        ++self->count;
        Py_RETURN_TRUE;
    }

    int use_deadline = (timeout_obj != Py_None);
    if (use_deadline) {
        double timeout = PyFloat_AsDouble(timeout_obj);
        if (PyErr_Occurred()) {
            return NULL;
        }
        if (timeout < 0.0) {
            timeout = 0.0;
        }

        struct timeval now;
        if (gettimeofday(&now, NULL) < 0) {
            PyErr_SetFromErrno(PyExc_OSError);
            return NULL;
        }
        long sec = (long) timeout;
        long nsec = (long) (1e9 * (timeout - sec) + 0.5);
        deadline.tv_sec = now.tv_sec + sec;
        deadline.tv_nsec = now.tv_usec * 1000 + nsec;
        deadline.tv_sec += (deadline.tv_nsec / 1000000000);
        deadline.tv_nsec %= 1000000000;
    }

    /* Check whether we can acquire without releasing the GIL and blocking */
    do {
        res = sem_trywait(self->handle);
        err = errno;
    } while (res < 0 && errno == EINTR && !PyErr_CheckSignals());
    errno = err;

    if (res < 0 && errno == EAGAIN && blocking) {
        printf("res:%d-errno:%d:block:%d}/end:%d}-(L:%d/K:%d/V:%d)\n",
                                            res, errno, blocking, deadline,
                                            self->handle,
                                            self->kind,
                                            self->maxvalue) ;
        /* Couldn't acquire immediately, need to block */
        do {
            Py_BEGIN_ALLOW_THREADS
            if (!use_deadline) {
                res = sem_wait(self->handle);
            }
            else {
                res = sem_timedwait(self->handle, &deadline);
            }
            Py_END_ALLOW_THREADS
            err = errno;
            if (res == MP_EXCEPTION_HAS_BEEN_SET)
                break;
        } while (res < 0 && errno == EINTR && !PyErr_CheckSignals());
    }

    if (res < 0) {
        errno = err;
        if (errno == EAGAIN || errno == ETIMEDOUT) {
            Py_RETURN_FALSE;
        }
        if (errno == EINTR) {
            return NULL;
        }
        return PyErr_SetFromErrno(PyExc_OSError);
    }

#ifdef HAVE_BROKEN_SEM_GETVALUE
    if (ISSEMAPHORE(self)) {
        if (ACQUIRE_COUNTER_LOCK(self->counter)) {
            _on_update_counter_value(self->counter, -1) ;
            RELEASE_COUNTER_LOCK(self->counter) ;
        } else {
            printf("func:'%s' on %d -> error:(%d , %s)\n", __func__, self->counter->handle, errno, strerror(errno)) ;
            return PyErr_SetFromErrno(PyExc_OSError);
        }
    }
#endif

    ++self->count;
    self->last_tid = PyThread_get_thread_ident();

    Py_RETURN_TRUE;
}

/*[clinic input]
@critical_section
_multiprocessing.SemLock.release

Release the semaphore/lock.
[clinic start generated code]*/

static PyObject *
_multiprocessing_SemLock_release_impl(SemLockObject *self)
/*[clinic end generated code: output=b22f53ba96b0d1db input=9bd62d3645e7a531]*/
{
ENTER(stdout, __func__) ;

    if (self->kind == RECURSIVE_MUTEX) {
        if (!ISMINE(self)) {
            PyErr_SetString(PyExc_AssertionError, "attempt to "
                            "release recursive lock not owned "
                            "by thread");
            return NULL;
        }
        if (self->count > 1) {
            --self->count;
            Py_RETURN_NONE;
        }
        assert(self->count == 1);
    } else {
#ifdef HAVE_BROKEN_SEM_GETVALUE
        int sval;
        /* We will only check properly the maxvalue == 1 case */
        if (self->maxvalue == 1) {
            /* make sure that already locked */
            if (sem_trywait(self->handle) < 0) {
                if (errno != EAGAIN) {
                    PyErr_SetFromErrno(PyExc_OSError);
                    return NULL;
                }
                /* it is already locked as expected */
            } else {
                /* it was not locked so undo wait and raise  */
                if (sem_post(self->handle) < 0) {
                    PyErr_SetFromErrno(PyExc_OSError);
                    return NULL;
                }
                PyErr_SetString(PyExc_ValueError, "semaphore "
                                "or lock released too many "
                                "times");
                return NULL;
            }
        } else {
            if (ISSEMAPHORE(self)) {
                sval = on_get_counter_value(self->counter) ;
                if(sval < 0) {
                    return PyErr_SetFromErrno(PyExc_OSError);
                } else { if (sval >= self->maxvalue) {
                    PyErr_SetString(PyExc_ValueError, "semaphore or lock "
                            "released too many times");
                    return NULL;
                    }
                }
            }
        }
#else
        int sval;
        /* This check is not an absolute guarantee that the semaphore
            does not rise above maxvalue. */
        if (sem_getvalue(self->handle, &sval) < 0) {
            return PyErr_SetFromErrno(PyExc_OSError);
        } else if (sval >= self->maxvalue) {
            PyErr_SetString(PyExc_ValueError, "semaphore or lock "
                            "released too many times");
            return NULL;
        }
#endif
    }

    if (sem_post(self->handle) < 0)
        return PyErr_SetFromErrno(PyExc_OSError);

#ifdef HAVE_BROKEN_SEM_GETVALUE
    if (ISSEMAPHORE(self)) {
        if (ACQUIRE_COUNTER_LOCK(self->counter)) {
            _on_update_counter_value(self->counter, +1) ;
            RELEASE_COUNTER_LOCK(self->counter) ;
        } else {
            printf("func:%s on %d -> %d , %s\n", __func__, self->counter->handle, errno, strerror(errno)) ;
            return PyErr_SetFromErrno(PyExc_OSError);
        }
    }
#endif

    --self->count;
    Py_RETURN_NONE;
}

#endif /* !MS_WINDOWS */

/*
 * All platforms
 */

static PyObject *
newsemlockobject(PyTypeObject *type, SEM_HANDLE handle, int kind, int maxvalue,
                 char *name)
{
    SemLockObject *self = (SemLockObject *)type->tp_alloc(type, 0);
    if (!self)
        return NULL;
    self->handle = handle;
    self->kind = kind;
    self->count = 0;
    self->last_tid = 0;
    self->maxvalue = maxvalue;
    self->name = name;
    return (PyObject*)self;
}

/*[clinic input]
@classmethod
_multiprocessing.SemLock.__new__

    kind: int
    value: int
    maxvalue: int
    name: str
    unlink: bool

[clinic start generated code]*/

static PyObject *
_multiprocessing_SemLock_impl(PyTypeObject *type, int kind, int value,
                              int maxvalue, const char *name, int unlink)
/*[clinic end generated code: output=30727e38f5f7577a input=fdaeb69814471c5b]*/
{
char buf[128] ;
ENTER(stdout, __func__) ;
    SEM_HANDLE handle = SEM_FAILED;
    PyObject *result;
    char *name_copy = NULL;

    if (kind != RECURSIVE_MUTEX && kind != SEMAPHORE) {
        PyErr_SetString(PyExc_ValueError, "unrecognized kind");
        return NULL;
    }

    if (!unlink) {
        name_copy = PyMem_Malloc(strlen(name) + 1);
        if (name_copy == NULL) {
            return PyErr_NoMemory();
        }
        strcpy(name_copy, name);
    }

    SEM_CLEAR_ERROR();
    handle = SEM_CREATE(name, value, maxvalue);
    /* On Windows we should fail if GetLastError()==ERROR_ALREADY_EXISTS */
    if (handle == SEM_FAILED || SEM_GET_LAST_ERROR() != 0)
        goto failure;

    if (unlink && SEM_UNLINK(name) < 0)
        goto failure;

    result = newsemlockobject(type, handle, kind, maxvalue, name_copy);
    if (!result)
        goto failure;

#ifdef HAVE_BROKEN_SEM_GETVALUE
    if (ISSEMAPHORE((SemLockObject *)result)) {
        CounterObject *c = new_counter((SemLockObject *)result, value) ;
        sprintf(buf, "%p -> Handle Lock: %d", c, c->handle_lock) ;
        SUB_ENTER_STR(stdout, __func__, buf) ;
    }
#endif

    return result;

  failure:
    if (!PyErr_Occurred()) {
        _PyMp_SetError(NULL, MP_STANDARD_ERROR);
    }
    if (handle != SEM_FAILED)
        SEM_CLOSE(handle);
    PyMem_Free(name_copy);
    return NULL;
}

/*[clinic input]
@classmethod
_multiprocessing.SemLock._rebuild

    handle: SEM_HANDLE
    kind: int
    maxvalue: int
    name: str(accept={str, NoneType})
    /

[clinic start generated code]*/

static PyObject *
_multiprocessing_SemLock__rebuild_impl(PyTypeObject *type, SEM_HANDLE handle,
                                       int kind, int maxvalue,
                                       const char *name)
/*[clinic end generated code: output=2aaee14f063f3bd9 input=f7040492ac6d9962]*/
{
char buf[256] ;
ENTER(stdout, __func__) ;

    PyObject *result = NULL ;
    char *name_copy = NULL;
#ifdef HAVE_BROKEN_SEM_GETVALUE
    SEM_HANDLE ref_handle = handle ;
#endif

    if (name != NULL) {
        name_copy = PyMem_Malloc(strlen(name) + 1);
        if (name_copy == NULL)
            return PyErr_NoMemory();
        strcpy(name_copy, name);
    }

#ifndef MS_WINDOWS
    if (name != NULL) {
        handle = sem_open(name, 0);
        if (handle == SEM_FAILED) {
            PyErr_SetFromErrno(PyExc_OSError);
            PyMem_Free(name_copy);
            return NULL;
        }
    }
#endif
    result = newsemlockobject(type, handle, kind, maxvalue, name_copy);

#ifdef HAVE_BROKEN_SEM_GETVALUE
    sprintf(buf, "name:%s, Handle: %d vs Ref Handle: %d", name, handle, ref_handle) ;
    SUB_ENTER_STR(stdout, __func__, buf) ;

    if (ISSEMAPHORE((SemLockObject *)result)) {
        CounterObject *c = connect_counter((SemLockObject *)result, ref_handle) ;
    }
#endif
    return result ;
}

static void
semlock_dealloc(SemLockObject* self)
{
ENTER(stdout, __func__) ;
    PyTypeObject *tp = Py_TYPE(self);
    PyObject_GC_UnTrack(self);
    if (self->handle != SEM_FAILED) {
        SEM_CLOSE(self->handle);

#ifdef HAVE_BROKEN_SEM_GETVALUE
    if (ISSEMAPHORE(self)) {
        unlink_close_counter(self) ;
    }
#endif
    }
    PyMem_Free(self->name);
    tp->tp_free(self);
    Py_DECREF(tp);
}

/*[clinic input]
@critical_section
_multiprocessing.SemLock._count

Num of `acquire()`s minus num of `release()`s for this process.
[clinic start generated code]*/

static PyObject *
_multiprocessing_SemLock__count_impl(SemLockObject *self)
/*[clinic end generated code: output=5ba8213900e517bb input=9fa6e0b321b16935]*/
{
// ENTER(stdout, __func__) ;
    return PyLong_FromLong((long)self->count);
}

/*[clinic input]
_multiprocessing.SemLock._is_mine

Whether the lock is owned by this thread.
[clinic start generated code]*/

static PyObject *
_multiprocessing_SemLock__is_mine_impl(SemLockObject *self)
/*[clinic end generated code: output=92dc98863f4303be input=a96664cb2f0093ba]*/
{
// ENTER(stdout, __func__) ;
    /* only makes sense for a lock */
    return PyBool_FromLong(ISMINE(self));
}

/*[clinic input]
_multiprocessing.SemLock._get_value

Get the value of the semaphore.
[clinic start generated code]*/

static PyObject *
_multiprocessing_SemLock__get_value_impl(SemLockObject *self)
/*[clinic end generated code: output=64bc1b89bda05e36 input=cb10f9a769836203]*/
{
// ENTER(stdout, __func__) ;
    int sval = -1 ;
#ifdef HAVE_BROKEN_SEM_GETVALUE
    if (self->maxvalue == 1) {
        if (sem_trywait(self->handle) < 0) {
            if (errno == EAGAIN) {
                sval = 0 ;
                return PyLong_FromLong((long)sval);
            }
            return _PyMp_SetError(NULL, MP_STANDARD_ERROR);
        }
        if (sem_post(self->handle) < 0) {
            return _PyMp_SetError(NULL, MP_STANDARD_ERROR);
        }
        sval = 1 ;
        return PyLong_FromLong((long)sval);
    }
    // Semaphore with maxvalue > 1
    sval = on_get_counter_value(self->counter) ;
    if (sval < 0) {
        return _PyMp_SetError(NULL, MP_STANDARD_ERROR) ;
    }
    return PyLong_FromLong((long)sval);
#else
    if (SEM_GETVALUE(self->handle, &sval) < 0) {
        return _PyMp_SetError(NULL, MP_STANDARD_ERROR);
    }
    /* some posix implementations use negative numbers to indicate
       the number of waiting threads */
    if (sval < 0) {
        sval = 0;
    }
    return PyLong_FromLong((long)sval);
#endif
}

/*[clinic input]
_multiprocessing.SemLock._is_zero

Return whether semaphore has value zero.
[clinic start generated code]*/

static PyObject *
_multiprocessing_SemLock__is_zero_impl(SemLockObject *self)
/*[clinic end generated code: output=815d4c878c806ed7 input=294a446418d31347]*/
{
// ENTER(stdout, __func__) ;
#ifdef HAVE_BROKEN_SEM_GETVALUE
    if (sem_trywait(self->handle) < 0) {
        if (errno == EAGAIN)
            Py_RETURN_TRUE;
        return _PyMp_SetError(NULL, MP_STANDARD_ERROR);
    } else {
        if (sem_post(self->handle) < 0)
            return _PyMp_SetError(NULL, MP_STANDARD_ERROR);
        Py_RETURN_FALSE;
    }
#else
    int sval;
    if (SEM_GETVALUE(self->handle, &sval) < 0)
        return _PyMp_SetError(NULL, MP_STANDARD_ERROR);
    return PyBool_FromLong((long)sval == 0);
#endif
}

/*[clinic input]
_multiprocessing.SemLock._after_fork

Rezero the net acquisition count after fork().
[clinic start generated code]*/

static PyObject *
_multiprocessing_SemLock__after_fork_impl(SemLockObject *self)
/*[clinic end generated code: output=718bb27914c6a6c1 input=190991008a76621e]*/
{
ENTER(stdout, __func__) ;
    self->count = 0;
    Py_RETURN_NONE;
}

/*[clinic input]
@critical_section
_multiprocessing.SemLock.__enter__

Enter the semaphore/lock.
[clinic start generated code]*/

static PyObject *
_multiprocessing_SemLock___enter___impl(SemLockObject *self)
/*[clinic end generated code: output=beeb2f07c858511f input=d35c9860992ee790]*/
{
    return _multiprocessing_SemLock_acquire_impl(self, 1, Py_None);
}

/*[clinic input]
@critical_section
_multiprocessing.SemLock.__exit__

    exc_type: object = None
    exc_value: object = None
    exc_tb: object = None
    /

Exit the semaphore/lock.
[clinic start generated code]*/

static PyObject *
_multiprocessing_SemLock___exit___impl(SemLockObject *self,
                                       PyObject *exc_type,
                                       PyObject *exc_value, PyObject *exc_tb)
/*[clinic end generated code: output=3b37c1a9f8b91a03 input=1610c8cc3e0e337e]*/
{
    return _multiprocessing_SemLock_release_impl(self);
}

static int
semlock_traverse(SemLockObject *s, visitproc visit, void *arg)
{
    Py_VISIT(Py_TYPE(s));
    return 0;
}

/*
 * Semaphore methods
 */

static PyMethodDef semlock_methods[] = {
    _MULTIPROCESSING_SEMLOCK_ACQUIRE_METHODDEF
    _MULTIPROCESSING_SEMLOCK_RELEASE_METHODDEF
    _MULTIPROCESSING_SEMLOCK___ENTER___METHODDEF
    _MULTIPROCESSING_SEMLOCK___EXIT___METHODDEF
    _MULTIPROCESSING_SEMLOCK__COUNT_METHODDEF
    _MULTIPROCESSING_SEMLOCK__IS_MINE_METHODDEF
    _MULTIPROCESSING_SEMLOCK__GET_VALUE_METHODDEF
    _MULTIPROCESSING_SEMLOCK__IS_ZERO_METHODDEF
    _MULTIPROCESSING_SEMLOCK__REBUILD_METHODDEF
    _MULTIPROCESSING_SEMLOCK__AFTER_FORK_METHODDEF
    {NULL}
};

/*
 * Member table
 */

static PyMemberDef semlock_members[] = {
    {"handle", T_SEM_HANDLE, offsetof(SemLockObject, handle), Py_READONLY,
     ""},
    {"kind", Py_T_INT, offsetof(SemLockObject, kind), Py_READONLY,
     ""},
    {"maxvalue", Py_T_INT, offsetof(SemLockObject, maxvalue), Py_READONLY,
     ""},
    {"name", Py_T_STRING, offsetof(SemLockObject, name), Py_READONLY,
     ""},
    {NULL}
};

/*
 * Semaphore type
 */

static PyType_Slot _PyMp_SemLockType_slots[] = {
    {Py_tp_dealloc, semlock_dealloc},
    {Py_tp_getattro, PyObject_GenericGetAttr},
    {Py_tp_setattro, PyObject_GenericSetAttr},
    {Py_tp_methods, semlock_methods},
    {Py_tp_members, semlock_members},
    {Py_tp_alloc, PyType_GenericAlloc},
    {Py_tp_new, _multiprocessing_SemLock},
    {Py_tp_traverse, semlock_traverse},
    {Py_tp_free, PyObject_GC_Del},
    {Py_tp_doc, (void *)PyDoc_STR("Semaphore/Mutex type")},
    {0, 0},
};

PyType_Spec _PyMp_SemLockType_spec = {
    .name = "_multiprocessing.SemLock",
    .basicsize = sizeof(SemLockObject),
    .flags = (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE |
              Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_IMMUTABLETYPE),
    .slots = _PyMp_SemLockType_slots,
};

/*
 * Function to unlink semaphore names
 */

PyObject *
_PyMp_sem_unlink(const char *name)
{
ENTER(stdout, __func__) ;
    if (SEM_UNLINK(name) < 0) {
        _PyMp_SetError(NULL, MP_STANDARD_ERROR);
        return NULL;
    }

    Py_RETURN_NONE;
}
#endif // HAVE_MP_SEMAPHORE
