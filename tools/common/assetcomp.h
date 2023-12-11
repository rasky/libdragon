#ifndef COMMON_ASSETCOMP_H
#define COMMON_ASSETCOMP_H

#include <stdint.h>
#include <stdbool.h>

#define DEFAULT_COMPRESSION     1
#define MAX_COMPRESSION         3

// Default window size for streaming decompression (asset_fopen())
#define DEFAULT_WINSIZE_STREAMING    (4*1024)

extern bool asset_write_header;

bool asset_compress(const char *infn, const char *outfn, int compression, int winsize);
void asset_compress_mem(int compression, const uint8_t *inbuf, int size, uint8_t **outbuf, int *cmp_size, int *winsize, int *margin);

#endif
