#ifndef SEMAPHORE_MACOSX_H
#define SEMAPHORE_MACOSX_H

#include <unistd.h>     // sysconf(SC_PAGESIZE)
#include <sys/mman.h>   // shm_open, shm_unlink
#include <assert.h>     // assert

#define ACQUIRE_GENERAL_LOCK    (sem_wait(shm_semlock_counters.handle_gmlock) >= 0)
#define RELEASE_GENERAL_LOCK    (sem_post(shm_semlock_counters.handle_gmlock) >= 0)

#define ACQUIRE_COUNTER_MUTEX(s) (sem_wait((s)->handle_mutex) >= 0)
#define RELEASE_COUNTER_MUTEX(s) (sem_post((s)->handle_mutex) >= 0)

typedef struct {
    char filler[16] ; // Filler
    int nb_semlocks;  // Current number of semaphores. Starts 0.
    int max_slots;    // Max size of counter array.
    int size_shm;     // Size of allocated shared memory.
} HeaderObject;

typedef struct {
    char sem_name[16];  // Name of semaphore.
    int internal_value; // Internal value of semaphore, update on each acquire/release.
    int n_procs;        // Number of attached processes (Used to check).
    int reset_counter;  // Reset counter when dealloc.
} CounterObject;

/*-----------
mmap allocate at least a PAGESIZE bytes block.
This value could be get throught a sysconf(SC_PAGESIZE).
Size of counters array is evaluted regarding size of
CounterObject. First 'item' is used as header.
-------------*/

#define BASE_ALLOCATE 4096*4 // sysconf(_SC_PAGESIZE)
#define MAX_COUNTERS  ((BASE_ALLOCATE/sizeof(CounterObject))-1)

typedef struct {
    HeaderObject header;
    CounterObject array_counters[MAX_COUNTERS];
} SharedCounters;

/*
2 -> Structure of static memory:
*/

typedef int MEMORY_HANDLE;
enum _state {THIS_NOT_OPEN, THIS_AVAILABLE, THIS_CLOSED};

typedef struct {
    int state_this;           // State of this structure.
    char *name_shm;
    MEMORY_HANDLE handle_shm; // Memory handle.
    int create_shm;           // Did I create this shared memory ?
    char *name_gmlock;
    SEM_HANDLE handle_gmlock; // Global memory lock to handle shared memory.
    SharedCounters *counters; // Data from mmap, shares by all processes.
}  CountersWorkaround;

#define ISSEMAPHORE2(m, k) (m > 1 && k >= SEMAPHORE)
#define ISSEMAPHORE(o) ((o)->maxvalue > 1 && (o)->kind >= SEMAPHORE)
#define NO_VALUE (-11111111)

#define _DEBUG_SEMAPHORE  0
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
                                                     "%s, PID:%d, n:%s, h:%p/%p, c:%p, v:%d, p:%d, r:%d", \
                                                    m, getpid(), \
                                                    s->name, \
                                                    s->handle, s->handle_mutex, \
                                                    s->counter, \
                                                    s->counter->internal_value, \
                                                    s->counter->n_procs, \
                                                    s->counter->reset_counter); \
                                        if (_DEBUG_SEMAPHORE) {puts(buf);} \
                                    } \
                                } while(0)

#define INIT_SEMAPHORE_LOG(m, n, k, mv, u) do { \
                                                if (ISSEMAPHORE2(mv, k)) { \
                                                        char buf[256]; \
                                                        PyOS_snprintf(buf, sizeof(buf), \
                                                                     "%s, PID:%d, n:%s, k:%d, mv:%d, u:%d, f:%d", \
                                                                     m, getpid(), \
                                                                     n, k, mv, u, k>100); \
                                                if (_DEBUG_SEMAPHORE) { puts(buf); } \
                                                } \
                                            } while(0)

#endif /* SEMAPHORE_MACOSX_H */