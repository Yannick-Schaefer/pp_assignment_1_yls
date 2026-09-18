#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include "aes256.h"
#include "utils.h"
#include "crack_common.h"

// Parallel AES-256 password brute-forcer (MPI, final version).
// Rank 0 loads the ciphertext and reference digest and shares them with
// MPI_Bcast, so data between processes flows only through MPI, never NFS.
// Flags select the milestone the measurements compare:
//   baseline       --partition block  --sync collective
//   optimization 1 --partition cyclic --sync collective   (better time-to-find)
//   optimization 2 --partition cyclic --sync p2p          (default)
// The collective sync calls MPI_Allreduce every `interval` candidates (a global
// barrier). The p2p sync only does a local MPI_Iprobe per interval and notifies
// the others with MPI_Isend once the password is found, so no global barrier
// runs during the search.

#define DEFAULT_ENC "./files/myfile.enc"
#define DEFAULT_SHA "./files/myfile.sha512"
#define DEFAULT_MAX_LEN 6
#define DEFAULT_INTERVAL 4096
#define NO_HIT UINT64_MAX
#define TAG_STOP 1

typedef enum { PART_BLOCK, PART_BC } part_t;  // BC covers cyclic (blk==1)
typedef enum { SYNC_COLL, SYNC_P2P } sync_t;

// Map local position j of `rank` to a global candidate index; *valid is 0 when
// j is past this rank's assigned candidates.
static inline uint64_t loc2glob(part_t mode, int rank, int size, uint64_t blk,
                                uint64_t j, uint64_t space,
                                uint64_t blk_lo, uint64_t blk_cnt, int *valid) {
  if (mode == PART_BLOCK) { *valid = (j < blk_cnt); return blk_lo + j; }
  uint64_t m = j / blk, o = j % blk;
  uint64_t g = ((uint64_t)rank + m * (uint64_t)size) * blk + o;
  *valid = (g < space);
  return g;
}

// Upper bound of candidates per rank (identical on every rank).
static uint64_t upper_bound(part_t mode, uint64_t space, int size, uint64_t blk) {
  if (mode == PART_BLOCK) return space / size + (space % size ? 1 : 0);
  uint64_t total_blocks = (space + blk - 1) / blk;
  return ((total_blocks + size - 1) / size) * blk;
}

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

// Drain any delivered stop messages so nothing lingers before MPI_Finalize.
static void drain_stops(void) {
  int flag; MPI_Status st; uint64_t junk;
  do {
    MPI_Iprobe(MPI_ANY_SOURCE, TAG_STOP, MPI_COMM_WORLD, &flag, &st);
    if (flag) MPI_Recv(&junk, 1, MPI_UINT64_T, st.MPI_SOURCE, TAG_STOP, MPI_COMM_WORLD, &st);
  } while (flag);
}

