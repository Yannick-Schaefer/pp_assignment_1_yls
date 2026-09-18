#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include "aes256.h"
#include "utils.h"
#include "crack_common.h"

// Parallel AES-256 password brute-forcer (MPI baseline).
// Rank 0 loads the ciphertext and reference checksum and distributes them via
// MPI_Bcast (inter-process data flows through the MPI API only, never NFS).
// The [0-9a-zA-Z] space of each length is split by BLOCK partitioning; early
// termination is coordinated with a round-synchronized collective: every
// `interval` candidates all ranks call MPI_Allreduce(MIN) on their best hit,
// so the collective is invoked the same number of times on every rank.

#define DEFAULT_ENC "./files/myfile.enc"
#define DEFAULT_SHA "./files/myfile.sha512"
#define DEFAULT_MAX_LEN 6
#define DEFAULT_INTERVAL 4096
#define NO_HIT UINT64_MAX

// Full per-candidate pipeline; returns 1 on checksum match, 0 otherwise.
static int try_candidate(uint64_t index, uint32_t len,
                         const uint8_t *ciphertext, int32_t ciphertext_len,
                         const uint8_t *ref_checksum, uint8_t *plaintext) {
  uint8_t key[AES_256_KEY_LENGTH];
  uint8_t checksum[SHA512_DIGEST_LENGTH];
  char pw[16];
  index_to_password(index, len, pw);
  pbkdf2(pw, (int32_t)len, key);
  int32_t plaintext_len = decrypt(ciphertext, ciphertext_len, key, plaintext);
  if (plaintext_len <= 0) return 0;
  sha512sum(plaintext, plaintext_len, checksum);
  return sha512cmp((uint8_t *)ref_checksum, checksum) == 0;
}

