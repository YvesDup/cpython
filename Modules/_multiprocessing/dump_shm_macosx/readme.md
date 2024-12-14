** For MacOSX only **
---
This directory contains 2 programs :

* dump_shared_mem: view content of shared memory.
* reset_shared_mem: erase all stored datas of shared memory.

the make_all.sh builds 2 programs.

This directory contains a python script that creates
a BoundedSemaphore. This is script is to run in order tu open the sharedd memory.

Runs 
```zsh 
dump_shm -1 1500
``` 
Executes this program forever, and check all 1500 *us* if sharerd memory changes.

When changes print the new content of shared memory as below (Interrupts with ctrl+C):

```zsh
Fri Feb  7 10:01:26 2025

dump_shm_semlock_counters
header:0x1043c8000 - counter array:0x1043c801c
nb sems:6 - nb sem slots:291, size_shm:8176
(n:/mp-y6btb9yi, v:32767, p:1, r:0)
(n:/mp-w4ivzk09, v:32767, p:1, r:0)
(n:/mp-lteqb5_v, v:0, p:1, r:0)
(n:/mp-fvndixyd, v:0, p:1, r:0)
(n:/mp-cpioqg2i, v:0, p:1, r:0)
(n:/mp-h_3rlywc, v:0, p:1, r:0)
==========
```