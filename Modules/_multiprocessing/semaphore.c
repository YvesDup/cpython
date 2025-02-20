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
    type = "SEM_HANDLE
    format_unit = '"F_SEM_HANDLE"'

[python start generated code]*/
/*[python end generated code: output=da39a3ee5e6b4b0d input=3e0ad43e482d8716]*/

/*[clinic input]
module _multiprocessing
class _multiprocessing.SemLock "SemLockObject *" "&_PyMp_SemLockType"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=935fb41b7d032599]*/

#ifndef HAVE_BROKEN_SEM_GETVALUE

typedef struct {
    PyObject_HEAD
    SEM_HANDLE handle;
    unsigned long last_tid;
    int count;
    int maxvalue;
    int kind;
    char *name;
} SemLockObject;

#define _SemLockObject_CAST(op) ((SemLockObject *)(op))

#include "clinic/semaphore.c.h"

#define ISMINE(o) (o->count > 0 && PyThread_get_thread_ident() == o->last_tid)

#endif /* !HAVE_BROKEN_SEM_GETVALUE */

#ifdef MS_WINDOWS

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

    for (delay = 0;; delay += 1000) {
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

#ifdef HAVE_BROKEN_SEM_GETVALUE
/*
cf: https://github.com/python/cpython/issues/125828

On MacOSX, `sem_getvalue` is not implemented. This workaround proposes to handle
the internal value Semaphore ((R)Lock are out of scope) in a shared memory
available for each processed.

This internal value is stored in a structure named CounterObject with:
+ the referenced semaphore name,
+ the (internal) current value,
+ a flag to reset counter when dealloc.
+ a created timestamp.

A header is
+ the count of semaphore stored.
+ the count of available slots.
+ the size of shared memory.
+ a count of attached processes.

With each Semaphore (SemLock) a mutex is created in order to protect data race and
a reference to the CounterObject is updated.

When impl/rebuid functions are called, CounterObject is associated to the Semaphore.
When acquire/release functions are called, this internal value is updated.
When getvalue function is called, the internal value is returned.
When dealloc fonction is called, CounterObject is reset.
When unlink is called, Semaphore and its mutex are unlink.

A global lock is also created to handle shared memory when the workaround needs
a new counter, when counter is no more used or when an extension of shared memory is necessary.

1 -> Structure of shared memory:

                   --------- Extendable array of 'N' Counters ---------
                 /                                                      \
+-----------------+----------------+---/  /---+-------------+-------------+
|  Header         |    Counter 1   |          | Counter N-1 |  Counter N  |
|-----------------|----------------|   ....   |-------------|-------------|
|                 |                |          |             |             |
|  n_semlocks     | sem_name       |          |             |             |
|  n_semlocks     | internal_value |          |             |             |
|  size_shm       | reset_counter  |          |             |             |
|  n_procs        | ctimestamp     |          |             |             |
+-----------------+----------------+---/  /---+-------------+-------------+
*/
// ------------- list of structures --------------

#include "semaphore_macosx.h"

/*
Datas for each process.
*/
static CountersWorkaround shm_semlock_counters = {
    .state_this = THIS_NOT_OPEN,
    .name_shm = "/shm_gh125828",
    .handle_shm = (MEMORY_HANDLE)0,
    .create_shm = 0,
    .name_gmlock = "/mp_gh125828",
    .handle_gmlock = (SEM_HANDLE)0,
    .header = (HeaderObject *)NULL,
    .counters = (CounterObject *)NULL
};

/*
SemLockObject with aditionnal members:
+ semaphore as mutex to protect access to associated CounterObject.
+ pointer to CounterObject.
*/
typedef struct {
    PyObject_HEAD
    SEM_HANDLE handle;
    unsigned long last_tid;
    int count;
    int maxvalue;
    int kind;
    char *name;
    SEM_HANDLE handle_mutex;
    CounterObject *counter;
} SemLockObject;

#define _SemLockObject_CAST(op) ((SemLockObject *)(op))

#define ISMINE(o) ((o)->count > 0 && PyThread_get_thread_ident() == (o)->last_tid)

#include "clinic/semaphore.c.h"

/*
Shared memory management
*/

void delete_shm_semlock_counters(void) {

    if (shm_semlock_counters.handle_gmlock != SEM_FAILED &&
        shm_semlock_counters.state_this == THIS_AVAILABLE) {
        if (shm_semlock_counters.counters) {
            if (ACQUIRE_GENERAL_LOCK) {
                // unmmap
                munmap(shm_semlock_counters.counters,
                       shm_semlock_counters.header->size_shm);

                --(shm_semlock_counters.header->n_procs);
                /*
                Close shared mem
                When shm_unlink(shm_semlock_counters.name_shm) is always called,
                some tests about the "test.test_multiprocessing_spawn.test_processes.
                WithProcessesTestBarrier" are randomly failed.

                I don't figure out why.
                */
                if (shm_semlock_counters.handle_shm && !shm_semlock_counters.header->n_procs) {
                    STR_SEMAPHORE_LOG("\tdelete_mem", "shm_unlink");
                    shm_unlink(shm_semlock_counters.name_shm);
                } else {
                    STR_SEMAPHORE_LOG("\tdelete_mem", "Not shm_unlink");
                }
                shm_semlock_counters.state_this = THIS_CLOSED;

                if (RELEASE_GENERAL_LOCK) {
                    // close lock and unlink
                    STR_SEMAPHORE_LOG("\tdelete_mem", "sem_close");
                    sem_close(shm_semlock_counters.handle_gmlock);
                    sem_unlink(shm_semlock_counters.name_gmlock);
                }
            }
        }
    }
}

void create_shm_semlock_counters(const char *from_sem_name) {
    int oflag = O_RDWR;
    int shm = -1;
    int res = -1;
    SEM_HANDLE sem = SEM_FAILED;
    char *datas = NULL;
    HeaderObject *header = NULL;
    long size_shm = CALC_SIZE_SHM;

    // Calculate a new size as a multiple of SC_PAGESIZE
    size_shm = ALIGN_SHM_PAGE(size_shm);

    // already done
    if (shm_semlock_counters.state_this != THIS_NOT_OPEN) {
        return;
    }
    errno = 0;
    sem = SEM_CREATE(shm_semlock_counters.name_gmlock, O_CREAT, 1);
    if (sem == SEM_FAILED) {
        errno = 0;
        // Semaphore exists, just opens it.
        sem = sem_open(shm_semlock_counters.name_gmlock, 0);
    }
    shm_semlock_counters.handle_gmlock = sem;

    // Acquire the global lock in order to be alone to
    // create shared memory.
    if (sem != SEM_FAILED && ACQUIRE_GENERAL_LOCK) {
        if (shm_semlock_counters.handle_shm == (MEMORY_HANDLE)0) {
            shm = shm_open(shm_semlock_counters.name_shm, oflag, 0);
            res = 0;
            if (shm == -1) {
                oflag |= O_CREAT;
                shm = shm_open(shm_semlock_counters.name_shm, oflag, S_IRUSR | S_IWUSR);
                // Set size.
                res = ftruncate(shm, size_shm);
                shm_semlock_counters.create_shm = 1;
            }
            // mmap
            if (res >= 0) {
                shm_semlock_counters.handle_shm = shm;
                datas = (char *)mmap(NULL,
                                     size_shm,
                                     (PROT_WRITE | PROT_READ),
                                     (MAP_SHARED),
                                     shm_semlock_counters.handle_shm,
                                     0L);
                if (datas != MAP_FAILED) {
                    shm_semlock_counters.header = (HeaderObject *)datas;
                    shm_semlock_counters.counters = (CounterObject *)(datas+sizeof(HeaderObject));
                    header = shm_semlock_counters.header;
                    // When mmap is just created, initialize all members.
                    if (oflag & O_CREAT) {
                        header->size_shm = size_shm;
                        header->n_slots = CALC_NB_SLOTS(size_shm);
                        header->n_semlocks = 0;
                        header->n_procs = 0;
                    }
                    ++header->n_procs;

                    // Initialization is successful.
                    shm_semlock_counters.state_this = THIS_AVAILABLE;
                    atexit(delete_shm_semlock_counters);
                }
            }
        }
        RELEASE_GENERAL_LOCK;
    }
}

/*
Build name of mutex associated with each Semaphore.
From python SemLock class, name is unique.
static char *gh_name = "_gh125828";
static char *_build_sem_name(char *buf, const char *name) {
    strcpy(buf, name);
    strcat(buf, gh_name);
    return buf;
}
*/

/*
Search if semaphore name is already store in a counter of the shared memory.
*/
static CounterObject* _search_counter_from_sem_name(const char *sem_name) {
    int i = 0, j = 0;
    HeaderObject *header = shm_semlock_counters.header;
    CounterObject *counter = shm_semlock_counters.counters;

    while(i < header->n_slots && j < header->n_semlocks) {
        if(PyOS_stricmp(counter->sem_name, sem_name) == 0) {
            return counter;
        }
        if (counter->sem_name[0] != 0) {
            ++j;
        }
        ++i;
        ++counter;
    }
    return (CounterObject *)NULL;
}

/*
Search for a free slot to store data for a new counter.
*/
static CounterObject* _search_counter_free_slot(void) {
    int i = 0;
    HeaderObject *header = shm_semlock_counters.header;
    CounterObject *counter = shm_semlock_counters.counters;

    while (i < header->n_slots) {
        if(counter->sem_name[0] == 0) {
            /* Mark as reserved */
            counter->sem_name[0] = 'X';
            return counter;
        }
        ++counter;
        ++i;
    }

    /*
    Not enough memory: see sysconf(_SC_SEM_NSEMS_MAX)
    */
    return (CounterObject *)NULL;
}

/*
Connect a Semaphore with an existing counter, from `SemLock__rebuild.
*/
static CounterObject *connect_counter(SemLockObject *self, const char *name) {
    CounterObject *counter = NULL;

    if (shm_semlock_counters.state_this == THIS_NOT_OPEN) {
        create_shm_semlock_counters(name);
    }
    errno = 0;
    if (ACQUIRE_GENERAL_LOCK) {
        counter = _search_counter_from_sem_name(name);
        if (counter) {
            // Find counter.
            STR_SEMAPHORE_LOG("Link_connect", counter->sem_name);
            return counter;
        } else {
            PyErr_SetString(PyExc_ValueError, "Can't find reference to this Semaphore");
        }
        errno = 0;
        if (!RELEASE_GENERAL_LOCK || !counter) {
            PyErr_SetFromErrno(PyExc_OSError);
        }
    } else {
        PyErr_SetFromErrno(PyExc_OSError);
    }
    return NULL;
}

/*
Create a new counter for a Semaphore, from `SemLock_Impl`.
*/
static CounterObject *new_counter(SemLockObject *self, const char *name,
                                  int value, int reset_counter) {
    CounterObject *counter = NULL;

    if (shm_semlock_counters.state_this == THIS_NOT_OPEN) {
        create_shm_semlock_counters(name);
    }

    errno = 0;
    if (ACQUIRE_GENERAL_LOCK) {
        counter = _search_counter_free_slot();
        if (counter == NULL) {
            PyErr_SetString(PyExc_MemoryError, "Can't allocate more "
                            "shared memory for workaround");
            return NULL;
        }
        // Create/copy a new counter.
        strcpy(counter->sem_name, name);
        counter->internal_value = value;
        counter->reset_counter = reset_counter;
        counter->ctimestamp = time(NULL);

        // Update header.
        ++shm_semlock_counters.header->n_semlocks;
        STR_SEMAPHORE_LOG("Link_create", counter->sem_name);

        errno = 0;
        if (!RELEASE_GENERAL_LOCK || !counter) {
            PyErr_SetFromErrno(PyExc_OSError);
        }
    } else {
        PyErr_SetFromErrno(PyExc_OSError);
    }
    return counter;
}

/*
Checks if counter must be remove from shared memory.
*/
static void dealloc_counter(SemLockObject *self) {
    CounterObject *counter = self->counter;

    if (counter && counter->reset_counter) {
        errno = 0;
        if (ACQUIRE_GENERAL_LOCK) {
            /*
            Reset counter members and update header.
            */
            SEMAPHORE_LOG("\tdel_share", self);
            // Reset counter.
            memset(counter, 0, sizeof(CounterObject));
            // Update semlock str
            self->counter = NULL;

            // Update header.
            --(shm_semlock_counters.header->n_semlocks);
            errno = 0;
            if (!RELEASE_GENERAL_LOCK) {
                PyErr_SetFromErrno(PyExc_OSError);
            }
        } else {
            PyErr_SetFromErrno(PyExc_OSError);
        }
    }
}

#endif /* HAVE_BROKEN_SEM_GETVALUE */

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
#ifdef HAVE_BROKEN_SEM_GETVALUE
    SEMAPHORE_LOG("acquire", self);
#endif

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
        /* here we are in a critical section */
        --self->counter->internal_value;
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
#ifdef HAVE_BROKEN_SEM_GETVALUE
    SEMAPHORE_LOG("release", self);
#endif

    if (self->kind == RECURSIVE_MUTEX) {
        if (!ISMINE(self)) {
            PyErr_SetString(PyExc_AssertionError, "attempt to "
                            "release recursive lock "
                            "not owned by thread");
            return NULL;
        }
        if (self->count > 1) {
            --self->count;
            Py_RETURN_NONE;
        }
        assert(self->count == 1);
    } else {
#ifdef HAVE_BROKEN_SEM_GETVALUE
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
                if ( self->counter->internal_value >= self->maxvalue) {
                    PyErr_SetString(PyExc_ValueError, "semaphore or lock "
                            "released too many times");
                    return NULL;
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
        /* here we are in a critical section */
        ++self->counter->internal_value;
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
#ifdef HAVE_BROKEN_SEM_GETVALUE
    self->counter = NULL;
#endif
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
#ifdef HAVE_BROKEN_SEM_GETVALUE
    INIT_SEMAPHORE_LOG("impl", name, kind, maxvalue, unlink);
#endif

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
    SemLockObject *semlock = _SemLockObject_CAST(result);

    if (ISSEMAPHORE2(maxvalue, kind)) {
        semlock->counter = new_counter(semlock, name, value, unlink);
        if (!semlock->counter) {
            PyObject_GC_UnTrack(semlock);
            type->tp_free(semlock);
            Py_DECREF(type);
            goto failure;
        }
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
#ifdef HAVE_BROKEN_SEM_GETVALUE
    INIT_SEMAPHORE_LOG("rebuild", name, kind, maxvalue, -1);
#endif

    PyObject *result = NULL;
    char *name_copy = NULL;

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
    SemLockObject *semlock = _SemLockObject_CAST(result);

    if (ISSEMAPHORE(semlock)) {
        semlock->counter = connect_counter(semlock, name);
        if (!semlock->counter) {
            PyObject_GC_UnTrack(semlock);
            type->tp_free(semlock);
            Py_DECREF(type);

            PyErr_SetFromErrno(PyExc_OSError);
            PyMem_Free(name_copy);
            return NULL;
        }
    }
#endif
    return result;
}

static void
semlock_dealloc(SemLockObject* self)
{
#ifdef HAVE_BROKEN_SEM_GETVALUE
    SEMAPHORE_LOG("desalloc", self);
#endif

    PyTypeObject *tp = Py_TYPE(self);
    PyObject_GC_UnTrack(self);
    if (self->handle != SEM_FAILED) {
        SEM_CLOSE(self->handle);
    }
#ifdef HAVE_BROKEN_SEM_GETVALUE
    if (ISSEMAPHORE(self)) {
        dealloc_counter(self);
    }
#endif
    if (self->name) {
        PyMem_Free(self->name);
    }
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
// ENTER(__func__);
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
// ENTER(__func__);
    /* only makes sense for a lock */
    return PyBool_FromLong(ISMINE(self));
}

/*[clinic input]
_multiprocessing.SemLock._get_value

Get the value of the semaphore.
[clinic start generated code]*/

PyObject *_multiprocessing_SemLock__is_zero_impl(SemLockObject *self);
static PyObject *
_multiprocessing_SemLock__get_value_impl(SemLockObject *self)
/*[clinic end generated code: output=64bc1b89bda05e36 input=cb10f9a769836203]*/
{
    int sval = -1;
#ifdef HAVE_BROKEN_SEM_GETVALUE
    if (self->maxvalue <= 1) {
        return PyLong_FromLong((long)Py_IsFalse(_multiprocessing_SemLock__is_zero_impl(self)));
    }
    // Semaphore with maxvalue > 1
    sval = self->counter->internal_value;
    if (sval < 0) {
        sval = 0;
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
    if (SEM_UNLINK(name) < 0) {
        _PyMp_SetError(NULL, MP_STANDARD_ERROR);
        return NULL;
    }
#ifdef HAVE_BROKEN_SEM_GETVALUE
    CounterObject *counter = NULL;

    if (ACQUIRE_GENERAL_LOCK) {
        // _build_sem_name(mutex_name, name);
        counter = _search_counter_from_sem_name(name);
        RELEASE_GENERAL_LOCK;
    }
    if (counter) {
        STR_SEMAPHORE_LOG("\t\tunlink", name);
        counter->reset_counter = 1;
    }
#endif

    Py_RETURN_NONE;
}

#endif // HAVE_MP_SEMAPHORE
