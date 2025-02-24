#ifndef SEMAPHORE_MACOSX_H
#define SEMAPHORE_MACOSX_H

#include <unistd.h>     // sysconf(SC_PAGESIZE)
#include <sys/mman.h>   // shm_open, shm_unlink
#include <assert.h>     // assert

#define SC_PAGESIZE             sysconf(_SC_PAGESIZE)

#define CALC_NB_SLOTS(size)     (int)(((size) - sizeof(HeaderObject)) / sizeof(CounterObject))
#define CALC_SIZE_SHM           (sysconf(_SC_SEM_NSEMS_MAX) * sizeof(CounterObject)) + sizeof(HeaderObject);
#define ALIGN_SHM_PAGE(s)       ((int)((s)/SC_PAGESIZE)+1)*SC_PAGESIZE

/*
Structure in shared memory
*/
typedef struct {
    int n_semlocks;   // Current number of semaphores. Starts 0.
    int n_slots;      // Current slots in the counter array.
    int size_shm;     // Size of allocated shared memory (this and N counters).
    int n_procs;      // Number of attached processes (Used to check).
} HeaderObject;

typedef struct {
    char sem_name[16];  // Name of semaphore.
    int internal_value; // Internal value of semaphore, update on each acquire/release.
    int reset_counter;  // Can reset counter on dealloc call.
    time_t ctimestamp;  // Created timestamp.
} CounterObject;

/*
2 -> Structure of static memory:
*/

typedef int MEMORY_HANDLE;
enum _state {THIS_NOT_OPEN, THIS_AVAILABLE, THIS_CLOSED};

typedef struct {
    /*-- global datas --*/
    int state_this;           // State of this structure.
    char *name_shm;
    MEMORY_HANDLE handle_shm; // Memory handle.
    int create_shm;           // Did I create this shared memory ?
    char *name_gmlock;
    SEM_HANDLE handle_gmlock; // Global memory lock to handle shared memory.
    /*-- Pointers to shared memory --*/
    HeaderObject *header;     // Pointer to header (shared memory).
    CounterObject*counters;   // Pointer to first item of fix array (shared memory).
}  CountersWorkaround;

#define SHOW_WHERE(m)   (printf("%s %d\t", m, __LINE__))

#define ACQUIRE_GENERAL_LOCK    (/*SHOW_WHERE("Acquire") &&*/ acquire_lock(shm_semlock_counters.handle_gmlock) >= 0)
#define RELEASE_GENERAL_LOCK    (/*SHOW_WHERE("Release") &&*/ release_lock(shm_semlock_counters.handle_gmlock) >= 0)

#define ACQUIRE_COUNTER_MUTEX(s) (acquire_lock((s)) >= 0)
#define RELEASE_COUNTER_MUTEX(s) (release_lock((s)) >= 0)

#define ISSEMAPHORE2(m, k) (m > 1 && k == SEMAPHORE)
#define ISSEMAPHORE(o) ((o)->maxvalue > 1 && (o)->kind == SEMAPHORE)

#define NO_VALUE (-11111111)

/* Debug logs */
#define _DEBUG_SEMAPHORE  0

#define STR_SEMAPHORE_HEADER(m, h)   do { \
                                        char buf[256]; \
                                        PyOS_snprintf(buf, sizeof(buf), \
                                                    "%s, PID:%d - nb sems:%d - nb sem slots:%d, new_size:%d", \
                                                    m, getpid(), (h)->nb_semlocks, \
                                                                 (h)->nb_slots, \
                                                                 (h)->size_shm); \
                                        puts(buf); \
                                        } while(0)

#define STR_SEMAPHORE_PTRS(m, h, c) do { \
                                        char buf[256]; \
                                        PyOS_snprintf(buf, sizeof(buf), \
                                                    "%s, PID:%d - Header:%p, counters:%p", \
                                                    m, getpid(), \
                                                    h, c); \
                                        puts(buf); \
                                        } while(0)
#define STR_SEMAPHORE_LOG(m, s) do { \
                                    char buf[256]; \
                                    PyOS_snprintf(buf, sizeof(buf), \
                                                    "%s, PID:%d, %s", \
                                                    m, getpid(), \
                                                    s); \
                                    if (_DEBUG_SEMAPHORE) {puts(buf);} \
                                    } while(0)

#define SEMAPHORE_LOG(m, s)    do { \
                                    if (ISSEMAPHORE(s)) { \
                                        char buf[256]; \
                                        PyOS_snprintf(buf, sizeof(buf), \
                                                     "%s, PID:%d, n:%s, h:%p/%p, c:%p, v:%d, r:%d, t:%lu", \
                                                    m, getpid(), \
                                                    (s)->name, \
                                                    (s)->handle, (s)->handle_mutex, \
                                                    (s)->counter, \
                                                    (s)->counter->internal_value, \
                                                    (s)->counter->reset_counter, \
                                                    (long)(s)->counter->ctimestamp); \
                                        if (_DEBUG_SEMAPHORE) {puts(buf);} \
                                    } \
                                    } while(0)

#define INIT_SEMAPHORE_LOG(m, n, k, mv, u) do { \
                                                if (ISSEMAPHORE2(mv, k)) { \
                                                        char buf[256]; \
                                                        PyOS_snprintf(buf, sizeof(buf), \
                                                                     "%s, PID:%d, n:%s, k:%d, mv:%d, u:%d, f:%d", \
                                                                     m, getpid(), \
                                                                     n, k, mv, u, k); \
                                                if (_DEBUG_SEMAPHORE) { puts(buf); } \
                                                } \
                                            } while(0)

#endif /* SEMAPHORE_MACOSX_H */