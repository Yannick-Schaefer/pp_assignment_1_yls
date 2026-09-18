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
