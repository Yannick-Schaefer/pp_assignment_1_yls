#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string.h>
#include <openssl/sha.h>

unsigned char PBKDF2_SALT[32] = {
  0xA6, 0x67, 0x21, 0xB3, 0xA6, 0x67, 0x21, 0xB3,
  0xA6, 0x67, 0x21, 0xB3, 0xA6, 0x67, 0x21, 0xB3,
  0xA6, 0x67, 0x21, 0xB3, 0xA6, 0x67, 0x21, 0xB3,
  0xA6, 0x67, 0x21, 0xB3, 0xA6, 0x67, 0x21, 0xB3,
};

const int PBKDF2_ITER = 10;

unsigned char AES_256_IV[16] = {
  0xA6, 0x67, 0x21, 0xB3, 0xA6, 0x67, 0x21, 0xB3,
  0xA6, 0x67, 0x21, 0xB3, 0xA6, 0x67, 0x21, 0xB3,
};

// Perform key derivation using KDF
int32_t pbkdf2(const char *password, const int32_t password_len, uint8_t *key){
  int status=PKCS5_PBKDF2_HMAC(
					password,
					password_len,
					PBKDF2_SALT,
					32, // Salt size
					PBKDF2_ITER,
					EVP_sha256(),
					32, // Size of the key to generate
					key);
  return !status ? 1: 0;
}

void sha512sum(const uint8_t *data, const int32_t data_size, uint8_t *sha512){
  SHA512(data, data_size, sha512);
}

uint32_t encrypt(const uint8_t *plaintext, const int32_t plaintext_length, const uint8_t *key, uint8_t *ciphertext){
  int offset;
  int ciphertext_length;
  // Create context
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  // Setup context
  EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, AES_256_IV);
  // Start encryption
  EVP_EncryptUpdate(ctx, ciphertext, &offset, plaintext, plaintext_length);
  // Store current encryption offset
  ciphertext_length = offset;
  // Complete encryption
  EVP_EncryptFinal_ex(ctx, ciphertext + offset, &offset);
  ciphertext_length += offset;
  // Free context
  EVP_CIPHER_CTX_free(ctx);
  return ciphertext_length;
}


uint32_t decrypt(const uint8_t *ciphertext, const int32_t ciphertext_length, const uint8_t *key, uint8_t *plaintext){
  int length;
  int plaintext_length;
  // Create context
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  // Setup decryption
  EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, AES_256_IV);
  // Start decryption
  EVP_DecryptUpdate(ctx, plaintext, &length, ciphertext, ciphertext_length);
  // Store current decryption offset
  plaintext_length = length;
  // Complete decryption
  if (EVP_DecryptFinal_ex(ctx, plaintext + length, &length) <= 0) {
	EVP_CIPHER_CTX_free(ctx);
	return 0; // Something wrong happened
  }
  plaintext_length += length;
  // Free context
  EVP_CIPHER_CTX_free(ctx);
  return plaintext_length;
}

// Compare if 2 sha512 are equals
int32_t sha512cmp(uint8_t *sha1, uint8_t *sha2){
  for (int32_t i = 0; i < SHA512_DIGEST_LENGTH ; i++){
	if (sha1[i] != sha2[i])
	  return 1;
  }
  return 0;
}
