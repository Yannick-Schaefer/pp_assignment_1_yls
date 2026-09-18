#pragma once

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define MAX_FILE_SIZE_MB 1
#define MAX_FILE_SIZE_B (1024*1024*MAX_FILE_SIZE_MB)

int32_t file_load(const char *file_path, uint8_t *data);
// Save memory region to file
int32_t file_save(const char *file_path, const uint8_t *data, const int32_t data_size);
