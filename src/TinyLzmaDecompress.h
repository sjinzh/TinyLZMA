
#ifndef   __TINY_LZMA_DECOMPRESS_H__
#define   __TINY_LZMA_DECOMPRESS_H__

#include <stdint.h>
#include <stddef.h>

int tinyLzmaDecompress (const uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len);

// return code of tinyLzmaDecompressor --------------------
#define   R_OK                           0
#define   R_ERR_UNSUPPORTED              1
#define   R_ERR_OUTPUT_OVERFLOW          2
#define   R_ERR_INPUT_OVERFLOW           3
#define   R_ERR_DATA                     4
#define   R_ERR_OUTPUT_LEN_MISMATCH      5
#define   R_ERR_NOT_YET_SUPPORTED        32

#endif // __TINY_LZMA_DECOMPRESS_H__
