#include <unistd.h>
#include <stdio.h>  // puts, printf, scanf
#include <time.h>   // ctime, time
#include <string.h> // memcpy, memcmp

#include <semaphore.h> // sem_t
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

static char *show_counter(char *p, CounterObject *counter) {
    sprintf(p, "(n:%s, v:%d, p:%d, r:%d)", counter->sem_name,
                                           counter->internal_value,
                                           counter->n_procs,
                                           counter->reset_counter);
    return p;
}

static void dump_shm_semlock_counters(void) {
puts(__func__);

    char buf[128];
    int i = 0, j = 0;

    if (shm_semlock_counters.state_this == THIS_AVAILABLE) {
        if (ACQUIRE_GENERAL_LOCK) {
            CounterObject *counter = shm_semlock_counters.counters->array_counters;
            HeaderObject *header = &shm_semlock_counters.counters->header;
            dump_shm_semlock_header_counters();
            dump_shm_semlock_header();
            for(;i < header->max_slots && j < header->nb_semlocks;i++, counter++ ) {
                if (counter->sem_name[0] != 0) {
                    puts(show_counter(buf, counter));
                    ++j;
                }
            }
            RELEASE_GENERAL_LOCK;
        }
    }
}

int main(int argc, char *argv[]) {
    int repeat = 0;
    long udelay = 5000;
    SharedCounters save;

    puts("--------");
    connect_shm_semlock_counters();
    puts("+++++++++");
    if (argc > 1) {
        sscanf(argv[1], "%d", &repeat);
        if (argc >= 2) {
            puts(argv[2]);
            sscanf(argv[2], "%lu", &udelay);
        }
    } else {
        puts("dump_shared_mem <repeat> <delay> where:\n repeat (-1 is infinite) and delay between two dumps (us)\n");
    }

    printf("Repeat:%d, udelay:%lu\n", repeat, udelay);

    if (shm_semlock_counters.state_this == THIS_AVAILABLE) {
        memset(&save, '\0', sizeof(save));
        do {
            if (memcmp(&save, shm_semlock_counters.counters, sizeof(save)) ) {
                time_t timestamp = time( NULL );
                puts(ctime(&timestamp));
                dump_shm_semlock_counters();
                memcpy(&save, shm_semlock_counters.counters, sizeof(save));
                puts("==========");
            }
            usleep(udelay);
        } while(repeat--);
    }
    return 1;
}