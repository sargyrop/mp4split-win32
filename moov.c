/*******************************************************************************
 moov.c - A library for splitting Quicktime/MPEG4.

 Copyright (C) 2007-2009 CodeShop B.V.
 http://www.code-shop.com

 For licensing see the LICENSE file
******************************************************************************/ 

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef __cplusplus
#define __STDC_FORMAT_MACROS // C++ should define this for PRIu64
#define __STDC_LIMIT_MACROS  // C++ should define this for UINT64_MAX
#endif

#include "moov.h"
#include "mp4_io.h"
#include "mp4_reader.h"

#ifdef _MSC_VER
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

/* 
  The QuickTime File Format PDF from Apple:
    http://developer.apple.com/techpubs/quicktime/qtdevdocs/PDF/QTFileFormat.pdf
    http://developer.apple.com/documentation/QuickTime/QTFF/QTFFPreface/qtffPreface.html
*/

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>  // FreeBSD doesn't define off_t in stdio.h
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif
#ifdef HAVE_INTTYPES_H
# include <inttypes.h>
#endif

#ifdef WIN32
// #define ftello _ftelli64
// #define fseeko _fseeki64
#include <windows.h>
#define DIR_SEPARATOR '\\'
#endif

#ifndef WIN32
#include "dirent.h"
#define DIR_SEPARATOR '/'
#endif

struct bucket_t* bucket_init(enum bucket_type_t bucket_type)
{
  struct bucket_t* bucket = (struct bucket_t*)malloc(sizeof(struct bucket_t));
  bucket->type_ = bucket_type;
  bucket->prev_ = bucket;
  bucket->next_ = bucket;

  return bucket;
}

static void bucket_exit(struct bucket_t* bucket)
{
  switch(bucket->type_)
  {
  case BUCKET_TYPE_MEMORY:
    free(bucket->buf_);
    break;
  case BUCKET_TYPE_FILE:
    break;
  }
  free(bucket);
}

extern struct bucket_t* bucket_init_memory(void const* buf, uint64_t size)
{
  struct bucket_t* bucket = bucket_init(BUCKET_TYPE_MEMORY);
  bucket->buf_ = malloc((size_t)size);
  memcpy(bucket->buf_, buf, (size_t)size);
  bucket->size_ = size;
  return bucket;
}

extern struct bucket_t* bucket_init_file(uint64_t offset, uint64_t size)
{
  struct bucket_t* bucket = bucket_init(BUCKET_TYPE_FILE);
  bucket->offset_ = offset;
  bucket->size_ = size;
  return bucket;
}

static void bucket_insert_after(struct bucket_t* after, struct bucket_t* bucket)
{
  bucket->prev_ = after;
  bucket->next_ = after->next_;
  after->next_->prev_ = bucket;
  after->next_ = bucket;
}

extern void bucket_insert_tail(struct bucket_t** head, struct bucket_t* bucket)
{
  if(*head == NULL)
  {
    *head = bucket;
  }

  bucket_insert_after((*head)->prev_, bucket);
}

extern void bucket_insert_head(struct bucket_t** head, struct bucket_t* bucket)
{
  bucket_insert_tail(head, bucket);
  *head = bucket;
}

static void bucket_remove(struct bucket_t* bucket)
{
  struct bucket_t* prev = bucket->prev_;
  struct bucket_t* next = bucket->next_;
  bucket->prev_->next_ = next;
  bucket->next_->prev_ = prev;
}

extern void buckets_exit(struct bucket_t* buckets)
{
  struct bucket_t* bucket = buckets;
  do
  {
    struct bucket_t* next = bucket->next_;
    bucket_exit(bucket);
    bucket = next;
  } while(bucket != buckets);
}

/* Returns true when the test string is a prefix of the input */
int starts_with(const char* input, const char* test)
{
  while(*input && *test)
  {
    if(*input != *test)
      return 0;
    ++input;
    ++test;
  }

  return *test == '\0';
}

/* Returns true when the test string is a suffix of the input */
int ends_with(const char* input, const char* test)
{
  const char* it = input + strlen(input);
  const char* pit = test + strlen(test);
  while(it != input && pit != test)
  {
    if(*it != *pit)
      return 0;
    --it;
    --pit;
  }

  return pit == test;
}

