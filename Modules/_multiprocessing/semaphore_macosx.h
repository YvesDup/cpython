#ifndef SEMAPHORE_MACOSX_H
#define SEMAPHORE_MACOSX_H

#include <unistd.h>     // sysconf(SC_PAGESIZE)
#include <sys/mman.h>   // shm_open, shm_unlink

/*
On my MacOSX m4 pro, sysconf(_SC_SEM_NSEM_MAX) returns 87381.
Perharps, this value is to high ?
*/
#define NSEMS_MAX               sysconf(_SC_SEM_NSEMS_MAX)

#define CALC_SIZE_SHM           (NSEMS_MAX * sizeof(CounterObject)) + sizeof(HeaderObject);

#define SC_PAGESIZE             sysconf(_SC_PAGESIZE)
#define ALIGN_SHM_PAGE(n)       ((int)((n)/SC_PAGESIZE)+1)*SC_PAGESIZE

#define CALC_NB_SLOTS(n)        (int)((((n)) - sizeof(HeaderObject)) / sizeof(CounterObject))

/*
Structure in shared memory
*/
typedef struct {
    int n_semlocks;   // Current number of semaphores. Starts 0.
    int n_slots;      // Current slots in the counter array.
    int size_shm;     // Size of allocated shared memory (this and N counters).
    //int n_procs;      // Number of attached processes (Used to check).
} HeaderObject;

#define SIZE_SEM_NAME 16
#define SIZE_MUTEX_NAME (SIZE_SEM_NAME<<1)

typedef struct {
    char sem_name[SIZE_SEM_NAME];  // Name of semaphore.
    short internal_value;     // Internal value of semaphore, update on each acquire/release.
    short n_instances;        // Number of SemLockObject linked to this CounterObject.
    short unlink;             // Flag to know if this CounterObject is unlink or not.
    time_t ctimestamp;      // Created timestamp (debug log).
} CounterObject;

/*
Structure of static memory:
*/

typedef int MEMORY_HANDLE;
enum _state {THIS_NOT_OPEN, THIS_AVAILABLE, THIS_CLOSED};

#define SHAREDMEM_NAME  "/psm_gh125828"
#define GLOCK_NAME      "/mp_gh125828"

struct _CountersWorkaround{
    /*-- global datas --*/
    PyMutex shm_counters_mutex ; // Mutex to handle safely the shared memory counters.
    int state_this;           // State of this structure.
    char *name_shm;
    MEMORY_HANDLE handle_shm; // Memory handle.
    char *name_glock;
    SEM_HANDLE handle_glock; // Global memory lock to handle shared memory.
    /*-- Pointers to shared memory --*/
    HeaderObject *header;     // Pointer to header (shared memory).
    CounterObject*counters;   // Pointer to the first item of fixed array (shared memory).
};

#define ISSEMAPHORE(o) ((o)->maxvalue > 1 && (o)->kind == SEMAPHORE)

#define DEBUG_MACOSX_SEMAPHORE 0
#if defined(Py_DEBUG) && DEBUG_MACOSX_SEMAPHORE == 1
    #define DEBUG_PID_FUNC(n, h, c, m)  do { \
                                            fprintf(stdout, "%-40s - PID:%05d - (%s - hdl:'%02lX') - " \
                                                            "[HD:%lX - c:%lX] - %03d sems - %s\n", \
                                                            __func__, \
                                                            getpid(), \
                                                            n, \
                                                            (unsigned long)h, \
                                                            (unsigned long)shm_semlock_counters.header, \
                                                            (unsigned long)c, \
                                                            shm_semlock_counters.header ? shm_semlock_counters.header->n_semlocks : 255, \
                                                            m); \
                                            } while(0);

    #define LOG_GLOCK(m)      1 // fprintf(stderr, "%s %s: %03d\n", m, __func__, __LINE__)
    #define LOG_LOCK(m, h)    1 // fprintf(stderr, "%s(%02lX) %s: %03d\n", m, (unsigned long)h, __func__, __LINE__)
#else
    #define DEBUG_PID_FUNC(n, h, c, m)  1
    #define LOG_GLOCK(m)      1
    #define LOG_LOCK(m, h)    1
#endif

#define EXIST_GLOCK       (LOG_GLOCK("EXIST_GLOCK") > 0 && exist_lock(shm_semlock_counters.handle_glock) == 1)
#define ACQUIRE_GLOCK     (LOG_GLOCK("ACQ_GLOCK") > 0 && acquire_lock(shm_semlock_counters.handle_glock) == 0)
#define RELEASE_GLOCK     (LOG_GLOCK("\tREL_GLOCK") > 0 && release_lock(shm_semlock_counters.handle_glock) == 0)
#define ACQUIRE_COUNTER_MUTEX(h) (LOG_LOCK("acq", h) && acquire_lock((h)) == 0)
#define RELEASE_COUNTER_MUTEX(h) (LOG_LOCK("\trel", h) && release_lock((h)) == 0)

#endif /* SEMAPHORE_MACOSX_H */
