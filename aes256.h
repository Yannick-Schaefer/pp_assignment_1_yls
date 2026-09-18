#pragma once

#include <stdint.h>
#include <openssl/evp.h>

#ifndef SHA512_DIGEST_LENGTH
#define SHA512_DIGEST_LENGTH 64
#endif

#define AES_256_KEY_LENGTH (EVP_CIPHER_key_length(EVP_aes_256_cbc()))

// Generates AES-256 key from a given password
int32_t pbkdf2(const char *password, const int32_t password_len, uint8_t *key);

// Encrypt with AES-256
uint32_t encrypt(const uint8_t *plaintext, const int32_t plaintext_length, const uint8_t *key, uint8_t *ciphertext);

// Decrypt with AES-256
uint32_t decrypt(const uint8_t *ciphertext, const int32_t ciphertext_length, const uint8_t *key, uint8_t *plaintext);

/*
  sha512 should be of length SHA512_DIGEST_LENGTH (64 bytes)
*/
void sha512sum(const uint8_t *data, const int32_t data_size, uint8_t *sha512);
/*
  Compares 2 sha512 and return 0 if equal
*/
int32_t sha512cmp(uint8_t *sha1, uint8_t *sha2);
