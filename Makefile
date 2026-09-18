# Build configuration.
# On the IFI cluster (Linux, OpenSSL in the default path) the defaults work:
#   make          # builds main + encrypt
#   make mainMPI  # builds the MPI version
# For a local build with OpenSSL outside the default path, override the flags:
#   make CFLAGS="-I/opt/homebrew/opt/openssl@3/include" \
#        LDFLAGS="-L/opt/homebrew/opt/openssl@3/lib"

CC      ?= gcc
MPICC   ?= mpicc
CFLAGS  ?=
LDFLAGS ?=
LDLIBS  := -lcrypto

COMMON  := aes256.c utils.c
HEADERS := aes256.h utils.h crack_common.h

all: main encrypt

# Sequential brute-forcer (baseline).
main: main.c $(COMMON) $(HEADERS)
	$(CC) $(CFLAGS) main.c $(COMMON) -o $@ $(LDFLAGS) $(LDLIBS)

# Provided file-encryption tool.
encrypt: encrypt.c $(COMMON) $(HEADERS)
	$(CC) $(CFLAGS) encrypt.c $(COMMON) -o $@ $(LDFLAGS) $(LDLIBS)

# Parallel MPI brute-forcer.
mainMPI: mainMPI.c $(COMMON) $(HEADERS)
	$(MPICC) $(CFLAGS) mainMPI.c $(COMMON) -o $@ $(LDFLAGS) $(LDLIBS)

# Convenience aliases.
seq: main
mpi: mainMPI

clean:
	rm -f main encrypt mainMPI

.PHONY: all seq mpi clean
