 ![language](https://img.shields.io/badge/language-C-green.svg) ![build](https://img.shields.io/badge/build-Windows-blue.svg) ![build](https://img.shields.io/badge/build-linux-FF1010.svg)

TinyLzma
===========================

A minimal LZMA data compressor & decompressor. Only hundreds of lines of C.

　

LZMA is a lossless data compression method with a higher compression ratio than Deflate and Bzip. LZMA is mainly used in ".7z" and ".xz" format.

The well-known ".zip" format also supports LZMA, although its default compression method is Deflate.

".lzma" is a very simple format for containing LZMA compressed data, which is legacy and gradually replaced by ".xz" format.

　

TinyLzma supports 3 modes:

- compress a file into a ".zip" file (compress method = LZMA)
- compress a file into a ".lzma" file
- decompress a ".lzma" file

　

## Linux Build

On Linux, run command:

```bash
gcc src/*.c -o tlzma -O3 -Wall
```

or just run the script I provide:

```bash
sh build.sh
```

The output executable file is `tlzma`

　

## Windows Build

First, you should add the Microsoft C compiler `cl.exe` (from Visual Studio or Visual C++) to environment variables. Then run command:

```powershell
cl.exe  src\*.c  /Fetlzma.exe  /Ox
```

or just run the script I provide:

```powershell
.\build.bat
```

The output executable file is `tlzma.exe`

　

## Usage

Run TinyLzma to show usage:

```
└─$ ./tlzma
  Tiny LZMA compressor & decompressor V0.1
  Source from https://github.com/WangXuan95/TinyLzma

  Usage :
     mode1 : decompress .lzma file :
       tlzma  <input_file(.lzma)>  <output_file>

     mode2 : compress a file to .lzma file :
       tlzma  <input_file>  <output_file(.lzma)>

     mode3 : compress a file to .zip file (use lzma algorithm) :
       tlzma  <input_file>  <output_file(.zip)>

  Note : on Windows, use 'tlzma.exe' instead of 'tlzma'
```

　

For example, you can using compress the file `data3.txt` in directory `testdata` to `data3.txt.zip` using command:

```bash
./tlzma testdata/data3.txt data3.txt.zip
```

The outputting ".zip" file can be extracted by other compression software, such as 7ZIP, WinZip, WinRAR, etc.

　

You can also use following command to compress a file to a ".lzma" file :

```bash
./tlzma testdata/data3.txt data3.txt.lzma
```

To verify the outputting ".lzma" file, you can decompress it using the official "XZ" tool on Linux. You should firstly install it:

```bash
apt-get install xz-utils
```

Then use following command to decompress the ".lzma" file.

```bash
xz -dk data3.txt.lzma
```

The decompressed `data3.txt` should be same as the original one.

　

You can also use following command to decompress a ".lzma" file :

```bash
./tlzma data3.txt.lzma data3.txt
```

　

## Notice

- TinyLzma is verified on hundreds of files using automatic scripts.
- To be simpler, TinyLzma loads the whole file data to memory to perform compresses/decompresses, so it is limited by memory capacity and cannot handle files that are too large.
- The search strategy of TinyLzma's compressor is very simple (no hash table or search tree), so the performance is low, and the compression ratio can only reach between `-2` and `-4` levels of the XZ tool (XZ tool has a total of 10 levels, from `-0` to `-9` . The larger, the higher the compression ratio).

　

## Related Links

- wikipedia : https://en.wikipedia.org/wiki/Lempel%E2%80%93Ziv%E2%80%93Markov_chain_algorithm
- LZMA SDK : LZMA official code and specification : https://www.7-zip.org/sdk.html