////////////////////////////////////////////////////////////////////////////////

static void stco_shift_offsets(struct stco_t* stco, int offset)
{
  unsigned int i;
  for(i = 0; i != stco->entries_; ++i)
    stco->chunk_offsets_[i] += offset;
}

static void trak_shift_offsets(struct trak_t* trak, int64_t offset)
{
  struct stco_t* stco = trak->mdia_->minf_->stbl_->stco_;
  stco_shift_offsets(stco, (int32_t)offset);
}

static void moov_shift_offsets(struct moov_t* moov, int64_t offset)
{
  unsigned int i;
  for(i = 0; i != moov->tracks_; ++i)
  {
    trak_shift_offsets(moov->traks_[i], offset);
  }
}

// reported by everwanna:
// av out of sync because: 
// audio track 0 without stss, seek to the exact time. 
// video track 1 with stss, seek to the nearest key frame time.
//
// fixed:
// first pass we get the new aligned times for traks with an stss present
// second pass is for traks without an stss
static int get_aligned_start_and_end(struct mp4_context_t const* mp4_context,
                                     unsigned int start, unsigned int end,
                                     unsigned int* trak_sample_start,
                                     unsigned int* trak_sample_end)
{
  unsigned int pass;
  struct moov_t* moov = mp4_context->moov;
  long moov_time_scale = moov->mvhd_->timescale_;

  for(pass = 0; pass != 2; ++pass)
  {
    unsigned int i;
    for(i = 0; i != moov->tracks_; ++i)
    {
      struct trak_t* trak = moov->traks_[i];
      struct stbl_t* stbl = trak->mdia_->minf_->stbl_;
      long trak_time_scale = trak->mdia_->mdhd_->timescale_;

      // 1st pass: stss present, 2nd pass: no stss present
      if(pass == 0 && !stbl->stss_)
        continue;
      if(pass == 1 && stbl->stss_)
        continue;

      // get start
      if(start == 0)
      {
        trak_sample_start[i] = start;
      }
      else
      {
        start = stts_get_sample(stbl->stts_,
          moov_time_to_trak_time(start, moov_time_scale, trak_time_scale));

        MP4_INFO("start=%u (trac time)\n", start);
        MP4_INFO("start=%.2f (seconds)\n",
          stts_get_time(stbl->stts_, start) / (float)trak_time_scale);

        start = stbl_get_nearest_keyframe(stbl, start + 1) - 1;
        MP4_INFO("start=%u (zero based keyframe)\n", start);
        trak_sample_start[i] = start;
        start = (unsigned int)(trak_time_to_moov_time(
          stts_get_time(stbl->stts_, start), moov_time_scale, trak_time_scale));
        MP4_INFO("start=%u (moov time)\n", start);
        MP4_INFO("start=%.2f (seconds)\n", start / (float)moov_time_scale);
      }

      // get end
      if(end == 0)
      {
        // The default is till-the-end of the track
        trak_sample_end[i] = trak->samples_size_;
      }
      else
      {
        end = stts_get_sample(stbl->stts_,
          moov_time_to_trak_time(end, moov_time_scale, trak_time_scale));
        MP4_INFO("end=%u (trac time)\n", end);
        MP4_INFO("end=%.2f (seconds)\n",
          stts_get_time(stbl->stts_, end) / (float)trak_time_scale);

        if(end >= trak->samples_size_)
        {
          end = trak->samples_size_;
        }
        else
        {
          end = stbl_get_nearest_keyframe(stbl, end + 1) - 1;
        }
        MP4_INFO("end=%u (zero based keyframe)\n", end);
        trak_sample_end[i] = end;
//          MP4_INFO("endframe=%u, samples_size_=%u\n", end, trak->samples_size_);
        end = (unsigned int)trak_time_to_moov_time(
          stts_get_time(stbl->stts_, end), moov_time_scale, trak_time_scale);
        MP4_INFO("end=%u (moov time)\n", end);
        MP4_INFO("end=%.2f (seconds)\n", end / (float)moov_time_scale);
      }
    }
  }

  MP4_INFO("start=%u\n", start);
  MP4_INFO("end=%u\n", end);

  if(end && start >= end)
  {
    return 0;
  }

  return 1;
}


