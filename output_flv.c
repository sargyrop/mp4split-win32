/*******************************************************************************
 output_flv.c - A library for writing FLV.

 Copyright (C) 2009 CodeShop B.V.
 http://www.code-shop.com

 For licensing see the LICENSE file
******************************************************************************/ 

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef __cplusplus
#define __STDC_FORMAT_MACROS // C++ should define this for PRIu64
#endif

#include "output_flv.h"
#include "mp4_io.h"
#include "moov.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RTMP_AAC_SEQUENCE_HEADER  0
#define RTMP_AAC_RAW              1

#define RTMP_AVC_SEQUENCE_HEADER  0
#define RTMP_AVC_NALU             1

extern int output_flv(struct mp4_context_t const* mp4_context,
                      unsigned int* trak_sample_start,
                      unsigned int* trak_sample_end,
                      struct bucket_t** buckets,
                      struct mp4_split_options_t* options)
{
  struct moov_t* moov = mp4_context->moov;
  unsigned int track = 0;

  for(track = 0; track != moov->tracks_; ++track)
  {
    struct trak_t* trak = moov->traks_[track];
    struct stsd_t const* stsd = trak->mdia_->minf_->stbl_->stsd_;
    struct sample_entry_t const* sample_entry = &stsd->sample_entries_[0];
    unsigned int start_sample = trak_sample_start[track];
    unsigned int end_sample = trak_sample_end[track];
    unsigned int s;

    if(trak->mdia_->hdlr_->handler_type_ != FOURCC('v', 'i', 'd', 'e'))
      continue;

    if(trak->mdia_->hdlr_->handler_type_ == FOURCC('v', 'i', 'd', 'e'))
    {
      unsigned char* buffer = (unsigned char*)malloc(1 + 1 + 3 + sample_entry->codec_private_data_length_);
      unsigned char* p = buffer;

      p = write_8(p, 0x17);
      p = write_8(p, RTMP_AVC_SEQUENCE_HEADER);
      p = write_24(p, 0);
      memcpy(p, sample_entry->codec_private_data_,
             sample_entry->codec_private_data_length_);
      p += sample_entry->codec_private_data_length_;
      bucket_insert_tail(buckets, bucket_init_memory(buffer, p - buffer));
      free(buffer);
    } else
    if(trak->mdia_->hdlr_->handler_type_ == FOURCC('s', 'o', 'u', 'n'))
    {
      unsigned char* buffer = (unsigned char*)malloc(1 + 1 + sample_entry->codec_private_data_length_);
      unsigned char* p = buffer;

      p = write_8(p, 0xaf);
      p = write_8(p, RTMP_AAC_SEQUENCE_HEADER);

      memcpy(p, sample_entry->codec_private_data_,
             sample_entry->codec_private_data_length_);
      p += sample_entry->codec_private_data_length_;
      bucket_insert_tail(buckets, bucket_init_memory(buffer, p - buffer));
      free(buffer);
    } else
    {
      continue;
    }

    for(s = start_sample; s != end_sample; ++s)
    {
      uint64_t sample_pos = trak->samples_[s].pos_;
      unsigned int sample_size = trak->samples_[s].size_;
      int cto = trak->samples_[s].cto_;

      // FLV uses a fixed 1000 timescale
      unsigned int composition_time = (unsigned int)
        (trak_time_to_moov_time(cto, 1000, trak->mdia_->mdhd_->timescale_));

      MP4_INFO(
        "frame=%u pts=%u offset=%"PRIu64" size=%u\n",
        s, composition_time, sample_pos, sample_size);

      if(trak->mdia_->hdlr_->handler_type_ == FOURCC('v', 'i', 'd', 'e'))
      {
//        if(is_avc)
        {
          // VIDEODATA
          unsigned char header[5];
          unsigned int is_keyframe = trak->samples_[s].is_ss_;
          unsigned int codec_id = 7;          // AVC
          write_8(header, ((is_keyframe ? 1 : 2) << 4) + codec_id);

          write_8(header + 1, RTMP_AVC_NALU);
          write_24(header + 2, composition_time);
          bucket_insert_tail(buckets, bucket_init_memory(header, 5));
          bucket_insert_tail(buckets, bucket_init_file(sample_pos, sample_size));
        }
      }
      else
      {
        // AUDIODATA
        unsigned char header[2];
        write_8(header, 0xaf);
        write_8(header + 1, RTMP_AAC_RAW);
        // AACAUDIODATA
        bucket_insert_tail(buckets, bucket_init_memory(header, 2));
        bucket_insert_tail(buckets, bucket_init_file(sample_pos, sample_size));
      }
    }
  }

  return 1;
}

// End Of File

