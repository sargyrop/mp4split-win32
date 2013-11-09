/*******************************************************************************
 moov.h (version 2)

 moov - A library for splitting Quicktime/MPEG4 files.
 http://h264.code-shop.com

 Copyright (C) 2007-2009 CodeShop B.V.

 Licensing
 The H264 Streaming Module is licened under a Creative Commons License. It
 allows you to use, modify and redistribute the module, but only for
 *noncommercial* purposes. For corporate use, please apply for a
 commercial license.

 Creative Commons License:
 http://creativecommons.org/licenses/by-nc-sa/3.0/

 Commercial License:
 http://h264.code-shop.com/trac/wiki/Mod-H264-Streaming-License-Version2
******************************************************************************/ 

#ifndef MOOV_H_AKW
#define MOOV_H_AKW

// NOTE: don't include stdio.h (for FILE) or sys/types.h (for off_t).
// nginx redefines _FILE_OFFSET_BITS and off_t will have different sizes
// depending on include order

#include "mod_streaming_export.h"

#ifndef _MSC_VER
#include <inttypes.h>
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum fragment_type_t
{
  FRAGMENT_TYPE_UNKNOWN,
  FRAGMENT_TYPE_AUDIO,
  FRAGMENT_TYPE_VIDEO
};

enum output_format_t
{
  OUTPUT_FORMAT_MP4,
  OUTPUT_FORMAT_RAW,
  OUTPUT_FORMAT_FLV
};

struct mp4_split_options_t
{
  int client_is_flash;
  float start;
  float end;
  int adaptive;
  int fragments;
  int manifest;
  enum fragment_type_t fragment_type;
  enum output_format_t output_format;
  uint64_t fragment_start;
  int seconds;
  uint64_t* byte_offsets;
};
typedef struct mp4_split_options_t mp4_split_options_t;

enum bucket_type_t
{
  BUCKET_TYPE_MEMORY,
  BUCKET_TYPE_FILE,
};
typedef enum bucket_type_t bucket_type_t;

struct bucket_t
{
  int type_;
//  union {
    void* buf_;
    uint64_t offset_;
//  };
  uint64_t size_;
  struct bucket_t* prev_;
  struct bucket_t* next_;
};
typedef struct bucket_t bucket_t;
MOD_STREAMING_DLL_LOCAL extern bucket_t* bucket_init(bucket_type_t bucket_type);
MOD_STREAMING_DLL_LOCAL extern
bucket_t* bucket_init_memory(void const* buf, uint64_t size);
MOD_STREAMING_DLL_LOCAL extern
bucket_t* bucket_init_file(uint64_t offset, uint64_t size);
MOD_STREAMING_DLL_LOCAL extern
void buckets_exit(struct bucket_t* buckets);
MOD_STREAMING_DLL_LOCAL extern
void bucket_insert_tail(bucket_t** head, bucket_t* bucket);
MOD_STREAMING_DLL_LOCAL extern
void bucket_insert_head(bucket_t** head, bucket_t* bucket);

struct mp4_files_t
{
  char* name_;
};

MOD_STREAMING_DLL_LOCAL extern
int mp4_scanfiles(const char* input_file, unsigned int* files,
                  struct mp4_files_t* filespecs);

MOD_STREAMING_DLL_LOCAL extern
mp4_split_options_t* mp4_split_options_init();
MOD_STREAMING_DLL_LOCAL extern
int mp4_split_options_set(mp4_split_options_t* options,
                          const char* args_data,
                          unsigned int args_size);
MOD_STREAMING_DLL_LOCAL extern
void mp4_split_options_exit(mp4_split_options_t* options);

struct mp4_context_t;

MOD_STREAMING_DLL_LOCAL extern
int mp4_split(struct mp4_context_t* mp4_context,
              unsigned int* trak_sample_start,
              unsigned int* trak_sample_end,
              mp4_split_options_t* options);

/* Returns true when the test string is a prefix of the input */
MOD_STREAMING_DLL_LOCAL extern
int starts_with(const char* input, const char* test);
/* Returns true when the test string is a suffix of the input */
MOD_STREAMING_DLL_LOCAL extern
int ends_with(const char* input, const char* test);

#ifdef __cplusplus
} /* extern C definitions */
#endif

#endif // MOOV_H_AKW

// End Of File

