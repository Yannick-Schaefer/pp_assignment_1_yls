#include "utils.h"

// Load ENTIRE file in memory
int32_t file_load(const char *file_path, uint8_t *data){
  FILE *file = fopen(file_path, "rb");
  if (!file) return -1;
  uint8_t buffer[32];
  int32_t file_size=0;
  int32_t bytes_read;
  while ((bytes_read = fread(buffer, 1, 32, file)) > 0) {
	if(file_size+32 > MAX_FILE_SIZE_B)
	  return -2;
	memcpy(data+file_size,buffer,bytes_read);
	file_size+= bytes_read;
  }
  fclose(file);
  return file_size;
}

// Save memory region to file
int32_t file_save(const char *file_path, const uint8_t *data, const int32_t data_size){
  FILE *file = fopen(file_path, "wb");
  if (!file) return -1;
  fwrite(data,sizeof(uint8_t), data_size, file);
  fclose(file);
  return 0;
}
