/*******************************************************************************
 output_ismv.c - A library for reading and writing Fragmented MPEG4.

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

#include "output_ismv.h"
#include "mp4_io.h"
#include "mp4_reader.h"
#include "mp4_writer.h"
#include "moov.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#else
#include <byteswap.h>
#endif

static const uint32_t aac_samplerates[] =
{
  96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
  16000, 12000, 11025,  8000,  7350,     0,     0,     0
};

static const uint32_t aac_channels[] =
{
  0, 1, 2, 3, 4, 5, 6, 8,
  0, 0, 0, 0, 0, 0, 0, 0
};

static int mp4_samplerate_to_index(unsigned int samplerate)
{
  unsigned int i;
  for(i = 0; i != 13; ++i)
  {
    if(aac_samplerates[i] == samplerate)
      return i;
  }
  return 4;
}

static uint16_t byteswap16(uint16_t val)
{
#ifdef WIN32
  return _byteswap_ushort(val);
#else
  return bswap_16(val);
#endif
}

static uint32_t byteswap32(uint32_t val)
{
#ifdef WIN32
  return _byteswap_ulong(val);
#else
  return bswap_32(val);
#endif
}

// find track index for audio/video fragment
static int
get_fragment_track(struct mp4_context_t const* mp4_context,
                   struct mp4_split_options_t const* options)
{
  unsigned int i;
  for(i = 0; i != mp4_context->moov->tracks_; ++i)
  {
    struct trak_t* trak = mp4_context->moov->traks_[i];
    switch(trak->mdia_->hdlr_->handler_type_)
    {
    case FOURCC('v', 'i', 'd', 'e'):
      if(options->fragment_type == FRAGMENT_TYPE_VIDEO)
      {
        return i;
      }
      break;
     case FOURCC('s', 'o', 'u', 'n'):
      if(options->fragment_type == FRAGMENT_TYPE_AUDIO)
      {
        return i;
      }
      break;
    }
  }

  MP4_ERROR("Requested %s track not found in moov atom\n",
    options->fragment_type == FRAGMENT_TYPE_VIDEO ? "video" : "audio");

  return -1;
}

struct tfra_table_t
{
  uint64_t time_;
  uint64_t moof_offset_;
  uint32_t traf_number_;
  uint32_t trun_number_;
  uint32_t sample_number_;
};
typedef struct tfra_table_t tfra_table_t;

struct tfra_t
{
  unsigned int version_;
  unsigned int flags_;
  uint32_t track_id_;
  unsigned int length_size_of_traf_num_;
  unsigned int length_size_of_trun_num_;
  unsigned int length_size_of_sample_num_;
  uint32_t number_of_entry_;
  struct tfra_table_t* table_;
};
typedef struct tfra_t tfra_t;

static tfra_t* tfra_init()
{
  tfra_t* tfra = (tfra_t*)malloc(sizeof(tfra_t));
  tfra->table_ = 0;

  return tfra;
}

static void tfra_exit(tfra_t* tfra)
{
  if(tfra->table_)
  {
    free(tfra->table_);
  }
  free(tfra);
}

static void* tfra_read(struct mp4_context_t const* UNUSED(mp4_context),
                       void* UNUSED(parent),
                       unsigned char* buffer, uint64_t UNUSED(size))
{
  unsigned int i;
  unsigned int length_fields;

  tfra_t* tfra = tfra_init();

  tfra->version_ = read_8(buffer + 0);
  tfra->flags_ = read_24(buffer + 1);

  tfra->track_id_ = read_32(buffer + 4);
  length_fields = read_32(buffer + 8);
  tfra->length_size_of_traf_num_ = (((length_fields >> 4) & 3) + 1);
  tfra->length_size_of_trun_num_ = (((length_fields >> 2) & 3) + 1);
  tfra->length_size_of_sample_num_ = (((length_fields >> 0) & 3) + 1);
  tfra->number_of_entry_ = read_32(buffer + 12);
  tfra->table_ = (tfra_table_t*)malloc(tfra->number_of_entry_ * sizeof(tfra_table_t));
  buffer += 16;
  for(i = 0; i != tfra->number_of_entry_; ++i)
  {
    if(tfra->version_ == 0)
    {
      tfra->table_[i].time_ = read_32(buffer + 0);
      tfra->table_[i].moof_offset_ = read_32(buffer + 4);
      buffer += 8;
    }
    else
    {
      tfra->table_[i].time_ = read_64(buffer + 0);
      tfra->table_[i].moof_offset_ = read_64(buffer + 8);
      buffer += 16;
    }
    tfra->table_[i].traf_number_ =
      read_n(buffer, tfra->length_size_of_traf_num_ * 8) - 1;
    buffer += tfra->length_size_of_traf_num_;

    tfra->table_[i].trun_number_ =
      read_n(buffer, tfra->length_size_of_trun_num_ * 8) - 1;
    buffer += tfra->length_size_of_trun_num_;

    tfra->table_[i].sample_number_ =
      read_n(buffer, tfra->length_size_of_sample_num_ * 8) - 1;
    buffer += tfra->length_size_of_sample_num_ ;
  }

  return tfra;
}

static unsigned char* tfra_write(void const* atom, unsigned char* buffer)
{
  tfra_t const* tfra = (tfra_t const*)atom;
  unsigned int i;
  uint32_t length_fields;

  buffer = write_8(buffer, tfra->version_);
  buffer = write_24(buffer, tfra->flags_);

  buffer = write_32(buffer, tfra->track_id_);
  length_fields = ((tfra->length_size_of_traf_num_ - 1) << 4) +
                  ((tfra->length_size_of_trun_num_ - 1) << 2) +
                  ((tfra->length_size_of_sample_num_ - 1) << 0);
  buffer = write_32(buffer, length_fields);

  buffer = write_32(buffer, tfra->number_of_entry_);
  for(i = 0; i != tfra->number_of_entry_; ++i)
  {
    tfra_table_t* table = &tfra->table_[i];
    if(tfra->version_ == 0)
    {
      buffer = write_32(buffer, (uint32_t)table->time_);
      buffer = write_32(buffer, (uint32_t)table->moof_offset_);
    }
    else
    {
      buffer = write_64(buffer, table->time_);
      buffer = write_64(buffer, table->moof_offset_);
    }

    buffer = write_n(buffer, tfra->length_size_of_traf_num_ * 8, 
                     table->traf_number_ + 1);
    buffer = write_n(buffer, tfra->length_size_of_trun_num_ * 8, 
                     table->trun_number_ + 1);
    buffer = write_n(buffer, tfra->length_size_of_sample_num_ * 8, 
                     table->sample_number_ + 1);
  }

  return buffer;
}

struct mfra_t
{
  struct unknown_atom_t* unknown_atoms_;
  unsigned int tracks_;
  struct tfra_t* tfras_[MAX_TRACKS];
};
typedef struct mfra_t mfra_t;

static mfra_t* mfra_init()
{
  mfra_t* mfra = (mfra_t*)malloc(sizeof(mfra_t));
  mfra->unknown_atoms_ = 0;
  mfra->tracks_ = 0;

  return mfra;
}

static void mfra_exit(mfra_t* atom)
{
  unsigned int i;
  if(atom->unknown_atoms_)
  {
    unknown_atom_exit(atom->unknown_atoms_);
  }
  for(i = 0; i != atom->tracks_; ++i)
  {
    tfra_exit(atom->tfras_[i]);
  }
  free(atom);
}

static int mfra_add_tfra(struct mp4_context_t const* UNUSED(mp4_context),
                         void* parent, void* child)
{
  mfra_t* mfra = (mfra_t*)parent;
  tfra_t* tfra = (tfra_t*)child;
  if(mfra->tracks_ == MAX_TRACKS)
  {
    mfra_exit(mfra);
    return 0;
  }

  mfra->tfras_[mfra->tracks_] = tfra;
  ++mfra->tracks_;

  return 1;
}

static void* mfra_read(struct mp4_context_t const* mp4_context,
                       void* UNUSED(parent),
                       unsigned char* buffer, uint64_t size)
{
  struct mfra_t* atom = mfra_init();

  struct atom_read_list_t atom_read_list[] = {
    { FOURCC('t', 'f', 'r', 'a'), &mfra_add_tfra, &tfra_read },
  };

  int result = atom_reader(mp4_context,
                  atom_read_list,
                  sizeof(atom_read_list) / sizeof(atom_read_list[0]),
                  atom,
                  buffer, size);

  if(!result)
  {
    mfra_exit(atom);
    return 0;
  }

  return atom;
}

static uint32_t mfra_write(mfra_t const* mfra, unsigned char* buffer)
{
  unsigned i;

  unsigned char* atom_start = buffer;
  uint32_t atom_size;

  // atom size
  buffer += 4;

  // atom type
  buffer = write_32(buffer, FOURCC('m', 'f', 'r', 'a'));

  buffer = atom_writer(mfra->unknown_atoms_, NULL, 0, buffer);

  for(i = 0; i != mfra->tracks_; ++i)
  {
    atom_write_list_t mfra_atom_write_list[] = {
      { FOURCC('t', 'f', 'r', 'a'), mfra->tfras_[i], &tfra_write },
    };
    buffer = atom_writer(0,
                         mfra_atom_write_list,
                         sizeof(mfra_atom_write_list) / sizeof(mfra_atom_write_list[0]),
                         buffer);
  }

  // write Movie Fragment Random Access Offset Box (mfro)
  {
    buffer = write_32(buffer, 16);
    buffer = write_32(buffer, FOURCC('m', 'f', 'r', 'o'));
    buffer = write_32(buffer, 0);
    buffer = write_32(buffer, (uint32_t)(buffer - atom_start + 4));
  }

  atom_size = (uint32_t)(buffer - atom_start);
  write_32(atom_start, atom_size);

  return atom_size;
}

static int mfra_get_track_fragment(struct mfra_t const* mfra,
                                   struct mp4_context_t const* mp4_context,
                                   struct bucket_t** buckets,
                                   struct mp4_split_options_t const* options)
{
  int fragment_track = get_fragment_track(mp4_context, options);

  if(fragment_track < 0)
  {
    return 0;
  }
  else
  {
    struct trak_t* trak = mp4_context->moov->traks_[fragment_track];
    long trak_time_scale = trak->mdia_->mdhd_->timescale_;
    uint64_t time = trak_time_to_moov_time(options->fragment_start,
      10000000, trak_time_scale);
    uint64_t moof_offset = 0;
    uint64_t moof_size = 0;

    unsigned int mfra_track = 0;
    for(mfra_track = 0; mfra_track != mfra->tracks_; ++mfra_track)
    {
      struct tfra_t* tfra = mfra->tfras_[mfra_track];
      if(tfra->track_id_ == trak->tkhd_->track_id_)
        break;
    }
    if(mfra_track == mfra->tracks_)
    {
      MP4_ERROR("Requested %s track (with id=%u) not found in mfra atom\n",
        options->fragment_type == FRAGMENT_TYPE_VIDEO ? "video" : "audio",
        trak->tkhd_->track_id_);
      return 0;
    }
    {
      struct tfra_t* tfra = mfra->tfras_[mfra_track];
      struct tfra_table_t* table = tfra->table_;
      unsigned int i;
      for(i = 0; i != tfra->number_of_entry_; ++i)
      {
        if(time == table[i].time_)
        {
          struct mp4_atom_t fragment_moof_atom;
          struct mp4_atom_t fragment_mdat_atom;

          moof_offset = table[i].moof_offset_;

          // find the size of the MOOF and following MDAT atom
          fseeko(mp4_context->infile, moof_offset, SEEK_SET);
          if(!mp4_atom_read_header(mp4_context, mp4_context->infile, &fragment_moof_atom))
          {
            MP4_ERROR("%s", "Error reading MOOF atom\n");
            return 0;
          }
          fseeko(mp4_context->infile, fragment_moof_atom.end_, SEEK_SET);
          if(!mp4_atom_read_header(mp4_context, mp4_context->infile, &fragment_mdat_atom))
          {
            MP4_ERROR("%s", "Error reading MDAT atom\n");
            return 0;
          }

          moof_size = fragment_moof_atom.size_ + fragment_mdat_atom.size_;
          break;
        }
      }

      if(moof_size == 0)
      {
        MP4_ERROR("%s", "No matching MOOF atom found for fragment\n");
        return 0;
      }

      bucket_insert_tail(buckets, bucket_init_file(moof_offset, moof_size));
    }

    return 1;
  }
}

int moof_from_mfra(struct mp4_context_t const* mp4_context,
                   struct bucket_t** buckets,
                   struct mp4_split_options_t const* options)
{
  int result = 0;

  struct mfra_t* mfra = (struct mfra_t*)
    mfra_read(mp4_context, NULL,
              mp4_context->mfra_data + ATOM_PREAMBLE_SIZE,
              mp4_context->mfra_atom.size_ - ATOM_PREAMBLE_SIZE);

  if(mfra != NULL)
  {
    result = mfra_get_track_fragment(mfra, mp4_context, buckets, options);
    mfra_exit(mfra);
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////

struct mfhd_t
{
  unsigned int version_;
  unsigned int flags_;
  // the ordinal number of this fragment, in increasing order
  uint32_t sequence_number_;
};

static struct mfhd_t* mfhd_init()
{
  struct mfhd_t* mfhd = (struct mfhd_t*)malloc(sizeof(struct mfhd_t));
  mfhd->version_ = 0;
  mfhd->flags_ = 0;
  mfhd->sequence_number_ = 0;

  return mfhd;
}

static void mfhd_exit(struct mfhd_t* atom)
{
  free(atom);
}

struct tfhd_t
{
  unsigned int version_;
  unsigned int flags_;
  uint32_t track_id_;
  // all the following are optional fields
  uint64_t base_data_offset_;
  uint32_t sample_description_index_;
  uint32_t default_sample_duration_;
  uint32_t default_sample_size_;
  uint32_t default_sample_flags_;
};

static struct tfhd_t* tfhd_init()
{
  struct tfhd_t* tfhd = (struct tfhd_t*)malloc(sizeof(struct tfhd_t));

  tfhd->version_ = 0;
  tfhd->flags_ = 0;

  return tfhd;
}

static void tfhd_exit(struct tfhd_t* atom)
{
  free(atom);
}

struct trun_table_t
{
  uint32_t sample_duration_;
  uint32_t sample_size_;
//  uint32_t sample_flags_;
  uint32_t sample_composition_time_offset_;
};

struct trun_t
{
  unsigned int version_;
  unsigned int flags_;
  // the number of samples being added in this fragment; also the number of rows
  // in the following table (the rows can be empty)
  uint32_t sample_count_;
  // is added to the implicit or explicit data_offset established in the track
  // fragment header
  int32_t data_offset_;
  // provides a set of set for the first sample only of this run
  uint32_t first_sample_flags_;

  struct trun_table_t* table_;
};

static struct trun_t* trun_init()
{
  struct trun_t* trun = (struct trun_t*)malloc(sizeof(struct trun_t));
  trun->version_ = 0;
  trun->flags_ = 0;
  trun->sample_count_ = 0;
  trun->data_offset_ = 0;
  trun->first_sample_flags_ = 0;
  trun->table_ = 0;

  return trun;
}

static void trun_exit(struct trun_t* atom)
{
  if(atom->table_)
  {
    free(atom->table_);
  }
  free(atom);
}

struct traf_t
{
  struct unknown_atom_t* unknown_atoms_;
  struct tfhd_t* tfhd_;
  struct trun_t* trun_;
};

static struct traf_t* traf_init()
{
  struct traf_t* traf = (struct traf_t*)malloc(sizeof(struct traf_t));
  traf->unknown_atoms_ = 0;
  traf->tfhd_ = 0;
  traf->trun_ = 0;

  return traf;
}

static void traf_exit(struct traf_t* atom)
{
  if(atom->unknown_atoms_)
  {
    unknown_atom_exit(atom->unknown_atoms_);
  }
  if(atom->tfhd_)
  {
    tfhd_exit(atom->tfhd_);
  }
  if(atom->trun_)
  {
    trun_exit(atom->trun_);
  }
  free(atom);
}

struct moof_t
{
  struct unknown_atom_t* unknown_atoms_;
  struct mfhd_t* mfhd_;
  unsigned int tracks_;
  struct traf_t* trafs_[MAX_TRACKS];
};

static struct moof_t* moof_init()
{
  struct moof_t* moof = (struct moof_t*)malloc(sizeof(struct moof_t));
  moof->unknown_atoms_ = 0;
  moof->mfhd_ = 0;
  moof->tracks_ = 0;

  return moof;
}

static void moof_exit(struct moof_t* atom)
{
  unsigned int i;
  if(atom->unknown_atoms_)
  {
    unknown_atom_exit(atom->unknown_atoms_);
  }
  if(atom->mfhd_)
  {
    mfhd_exit(atom->mfhd_);
  }
  for(i = 0; i != atom->tracks_; ++i)
  {
    traf_exit(atom->trafs_[i]);
  }
  free(atom);
}

static unsigned char* tfhd_write(void const* atom, unsigned char* buffer)
{
  struct tfhd_t const* tfhd = (struct tfhd_t const*)atom;

  buffer = write_8(buffer, tfhd->version_);
  buffer = write_24(buffer, tfhd->flags_);

  buffer = write_32(buffer, tfhd->track_id_);

  if(tfhd->flags_ & 0x000001)
  {
    buffer = write_64(buffer, tfhd->base_data_offset_);
  }
  if(tfhd->flags_ & 0x000002)
  {
    buffer = write_32(buffer, tfhd->sample_description_index_);
  }
  if(tfhd->flags_ & 0x000008)
  {
    buffer = write_32(buffer, tfhd->default_sample_duration_);
  }
  if(tfhd->flags_ & 0x000010)
  {
    buffer = write_32(buffer, tfhd->default_sample_size_);
  }
  if(tfhd->flags_ & 0x000020)
  {
    buffer = write_32(buffer, tfhd->default_sample_flags_);
  }

  return buffer;
}

static unsigned char* trun_write(void const* atom, unsigned char* buffer)
{
  struct trun_t const* trun = (struct trun_t const*)atom;
  unsigned int i;

  buffer = write_8(buffer, trun->version_);
  buffer = write_24(buffer, trun->flags_);

  buffer = write_32(buffer, trun->sample_count_);

  // first sample flag
  if(trun->flags_ & 0x0001)
  {
    buffer = write_32(buffer, trun->data_offset_);
  }
  // first sample flag
  if(trun->flags_ & 0x0004)
  {
    buffer = write_32(buffer, trun->first_sample_flags_);
  }

  for(i = 0; i != trun->sample_count_; ++i)
  {
    if(trun->flags_ & 0x0100)
    {
      buffer = write_32(buffer, trun->table_[i].sample_duration_);
    }
    if(trun->flags_ & 0x0200)
    {
      buffer = write_32(buffer, trun->table_[i].sample_size_);
    }
    if(trun->flags_ & 0x0800)
    {
      buffer = write_32(buffer, trun->table_[i].sample_composition_time_offset_);
    }
  }

  return buffer;
}

static unsigned char* mfhd_write(void const* atom, unsigned char* buffer)
{
  struct mfhd_t const* mfhd = (struct mfhd_t const*)atom;

  buffer = write_8(buffer, mfhd->version_);
  buffer = write_24(buffer, mfhd->flags_);

  buffer = write_32(buffer, mfhd->sequence_number_);

  return buffer;
}

static unsigned char* traf_write(void const* atom, unsigned char* buffer)
{
  struct traf_t const* traf = (struct traf_t const*)atom;
  struct atom_write_list_t atom_write_list[] = {
    { FOURCC('t', 'f', 'h', 'd'), traf->tfhd_, &tfhd_write },
    { FOURCC('t', 'r', 'u', 'n'), traf->trun_, &trun_write }
  };

  buffer = atom_writer(traf->unknown_atoms_,
                       atom_write_list,
                       sizeof(atom_write_list) / sizeof(atom_write_list[0]),
                       buffer);

  return buffer;
}

static void moof_write(struct moof_t* atom, unsigned char* buffer)
{
  unsigned i;

  unsigned char* atom_start = buffer;

  struct atom_write_list_t atom_write_list[] = {
    { FOURCC('m', 'f', 'h', 'd'), atom->mfhd_, &mfhd_write },
  };

  // atom size
  buffer += 4;

  // atom type
  buffer = write_32(buffer, FOURCC('m', 'o', 'o', 'f'));

  buffer = atom_writer(atom->unknown_atoms_,
                       atom_write_list,
                       sizeof(atom_write_list) / sizeof(atom_write_list[0]),
                       buffer);

  for(i = 0; i != atom->tracks_; ++i)
  {
    struct atom_write_list_t traf_atom_write_list[] = {
      { FOURCC('t', 'r', 'a', 'f'), atom->trafs_[i], &traf_write },
    };
    buffer = atom_writer(0,
                         traf_atom_write_list,
                         sizeof(traf_atom_write_list) / sizeof(traf_atom_write_list[0]),
                         buffer);
  }
  write_32(atom_start, (uint32_t)(buffer - atom_start));
}

static int moof_create(struct mp4_context_t const* mp4_context,
                       struct moof_t* moof,
                       struct trak_t const* trak,
                       unsigned int start, unsigned int end,
                       struct bucket_t** buckets,
                       struct mp4_split_options_t const* options)
{
  uint32_t mdat_size = ATOM_PREAMBLE_SIZE;
  struct bucket_t* mdat_bucket = 0;
  if(options->output_format == OUTPUT_FORMAT_MP4)
  {
    unsigned char mdat_buffer[32];
    struct mp4_atom_t mdat_atom;
    int mdat_header_size;
    mdat_atom.type_ = FOURCC('m', 'd', 'a', 't');
    mdat_atom.short_size_ = 0;
    mdat_header_size = mp4_atom_write_header(mdat_buffer, &mdat_atom);
    mdat_bucket = bucket_init_memory(mdat_buffer, mdat_header_size);
    bucket_insert_tail(buckets, mdat_bucket);
  }

  moof->mfhd_ = mfhd_init();
  moof->mfhd_->sequence_number_ = 0;

  // ASSUMPTION: the sequence number is the nth sync-sample
  {
//    struct stss_t const* stss = trak->mdia_->minf_->stbl_->stss_;
//    for(i = 0; i != stss->entries_; ++i)
//    {
//      if(start == stss->sample_numbers_[i])
//        break;
//    }
//    moof->mfhd_->sequence_number_ = i;
    moof->mfhd_->sequence_number_ = 1;
  }

  {
    struct stsd_t const* stsd = trak->mdia_->minf_->stbl_->stsd_;
    struct sample_entry_t const* sample_entry = &stsd->sample_entries_[0];
    int is_avc = sample_entry->fourcc_ == FOURCC('a', 'v', 'c', '1');

    struct traf_t* traf = traf_init();
    moof->trafs_[moof->tracks_] = traf;
    ++moof->tracks_;
    {
//      struct stts_t const* stts = trak->mdia_->minf_->stbl_->stts_;
      struct ctts_t const* ctts = trak->mdia_->minf_->stbl_->ctts_;
      unsigned int trun_index = 0;
      unsigned int s;
      struct bucket_t* bucket_prev = 0;

      traf->tfhd_ = tfhd_init();
      // 0x000020 = default-sample-flags present
      traf->tfhd_->flags_ = 0x000020;
      traf->tfhd_->track_id_ = trak->tkhd_->track_id_;
      traf->tfhd_->default_sample_flags_ = 0x0000c0;

      traf->trun_ = trun_init();
      // 0x0004 = first_sample_flags is present
      // 0x0100 = samle-duration is present
      // 0x0200 = sample-size is present
      traf->trun_->flags_ = 0x000304;
      // 0x0800 = sample-composition-time-offset is present
      if(ctts)
      {
        traf->trun_->flags_ |= 0x000800;
      }

//      traf->trun_->sample_count_ = stts_get_samples(stts);
      traf->trun_->sample_count_ = end - start;
      traf->trun_->first_sample_flags_= 0x00000040;
      traf->trun_->table_ = (struct trun_table_t*)malloc(traf->trun_->sample_count_ * sizeof(struct trun_table_t));

      for(s = start; s != end; ++s)
      {
        // SmoothStreaming uses a fixed 10000000 timescale
        uint32_t timescale_ = trak->mdia_->mdhd_->timescale_;
        uint64_t pts1 = (trak_time_to_moov_time(
          trak->samples_[s + 1].pts_, 10000000, timescale_));
        uint64_t pts0 = (trak_time_to_moov_time(
          trak->samples_[s + 0].pts_, 10000000, timescale_));

        unsigned int sample_duration = (unsigned int)(pts1 - pts0);

        uint64_t sample_pos = trak->samples_[s].pos_;
        unsigned int sample_size = trak->samples_[s].size_;
        unsigned int cto = (unsigned int)(trak_time_to_moov_time(
          trak->samples_[s].cto_, 10000000, timescale_));

        traf->trun_->table_[trun_index].sample_duration_ = sample_duration;
        traf->trun_->table_[trun_index].sample_size_ = sample_size;
        traf->trun_->table_[trun_index].sample_composition_time_offset_ = cto;

        MP4_INFO(
          "frame=%u pts=%"PRIi64" cto=%u duration=%u offset=%"PRIu64" size=%u\n",
          s,
          trak->samples_[s].pts_,
          trak->samples_[s].cto_,
          sample_duration,
          sample_pos, sample_size);

        if(trak->mdia_->hdlr_->handler_type_ == FOURCC('v', 'i', 'd', 'e'))
        {
          if(bucket_prev == NULL)
          {
            // TODO: return error when no SPS and PPS are available
            if(is_avc)
            {
              unsigned char* buffer;
              unsigned char* p;
              unsigned int sps_pps_size =
                sample_entry->nal_unit_length_ + sample_entry->sps_length_ +
                sample_entry->nal_unit_length_ + sample_entry->pps_length_;

              if(sps_pps_size == 0)
              {
                MP4_ERROR("%s", "[Error] No SPS or PPS available\n");
                return 0;
              }

              buffer = (unsigned char*)malloc(sps_pps_size);
              p = buffer;

              // sps
              p = write_32(p, 0x00000001);
              memcpy(p, sample_entry->sps_, sample_entry->sps_length_);
              p += sample_entry->sps_length_;

              // pps
              p = write_32(p, 0x00000001);
              memcpy(p, sample_entry->pps_, sample_entry->pps_length_);
              p += sample_entry->pps_length_;

              bucket_insert_tail(buckets, bucket_init_memory(buffer, sps_pps_size));
              free(buffer);

              traf->trun_->table_[trun_index].sample_size_ += sps_pps_size;
              mdat_size += sps_pps_size;
            }
          }

          if(is_avc)
          {
            static const char nal_marker[4] = { 0, 0, 0, 1 };
            uint64_t first = sample_pos;
            uint64_t last = sample_pos + sample_size;
            while(first != last)
            {
              unsigned char buffer[4];
              unsigned int nal_size;
              bucket_insert_tail(buckets, bucket_init_memory(nal_marker, 4));

              if(fseeko(mp4_context->infile, first, SEEK_SET) != 0)
              {
                MP4_ERROR("%s", "Reached end of file prematurely\n");
                return 0;
              }
              if(fread(buffer, sample_entry->nal_unit_length_, 1, mp4_context->infile) != 1)
              {
                MP4_ERROR("%s", "Error reading NAL size\n");
                return 0;
              }
              nal_size = read_n(buffer, sample_entry->nal_unit_length_ * 8);

              if(nal_size == 0)
              {
                MP4_ERROR("%s", "Invalid NAL size (0)\n");
                return 0;
              }

              bucket_prev = bucket_init_file(first + sample_entry->nal_unit_length_, nal_size);
              bucket_insert_tail(buckets, bucket_prev);

              first += sample_entry->nal_unit_length_ + nal_size;
            }
          }
          else
          {
            // try to merge buckets
            if(bucket_prev &&
               sample_pos == bucket_prev->offset_ + bucket_prev->size_)
            {
              bucket_prev->size_ += sample_size;
            }
            else
            {
              bucket_prev = bucket_init_file(sample_pos, sample_size);
              bucket_insert_tail(buckets, bucket_prev);
            }
          }
        } else
        if(trak->mdia_->hdlr_->handler_type_ == FOURCC('s', 'o', 'u', 'n'))
        {
          // ADTS frame header
          if(sample_entry->wFormatTag == 0x00ff &&
             options->output_format == OUTPUT_FORMAT_RAW)
          {
            unsigned int syncword = 0xfff;
            unsigned int ID = 0; // MPEG-4
            unsigned int layer = 0;
            unsigned int protection_absent = 1;
            // 0 = Main profile AAC MAIN
            // 1 = Low Complexity profile (LC) AAC LC
            // 2 = Scalable Sample Rate profile (SSR) AAC SSR
            // 3 = (reserved) AAC LTP
            unsigned int profile = 1;
            unsigned int sampling_frequency_index =
              mp4_samplerate_to_index(sample_entry->nSamplesPerSec);
            unsigned int private_bit = 0;
            unsigned int channel_configuration = sample_entry->nChannels;
            unsigned int original_copy = 0;
            unsigned int home = 0;
            unsigned int copyright_identification_bit = 0;
            unsigned int copyright_identification_start = 0;
            unsigned int aac_frame_length = 7 + sample_size;
            unsigned int adts_buffer_fullness = 0x7ff;
            unsigned int no_raw_data_blocks_in_frame = 0;
            unsigned char buffer[8];

            uint64_t adts = 0;
            adts = (adts << 12) | syncword;
            adts = (adts << 1) | ID;
            adts = (adts << 2) | layer;
            adts = (adts << 1) | protection_absent;
            adts = (adts << 2) | profile;
            adts = (adts << 4) | sampling_frequency_index;
            adts = (adts << 1) | private_bit;
            adts = (adts << 3) | channel_configuration;
            adts = (adts << 1) | original_copy;
            adts = (adts << 1) | home;
            adts = (adts << 1) | copyright_identification_bit;
            adts = (adts << 1) | copyright_identification_start;
            adts = (adts << 13) | aac_frame_length;
            adts = (adts << 11) | adts_buffer_fullness;
            adts = (adts << 2) | no_raw_data_blocks_in_frame;

            write_64(buffer, adts);
            bucket_insert_tail(buckets, bucket_init_memory(buffer + 1, 7));

            traf->trun_->table_[trun_index].sample_size_ += 7;
            mdat_size += 7;

            bucket_prev = NULL;
          }

          // try to merge buckets
          if(bucket_prev &&
             sample_pos == bucket_prev->offset_ + bucket_prev->size_)
          {
            bucket_prev->size_ += sample_size;
          }
          else
          {
            bucket_prev = bucket_init_file(sample_pos, sample_size);
            bucket_insert_tail(buckets, bucket_prev);
          }
        }

        mdat_size += sample_size;

        ++trun_index;
      }
      // update size of mdat atom
      if(mdat_bucket)
      {
        write_32((unsigned char*)mdat_bucket->buf_, mdat_size);
      }
    }
  }

  return 1;
}

extern int output_ismv(struct mp4_context_t const* mp4_context,
                       unsigned int* trak_sample_start,
                       unsigned int* trak_sample_end,
                       struct bucket_t** buckets,
                       struct mp4_split_options_t const* options)
{
  struct moof_t* moof;
  int fragment_track = get_fragment_track(mp4_context, options);

  if(fragment_track < 0)
  {
    return 0;
  }
  else
  {
    struct trak_t const* trak = mp4_context->moov->traks_[fragment_track];

    // When we're requesting a fragment, then we set the end to
    // the next Smooth Streaming Sync Sample.
    unsigned int start = trak_sample_start[fragment_track];
    unsigned int end = start;
    if(end != trak->samples_size_)
    {
      ++end;
      while(end != trak->samples_size_)
      {
        if(trak->samples_[end].is_smooth_ss_)
          break;
        ++end;
      }
    }

    moof = moof_init();

    moof_create(mp4_context, moof, trak, start, end, buckets, options);

    if(options->output_format == OUTPUT_FORMAT_MP4)
    {
      unsigned char moof_data[8192];
      unsigned int moof_size;
      moof_write(moof, moof_data);
      moof_size = read_32(moof_data);
      bucket_insert_head(buckets, bucket_init_memory(moof_data, moof_size));
    }

    moof_exit(moof);

    return 1;
  }
}

////////////////////////////////////////////////////////////////////////////////
// Manifest

static char* hex64(unsigned char* first, unsigned char* last, char* out)
{
  static const char* hex = "0123456789ABCDEF";
  while(first != last)
  {
    int a = (*first >> 4) & 15;
    int b = (*first >> 0) & 15;
    *out++ = hex[a];
    *out++ = hex[b];
    ++first;
  }
  *out = '\0';

  return out;
}

struct quality_level_t
{
  uint32_t bitrate_;
  uint32_t fourcc_;
  uint32_t width_;
  uint32_t height_;
  char codec_private_data_[256];
};

static struct quality_level_t* quality_level_init()
{
  struct quality_level_t* that = (struct quality_level_t*)
    malloc(sizeof(struct quality_level_t));

  that->width_ = 0;
  that->height_ = 0;
  that->codec_private_data_[0] = '\0';

  return that;
}

static struct quality_level_t* quality_level_copy(struct quality_level_t* rhs)
{
  struct quality_level_t* that = (struct quality_level_t*)
    malloc(sizeof(struct quality_level_t));

  that->bitrate_ = rhs->bitrate_;
  that->fourcc_ = rhs->fourcc_;
  that->width_ = rhs->width_;
  that->height_ = rhs->height_;
  memcpy(that->codec_private_data_, rhs->codec_private_data_,
    sizeof(that->codec_private_data_) / sizeof(that->codec_private_data_[0]));

  return that;
}

static void quality_level_exit(struct quality_level_t* that)
{
  free(that);
}

static char*
quality_level_write(struct quality_level_t const* that, char* buffer)
{
  char* p = buffer;

  p += sprintf(p, "<QualityLevel"
                  " Bitrate=\"%u\""
                  " FourCC=\"%c%c%c%c\"",
               that->bitrate_,
               (that->fourcc_ >> 24) & 0xff,
               (that->fourcc_ >> 16) & 0xff,
               (that->fourcc_ >> 8) & 0xff,
               that->fourcc_ & 0xff);

  if(that->width_ && that->height_)
  {
    p += sprintf(p, " Width=\"%u\""
                    " Height=\"%u\""
                    " CodecPrivateData=\"%s\"",
                 that->width_,
                 that->height_,
                 that->codec_private_data_);

  }
  else
  {
    p += sprintf(p, " WaveFormatEx=\"%s\"",
                 that->codec_private_data_);
  }
  p += sprintf(p, " />\n");

  return p;
}

#define MAX_QUALITY_LEVELS 6
#define MAX_STREAMS 2

struct stream_t
{
  enum fragment_type_t type_;
  char subtype_[32];
  uint32_t chunks_;
  char url_[256];
  size_t quality_levels_;
  struct quality_level_t* quality_level_[MAX_QUALITY_LEVELS];
  uint64_t* durations_;
};

static struct stream_t* stream_init(enum fragment_type_t type, uint32_t chunks)
{
  struct stream_t* that = (struct stream_t*)malloc(sizeof(struct stream_t));

  that->type_ = type;
  that->subtype_[0] = '\0';
  that->chunks_ = chunks;
  that->url_[0] = '\0';
  that->quality_levels_ = 0;
  that->durations_ = (uint64_t*)malloc(chunks * sizeof(uint64_t));

  return that;
}

static struct stream_t* stream_copy(struct stream_t* rhs)
{
  struct stream_t* that = (struct stream_t*)malloc(sizeof(struct stream_t));
  size_t i;

  that->type_ = rhs->type_;
  strcpy(that->subtype_, rhs->subtype_);
  that->chunks_ = rhs->chunks_;
  strcpy(that->url_, rhs->url_);
  that->quality_levels_ = rhs->quality_levels_;
  for(i = 0; i != rhs->quality_levels_; ++i)
  {
    that->quality_level_[i] = quality_level_copy(rhs->quality_level_[i]);
  }
  that->durations_ = (uint64_t*)malloc(that->chunks_ * sizeof(uint64_t));
  memcpy(that->durations_, rhs->durations_, that->chunks_ * sizeof(uint64_t));

  return that;
}

static void stream_exit(struct stream_t* that)
{
  struct quality_level_t** first = that->quality_level_;
  struct quality_level_t** last = that->quality_level_ + that->quality_levels_;
  while(first != last)
  {
    quality_level_exit(*first);
    ++first;
  }

  free(that->durations_);
  free(that);
}

static void stream_add_quality_level(struct stream_t* that,
                                     struct quality_level_t* child)
{
  that->quality_level_[that->quality_levels_] = child;
  ++that->quality_levels_;
}

struct smooth_streaming_media_t
{
  uint64_t duration_;
  size_t streams_;
  struct stream_t* stream_[MAX_STREAMS]; // audio and video
};

static struct smooth_streaming_media_t* smooth_streaming_media_init()
{
  struct smooth_streaming_media_t* that = (struct smooth_streaming_media_t*)
    malloc(sizeof(struct smooth_streaming_media_t));

  that->streams_ = 0;

  return that;
}

static void smooth_streaming_media_exit(struct smooth_streaming_media_t* that)
{
  struct stream_t** first = that->stream_;
  struct stream_t** last = that->stream_ + that->streams_;
  while(first != last)
  {
    stream_exit(*first);
    ++first;
  }
  free(that);
}

static struct stream_t* smooth_streaming_media_find_stream(
  struct smooth_streaming_media_t* that, enum fragment_type_t type)
{
  struct stream_t** first = that->stream_;
  struct stream_t** last = that->stream_ + that->streams_;
  while(first != last)
  {
    if((*first)->type_ == type)
    {
      return *first;
    }
    ++first;
  }

  return NULL;
}

static struct stream_t* smooth_streaming_media_new_stream(
  struct smooth_streaming_media_t* that, enum fragment_type_t type,
  uint32_t chunks)
{
  struct stream_t* stream = smooth_streaming_media_find_stream(that, type);
  if(!stream)
  {
    stream = stream_init(type, chunks);
    that->stream_[that->streams_] = stream;
    ++that->streams_;
  }

  return stream;
}

static char*
stream_write(struct stream_t const* that, char* buffer)
{
  char* p = buffer;

  const char* type = (that->type_ == FRAGMENT_TYPE_AUDIO ? "audio" :
                     (that->type_ == FRAGMENT_TYPE_VIDEO ? "video" :
                     ("unknown")));

  p += sprintf(p, "<StreamIndex"
                  " Type=\"%s\""
                  " Subtype=\"%s\""
                  " Chunks=\"%u\""
                  " Url=\"%sFragments(%s={start time})\">\n",
               type, that->subtype_, that->chunks_, that->url_, type);

  {
    struct quality_level_t* const* first = that->quality_level_;
    struct quality_level_t* const* last = that->quality_level_ + that->quality_levels_;
    while(first != last)
    {
      p = quality_level_write(*first, p);
      ++first;
    }
  }

  {
    uint64_t const* first = that->durations_;
    uint64_t const* last = that->durations_ + that->chunks_;
    uint32_t chunk = 0;
    while(first != last)
    {
      p += sprintf(p, "<c"
                      " n=\"%u\""
                      " d=\"%"PRIu64"\""
                      " />\n",
                   chunk, *first);
      ++chunk;
      ++first;
    }
  }

  p += sprintf(p, "</StreamIndex>\n");

  return p;
}

static char*
smooth_streaming_media_write(struct smooth_streaming_media_t* that,
                             char* buffer)
{
  char* p = buffer;
  struct stream_t** first = that->stream_;
  struct stream_t** last = that->stream_ + that->streams_;

  p += sprintf(p, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
  p += sprintf(p, "<!--Created with mod_smooth_streaming(%s)-->\n",
               X_MOD_SMOOTH_STREAMING_VERSION);

  // write SmoothStreamingMedia
  {
    p += sprintf(p, "<SmoothStreamingMedia"
                    " MajorVersion=\"1\""
                    " MinorVersion=\"0\""
                    " Duration=\"%"PRIu64"\""
                    ">\n",
                 that->duration_);
  }

  while(first != last)
  {
    p = stream_write(*first, p);
    ++first;
  }

  p += sprintf(p, "</SmoothStreamingMedia>\n");

  return p;
}

static struct smooth_streaming_media_t*
create_manifest(struct mp4_context_t const* mp4_context,
                struct moov_t* moov, int is_mbr)
{
  struct smooth_streaming_media_t* smooth_streaming_media = NULL;
  unsigned int track;

  if(!moov_build_index(mp4_context, moov))
  {
    return smooth_streaming_media;
  }

  smooth_streaming_media = smooth_streaming_media_init();

  smooth_streaming_media->duration_ =
    trak_time_to_moov_time(moov->mvhd_->duration_,
                           10000000, moov->mvhd_->timescale_);

  for(track = 0; track != moov->tracks_; ++track)
  {
    struct trak_t* trak = moov->traks_[track];
    uint32_t chunks = 0;
    struct stream_t* stream;

    enum fragment_type_t type = FRAGMENT_TYPE_UNKNOWN;

    switch(trak->mdia_->hdlr_->handler_type_)
    {
    case FOURCC('s', 'o', 'u', 'n'):
      type = FRAGMENT_TYPE_AUDIO;
      break;
    case FOURCC('v', 'i', 'd', 'e'):
      type = FRAGMENT_TYPE_VIDEO;
      break;
    default:
      break;
    }

    // count the number of smooth streaming chunks
    {
      struct samples_t* first = trak->samples_;
      struct samples_t* last = trak->samples_ + trak->samples_size_;
      while(first != last)
      {
        if(first->is_smooth_ss_)
          ++chunks;
        ++first;
      }
    }

    stream =
      smooth_streaming_media_new_stream(smooth_streaming_media, type, chunks);

    if(is_mbr)
    {
      strcpy(stream->url_, "QualityLevels({bitrate})/");
    }

    // write StreamIndex
    {
      struct stsd_t const* stsd = trak->mdia_->minf_->stbl_->stsd_;
      struct sample_entry_t const* sample_entry = &stsd->sample_entries_[0];
      int is_avc = 0;

      switch(type)
      {
      case FRAGMENT_TYPE_VIDEO:
        is_avc = sample_entry->fourcc_ == FOURCC('a', 'v', 'c', '1');
        // H264 or WVC1
        sprintf(stream->subtype_, "%s", is_avc ? "H264" : "WVC1");
        break;
      case FRAGMENT_TYPE_AUDIO:
        // WmaPro or 
        if(sample_entry->fourcc_ == FOURCC('o', 'w', 'm', 'a'))
          sprintf(stream->subtype_, "WmaPro");
        else
          sprintf(stream->subtype_, "%c%c%c%c",
                  (sample_entry->fourcc_ >> 24),
                  (sample_entry->fourcc_ >> 16),
                  (sample_entry->fourcc_ >>  8),
                  (sample_entry->fourcc_ >>  0));
        break;
      default:
        break;
      }

      // write QualityLevel
      {
        struct quality_level_t* quality_level = quality_level_init();
        stream_add_quality_level(stream, quality_level);
        quality_level->fourcc_ = sample_entry->fourcc_;
        switch(type)
        {
        case FRAGMENT_TYPE_VIDEO:
          quality_level->bitrate_ = 4500 * 1000;
          quality_level->width_ = trak->tkhd_->width_ / 65536;
          quality_level->height_ = trak->tkhd_->height_ / 65536;

          quality_level->fourcc_ = is_avc ? FOURCC('H', '2', '6', '4')
                                          : FOURCC('W', 'V', 'C', '1');

          if(!sample_entry->codec_private_data_length_)
          {
            MP4_WARNING("%s", "[Warning]: No codec private data found\n");
          }

          hex64(sample_entry->codec_private_data_,
                sample_entry->codec_private_data_ +
                sample_entry->codec_private_data_length_,
                quality_level->codec_private_data_);
          break;
        case FRAGMENT_TYPE_AUDIO:
        {
          char* out = &quality_level->codec_private_data_[0];
          quality_level->bitrate_ = sample_entry->avg_bitrate_;

          if(!quality_level->bitrate_)
          {
            quality_level->bitrate_ = sample_entry->max_bitrate_;
          }

          // owma already includes the WAVEFORMATEX structure
          if(sample_entry->fourcc_ != FOURCC('o', 'w', 'm', 'a'))
          {
            // create WAVEFORMATEX
            //
            // WAVE_FORMAT_MPEG_ADTS_AAC (0x1600)
            // Advanced Audio Coding (AAC) audio in Audio Data Transport Stream
            // (ADTS) format.
            // No additional data is requried after the WAVEFORMATEX structure.
            //
            // WAVE_FORMAT_RAW_AAC1 (0x00ff)
            // Raw AAC.
            // The WAVEFORMATEX structure is followed by additional bytes that
            // contain the AudioSpecificConfig() data. The length of the
            // AudioSpecificConfig() data is 2 bytes for AAC-LC or HE-AAC with
            // implicit signaling of SBR/PS. It is more than 2 bytes for HE-AAC
            // with explicit signaling of SBR/PS.

  //          uint16_t wFormatTag = 0xa106; // WAVE_FORMAT_AAC
  //          uint16_t wFormatTag = 0x1600; // WAVE_FORMAT_MPEG_ADTS_AAC
  //          uint16_t wFormatTag = 0x00ff; // WAVE_FORMAT_RAW_AAC1
  //          uint16_t wFormatTag = 0x0055; // mp3
            uint16_t wFormatTag = sample_entry->wFormatTag;
            uint16_t nChannels = sample_entry->nChannels;
            uint32_t nSamplesPerSec = sample_entry->nSamplesPerSec;
            uint32_t nAvgBytesPerSec = sample_entry->nAvgBytesPerSec;
            uint16_t nBlockAlign = sample_entry->nBlockAlign != 0 ?
                                   sample_entry->nBlockAlign : 1;
            uint16_t wBitsPerSample = sample_entry->wBitsPerSample;
            uint16_t cbSize = (uint16_t)sample_entry->codec_private_data_length_;
            unsigned char waveformatex[18];
            unsigned char* wfx = waveformatex;

            if(cbSize >= 2)
            {
              // object_type
              // 0. Null
              // 1. AAC Main
              // 2. AAC LC
              // 3. AAC SSR
              // 4. AAC LTP
              // 6. AAC Scalable

              unsigned int object_type =
                sample_entry->codec_private_data_[0] >> 3;
              unsigned int frequency_index =
                ((sample_entry->codec_private_data_[0] & 7) << 1) |
                 (sample_entry->codec_private_data_[1] >> 7);
              unsigned int channels =
                (sample_entry->codec_private_data_[1] >> 3) & 15;

              MP4_INFO("AAC object_type/profile=%u frequency=%u channels=%u\n",
                      object_type - 1, aac_samplerates[frequency_index],
                      aac_channels[channels]);

              if(channels != nChannels)
              {
                MP4_WARNING("[Warning] settings channels in WAVEFORMATEX to %u\n",
                       channels);
                nChannels = (uint16_t)channels;
              }
            }

            wfx = write_16(wfx, byteswap16(wFormatTag));
            wfx = write_16(wfx, byteswap16(nChannels));
            wfx = write_32(wfx, byteswap32(nSamplesPerSec));
            wfx = write_32(wfx, byteswap32(nAvgBytesPerSec));
            wfx = write_16(wfx, byteswap16(nBlockAlign));
            wfx = write_16(wfx, byteswap16(wBitsPerSample));
            wfx = write_16(wfx, byteswap16(cbSize));
            out = hex64(waveformatex, wfx, out);

            MP4_INFO("%s", "WAVEFORMATEX:\n");
            MP4_INFO("  wFormatTag=0x%04x\n", wFormatTag);
            MP4_INFO("  wChannels=%u\n", nChannels);
            MP4_INFO("  nSamplesPerSec=%u\n", nSamplesPerSec);
            MP4_INFO("  nAvgBytesPerSec=%u\n", nAvgBytesPerSec);
            MP4_INFO("  nBlockAlign=%u\n", nBlockAlign);
            MP4_INFO("  wBitsPerSample=%u\n", wBitsPerSample);
            MP4_INFO("  cbSize=%u\n", cbSize);
          }

          if(!sample_entry->codec_private_data_length_)
          {
            MP4_WARNING("%s", "[Warning]: No codec private data found\n");
          }

          out = hex64(sample_entry->codec_private_data_,
                      sample_entry->codec_private_data_ +
                      sample_entry->codec_private_data_length_,
                      out);
        }
          break;
        default:
          break;
        }
      }

      // the chunks
      if(trak->samples_)
      {
        struct samples_t* first = trak->samples_;
        struct samples_t* last = trak->samples_ + trak->samples_size_ + 1;
        unsigned int chunk = 0;
        uint64_t begin_pts = (uint64_t)-1;
        while(first != last)
        {
          while(first != last)
          {
            if(first->is_smooth_ss_)
              break;
            ++first;
          }

          // SmoothStreaming uses a fixed 10000000 timescale
          uint64_t first_pts = trak_time_to_moov_time(first->pts_,
            10000000, trak->mdia_->mdhd_->timescale_);

          if(begin_pts != (uint64_t)(-1))
          {
            stream->durations_[chunk] = first_pts - begin_pts;
            ++chunk;
          }
          begin_pts = first_pts;
          if(first == last)
            break;
          ++first;
        }
      }
    }
  }

  return smooth_streaming_media;
}

static void manifest_merge(struct mp4_context_t* mp4_context,
                           struct smooth_streaming_media_t* manifest,
                           struct smooth_streaming_media_t* smooth_streaming_media)
{
  struct stream_t** first = smooth_streaming_media->stream_;
  struct stream_t** last =
    smooth_streaming_media->stream_ + smooth_streaming_media->streams_;
  while(first != last)
  {
    // merge with stream of same type or add new stream
    struct stream_t* stream =
      smooth_streaming_media_find_stream(manifest, (*first)->type_);
    if(!stream)
    {
      stream = stream_copy(*first);
      manifest->stream_[manifest->streams_] = stream;
      ++manifest->streams_;
    }
    else
    {
      size_t i;
      if(stream->chunks_ != (*first)->chunks_)
      {
        MP4_ERROR("Incompatible number of chunks (%u) in %s\n", (*first)->chunks_, remove_path(mp4_context->filename_));
        return;
      }
      // add quality levels
      for(i = 0; i != (*first)->quality_levels_; ++i)
      {
        stream_add_quality_level(stream, quality_level_copy((*first)->quality_level_[i]));
      }
    }
    ++first;
  }
}

extern int mp4_create_manifest(struct mp4_context_t** mp4_context,
                               unsigned int mp4_contexts,
                               struct bucket_t** buckets)
{
  unsigned int file;
  struct smooth_streaming_media_t* manifest = NULL;
  int result = 1;
  for(file = 0; file != mp4_contexts; ++file)
  {
    struct mp4_context_t* context = mp4_context[file];
    struct moov_t* moov = context->moov;
    int is_mbr = mp4_contexts > 1 ? 1 : 0;
    struct smooth_streaming_media_t* smooth_streaming_media =
      create_manifest(context, moov, is_mbr);

    if(smooth_streaming_media == NULL)
    {
      result = 0;
    }
    else
    {
      // patch the bitrate field with the number from the filename
      // E.g.: (video_1394000.ismv) -> bitrate = 1394000
      const char* underscore = strrchr(context->filename_, '_');
      if(underscore)
      {
        uint32_t bitrate = (uint32_t)(atoi64(underscore + 1));

        // for each stream
        struct stream_t* const* first = smooth_streaming_media->stream_;
        struct stream_t* const* last =
          smooth_streaming_media->stream_ + smooth_streaming_media->streams_;
        while(first != last)
        {
          // for each quality level
          struct quality_level_t** ql_first = (*first)->quality_level_;
          struct quality_level_t** ql_last =
            (*first)->quality_level_ + (*first)->quality_levels_;
          while(ql_first != ql_last)
          {
            (*ql_first)->bitrate_ = bitrate;
            ++ql_first;
          }
          ++first;
        }
      }
    }

    if(manifest == 0)
    {
      manifest = smooth_streaming_media;
    }
    else
    {
      manifest_merge(context, manifest, smooth_streaming_media),
      smooth_streaming_media_exit(smooth_streaming_media);
    }
  }

  if(manifest)
  {
    char* buffer = (char*)malloc(1024 * 256);
    char* p = smooth_streaming_media_write(manifest, buffer);
    bucket_insert_tail(buckets, bucket_init_memory(buffer, p - buffer));
    free(buffer);

    smooth_streaming_media_exit(manifest);
  }

  return result;
}

extern int mp4_fragment_file(struct mp4_context_t const* mp4_context,
                             struct bucket_t** buckets,
                             struct mp4_split_options_t const* options)
{
  struct moof_t* moof;
  uint64_t filepos = 0;
  int result = 1;

  struct moov_t* moov = mp4_context->moov;

  moov_build_index(mp4_context, mp4_context->moov);

  // Start with the ftyp
  {
    unsigned char ftyp[24];
    unsigned char* buffer = ftyp;
    buffer = write_32(buffer, 24);
    buffer = write_32(buffer, FOURCC('f', 't', 'y', 'p'));
    buffer = write_32(buffer, FOURCC('a', 'v', 'c', '1'));
    buffer = write_32(buffer, 0);
    buffer = write_32(buffer, FOURCC('i', 's', 'o', 'm'));
    buffer = write_32(buffer, FOURCC('i', 's', 'o', '2'));
    bucket_insert_tail(buckets, bucket_init_memory(ftyp, sizeof(ftyp)));
    filepos += sizeof(ftyp);
  }

  // A fragmented MPEG4 file starts with a MOOV atom with only the mandatory
  // atoms
  {
    struct moov_t* fmoov = moov_init();
    fmoov->mvhd_ = mvhd_copy(moov->mvhd_);
    fmoov->tracks_ = moov->tracks_;

    unsigned int i;
    for(i = 0; i != moov->tracks_; ++i)
    {
      unsigned int s;
      struct trak_t* trak = moov->traks_[i];
      struct trak_t* ftrak = trak_init();
      struct mdia_t* mdia = trak->mdia_;
      struct mdia_t* fmdia = mdia_init();
      struct minf_t* minf = mdia->minf_;
      struct minf_t* fminf = minf_init();
      struct stbl_t* stbl = minf->stbl_;
      struct stbl_t* fstbl = stbl_init();

      fmoov->traks_[i] = ftrak;
      ftrak->tkhd_ = tkhd_copy(trak->tkhd_);
      ftrak->mdia_ = fmdia;
      ftrak->samples_size_ = trak->samples_size_;
      ftrak->samples_ = (samples_t*)malloc(trak->samples_size_ * sizeof(samples_t));
      memcpy(ftrak->samples_, trak->samples_, trak->samples_size_ * sizeof(samples_t));
      fmdia->mdhd_ = mdhd_copy(mdia->mdhd_);
      fmdia->mdhd_->timescale_ = 10000000;
      fmdia->hdlr_ = hdlr_copy(mdia->hdlr_);
      fmdia->minf_ = fminf;
      fminf->smhd_ = minf->smhd_ == NULL ? NULL : smhd_copy(minf->smhd_);
      fminf->vmhd_ = minf->vmhd_ == NULL ? NULL : vmhd_copy(minf->vmhd_);
      fminf->dinf_ = dinf_copy(minf->dinf_);
      fminf->stbl_ = fstbl;
      fstbl->stts_ = stts_init();
      fstbl->ctts_ = ctts_init();
      fstbl->stsd_ = stsd_copy(stbl->stsd_);

      for(s = 0; s != ftrak->samples_size_; ++s)
      {
        // SmoothStreaming uses a fixed 10000000 timescale
        ftrak->samples_[s].pts_ = trak_time_to_moov_time(
          ftrak->samples_[s].pts_, ftrak->mdia_->mdhd_->timescale_,
          trak->mdia_->mdhd_->timescale_);
        ftrak->samples_[s].cto_ = (unsigned int)(trak_time_to_moov_time(
          ftrak->samples_[s].cto_, ftrak->mdia_->mdhd_->timescale_,
          trak->mdia_->mdhd_->timescale_));
      }
    }

    unsigned char* moov_data = mp4_context->moov_data;
    moov_write(fmoov, moov_data);
    uint32_t moov_size = read_32(moov_data);
    bucket_insert_tail(buckets, bucket_init_memory(moov_data, moov_size));
    filepos += moov_size;
    moov_exit(fmoov);
  }

  struct mfra_t* mfra = mfra_init();
  mfra->tracks_ = moov->tracks_;

  unsigned int i;
  unsigned int tfra_entries = 0;
  for(i = 0; i != moov->tracks_; ++i)
  {
    struct trak_t const* trak = moov->traks_[i];

    struct tfra_t* tfra = tfra_init();
    mfra->tfras_[i] = tfra;
    tfra->version_ = 1;
    tfra->flags_ = 0;
    tfra->track_id_ = trak->tkhd_->track_id_;
    tfra->length_size_of_traf_num_ = 1;
    tfra->length_size_of_trun_num_ = 1;
    tfra->length_size_of_sample_num_ = 1;

    // count the number of smooth sync samples (nr of moofs)
    tfra->number_of_entry_ = 0;
    unsigned int start;
    for(start = 0; start != trak->samples_size_; ++start)
    {
      {
        if(trak->samples_[start].is_smooth_ss_)
        {
          ++tfra->number_of_entry_;
        }
      }
    }
    tfra->table_ = (struct tfra_table_t*)
      malloc(tfra->number_of_entry_ * sizeof(struct tfra_table_t));

    tfra_entries += tfra->number_of_entry_;

    start = 0;
    unsigned int tfra_index = 0;
    while(start != trak->samples_size_)
    {
      unsigned int end = start;
      while(++end != trak->samples_size_)
      {
        if(trak->samples_[end].is_smooth_ss_)
          break;
      }

      struct bucket_t* bucket = bucket_init(BUCKET_TYPE_MEMORY);
      bucket_insert_tail(buckets, bucket);

      moof = moof_init();

      moof_create(mp4_context, moof, trak, start, end, buckets, options);

      if(options->output_format == OUTPUT_FORMAT_MP4)
      {
        unsigned char moof_data[8192];
        unsigned int moof_size;
        moof_write(moof, moof_data);
        moof_size = read_32(moof_data);

        bucket->buf_ = malloc(moof_size);
        bucket->size_ = moof_size;
        memcpy(bucket->buf_, moof_data, (size_t)bucket->size_);
//        bucket_insert_head(buckets, bucket_init_memory(moof_data, moof_size));
      }

      moof_exit(moof);

      struct tfra_table_t* table = &tfra->table_[tfra_index];
      // SmoothStreaming uses a fixed 10000000 timescale
      table->time_ = trak_time_to_moov_time(
        trak->samples_[start].pts_, 10000000, trak->mdia_->mdhd_->timescale_);
      table->moof_offset_ = filepos;
      table->traf_number_ = 0;
      table->trun_number_ = 0;
      table->sample_number_ = 0;

      // advance filepos for moof and mdat atom
      while(*buckets != bucket)
      {
        filepos += bucket->size_;
        bucket = bucket->next_;
      }

      // next fragment
      ++tfra_index;
      start = end;
    }
    // next track
  }

  // Write the Movie Fragment Random Access (MFRA) atom
  {
    unsigned char* mfra_data = (unsigned char*)malloc(8192 + tfra_entries * 28);
    uint32_t mfra_size = mfra_write(mfra, mfra_data);
    bucket_insert_tail(buckets, bucket_init_memory(mfra_data, mfra_size));
    mfra_exit(mfra);
    free(mfra_data);
  }

  return result;
}

// End Of File

