/*******************************************************************************
 output_flv.h - A library for writing FLV.

 Copyright (C) 2009 CodeShop B.V.
 http://www.code-shop.com

 For licensing see the LICENSE file
******************************************************************************/ 

#ifndef OUTPUT_FLV_H_AKW
#define OUTPUT_FLV_H_AKW

#include "mod_streaming_export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAVE_OUTPUT_FLV

struct mp4_context_t;
struct bucket_t;
struct mp4_split_options_t;

MOD_STREAMING_DLL_LOCAL extern
int output_flv(struct mp4_context_t const* mp4_context,
               unsigned int* trak_sample_start,
               unsigned int* trak_sample_end,
               struct bucket_t** buckets,
               struct mp4_split_options_t* options);

#ifdef __cplusplus
} /* extern C definitions */
#endif

#endif // OUTPUT_FLV_H_AKW

// End Of File

