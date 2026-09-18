# Assignment 1

Executing the `make` command will build the code example and the tool for encrypting files.

- `main.c` contains the code example to decrypt a file
- `encrypt.c` contains the encryption tool implementation
- `files/` stores the encrypted files you are working with

**Important note:** If you encrypt your own files with external tools you must ensure that the proper salt and Initialization Vector are used.

# Cluster access
The Computer Science department has a cluster of nodes that can be used to run your solution. By copying your files on the front-end (ificluster.ifi.uit.no) you will be able to access them from all the other nodes (shared file system). Be mindful of your processes and remember to quit them properly, as resources are shared among all users. In order to login to the cluster from Linux, use:

`ssh <your_UiT_ID>@ificluster.ifi.uit.no`

Make sure that you are familiar with the welcome message from the cluster, helping you to use the cluster correctly.

## Run experiments
From your machine, copy your assignment folder on your homedir:
- Create a destination folder on the cluster: 

`ssh <your_UiT_ID>@ificluster.ifi.uit.no 'mkdir <assignment_folder>'`
- Copy assignment folder to cluster:

`scp -r <assignment_folder>/* <your_UiT_ID>@ificluster.ifi.uit.no:<assignment_folder>`

Create a hostfile:
- Access the cluster: `ssh <your_UiT_ID>@ificluster.ifi.uit.no`
- Go into the assignment folder: `cd <assignment_folder>`
- List available nodes `/share/ifi/available-nodes.sh`
- Put the nodes you need in `hostfile`, in the same format as the one print by the previous script
- Compile your mpi version: `make mainMPI`
- Use the `mpirun` command to run the code on the cluster using the hostfile. You must ensure a fair distribution of the processes across the nodes. You can check it by listing the processes running on the nodes.

---

# Assignment 1 solution (INF-3201)

Brute-force recovery of an AES-256 file password over the `[0-9a-zA-Z]` space,
in three tagged, reproducible versions (see `report/report.md` for the full
write-up, design and measurements).

## Versions (git tags)
| Tag              | File       | What                                                        |
|------------------|------------|-------------------------------------------------------------|
| `sequential`     | `main.c`   | Sequential baseline                                         |
| `parallel`       | `mainMPI.c`| First MPI version (block partitioning, collective stop)     |
| `optimization-1` | `mainMPI.c`| + cyclic / block-cyclic partitioning (better time-to-find)  |
| `optimization-2` | `mainMPI.c`| + non-blocking point-to-point termination (less sync)       |

The final `mainMPI.c` selects every behaviour at run time, so one binary
reproduces all milestones:

    mainMPI --partition block  --sync collective   # baseline
    mainMPI --partition cyclic --sync collective   # optimization 1
    mainMPI --partition cyclic --sync p2p          # optimization 2  (default)

## Build
    make            # main (sequential) + encrypt (provided tool), on the cluster
    make mainMPI    # MPI version (mpicc)
    # Local build with non-default OpenSSL, e.g. Homebrew on macOS:
    make main mainMPI CFLAGS="-I/opt/homebrew/opt/openssl@3/include" \
                      LDFLAGS="-L/opt/homebrew/opt/openssl@3/lib"

## Run
    ./main                                   # crack files/myfile.enc  -> "abc"
    ./main --bench 5 5000000                 # deterministic throughput benchmark
    mpirun --hostfile hostfile -np 16 ./mainMPI
    mpirun --hostfile hostfile -np 16 ./mainMPI --bench 5 5000000 --sync p2p

Data between processes flows only through the MPI API: rank 0 loads the file and
distributes it with `MPI_Bcast`; nothing is exchanged via the shared file system.

## Reproduce the experiments
    # single node:
    PROCS="1 2 4 8" REPEATS=3 scripts/run_experiments.sh
    python3 scripts/plot.py        # tables to stdout, graphs to results/
    scripts/make_target.sh <pw> files/deep "secret"   # optional deeper target

On the cluster, run the MPI version with the hostfile and place the ranks fairly
across the nodes: `mpirun --hostfile hostfile --map-by node -np <P> ./mainMPI`,
with the hostfile `slots` set to the physical cores per node (no oversubscribe).
The measurements in `report/report.md` were taken on a single node (cluster
access was unavailable).
