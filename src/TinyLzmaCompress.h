
#ifndef   __TINY_LZMA_COMPRESS_H__
#define   __TINY_LZMA_COMPRESS_H__

#include <stdint.h>
#include <stddef.h>

int tinyLzmaCompress               (const uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len, uint8_t with_end_mark);

int tinyLzmaCompressToZipContainer (const uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len, const char *file_name_in_zip);

// return code of tinyLzmaCompressor --------------------
#define   R_OK                           0
#define   R_ERR_UNSUPPORTED              1
#define   R_ERR_OUTPUT_OVERFLOW          2

#endif // __TINY_LZMA_COMPRESS_H__