int main(int argc, char *argv[]) {
  MPI_Init(&argc, &argv);
  int rank, size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  const char *enc_path = DEFAULT_ENC;
  const char *sha_path = DEFAULT_SHA;
  uint32_t max_len = DEFAULT_MAX_LEN;
  uint64_t interval = DEFAULT_INTERVAL;
  int bench = 0; uint32_t bench_len = 0; uint64_t bench_count = 0;

  // Arg forms: mainMPI [--maxlen L] [--interval N] [enc sha]
  //            mainMPI --bench <len> <count> [--interval N] [enc sha]
  for (int a = 1; a < argc; a++) {
    if (!strcmp(argv[a], "--bench") && a + 2 < argc) {
      bench = 1; bench_len = (uint32_t)strtoul(argv[++a], NULL, 10);
      bench_count = strtoull(argv[++a], NULL, 10);
    } else if (!strcmp(argv[a], "--maxlen") && a + 1 < argc) {
      max_len = (uint32_t)strtoul(argv[++a], NULL, 10);
    } else if (!strcmp(argv[a], "--interval") && a + 1 < argc) {
      interval = strtoull(argv[++a], NULL, 10);
    } else if (argv[a][0] != '-') {
      enc_path = argv[a];
      if (a + 1 < argc) sha_path = argv[++a];
    }
  }
  if (interval == 0) interval = 1;

  // --- Rank 0 loads input; everything else is distributed over MPI. ---
  uint8_t *ciphertext = (uint8_t *)malloc(MAX_FILE_SIZE_B);
  uint8_t *plaintext = (uint8_t *)malloc(MAX_FILE_SIZE_B);
  uint8_t ref_checksum[SHA512_DIGEST_LENGTH];
  int ciphertext_len = 0;
  if (rank == 0) {
    ciphertext_len = file_load(enc_path, ciphertext);
    if (ciphertext_len <= 0 ||
        file_load(sha_path, ref_checksum) != SHA512_DIGEST_LENGTH) {
      fprintf(stderr, "Rank 0: cannot load %s / %s\n", enc_path, sha_path);
      ciphertext_len = -1;
    }
  }
  MPI_Bcast(&ciphertext_len, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (ciphertext_len <= 0) { free(ciphertext); free(plaintext); MPI_Finalize(); return 3; }
  MPI_Bcast(ciphertext, ciphertext_len, MPI_BYTE, 0, MPI_COMM_WORLD);
  MPI_Bcast(ref_checksum, SHA512_DIGEST_LENGTH, MPI_BYTE, 0, MPI_COMM_WORLD);

  MPI_Barrier(MPI_COMM_WORLD);
  double t0 = MPI_Wtime();

  // --- Benchmark mode: full deterministic scan, no early exit. ---
  if (bench) {
    uint64_t chunk = bench_count / size, rem = bench_count % size;
    uint64_t lo = (uint64_t)rank * chunk + (rank < (int)rem ? rank : rem);
    uint64_t cnt = chunk + (rank < (int)rem ? 1 : 0);
    uint64_t max_cnt = chunk + (rem > 0 ? 1 : 0);
    uint64_t rounds = (max_cnt + interval - 1) / interval;
    uint64_t hits = 0, dummy;
    for (uint64_t r = 0; r < rounds; r++) {
      uint64_t js = r * interval, je = js + interval; if (je > cnt) je = cnt;
      for (uint64_t j = js; j < je; j++)
        hits += try_candidate(lo + j, bench_len, ciphertext, ciphertext_len,
                              ref_checksum, plaintext);
      MPI_Allreduce(&hits, &dummy, 1, MPI_UINT64_T, MPI_MAX, MPI_COMM_WORLD);
    }
    double local = MPI_Wtime() - t0, wall;
    uint64_t total_hits;
    MPI_Reduce(&local, &wall, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&hits, &total_hits, 1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
    if (rank == 0)
      printf("[bench] procs=%d len=%u count=%llu interval=%llu time=%.6f s "
             "throughput=%.0f cand/s hits=%llu\n", size, bench_len,
             (unsigned long long)bench_count, (unsigned long long)interval,
             wall, bench_count / wall, (unsigned long long)total_hits);
    free(ciphertext); free(plaintext); MPI_Finalize();
    return 0;
  }

  // --- Crack mode: grow length; block-partition each length across ranks. ---
  uint64_t gwin = NO_HIT; uint32_t win_len = 0;
  for (uint32_t len = 1; len <= max_len && gwin == NO_HIT; len++) {
    uint64_t space = pow62(len);
    uint64_t chunk = space / size, rem = space % size;
    uint64_t lo = (uint64_t)rank * chunk + (rank < (int)rem ? rank : rem);
    uint64_t cnt = chunk + (rank < (int)rem ? 1 : 0);
    uint64_t max_cnt = chunk + (rem > 0 ? 1 : 0);
    uint64_t rounds = (max_cnt + interval - 1) / interval; // identical on all ranks
    uint64_t win = NO_HIT;
    for (uint64_t r = 0; r < rounds; r++) {
      uint64_t js = r * interval, je = js + interval; if (je > cnt) je = cnt;
      for (uint64_t j = js; j < je && win == NO_HIT; j++) {
        if (try_candidate(lo + j, len, ciphertext, ciphertext_len,
                          ref_checksum, plaintext)) { win = lo + j; break; }
      }
      MPI_Allreduce(&win, &gwin, 1, MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD);
      if (gwin != NO_HIT) { win_len = len; break; }
    }
  }

  double local = MPI_Wtime() - t0, wall;
  MPI_Reduce(&local, &wall, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  if (rank == 0) {
    if (gwin != NO_HIT) {
      char pw[16];
      index_to_password(gwin, win_len, pw); pw[win_len] = '\0';
      printf("Password found: \"%s\" (length %u) | procs=%d time=%.6f s\n",
             pw, win_len, size, wall);
    } else {
      printf("Password not found up to length %u | procs=%d time=%.6f s\n",
             max_len, size, wall);
    }
  }

  free(ciphertext); free(plaintext);
  MPI_Finalize();
  return 0;
}