////////////////////////////////////////////////////////////////////////////////

struct mp4_split_options_t* mp4_split_options_init()
{
  struct mp4_split_options_t* options = (struct mp4_split_options_t*)
    malloc(sizeof(struct mp4_split_options_t));
  options->client_is_flash = 0;
  options->start = 0.0;
  options->end = 0.0;
  options->adaptive = 0;
  options->fragments = 0;
  options->manifest = 0;
  options->fragment_type = FRAGMENT_TYPE_UNKNOWN;
  options->output_format = OUTPUT_FORMAT_MP4;
  options->fragment_start = 0;
  options->seconds = 0;
  options->byte_offsets = 0;

  return options;
}

int mp4_split_options_set(struct mp4_split_options_t* options,
                          const char* args_data,
                          unsigned int args_size)
{
  int result = 1;

  {
    const char* first = args_data;
    const char* last = first + args_size + 1;

    if(*first == '?')
    {
      ++first;
    }

    {
      char const* key = first;
      char const* val = NULL;
      int is_key = 1;
      size_t key_len = 0;

      float vbegin = 0.0f;
      float vend = 0.0f;

      while(first != last)
      {
        // the args_data is not necessarily 0 terminated, so fake it
        int ch = (first == last - 1) ? '\0' : *first;
        switch(ch)
        {
        case '=':
          val = first + 1;
          key_len = first - key;
          is_key = 0;
          break;
        case '&':
        case '\0':
          if(!is_key)
          {
            // make sure the value is zero-terminated (for strtod,atoi64)
            int val_len = first - val;
            char* valz = (char*)malloc(val_len + 1);
            memcpy(valz, val, val_len);
            valz[val_len] = '\0';

            if(!strncmp("client", key, key_len))
            {
              options->client_is_flash = starts_with(valz, "FLASH");
            } else
            if(!strncmp("start", key, key_len))
            {
              options->start = (float)(strtod(valz, NULL));
            } else
            if(!strncmp("end", key, key_len))
            {
              options->end = (float)(strtod(valz, NULL));
            } else
            if(!strncmp("vbegin", key, key_len))
            {
              vbegin = (float)(strtod(valz, NULL));
            } else
            if(!strncmp("vend", key, key_len))
            {
              vend = (float)(strtod(valz, NULL));
            } else
            if(!strncmp("adaptive", key, key_len))
            {
              options->adaptive = 1;
            } else
            if(!strncmp("video", key, key_len))
            {
              options->fragments = 1;
              options->fragment_type = FRAGMENT_TYPE_VIDEO;
              options->fragment_start = atoi64(valz);
            } else
            if(!strncmp("audio", key, key_len))
            {
              options->fragments = 1;
              options->fragment_type = FRAGMENT_TYPE_AUDIO;
              options->fragment_start = atoi64(valz);
            } else
            if(!strncmp("manifest", key, key_len))
            {
              options->manifest = 1;
            }
            free(valz);
          }
          key = first + 1;
          val = NULL;
          is_key = 1;
          break;
        }
        ++first;
      }

      // If we have specified a begin point of the virtual video clip,
      // then adjust the start offset
      options->start += vbegin;

      // If we have specified an end, adjust it in case of a virtual video clip.
      if(options->end)
      {
        options->end += vbegin;
      }
      else
      {
        options->end = vend;
      }

      // Validate the start/end for the virtual video clip (begin).
      if(vbegin)
      {
        if(options->start < vbegin)
          result = 0;
        if(options->end && options->end < vbegin)
          result = 0;
      }
      // Validate the start/end for the virtual video clip (end).
      if(vend)
      {
        if(options->start > vend)
          result =  0;
        if(options->end && options->end > vend)
          result = 0;
      }
    }
  }

  return result;
}

void mp4_split_options_exit(struct mp4_split_options_t* options)
{
  if(options->byte_offsets)
  {
    free(options->byte_offsets);
  }

  free(options);
}

