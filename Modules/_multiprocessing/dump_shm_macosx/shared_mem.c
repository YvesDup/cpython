#include <fcntl.h>  /* O_CREAT and O_EXCL */
#include <signal.h> // signal
#include <stdio.h>  // printf, puts
#include <stdlib.h> // atexit

#include <semaphore.h> // sem_open
typedef sem_t *SEM_HANDLE;

#include "../semaphore_macosx.h"
#include "shared_mem.h"

void sigterm(int code) {
    exit(EXIT_SUCCESS);
}

void connect_shm_semlock_counters(void) {
puts(__func__);

    int oflag = O_RDWR;
    int shm = -1;
    int res = -1;
    SEM_HANDLE sem = SEM_FAILED;
    int size_shm = sysconf(_SC_PAGESIZE);
    // Install signals.
    signal(SIGTERM, &sigterm);
    signal(SIGINT, &sigterm);

    // Connect to semaphore.
    sem = sem_open(shm_semlock_counters.name_gmlock, 0);
    shm_semlock_counters.handle_gmlock = sem;

    // Locks to semaphore.
    if (sem != SEM_FAILED && ACQUIRE_GENERAL_LOCK) {
        printf("Lock ok on %p\n", sem);
        // connect to Shared mem
        shm = shm_open(shm_semlock_counters.name_shm, oflag, 0);
        if (shm != -1) {
            shm_semlock_counters.handle_shm = shm;
            printf("Shared Mem ok on '%d'\n", shm);
            shm_semlock_counters.counters = (SharedCounters *)mmap(NULL,
                                                        size_shm,
                                                        (PROT_WRITE | PROT_READ),
                                                        MAP_SHARED,
                                                        shm_semlock_counters.handle_shm,
                                                        0L);
            printf("Shared memory size is %d vs %d\n", size_shm,
                                                       shm_semlock_counters.counters->header.size_shm);
            // Initialization is successful.
            shm_semlock_counters.state_this = THIS_AVAILABLE;
            header = &shm_semlock_counters.counters->header;
            counter = shm_semlock_counters.counters->array_counters;
            atexit(delete_shm_semlock_counters);
            puts("Ok....");
        } else {
            puts("No Shared memory available");
        }
        RELEASE_GENERAL_LOCK;
    } else {
        puts("No Semaphore opened !!");
    }
}

void delete_shm_semlock_counters(void) {
puts(__func__);

    puts("clean up...");
    if (shm_semlock_counters.state_this == THIS_AVAILABLE) {
        if (shm_semlock_counters.counters) {
            if (ACQUIRE_GENERAL_LOCK) {
                // unmmap
                munmap(shm_semlock_counters.counters,
                       shm_semlock_counters.counters->header.size_shm);
                shm_unlink(shm_semlock_counters.name_shm);
                shm_semlock_counters.state_this = THIS_CLOSED;
                RELEASE_GENERAL_LOCK;
            }
        }
        // close lock
        sem_close(shm_semlock_counters.handle_gmlock);
        sem_unlink(shm_semlock_counters.name_gmlock);
    }
}

void dump_shm_semlock_header(void) {
    printf("nb sems:%d - nb sem_slots:%d, size_shm:%d\n", header->nb_semlocks,
                                                           header->max_slots,
                                                           header->size_shm);
}

void dump_shm_semlock_header_counters(void) {
    printf("header:%p - counter array:%p\n", header, counter);
}
