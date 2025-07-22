#include <unistd.h>
#include <stdio.h>  // puts, printf, scanf
#include <time.h>   // ctime, time
#include <string.h> // memcpy, memcmp

#include <semaphore.h> // sem_t
typedef sem_t *SEM_HANDLE;

#define MAX_SEMAPHORES_SHOW  32

#include "../semaphore_macosx.h"
#include "shared_mem.h"

// Static datas for each process.
CountersWorkaround shm_semlock_counters = {
    .state_this = THIS_NOT_OPEN,
    .name_shm = "/shm_gh125828",
    .handle_shm = (MEMORY_HANDLE)0,
    .create_shm = 0,
    .name_shm_lock = "/mp_gh125828",
    .handle_shm_lock = (SEM_HANDLE)0,
    .header = (HeaderObject *)NULL,
    .counters = (CounterObject *)NULL,
};

HeaderObject *header = NULL;
CounterObject *counter =  NULL;

static char *show_counter(char *p, CounterObject *counter) {
    sprintf(p, "p:%p, n:%s, v:%d, u:%d"
#if Py_DEBUG
                                ", t:%s",
#endif
                                counter,
                                counter->sem_name,
                                counter->internal_value,
                                counter->unlink_done
#if Py_DEBUG
                                ,ctime(&counter->ctimestamp)
#endif
                            );
    return p;
}

static void dump_shm_semlock_counters(void) {
puts(__func__);

    char buf[256];
    int i = 0, j = 0;

    if (shm_semlock_counters.state_this == THIS_AVAILABLE) {
        CounterObject *counter = shm_semlock_counters.counters;
        HeaderObject *header = shm_semlock_counters.header;
        dump_shm_semlock_header_counters();
        dump_shm_semlock_header();
        int show_max = header->n_semlocks > MAX_SEMAPHORES_SHOW ? MAX_SEMAPHORES_SHOW : header->n_semlocks;
        for(; i < header->n_slots && j < show_max; i++, counter++ ) {
            if (counter->sem_name[0] != 0) {
                printf("%s", show_counter(buf, counter));
                ++j;
            }
        }
        if (show_max < header->n_semlocks) {
            printf("......\n--------- More %d Semphores ---------\n", header->n_semlocks-show_max);
        }
    }
}

int main(int argc, char *argv[]) {
    int repeat = 0;
    long udelay = 1000;
    HeaderObject save = {0};
    int unlink = 0;
    int force_open = 1;
    int release_lock = 1;

    puts("--------");
    SEM_HANDLE test_sem = (SEM_HANDLE)0x03;
    exist_lock(test_sem);
    printf("PID:%d, PPID:%d\n", getpid(), getppid());
    puts("+++++++++");
    if (argc > 1) {
        sscanf(argv[1], "%d", &repeat);
        if (argc >= 2) {
            puts(argv[2]);
            sscanf(argv[2], "%lu", &udelay);
        }
    } else {
        puts("dump_shared_mem <repeat> <delay> where:\n repeat (-1 "
             "is infinite) and a delay (us) between two dumps \n");
        return 1;
    }

    printf("Repeat:%d, udelay:%lu\n", repeat, udelay);

    connect_shm_semlock_counters(unlink, force_open, release_lock);
    if (shm_semlock_counters.state_this == THIS_AVAILABLE) {
        memset(&save, '\0', sizeof(save));
        do {
            if ACQUIRE_SHM_LOCK {
                if (memcmp(&save, shm_semlock_counters.header, sizeof(HeaderObject)) ) {
                    time_t timestamp = time(NULL);
                    puts(ctime(&timestamp));
                    dump_shm_semlock_counters();
                    memcpy(&save, shm_semlock_counters.header, sizeof(HeaderObject));
                    puts("==========");
                }
                //RELEASE_SHM_LOCK;
                if (sem_post(shm_semlock_counters.handle_shm_lock) < 0) {
                    break;
                }
            }
            usleep(udelay);
        } while(repeat--);
    }
    return 1;
}
