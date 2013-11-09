/*******************************************************************************
 patch.cpp (version 2)

 patch264 - A command line tool for use together with the mod_h264_streaming
 module.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _MSC_VER
#define _CRTDBG_MAP_ALLOC
// #define _DEBUG
#include <stdlib.h>
#include <crtdbg.h>
#endif

#define __STDC_FORMAT_MACROS // C++ should define this for PRIu64
#include "mp4_io.h"
#include "moov.h"
#include "output_mp4.h"
#include "output_ismv.h"
#include "output_flv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <memory.h>
#include <getopt.h>

#ifdef WIN32
#define stat _stat64
#define strdup _strdup
#endif

namespace // anonymous
{

#if 0
uint64_t atoi64(const char* p)
{
#ifdef WIN32
  return _atoi64(p);
#else
  return atol(p);
#endif
}
#endif

uint64_t get_filesize(const char *path)
{
  struct stat status;
  if(stat(path, &status))
    perror("get_file_length stat:");
  return status.st_size;
}

#define COPY_BUFFER_SIZE 4096

int copy_data(FILE* infile, FILE* outfile, uint64_t size)
{
  char copy_buffer[COPY_BUFFER_SIZE];
  while(size)
  {
    unsigned int bytes_to_copy = size < COPY_BUFFER_SIZE ? (unsigned int)size : COPY_BUFFER_SIZE;

    if(fread(copy_buffer, bytes_to_copy, 1, infile) != 1)
    {
      printf("Error: reading file\n");
      return 0;
    }

    if(fwrite(copy_buffer, bytes_to_copy, 1, outfile) != 1)
    {
      printf("Error: writing file\n");
      return 0;
    }

    size -= bytes_to_copy;
  }

  return 1;
}

} // anonymous

////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
  char* input_file = 0;
  char* output_file = 0;
  int verbose = 1;

  FILE* infile = 0;
  FILE* outfile = 0;

  char* query_params;

#ifdef _MSC_VER
  _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
#endif

  printf("mp4split " X_MOD_H264_STREAMING_VERSION
         "     Copyright 2007-2009 CodeShop B.V.\n");

  int c;
  bool show_usage = false;
  while(((c = getopt(argc, argv, "i:o:v:")) != EOF) && !show_usage)
  {
    switch (c)
    {
      case 'i':
        input_file = optarg;
        break;
      case 'o':
        output_file = optarg;
        break;
      case 'v':
        verbose = atoi(optarg);
        break;
      default:
        show_usage = true;
        return 0;
    }
  }

  if(input_file == NULL)
  {
    printf("Usage: mp4split [options]\n");
    printf(
    " -i infile                 MP4 input file and parameters\n"
    "    infile.mp4/manifest    output the SmoothStreaming manifest\n"
    "    infile.mp4?start=100.0 output video starting at 01:40\n"
    "    infile.mp4?end=20.0    output first 20 seconds of video\n"
    "    infile.mp4?(video=0)   output MP4 fragment\n"
    " [-o outfile]              output file\n"
//    " [-o outfile]              output file, defaults to:\n"
//    "    infile.ism             for server manifest files\n"
//    "    infile.ismc            for client manifest files\n"
//    "    infile.h264            for raw output\n"
    " [-v level]                0=quiet 1=error 2=warning 3=info\n"
    "\n");
     return 0;
  }

  query_params = strstr(input_file, "?");
  if(query_params)
  {
    query_params[0] = '\0';
    ++query_params;
  }

  int result = 1;
  struct mp4_split_options_t* options = mp4_split_options_init();

  if(ends_with(input_file, "/manifest"))
  {
    options->manifest = 1;
    input_file[strlen(input_file) - sizeof("/manifest") + 1] = '\0';
    printf("Creating manifest file (%s) for %s\n", output_file, input_file);
  }
  else if(query_params)
  {
    result = mp4_split_options_set(options, query_params, strlen(query_params));

    if(!result)
    {
      printf("Error reading query parameters for %s\n", query_params);
    }
    else
    {
      if(options->fragments)
      {
        printf("Creating MP4 fragment (%s) for %s\n", output_file, input_file);
      }
      else
      if(options->manifest)
      {
        printf("Creating manifest file (%s) for %s\n", output_file, input_file);
      }
      else
      {
        printf("Creating MP4 file (%s) for %s [%.2f-%.2f>\n",
          output_file, input_file, options->start, options->end);
      }
    }
  }

#define MAX_FILES 8

  unsigned int files = 0;
  struct mp4_files_t filespecs[MAX_FILES];

  if(result)
  {
    printf("statting %s\n", input_file);

    struct stat file_stat;
    if(stat(input_file, &file_stat))
    {
      perror("stat:");
      result = 0;
    }
    else
    {
      infile = fopen(input_file, "rb");

      if(infile && (file_stat.st_mode & S_IFMT) == S_IFREG)
      {
        filespecs[files].name_ = strdup(input_file);
        ++files;
      } else
      if(options->manifest && (file_stat.st_mode & S_IFMT) == S_IFDIR)
      {
        // the name is 'video.mp4'. Scan the directory ./video.ism/video_*.ismv
        files = MAX_FILES;
        mp4_scanfiles(input_file, &files, filespecs);
      } else
      if((file_stat.st_mode & S_IFMT) != S_IFREG)
      {
        printf("file %s is not a regular file\n", input_file);
        result = 0;
      } else
      {
        perror(input_file);
        result = 0;
      }
    }
  }

  bool fragment_file = false;
  if(output_file)
  {
    outfile = fopen(output_file, "wb");
    if(!outfile)
    {
      perror(optarg);
      result = 0;
    }

    // the output file defines the output format
    if(ends_with(output_file, ".mp4"))
    {
      options->output_format = OUTPUT_FORMAT_MP4;
    }
    else if(ends_with(output_file, ".aac") || ends_with(output_file, ".264"))
    {
      options->output_format = OUTPUT_FORMAT_RAW;
    }
    else if(ends_with(output_file, ".flv"))
    {
      options->output_format = OUTPUT_FORMAT_FLV;
    }
    else if(ends_with(output_file, ".ismv"))
    {
      fragment_file = true;
    }
  }

  if(result)
  {
    options->client_is_flash = 1;

    // output buckets
    struct bucket_t* buckets = 0;

    printf("found %u files\n", files);

    struct mp4_context_t* mp4_context[MAX_FILES];
    for(unsigned int file = 0; file != files; ++file)
    {
      uint64_t filesize = get_filesize(filespecs[file].name_);
      int mfra_only = options->fragments;
      mp4_context[file] = mp4_open(filespecs[file].name_,
                                   filesize, mfra_only, verbose);
      if(mp4_context[file] == NULL)
      {
        printf("[Error] opening file %s\n", filespecs[file].name_);
        result = 0;
        break;
      }
    }

    if(result)
    {
      if(fragment_file)
      {
        mp4_fragment_file(mp4_context[0], &buckets, options);
      }
      else if(options->manifest)
      {
        // create manifest file for smooth streaming
        result = mp4_create_manifest(&mp4_context[0], files, &buckets);
      }
      else
      {
#ifdef HAVE_OUTPUT_ISMV
        if(options->fragments && mp4_context[0]->mfra_data)
        {
          result = moof_from_mfra(mp4_context[0], &buckets, options);
        }
        else
#endif
        {
          // split the movie
          unsigned int trak_sample_start[MAX_TRACKS];
          unsigned int trak_sample_end[MAX_TRACKS];
          result = mp4_split(mp4_context[0], trak_sample_start, trak_sample_end,
                             options);
          if(result)
          {
            if(0)
            {
            }
            else if(options->fragments)
            {
              result = output_ismv(mp4_context[0],
                                   trak_sample_start,
                                   trak_sample_end,
                                   &buckets, options);
            }
            else if(options->output_format == OUTPUT_FORMAT_FLV)
            {
              result = output_flv(mp4_context[0],
                                  trak_sample_start,
                                  trak_sample_end,
                                  &buckets, options);
            }
            else if(options->output_format == OUTPUT_FORMAT_MP4)
            {
              result = output_mp4(mp4_context[0],
                                  trak_sample_start,
                                  trak_sample_end,
                                  &buckets, options);
            }
          }
        }
      }
    }

    for(unsigned int file = 0; file != files; ++file)
    {
      if(mp4_context[file] != NULL)
      {
        mp4_close(mp4_context[file]);
      }
    }

    if(result)
    {
      for(int second = 0; second != options->seconds; ++second)
      {
//        printf("%" PRIu64 ", ", options->byte_offsets[second]);
      }

      if(outfile)
      {
        struct bucket_t* bucket = buckets;
        uint64_t filesize = 0;
        int bucket_count = 0;
        if(bucket)
        {
          do
          {
            filesize += bucket->size_;
            bucket = bucket->next_;
            ++bucket_count;
          } while(bucket != buckets && result);

          struct bucket_t* bucket = buckets;
          printf("writing %u buckets for a total of %"PRIu64" KBytes:\n", bucket_count, filesize >> 10);
          uint64_t filepos = 0;
          do
          {
            switch(bucket->type_)
            {
            case BUCKET_TYPE_MEMORY:
//              printf("memory (%"PRIu64"), ", bucket->size_);
              if(fwrite(bucket->buf_, (off_t)bucket->size_, 1, outfile) != 1)
              {
                result = 0;
              }
              break;
            case BUCKET_TYPE_FILE:
//              printf("file (%"PRIu64"), ", bucket->size_);
              fseeko(infile, bucket->offset_, SEEK_SET);
              result = copy_data(infile, outfile, bucket->size_);
              break;
            }

            filepos += bucket->size_;
            static char const* progress0 =
              "======================================================================";
            static char const* progress1 =
              "                                                                      ";
            int done = (int)(70 * filepos / filesize);
            printf("\r%3u%%[%.*s>%.*s] ",
                   (unsigned int)(100 * filepos / filesize),
                   done, progress0,
                   70 - done, progress1);

            bucket = bucket->next_;
          } while(bucket != buckets && result);
        }
        printf("\n");
      }
    }
    else
    {
      printf("mp4_split returned error\n");
    }

    if(buckets)
    {
      buckets_exit(buckets);
    }
  }

  for(unsigned int file = 0; file != files; ++file)
  {
    free(filespecs[file].name_);
  }

  mp4_split_options_exit(options);

  if(infile)
  {
    fclose(infile);
  }

  if(outfile)
  {
    fclose(outfile);
  }

  return result == 0 ? 1 : 0;
}

// End Of File

