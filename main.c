#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "aes256.h"
#include "utils.h"
#include "crack_common.h"

// Sequential AES-256 password brute-forcer (baseline for the assignment).
// Enumerates the [0-9a-zA-Z] space with growing length until the decrypted
// plaintext matches the reference sha512, or a benchmark mode measures the
// raw candidate throughput over a fixed, deterministic slice of the space.

#define DEFAULT_ENC "./files/myfile.enc"
#define DEFAULT_SHA "./files/myfile.sha512"
#define DEFAULT_MAX_LEN 6

// Monotonic wall-clock seconds; accurate and unaffected by NTP steps.
static double now_seconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

// Full per-candidate pipeline: index -> password -> key -> decrypt -> sha512.
// Returns 1 on a checksum match (password found), 0 otherwise. `plaintext` is
// a caller-owned scratch buffer reused across calls to avoid per-try allocs.
static int try_candidate(uint64_t index, uint32_t len,
                         const uint8_t *ciphertext, int32_t ciphertext_len,
                         const uint8_t *ref_checksum,
                         uint8_t *plaintext, char *pw_out) {
  uint8_t key[AES_256_KEY_LENGTH];
  uint8_t checksum[SHA512_DIGEST_LENGTH];
  index_to_password(index, len, pw_out);
  pbkdf2(pw_out, (int32_t)len, key);
  int32_t plaintext_len = decrypt(ciphertext, ciphertext_len, key, plaintext);
  if (plaintext_len <= 0) return 0; // wrong key -> padding check fails
  sha512sum(plaintext, plaintext_len, checksum);
  return sha512cmp((uint8_t *)ref_checksum, checksum) == 0;
}

int main(int argc, char *argv[]) {
  const char *enc_path = DEFAULT_ENC;
  const char *sha_path = DEFAULT_SHA;
  uint32_t max_len = DEFAULT_MAX_LEN;
  int bench = 0;                 // benchmark mode flag
  uint32_t bench_len = 0;        // candidate length used while benchmarking
  uint64_t bench_count = 0;      // number of candidates to scan in benchmark

  // Arg forms: main [enc sha [maxlen]]
  //            main --bench <len> <count> [enc sha]
  if (argc >= 2 && strcmp(argv[1], "--bench") == 0) {
    if (argc < 4) { fprintf(stderr, "Usage: %s --bench <len> <count> [enc sha]\n", argv[0]); return 1; }
    bench = 1;
    bench_len = (uint32_t)strtoul(argv[2], NULL, 10);
    bench_count = strtoull(argv[3], NULL, 10);
    if (argc >= 6) { enc_path = argv[4]; sha_path = argv[5]; }
  } else {
    if (argc >= 3) { enc_path = argv[1]; sha_path = argv[2]; }
    if (argc >= 4) max_len = (uint32_t)strtoul(argv[3], NULL, 10);
  }

  // Load the ciphertext and the reference checksum once into heap buffers.
  uint8_t *ciphertext = (uint8_t *)malloc(MAX_FILE_SIZE_B);
  uint8_t *plaintext = (uint8_t *)malloc(MAX_FILE_SIZE_B);
  uint8_t ref_checksum[SHA512_DIGEST_LENGTH];
  if (!ciphertext || !plaintext) { fprintf(stderr, "Out of memory\n"); return 2; }
  int32_t ciphertext_len = file_load(enc_path, ciphertext);
  if (ciphertext_len <= 0) { fprintf(stderr, "Cannot load %s\n", enc_path); return 3; }
  if (file_load(sha_path, ref_checksum) != SHA512_DIGEST_LENGTH) {
    fprintf(stderr, "Cannot load reference checksum %s\n", sha_path); return 3;
  }

  char pw[16];

  // --- Benchmark mode: deterministic full scan, no early exit. ---
  if (bench) {
    double t0 = now_seconds();
    uint64_t hits = 0;
    for (uint64_t i = 0; i < bench_count; i++)
      hits += try_candidate(i, bench_len, ciphertext, ciphertext_len,
                            ref_checksum, plaintext, pw);
    double elapsed = now_seconds() - t0;
    printf("[bench] len=%u count=%llu time=%.6f s throughput=%.0f cand/s hits=%llu\n",
           bench_len, (unsigned long long)bench_count, elapsed,
           bench_count / elapsed, (unsigned long long)hits);
    free(ciphertext); free(plaintext);
    return 0;
  }

  // --- Crack mode: grow the length until the password is found. ---
  double t0 = now_seconds();
  int found = 0;
  for (uint32_t len = 1; len <= max_len && !found; len++) {
    uint64_t space = pow62(len);
    for (uint64_t i = 0; i < space; i++) {
      if (try_candidate(i, len, ciphertext, ciphertext_len,
                        ref_checksum, plaintext, pw)) {
        pw[len] = '\0';
        double elapsed = now_seconds() - t0;
        printf("Password found: \"%s\" (length %u)\n", pw, len);
        printf("Candidates tried up to match: %llu | time: %.6f s\n",
               (unsigned long long)(i + 1), elapsed);
        found = 1;
        break;
      }
    }
  }
  if (!found)
    printf("Password not found up to length %u\n", max_len);

  free(ciphertext); free(plaintext);
  return found ? 0 : 4;
}
