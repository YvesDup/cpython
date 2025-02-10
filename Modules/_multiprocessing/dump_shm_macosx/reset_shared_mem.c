#include <unistd.h>
#include <stdio.h>  // puts, printf, scanf
#include <string.h> // memcpy, memcmp, memset

#include <semaphore.h>
typedef sem_t *SEM_HANDLE;

#include "../semaphore_macosx.h"
#include "shared_mem.h"

// Static datas for each process.
CountersWorkaround shm_semlock_counters = {
    .state_this = THIS_NOT_OPEN,
    .name_shm = "/shm_gh125828",
    .handle_shm = (MEMORY_HANDLE)0,
    .create_shm = 0,
    .name_gmlock = "/mp_gh125828",
    .handle_gmlock = (SEM_HANDLE)0,
    .counters = (SharedCounters *)NULL,
};

HeaderObject *header = NULL;
CounterObject *counter =  NULL;

static void reset_shm_semlock_counters(void) {
puts(__func__);

    if (shm_semlock_counters.state_this == THIS_AVAILABLE) {
        if (ACQUIRE_GENERAL_LOCK) {
            CounterObject *counter = shm_semlock_counters.counters->array_counters;
            HeaderObject *header = &shm_semlock_counters.counters->header;
            dump_shm_semlock_header_counters();
            dump_shm_semlock_header();
            memset(counter,0, sizeof(CounterObject)*header->max_slots);
            header->nb_semlocks = 0;
            dump_shm_semlock_header();
            RELEASE_GENERAL_LOCK;
        }
    }
}

int main(int argc, char *argv[]) {
    char c;

    puts("--------");
    connect_shm_semlock_counters();
    puts("+++++++++");
    dump_shm_semlock_header_counters();
    dump_shm_semlock_header();
    if (shm_semlock_counters.state_this == THIS_AVAILABLE) {
        puts("confirm (Y/N):");
        c = getchar();
        if ( c == 'Y' || c == 'y') {
            reset_shm_semlock_counters();
        }
    }
    return 1;
}