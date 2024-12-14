#ifndef SHARED_MEM_H
#define SHARED_MEM_H

extern CountersWorkaround shm_semlock_counters;
extern HeaderObject *header;
extern CounterObject *counter;

void connect_shm_semlock_counters(void);
void delete_shm_semlock_counters(void);

void dump_shm_semlock_header(void);
void dump_shm_semlock_header_counters(void);

#endif /* SHARED_MEM_H */