int main(int argc, char *argv[]) {
  MPI_Init(&argc, &argv);
  int rank, size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  const char *enc_path = DEFAULT_ENC, *sha_path = DEFAULT_SHA;
  uint32_t max_len = DEFAULT_MAX_LEN;
  uint64_t interval = DEFAULT_INTERVAL, blk = 1;
  part_t mode = PART_BC;   // optimized defaults
  sync_t sync = SYNC_P2P;
  int bench = 0; uint32_t bench_len = 0; uint64_t bench_count = 0;

  // Args: [--maxlen L] [--interval N] [--partition block|cyclic|blockcyclic]
  //       [--block B] [--sync collective|p2p] [--bench <len> <count>] [enc sha]
  for (int a = 1; a < argc; a++) {
    if (!strcmp(argv[a], "--bench") && a + 2 < argc) {
      bench = 1; bench_len = (uint32_t)strtoul(argv[++a], NULL, 10);
      bench_count = strtoull(argv[++a], NULL, 10);
    } else if (!strcmp(argv[a], "--maxlen") && a + 1 < argc) {
      max_len = (uint32_t)strtoul(argv[++a], NULL, 10);
    } else if (!strcmp(argv[a], "--interval") && a + 1 < argc) {
      interval = strtoull(argv[++a], NULL, 10);
    } else if (!strcmp(argv[a], "--block") && a + 1 < argc) {
      blk = strtoull(argv[++a], NULL, 10);
    } else if (!strcmp(argv[a], "--partition") && a + 1 < argc) {
      const char *p = argv[++a];
      if (!strcmp(p, "block")) mode = PART_BLOCK;
      else if (!strcmp(p, "cyclic")) { mode = PART_BC; blk = 1; }
      else if (!strcmp(p, "blockcyclic")) mode = PART_BC;
    } else if (!strcmp(argv[a], "--sync") && a + 1 < argc) {
      sync = strcmp(argv[++a], "p2p") == 0 ? SYNC_P2P : SYNC_COLL;
    } else if (argv[a][0] != '-') {
      enc_path = argv[a];
      if (a + 1 < argc) sha_path = argv[++a];
    }
  }
  if (interval == 0) interval = 1;
  if (blk == 0) blk = 1;

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

  // --- Benchmark mode: deterministic full scan, no early exit. ---
  if (bench) {
    uint64_t space = bench_count;
    uint64_t chunk = space / size, rem = space % size;
    uint64_t blk_lo = (uint64_t)rank * chunk + (rank < (int)rem ? rank : rem);
    uint64_t blk_cnt = chunk + (rank < (int)rem ? 1 : 0);
    uint64_t ub = upper_bound(mode, space, size, blk);
    uint64_t rounds = (ub + interval - 1) / interval;
    uint64_t hits = 0, dummy;
    for (uint64_t r = 0; r < rounds; r++) {
      uint64_t js = r * interval, je = js + interval; if (je > ub) je = ub;
      for (uint64_t j = js; j < je; j++) {
        int valid; uint64_t g = loc2glob(mode, rank, size, blk, j, space, blk_lo, blk_cnt, &valid);
        if (valid) hits += try_candidate(g, bench_len, ciphertext, ciphertext_len, ref_checksum, plaintext);
      }
      if (sync == SYNC_COLL) {
        MPI_Allreduce(&hits, &dummy, 1, MPI_UINT64_T, MPI_MAX, MPI_COMM_WORLD);
      } else { // p2p: only a cheap local probe per interval
        int flag; MPI_Status st;
        MPI_Iprobe(MPI_ANY_SOURCE, TAG_STOP, MPI_COMM_WORLD, &flag, &st);
      }
    }
    double local = MPI_Wtime() - t0, wall; uint64_t total_hits;
    MPI_Reduce(&local, &wall, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&hits, &total_hits, 1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
    if (rank == 0)
      printf("[bench] procs=%d len=%u count=%llu interval=%llu part=%s blk=%llu sync=%s "
             "time=%.6f s throughput=%.0f cand/s hits=%llu\n", size, bench_len,
             (unsigned long long)bench_count, (unsigned long long)interval,
             mode == PART_BLOCK ? "block" : (blk == 1 ? "cyclic" : "blockcyclic"),
             (unsigned long long)blk, sync == SYNC_COLL ? "collective" : "p2p",
             wall, bench_count / wall, (unsigned long long)total_hits);
    free(ciphertext); free(plaintext); MPI_Finalize();
    return 0;
  }

  // --- Crack mode: grow length; partition each length across ranks. ---
  MPI_Request *reqs = (MPI_Request *)malloc((size_t)(size + 1) * sizeof(MPI_Request));
  uint64_t gwin = NO_HIT; uint32_t win_len = 0;
  for (uint32_t len = 1; len <= max_len && gwin == NO_HIT; len++) {
    uint64_t space = pow62(len);
    uint64_t chunk = space / size, rem = space % size;
    uint64_t blk_lo = (uint64_t)rank * chunk + (rank < (int)rem ? rank : rem);
    uint64_t blk_cnt = chunk + (rank < (int)rem ? 1 : 0);
    uint64_t ub = upper_bound(mode, space, size, blk);
    uint64_t rounds = (ub + interval - 1) / interval;
    uint64_t win = NO_HIT;
    int stopped = 0, nreq = 0;
    for (uint64_t r = 0; r < rounds && !stopped; r++) {
      uint64_t js = r * interval, je = js + interval; if (je > ub) je = ub;
      for (uint64_t j = js; j < je && win == NO_HIT; j++) {
        int valid; uint64_t g = loc2glob(mode, rank, size, blk, j, space, blk_lo, blk_cnt, &valid);
        if (valid && try_candidate(g, len, ciphertext, ciphertext_len, ref_checksum, plaintext)) { win = g; break; }
      }
      if (sync == SYNC_COLL) {
        MPI_Allreduce(&win, &gwin, 1, MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD);
        if (gwin != NO_HIT) { win_len = len; stopped = 1; }
      } else { // p2p
        if (win != NO_HIT) { // found: notify all others once, then stop
          for (int d = 0; d < size; d++)
            if (d != rank) MPI_Isend(&win, 1, MPI_UINT64_T, d, TAG_STOP, MPI_COMM_WORLD, &reqs[nreq++]);
          stopped = 1;
        } else {             // cheap local check for an incoming stop
          int flag; MPI_Status st;
          MPI_Iprobe(MPI_ANY_SOURCE, TAG_STOP, MPI_COMM_WORLD, &flag, &st);
          if (flag) { uint64_t jj; MPI_Recv(&jj, 1, MPI_UINT64_T, st.MPI_SOURCE, TAG_STOP, MPI_COMM_WORLD, &st); win = jj; stopped = 1; }
        }
      }
    }

    if (sync == SYNC_P2P) {
      // Single collective agreement on the global result for this length.
      MPI_Allreduce(&win, &gwin, 1, MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD);
      if (gwin != NO_HIT) win_len = len;
      // Guaranteed cleanup: drain delivered stops and complete pending sends.
      int alldone_g;
      do {
        drain_stops();
        int td = 1; if (nreq) MPI_Testall(nreq, reqs, &td, MPI_STATUSES_IGNORE);
        int ld = td ? 1 : 0;
        MPI_Allreduce(&ld, &alldone_g, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
      } while (!alldone_g);
    }
  }
  free(reqs);

  double local = MPI_Wtime() - t0, wall;
  MPI_Reduce(&local, &wall, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  if (rank == 0) {
    if (gwin != NO_HIT) {
      char pw[16]; index_to_password(gwin, win_len, pw); pw[win_len] = '\0';
      printf("Password found: \"%s\" (length %u) | procs=%d time=%.6f s\n", pw, win_len, size, wall);
    } else {
      printf("Password not found up to length %u | procs=%d time=%.6f s\n", max_len, size, wall);
    }
  }

  free(ciphertext); free(plaintext);
  MPI_Finalize();
  return 0;
}
