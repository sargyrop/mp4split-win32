/*******************************************************************************
 output_ismv.h - A library for reading and writing Fragmented MPEG4.

 Copyright (C) 2009 CodeShop B.V.
 http://www.code-shop.com

 For licensing see the LICENSE file
******************************************************************************/ 

#ifndef OUTPUT_ISMV_H_AKW
#define OUTPUT_ISMV_H_AKW

#include "mod_streaming_export.h"

#ifndef _MSC_VER
#include <inttypes.h>
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define HAVE_OUTPUT_ISMV

#define X_MOD_SMOOTH_STREAMING_KEY      "X-Mod-Smooth-Streaming"
#define X_MOD_SMOOTH_STREAMING_VERSION  "version=1.0.1"

struct mp4_context_t;
struct bucket_t;
struct mp4_split_options_t;

// Using the Movie Fragment Random Access (MFRA) box

MOD_STREAMING_DLL_LOCAL extern
int moof_from_mfra(struct mp4_context_t const* mp4_context,
                   struct bucket_t** buckets,
                   struct mp4_split_options_t const* options);

// Dynamically creating the fragments

MOD_STREAMING_DLL_LOCAL extern
int output_ismv(struct mp4_context_t const* mp4_context,
                unsigned int* trak_sample_start,
                unsigned int* trak_sample_end,
                struct bucket_t** buckets,
                struct mp4_split_options_t const* options);

// Manifest generation

MOD_STREAMING_DLL_LOCAL extern
int mp4_create_manifest(struct mp4_context_t** mp4_context,
                        unsigned int mp4_contexts,
                        struct bucket_t** buckets);

// Fragment a complete file

MOD_STREAMING_DLL_LOCAL extern
int mp4_fragment_file(struct mp4_context_t const* mp4_context,
                      struct bucket_t** buckets,
                      struct mp4_split_options_t const* options);


#ifdef __cplusplus
} /* extern C definitions */
#endif

#endif // OUTPUT_ISMV_H_AKW

// End Of File

