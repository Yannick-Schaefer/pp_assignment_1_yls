#include "aes256.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "utils.h"


int main(int argc, char *argv[]) {

  if(argc !=3){
	printf("Usage: %s <filepath> <password>\n", argv[0]);
	return 1;
  }

  // Generate key password
  uint8_t key[AES_256_KEY_LENGTH];
  pbkdf2(argv[2],strlen(argv[2]),key);
  // Encrypt
  uint8_t *buffer1=(uint8_t*)malloc(sizeof(uint8_t)*MAX_FILE_SIZE_B);
  uint8_t *buffer2=(uint8_t*)malloc(sizeof(uint8_t)*MAX_FILE_SIZE_B);
  int32_t length=file_load(argv[1],buffer1);
  if(length<=0){
	printf("Something wrong with the file: Empty? Exists?\n");
	// Free the allocated memory
	free(buffer1);
	free(buffer2);
	return 3;
  }
  int32_t cyphertext_length=encrypt(buffer1, length, key, buffer2);
  char encname[100];
  // Store encrypted data
  strcat(encname,argv[1]);
  strcat(encname,".enc");
  file_save(encname, buffer2,cyphertext_length);
  // Compute and store sha512
  uint8_t checksum[SHA512_DIGEST_LENGTH];
  sha512sum(buffer1, length,checksum);
  encname[0]='\0';
  strcat(encname,argv[1]);
  strcat(encname,".sha512");
  file_save(encname, checksum,SHA512_DIGEST_LENGTH);
  // Free the allocated memory
  free(buffer1);
  free(buffer2);
}
