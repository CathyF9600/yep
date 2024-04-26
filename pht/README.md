# Parallel Hash Tables

The code creates a hash table where each entry is a linked-list data structure chaining the values hashed by threads.

## Building

Run the following commands:
    meson setup build
    meson compile -C build

## Running
```
vscode ➜ /workspaces/ece353-labs/pht (main) $ build/pht-tester -t 4 -s 200000
Generation: 107,702 usec
Hash table base: 5,005,675 usec
  - 0 missing
Hash table v1: 8,598,527 usec
  - 0 missing
Hash table v2: 2,021,347 usec
  - 0 missing
```
## First Implementation

Describe your first implementation strategy here (the one with a single mutex).
Argue why your strategy is correct.

The first implementation locks the entire hash_table_v1_add_entry function such that only one thread can add entry to the table at a time.
This expects a slower performance than the base function since essentially multi-threading is not happening.

### Performance

Run the tester such that the base hash table completes in 1-2 seconds.
Report the relative speedup (or slow down) with a low number of threads and a
high number of threads. Note that the amount of work (`-t` times `-s`) should
remain constant. Explain any differences between the two.
```
vscode ➜ /workspaces/ece353-labs/pht (main) $ ./build/pht-tester -t 2 -s $(( 50000*5 )) 
Generation: 71,085 usec
Hash table base: 940,248 usec
  - 0 missing
Hash table v1: 1,778,486 usec
  - 0 missing
Hash table v2: 718,336 usec
  - 0 missing

vscode ➜ /workspaces/ece353-labs/pht (main) $ ./build/pht-tester -t 10 -s $(( 50000 )) 
Generation: 72,449 usec
Hash table base: 927,387 usec
  - 0 missing
Hash table v1: 2,305,183 usec
  - 0 missing
Hash table v2: 418,970 usec
  - 0 missing
```
Although both cases use the single thread implementation, more threads mean more overhead from locking and unlocking the function thus take longer time.

## Second Implementation

Describe your second implementation strategy here (the one with a multiple
mutexes). Argue why your strategy is correct.

The second implementation aims to allow multiple threads to access to the hash table as long as they are not accessing the same table entry. First the mutex is initialized for each hash_table_entry when the hash table is created. When add_entry is called, we lock the entire function on a table_entry level. Many bad things could happen if we don't. If the list entry already exists for a certain key, we should lock the part where we assign a new value to it, in case another thread wants to write to it and the value from only one thread will be written. If the key doesn't previously exist, we also lock the part where we use calloc and create the new entry, since we don't want another thread to access the same table entry and in the end one thread's memory content overwrites another's. We also lock the part where we insert into the SLIST since we don't want another list entry having the same key. In terms of performance, keeping the lock on a hash_table_entry level allows parallelism.

### Performance

Run the tester such that the base hash table completes in 1-2 seconds.
Report the relative speedup with number of threads equal to the number of
physical cores on your machine (at least 4). Note again that the amount of work
(`-t` times `-s`) should remain constant. Report the speedup relative to the
base hash table implementation. Explain the difference between this
implementation and your first, which respect why you get a performance increase.
```
vscode ➜ /workspaces/ece353-labs/pht (main) $ ./build/pht-tester -t 8 -s 50000
Generation: 58,074 usec
Hash table base: 560,941 usec
  - 0 missing
Hash table v1: 1,913,784 usec
  - 0 missing
Hash table v2: 240,511 usec
  - 0 missing
```
Comparing to the base hash table, the speedup ratio is 560,941 / 240,511 ~ 2.3.
It is not really 8 since it isn't really fair to compare these two while v2 is suffering from locking overhead.
Comparing v1 and v2 thus makes more sense and the ratio is 1,913,784 / 240,511 ~ 7.96 which is very close to 8.
This is because comparing to a single-thread implementation, multiple threads improve the performance by a factor of number of threads.
```
vscode ➜ /workspaces/ece353-labs/pht (main) $ ./build/pht-tester -t 10 -s 50000
Generation: 69,373 usec
Hash table base: 1,050,887 usec
  - 0 missing
Hash table v1: 2,523,054 usec
  - 0 missing
Hash table v2: 330,369 usec
  - 0 missing
```
Similarly the speedup ratio for ten threads is also close to 10, but since more mutexes create more overhead, the relation isn't exactly linear.
