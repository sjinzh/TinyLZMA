
#include <stdlib.h>
#include <stdio.h>

#include "FileIO.h"
#include "TinyLzmaCompress.h"
#include "TinyLzmaDecompress.h"



static int strEndswith (const char *pattern, const char *string) {
    char ch1, ch2;
    const char *p1 = pattern;
    const char *p2 = string;
    
    for (; *p1; p1++);
    for (; *p2; p2++);
    
    for (; p1>=pattern; p1--, p2--) {
        if (p2 < string)
            return 0;
        ch1 = *p1;
        ch2 = *p2;
        if (ch1 >= 'a' && ch1 <= 'z')
            ch1 -= ('a' - 'A');
        if (ch2 >= 'a' && ch2 <= 'z')
            ch2 -= ('a' - 'A');
        if (ch1 != ch2)
            return 0;
    }
    
    return 1;
}



static void removeDirectoryPathFromFileName (char *fname) {
    char *psrc = fname;
    char *pdst = fname;
    
    for (; *psrc; psrc++) {
        *pdst = *psrc;
        if (*psrc == '/' || *psrc == '\\')      // '/' is file sep of linux, '\' is file sep of windows
            pdst = fname;                       // back to base
        else
            pdst ++;
    };
    
    *pdst = '\0';
}



const char *USAGE =
    "  Tiny LZMA compressor & decompressor V0.1\n"
    "  Source from https://github.com/WangXuan95/TinyLzma\n"
    "\n"
    "  Usage : \n"
    "     mode1 : decompress .lzma file : \n"
    "       tlzma  <input_file(.lzma)>  <output_file>\n"
    "\n"
    "     mode2 : compress a file to .lzma file : \n"
    "       tlzma  <input_file>  <output_file(.lzma)>\n"
    "\n"
    "     mode3 : compress a file to .zip file (use lzma algorithm) : \n"
    "       tlzma  <input_file>  <output_file(.zip)>\n"
    "\n"
    "  Note : on Windows, use 'tlzma.exe' instead of 'tlzma'\n"
    "\n"
;



#define   DECOMPRESS_OUTPUT_MAX_LEN            0x20000000UL


int main(int argc, char **argv) {
    char    *fname_src = NULL;
    char    *fname_dst = NULL;
    uint8_t *p_src  , *p_dst;
    size_t   src_len,  dst_len;
    int      res, mode;
    
    if (argc < 3) {
        printf(USAGE);
        return -1;
    }
    
    fname_src = argv[1];
    fname_dst = argv[2];
    
    printf("input  file name = %s\n", fname_src);
    printf("output file name = %s\n", fname_dst);
    
    
    if        ( strEndswith(".zip" , fname_dst) ) {
        mode = 3;
        printf("mode             = 3 (compress to .zip file)\n");
    } else if ( strEndswith(".lzma", fname_dst) ) {
        mode = 2;
        printf("mode             = 2 (compress to .lzma file)\n");
    } else if ( strEndswith(".lzma", fname_src) ) {
        mode = 1;
        printf("mode             = 1 (decompress .lzma file)\n");
    } else {
        printf(USAGE);
        printf("*** error : unsupported command\n");
        return -1;
    }
    
    
    p_src = loadFromFile(&src_len, fname_src);
    
    if (p_src == NULL) {
        printf("*** error : load file %s failed\n", fname_src);
        return -1;
    }
    
    printf("input  length    = %lu\n", src_len);
    
    
    switch (mode) {
        case 1  :
            dst_len = DECOMPRESS_OUTPUT_MAX_LEN;
            break;
        case 2  : 
        default :  // case 3 : 
            dst_len = src_len + (src_len>>2) + 4096;
            if (dst_len < src_len)                                            // size_t data type overflow
                dst_len = (~((size_t)0));                                     // max value of size_t
            break;
    }
    
    
    p_dst = (uint8_t*)malloc(dst_len);
    
    if (p_dst == NULL) {
        free(p_src);
        printf("*** error : allocate output buffer failed\n");
        return -1;
    }
    
    
    switch (mode) {
        case 1  : 
            res = tinyLzmaDecompress(p_src, src_len, p_dst, &dst_len);
            break;
        case 2  :
            res = tinyLzmaCompress  (p_src, src_len, p_dst, &dst_len, 1);
            break;
        default :  // case 3 :
            removeDirectoryPathFromFileName(fname_src);
            res = tinyLzmaCompressToZipContainer(p_src, src_len, p_dst, &dst_len, fname_src);
            break;
    }
    
    free(p_src);
    
    if (res) {
        free(p_dst);
        printf("*** error : failed (return_code = %d)\n", res);
        return res;
    }
    
    
    printf("output length    = %lu\n", dst_len);
    
    if (saveToFile(p_dst, dst_len, fname_dst) < 0) {
        free(p_dst);
        printf("*** error : save file %s failed\n", fname_dst);
        return -1;
    }
    
    free(p_dst);
    
    return R_OK;
}