extern int mp4_scanfiles(const char* input_file,
                         unsigned int* files,
                         struct mp4_files_t* filespecs)
{
  size_t max_files = *files;
  *files = 0;

  // the name is 'video.mp4'. Scan the directory ./video.ism/video_*.ismv
  if(ends_with(input_file, ".mp4") || ends_with(input_file, ".ism"))
  {
    const char* name_start = strrchr(input_file, '/');
    const char* ext_start = input_file + strlen(input_file) - (sizeof(".mp4") - 1);
    const char* dir_end;
    char* filename = (char*)malloc(4096);
    if(name_start == NULL)
    {
      name_start = strrchr(input_file, '\\');
    }
    if(name_start == NULL)
    {
      name_start = input_file;
    }
    else
    {
      ++name_start;
    }
    sprintf(filename, "%.*s.ism%c",
            (int)(ext_start - input_file), input_file, DIR_SEPARATOR);
    dir_end = filename + strlen(filename);

    printf("scanning dir: %s\n", filename);

#ifdef WIN32
    strncat(filename, name_start, ext_start - name_start);
    strcat(filename, "_*.ismv");
    {
      WIN32_FIND_DATA ffd;
      HANDLE hFind = INVALID_HANDLE_VALUE;
      hFind = FindFirstFile(filename, &ffd);
      if(hFind != INVALID_HANDLE_VALUE)
      {
        do
        {
          {
            size_t len = dir_end - filename + strlen(ffd.cFileName);
            if((*files) == max_files)
            {
              break;
            }

            filespecs[*files].name_ = (char*)malloc(len + 1);
            filespecs[*files].name_[0] = '\0';
            strncat(filespecs[*files].name_, filename, dir_end - filename);
            strcat(filespecs[*files].name_, ffd.cFileName);
//            filespecs[*files].size_ =
//              ((uint64_t)(ffd.nFileSizeHigh) << 32) + ffd.nFileSizeLow;
            ++(*files);
          }
        } while(FindNextFile(hFind, &ffd) != 0);
        FindClose(hFind);
      }
    }
#else
    DIR* pDir = opendir(filename);
    if(pDir)
    {
      struct dirent* pEntry;
      while((pEntry = readdir(pDir)) != 0)
      {
        if(pEntry->d_type & DT_REG)
        {
          if(/*starts_with(pEntry->d_name, name_start, ext_start - name_start && */
              ends_with(pEntry->d_name, ".ismv"))
          {
            size_t len = dir_end - filename + strlen(pEntry->d_name);

            if((*files) == max_files)
            {
              break;
            }

            filespecs[*files].name_ = malloc(len + 1);
            filespecs[*files].name_[0] = '\0';
            strncat(filespecs[*files].name_, filename, dir_end - filename);
            strcat(filespecs[*files].name_, pEntry->d_name);
            ++(*files);
          }
        }
      }
      closedir(pDir);
    }
#endif

    free(filename);
  }

  return 1;
}

extern int mp4_split(struct mp4_context_t* mp4_context,
                     unsigned int* trak_sample_start,
                     unsigned int* trak_sample_end,
                     struct mp4_split_options_t* options)
{
  int result;
  float start_time;
  float end_time;

  moov_build_index(mp4_context, mp4_context->moov);

  start_time = options->start;
  end_time = options->end;

  // write fragment instead of complete moov atom (for Silverlight only)
  if(options->fragments)
  {
    start_time = (float)(options->fragment_start / 10000000.0);
    end_time = 0.0;
  }

  {
    struct moov_t const* moov = mp4_context->moov;
    long moov_time_scale = moov->mvhd_->timescale_;
    unsigned int start = (unsigned int)(start_time * moov_time_scale + 0.5f);
    unsigned int end = (unsigned int)(end_time * moov_time_scale + 0.5f);

    // for every trak, convert seconds to sample (time-to-sample).
    // adjust sample to keyframe
    result = get_aligned_start_and_end(mp4_context, start, end,
                                       trak_sample_start, trak_sample_end);
  }

  return result;
}

// End Of File

