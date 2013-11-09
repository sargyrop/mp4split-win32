/*******************************************************************************
 mp4_io.c - A library for general MPEG4 I/O.

 Copyright (C) 2007-2009 CodeShop B.V.
 http://www.code-shop.com

 For licensing see the LICENSE file
******************************************************************************/ 

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef __cplusplus
#define __STDC_FORMAT_MACROS // C++ should define this for PRIu64
#endif

#include "mp4_io.h"
#include "mp4_reader.h" // for moov_read
#include "moov.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>  // FreeBSD doesn't define off_t in stdio.h

#ifdef WIN32
// #define ftello _ftelli64
// #define fseeko _fseeki64
#define DIR_SEPARATOR '\\'
#define strdup _strdup
#else
#define DIR_SEPARATOR '/'
#endif

extern uint64_t atoi64(const char* val)
{
#ifdef WIN32
  return _atoi64(val);
#else // elif defined(HAVE_STRTOLL)
  return strtoll(val, NULL, 10);
#endif
}

extern const char* remove_path(const char *path)
{
  const char* p = strrchr(path, DIR_SEPARATOR);
  if(p != NULL && *p != '\0')
  {
    return p + 1;
  }

  return path;
}

extern void log_trace(const char* fmt, ...)
{
  va_list arglist;
  va_start(arglist, fmt);

  vprintf(fmt, arglist);

  va_end(arglist);
}


extern unsigned int read_8(unsigned char const* buffer)
{
  return buffer[0];
}

extern unsigned char* write_8(unsigned char* buffer, unsigned int v)
{
  buffer[0] = (uint8_t)v;

  return buffer + 1;
}

extern uint16_t read_16(unsigned char const* buffer)
{
  return (buffer[0] << 8) |
         (buffer[1] << 0);
}

extern unsigned char* write_16(unsigned char* buffer, unsigned int v)
{
  buffer[0] = (uint8_t)(v >> 8);
  buffer[1] = (uint8_t)(v >> 0);

  return buffer + 2;
}

extern unsigned int read_24(unsigned char const* buffer)
{
  return (buffer[0] << 16) |
         (buffer[1] << 8) |
         (buffer[2] << 0);
}

extern unsigned char* write_24(unsigned char* buffer, unsigned int v)
{
  buffer[0] = (uint8_t)(v >> 16);
  buffer[1] = (uint8_t)(v >> 8);
  buffer[2] = (uint8_t)(v >> 0);

  return buffer + 3;
}

extern uint32_t read_32(unsigned char const* buffer)
{
  return (buffer[0] << 24) |
         (buffer[1] << 16) |
         (buffer[2] << 8) |
         (buffer[3] << 0);
}

extern unsigned char* write_32(unsigned char* buffer, uint32_t v)
{
  buffer[0] = (uint8_t)(v >> 24);
  buffer[1] = (uint8_t)(v >> 16);
  buffer[2] = (uint8_t)(v >> 8);
  buffer[3] = (uint8_t)(v >> 0);

  return buffer + 4;
}

extern uint64_t read_64(unsigned char const* buffer)
{
  return ((uint64_t)(read_32(buffer)) << 32) + read_32(buffer + 4);
}

extern unsigned char* write_64(unsigned char* buffer, uint64_t v)
{
  write_32(buffer + 0, (uint32_t)(v >> 32));
  write_32(buffer + 4, (uint32_t)(v >> 0));

  return buffer + 8;
}

extern uint32_t read_n(unsigned char const* buffer, unsigned int n)
{
  switch(n)
  {
  case 8:
    return read_8(buffer);
  case 16:
    return read_16(buffer);
  case 24:
    return read_24(buffer);
  case 32:
    return read_32(buffer);
  default:
    // program error
    return 0;
  }
}

extern unsigned char* write_n(unsigned char* buffer, unsigned int n, uint32_t v)
{
  switch(n)
  {
  case 8:
    return write_8(buffer, v);
  case 16:
    return write_16(buffer, v);
  case 24:
    return write_24(buffer, v);
  case 32:
    return write_32(buffer, v);
  }
  return NULL;
}

extern int mp4_atom_read_header(mp4_context_t const* mp4_context,
                                FILE* infile, mp4_atom_t* atom)
{
  unsigned char atom_header[8];

  atom->start_ = ftello(infile);
  if(fread(atom_header, 8, 1, infile) != 1)
  {
    MP4_ERROR("%s", "Error reading atom header\n");
    return 0;
  }
  atom->short_size_ = read_32(&atom_header[0]);
  atom->type_ = read_32(&atom_header[4]);

  if(atom->short_size_ == 1)
  {
    if(fread(atom_header, 8, 1, infile) != 1)
    {
      MP4_ERROR("%s", "Error reading extended atom header\n");
      return 0;
    }
    atom->size_ = read_64(&atom_header[0]);
  }
  else
  {
    atom->size_ = atom->short_size_;
  }

  atom->end_ = atom->start_ + atom->size_;

  MP4_INFO("Atom(%c%c%c%c,%"PRIu64")\n",
           atom->type_ >> 24, atom->type_ >> 16,
           atom->type_ >> 8, atom->type_,
           atom->size_);

  if(atom->size_ < ATOM_PREAMBLE_SIZE)
  {
    MP4_ERROR("%s", "Error: invalid atom size\n");
    return 0;
  }

  return 1;
}

extern int mp4_atom_write_header(unsigned char* outbuffer,
                                 mp4_atom_t const* atom)
{
  int write_box64 = atom->short_size_ == 1 ? 1 : 0;

  if(write_box64)
    write_32(outbuffer, 1);
  else
    write_32(outbuffer, (uint32_t)atom->size_);

  write_32(outbuffer + 4, atom->type_);

  if(write_box64)
  {
    write_64(outbuffer + 8, atom->size_);
    return 16;
  }
  else
  {
    return 8;
  }
}


static unsigned char* read_box(struct mp4_context_t* mp4_context,
                               FILE* infile, struct mp4_atom_t* atom)
{
  unsigned char* box_data = (unsigned char*)malloc((size_t)atom->size_);
  fseeko(infile, atom->start_, SEEK_SET);
  if(fread(box_data, (off_t)atom->size_, 1, infile) != 1)
  {
    MP4_ERROR("Error reading %c%c%c%c atom\n",
           atom->type_ >> 24, atom->type_ >> 16,
           atom->type_ >> 8, atom->type_);
    free(box_data);
    fclose(infile);
    return 0;
  }
  return box_data;
}

static mp4_context_t* mp4_context_init(const char* filename, int verbose)
{
  mp4_context_t* mp4_context = (mp4_context_t*)malloc(sizeof(mp4_context_t));

  mp4_context->filename_ = strdup(filename);
  mp4_context->infile = NULL;
  mp4_context->verbose_ = verbose;

  memset(&mp4_context->ftyp_atom, 0, sizeof(struct mp4_atom_t));
  memset(&mp4_context->moov_atom, 0, sizeof(struct mp4_atom_t));
  memset(&mp4_context->mdat_atom, 0, sizeof(struct mp4_atom_t));
  memset(&mp4_context->mfra_atom, 0, sizeof(struct mp4_atom_t));

  mp4_context->moov_data = 0;
  mp4_context->mfra_data = 0;

  mp4_context->moov = 0;
//  mp4_context->mfra = 0;

  return mp4_context;
}

static void mp4_context_exit(struct mp4_context_t* mp4_context)
{
  free(mp4_context->filename_);

  if(mp4_context->infile)
  {
    fclose(mp4_context->infile);
  }

  if(mp4_context->moov_data)
  {
    free(mp4_context->moov_data);
  }

  if(mp4_context->mfra_data)
  {
    free(mp4_context->mfra_data);
  }

  if(mp4_context->moov)
  {
    moov_exit(mp4_context->moov);
  }

//  if(mp4_context->mfra)
//  {
//    mfra_exit(mp4_context->mfra);
//  }

  free(mp4_context);
}

extern mp4_context_t* mp4_open(const char* filename, int64_t filesize, int mfra_only, int verbose)
{
  mp4_context_t* mp4_context = mp4_context_init(filename, verbose);

  mp4_context->infile = fopen(filename, "rb");
  if(mp4_context->infile == NULL)
  {
    mp4_context_exit(mp4_context);
    return 0;
  }

  // fast-open if we're only interested in the mfra atom
  if(mfra_only)
  {
    unsigned char mfro[16];
    fseeko(mp4_context->infile, -16, SEEK_END);
    if(fread(mfro, 16, 1, mp4_context->infile) != 1)
    {
      MP4_ERROR("%s", "Error reading mfro header\n");
      return 0;
    }
    if(read_32(mfro + 4) == FOURCC('m', 'f', 'r', 'o'))
    {
      off_t mfra_size = read_32(mfro + 12);
      fseeko(mp4_context->infile, -mfra_size, SEEK_END);

      if(!mp4_atom_read_header(mp4_context, mp4_context->infile,
                               &mp4_context->mfra_atom))
      {
        mp4_context_exit(mp4_context);
        return 0;
      }
      mp4_context->mfra_data = read_box(mp4_context, mp4_context->infile, &mp4_context->mfra_atom);
      if(mp4_context->mfra_data == NULL)
      {
        mp4_context_exit(mp4_context);
        return 0;
      }
    }
    fseeko(mp4_context->infile, 0, SEEK_SET);
  }

  while(ftello(mp4_context->infile) < filesize)
  {
    struct mp4_atom_t leaf_atom;

    if(!mp4_atom_read_header(mp4_context, mp4_context->infile, &leaf_atom))
      break;

    switch(leaf_atom.type_)
    {
    case FOURCC('f', 't', 'y', 'p'):
      mp4_context->ftyp_atom = leaf_atom;
      break;
    case FOURCC('m', 'o', 'o', 'v'):
      mp4_context->moov_atom = leaf_atom;
      mp4_context->moov_data = read_box(mp4_context, mp4_context->infile, &mp4_context->moov_atom);
      if(mp4_context->moov_data == NULL)
      {
        mp4_context_exit(mp4_context);
        return 0;
      }
      break;
    case FOURCC('m', 'd', 'a', 't'):
      mp4_context->mdat_atom = leaf_atom;
      break;
    case FOURCC('m', 'f', 'r', 'a'):
      mp4_context->mfra_atom = leaf_atom;
      mp4_context->mfra_data = read_box(mp4_context, mp4_context->infile, &mp4_context->mfra_atom);
      if(mp4_context->mfra_data == NULL)
      {
        mp4_context_exit(mp4_context);
        return 0;
      }
      break;
    }

    if(leaf_atom.end_ > (uint64_t)filesize)
    {
      MP4_ERROR("%s", "Reached end of file prematurely\n");
      mp4_context_exit(mp4_context);
      return 0;
    }

    fseeko(mp4_context->infile, leaf_atom.end_, SEEK_SET);

    // short-circuit for mfra. We only need the moov atom (hopefully at
    // the beginning of the file) and the mfra atom (hopefully at the offset
    // specified by the mfro atom).
    if(mfra_only)
    {
      if(mp4_context->mfra_atom.size_ && mp4_context->moov_atom.size_)
      {
        break;
      }
    }
  }

  if(mp4_context->moov_atom.size_ == 0)
  {
    MP4_ERROR("%s", "Error: moov atom not found\n");
    mp4_context_exit(mp4_context);
    return 0;
  }

  if(mfra_only && mp4_context->mfra_data)
  {
    // we either need an mfra atom
  }
  else if(mp4_context->mdat_atom.size_ == 0)
  {
    // or mdat atom
    MP4_ERROR("%s", "Error: mdat atom not found\n");
    mp4_context_exit(mp4_context);
    return 0;
  }

  mp4_context->moov = (struct moov_t*)
    moov_read(mp4_context, NULL,
              mp4_context->moov_data + ATOM_PREAMBLE_SIZE,
              mp4_context->moov_atom.size_ - ATOM_PREAMBLE_SIZE);

  if(mp4_context->moov == 0 || mp4_context->moov->mvhd_ == 0)
  {
    MP4_ERROR("%s", "Error parsing moov header\n");
    mp4_context_exit(mp4_context);
    return 0;
  }

  return mp4_context;
}

extern void mp4_close(struct mp4_context_t* mp4_context)
{
  mp4_context_exit(mp4_context);
}

////////////////////////////////////////////////////////////////////////////////

extern struct unknown_atom_t* unknown_atom_init()
{
  struct unknown_atom_t* atom = (struct unknown_atom_t*)malloc(sizeof(struct unknown_atom_t));
  atom->atom_ = 0;
  atom->next_ = 0;

  return atom;
}

extern void unknown_atom_exit(struct unknown_atom_t* atom)
{
  while(atom)
  {
    struct unknown_atom_t* next = atom->next_;
    free(atom->atom_);
    free(atom);
    atom = next;
  }
}


extern struct moov_t* moov_init()
{
  struct moov_t* moov = (struct moov_t*)malloc(sizeof(struct moov_t));
  moov->unknown_atoms_ = 0;
  moov->mvhd_ = 0;
  moov->tracks_ = 0;

  return moov;
}

extern void moov_exit(struct moov_t* atom)
{
  unsigned int i;
  if(atom->unknown_atoms_)
  {
    unknown_atom_exit(atom->unknown_atoms_);
  }
  if(atom->mvhd_)
  {
    mvhd_exit(atom->mvhd_);
  }
  for(i = 0; i != atom->tracks_; ++i)
  {
    trak_exit(atom->traks_[i]);
  }
  free(atom);
}

extern struct trak_t* trak_init()
{
  struct trak_t* trak = (struct trak_t*)malloc(sizeof(struct trak_t));
  trak->unknown_atoms_ = 0;
  trak->tkhd_ = 0;
  trak->mdia_ = 0;
  trak->chunks_size_ = 0;
  trak->chunks_ = 0;
  trak->samples_size_ = 0;
  trak->samples_ = 0;

  return trak;
}

extern void trak_exit(struct trak_t* trak)
{
  if(trak->unknown_atoms_)
  {
    unknown_atom_exit(trak->unknown_atoms_);
  }
  if(trak->tkhd_)
  {
    tkhd_exit(trak->tkhd_);
  }
  if(trak->mdia_)
  {
    mdia_exit(trak->mdia_);
  }
  if(trak->chunks_)
  {
    free(trak->chunks_);
  }
  if(trak->samples_)
  {
    free(trak->samples_);
  }
  free(trak);
}

extern struct mvhd_t* mvhd_init()
{
  struct mvhd_t* atom = (struct mvhd_t*)malloc(sizeof(struct mvhd_t));

  return atom;
}

extern mvhd_t* mvhd_copy(mvhd_t const* rhs)
{
  mvhd_t* atom = (mvhd_t*)malloc(sizeof(mvhd_t));

  memcpy(atom, rhs, sizeof(mvhd_t));

  return atom;
}

extern void mvhd_exit(struct mvhd_t* atom)
{
  free(atom);
}

extern struct tkhd_t* tkhd_init()
{
  tkhd_t* tkhd = (tkhd_t*)malloc(sizeof(tkhd_t));

  return tkhd;
}

extern struct tkhd_t* tkhd_copy(tkhd_t const* rhs)
{
  tkhd_t* tkhd = (tkhd_t*)malloc(sizeof(tkhd_t));

  memcpy(tkhd, rhs, sizeof(tkhd_t));

  return tkhd;
}

extern void tkhd_exit(tkhd_t* tkhd)
{
  free(tkhd);
}

extern struct mdia_t* mdia_init()
{
  struct mdia_t* atom = (struct mdia_t*)malloc(sizeof(struct mdia_t));
  atom->unknown_atoms_ = 0;
  atom->mdhd_ = 0;
  atom->hdlr_ = 0;
  atom->minf_ = 0;

  return atom;
}

extern void mdia_exit(struct mdia_t* atom)
{
  if(atom->unknown_atoms_)
  {
    unknown_atom_exit(atom->unknown_atoms_);
  }
  if(atom->mdhd_)
  {
    mdhd_exit(atom->mdhd_);
  }
  if(atom->hdlr_)
  {
    hdlr_exit(atom->hdlr_);
  }
  if(atom->minf_)
  {
    minf_exit(atom->minf_);
  }
  free(atom);
}

extern struct mdhd_t* mdhd_init()
{
  struct mdhd_t* mdhd = (struct mdhd_t*)malloc(sizeof(struct mdhd_t));

  return mdhd;
}

extern mdhd_t* mdhd_copy(mdhd_t const* rhs)
{
  struct mdhd_t* mdhd = (struct mdhd_t*)malloc(sizeof(struct mdhd_t));

  memcpy(mdhd, rhs, sizeof(mdhd_t));

  return mdhd;
}

extern void mdhd_exit(struct mdhd_t* mdhd)
{
  free(mdhd);
}

extern struct hdlr_t* hdlr_init()
{
  struct hdlr_t* atom = (struct hdlr_t*)malloc(sizeof(struct hdlr_t));
  atom->name_ = 0;

  return atom;
}

extern hdlr_t* hdlr_copy(hdlr_t const* rhs)
{
  hdlr_t* atom = (hdlr_t*)malloc(sizeof(hdlr_t));

  atom->version_ = rhs->version_;
  atom->flags_ = rhs->flags_;
  atom->predefined_ = rhs->predefined_;
  atom->handler_type_ = rhs->handler_type_;
  atom->reserved1_ = rhs->reserved1_;
  atom->reserved2_ = rhs->reserved2_;
  atom->reserved3_ = rhs->reserved3_;
  atom->name_ = rhs->name_ == NULL ? NULL : strdup(rhs->name_);

  return atom;
}

extern void hdlr_exit(struct hdlr_t* atom)
{
  if(atom->name_)
  {
    free(atom->name_);
  }
  free(atom);
}

extern struct minf_t* minf_init()
{
  struct minf_t* atom = (struct minf_t*)malloc(sizeof(struct minf_t));
  atom->unknown_atoms_ = 0;
  atom->vmhd_ = 0;
  atom->smhd_ = 0;
  atom->dinf_ = 0;
  atom->stbl_ = 0;

  return atom;
}

extern void minf_exit(struct minf_t* atom)
{
  if(atom->unknown_atoms_)
  {
    unknown_atom_exit(atom->unknown_atoms_);
  }
  if(atom->vmhd_)
  {
    vmhd_exit(atom->vmhd_);
  }
  if(atom->smhd_)
  {
    smhd_exit(atom->smhd_);
  }
  if(atom->dinf_)
  {
    dinf_exit(atom->dinf_);
  }
  if(atom->stbl_)
  {
    stbl_exit(atom->stbl_);
  }
  free(atom);
}

extern struct vmhd_t* vmhd_init()
{
  struct vmhd_t* atom = (struct vmhd_t*)malloc(sizeof(struct vmhd_t));

  return atom;
}

extern vmhd_t* vmhd_copy(vmhd_t* rhs)
{
  vmhd_t* atom = (vmhd_t*)malloc(sizeof(vmhd_t));

  memcpy(atom, rhs, sizeof(vmhd_t));

  return atom;
}

extern void vmhd_exit(struct vmhd_t* atom)
{
  free(atom);
}

extern struct smhd_t* smhd_init()
{
  struct smhd_t* atom = (struct smhd_t*)malloc(sizeof(struct smhd_t));

  return atom;
}

extern smhd_t* smhd_copy(smhd_t* rhs)
{
  smhd_t* atom = (smhd_t*)malloc(sizeof(smhd_t));

  memcpy(atom, rhs, sizeof(smhd_t));

  return atom;
}

extern void smhd_exit(struct smhd_t* atom)
{
  free(atom);
}

extern dinf_t* dinf_init()
{
  dinf_t* atom = (dinf_t*)malloc(sizeof(dinf_t));

  atom->dref_ = 0;

  return atom;
}

extern dinf_t* dinf_copy(dinf_t* rhs)
{
  dinf_t* atom = (dinf_t*)malloc(sizeof(dinf_t));

  atom->dref_ = dref_copy(rhs->dref_);

  return atom;
}

extern void dinf_exit(dinf_t* atom)
{
  if(atom->dref_)
  {
    dref_exit(atom->dref_);
  }
  free(atom);
}

extern dref_t* dref_init()
{
  dref_t* atom = (dref_t*)malloc(sizeof(dref_t));

  atom->version_ = 0;
  atom->flags_ = 0;
  atom->entry_count_ = 0;
  atom->table_ = 0;

  return atom;
}

extern dref_t* dref_copy(dref_t const* rhs)
{
  unsigned int i;
  dref_t* atom = (dref_t*)malloc(sizeof(dref_t));

  atom->version_ = rhs->version_;
  atom->flags_ = rhs->flags_;
  atom->entry_count_ = rhs->entry_count_;
  atom->table_ = atom->entry_count_ == 0 ? NULL : (dref_table_t*)malloc(atom->entry_count_ * sizeof(dref_table_t));
  for(i = 0; i != atom->entry_count_; ++i)
  {
    dref_table_assign(&atom->table_[i], &rhs->table_[i]);
  }

  return atom;
}

extern void dref_exit(dref_t* atom)
{
  unsigned int i;
  for(i = 0; i != atom->entry_count_; ++i)
  {
    dref_table_exit(&atom->table_[i]);
  }
  if(atom->table_)
  {
    free(atom->table_);
  }
  free(atom);
}

extern void dref_table_init(dref_table_t* entry)
{
  entry->flags_ = 0;
  entry->name_ = 0;
  entry->location_ = 0;
}

extern void dref_table_assign(dref_table_t* lhs, dref_table_t const* rhs)
{
  lhs->flags_ = rhs->flags_;
  lhs->name_ = rhs->name_ == NULL ? NULL : strdup(rhs->name_);
  lhs->location_ = rhs->location_ == NULL ? NULL : strdup(rhs->location_);
}

extern void dref_table_exit(dref_table_t* entry)
{
  if(entry->name_)
  {
    free(entry->name_);
  }
  if(entry->location_)
  {
    free(entry->location_);
  }
}

extern struct stbl_t* stbl_init()
{
  struct stbl_t* atom = (struct stbl_t*)malloc(sizeof(struct stbl_t));
  atom->unknown_atoms_ = 0;
  atom->stsd_ = 0;
  atom->stts_ = 0;
  atom->stss_ = 0;
  atom->stsc_ = 0;
  atom->stsz_ = 0;
  atom->stco_ = 0;
  atom->ctts_ = 0;

  return atom;
}

extern void stbl_exit(struct stbl_t* atom)
{
  if(atom->unknown_atoms_)
  {
    unknown_atom_exit(atom->unknown_atoms_);
  }
  if(atom->stsd_)
  {
    stsd_exit(atom->stsd_);
  }
  if(atom->stts_)
  {
    stts_exit(atom->stts_);
  }
  if(atom->stss_)
  {
    stss_exit(atom->stss_);
  }
  if(atom->stsc_)
  {
    stsc_exit(atom->stsc_);
  }
  if(atom->stsz_)
  {
    stsz_exit(atom->stsz_);
  }
  if(atom->stco_)
  {
    stco_exit(atom->stco_);
  }
  if(atom->ctts_)
  {
    ctts_exit(atom->ctts_);
  }

  free(atom);
}

extern unsigned int stbl_get_nearest_keyframe(struct stbl_t const* stbl,
                                              unsigned int sample)
{
  // If the sync atom is not present, all samples are implicit sync samples.
  if(!stbl->stss_)
    return sample;

  return stss_get_nearest_keyframe(stbl->stss_, sample);
}

extern struct stsd_t* stsd_init()
{
  struct stsd_t* atom = (struct stsd_t*)malloc(sizeof(struct stsd_t));
  atom->entries_ = 0;
  atom->sample_entries_ = 0;

  return atom;
}

extern stsd_t* stsd_copy(stsd_t const* rhs)
{
  unsigned int i;
  struct stsd_t* atom = (struct stsd_t*)malloc(sizeof(struct stsd_t));

  atom->version_ = rhs->version_;
  atom->flags_ = rhs->flags_;
  atom->entries_ = rhs->entries_;
  atom->sample_entries_ =
    (sample_entry_t*)malloc(atom->entries_ * sizeof(sample_entry_t));
  for(i = 0; i != atom->entries_; ++i)
  {
    sample_entry_assign(&atom->sample_entries_[i], &rhs->sample_entries_[i]);
  }

  return atom;
}

extern void stsd_exit(struct stsd_t* atom)
{
  unsigned int i;
  for(i = 0; i != atom->entries_; ++i)
  {
    struct sample_entry_t* sample_entry = &atom->sample_entries_[i];
    sample_entry_exit(sample_entry);
  }
  if(atom->sample_entries_)
  {
    free(atom->sample_entries_);
  }
  free(atom);
}

extern void sample_entry_init(struct sample_entry_t* sample_entry)
{
  sample_entry->len_ = 0;
  sample_entry->buf_ = 0;
  sample_entry->codec_private_data_length_ = 0;
  sample_entry->codec_private_data_ = 0;

  sample_entry->nal_unit_length_ = 0;
  sample_entry->sps_length_ = 0;
  sample_entry->sps_ = 0;
  sample_entry->pps_length_ = 0;
  sample_entry->pps_ = 0;

  sample_entry->wFormatTag = 0;
  sample_entry->nChannels = 2;
  sample_entry->nSamplesPerSec = 44100;
  sample_entry->nAvgBytesPerSec = 0;
  sample_entry->nBlockAlign = 0;
  sample_entry->wBitsPerSample = 16;
}

extern void sample_entry_assign(sample_entry_t* lhs, sample_entry_t const* rhs)
{
  memcpy(lhs, rhs, sizeof(sample_entry_t));
  if(rhs->buf_ != NULL)
  {
    lhs->buf_ = (unsigned char*)malloc(rhs->len_);
    memcpy(lhs->buf_, rhs->buf_, rhs->len_);
  }
}

extern void sample_entry_exit(struct sample_entry_t* sample_entry)
{
  if(sample_entry->buf_)
  {
    free(sample_entry->buf_);
  }
}

extern struct stts_t* stts_init()
{
  struct stts_t* atom = (struct stts_t*)malloc(sizeof(struct stts_t));
  atom->version_ = 0;
  atom->flags_ = 0;
  atom->entries_ = 0;
  atom->table_ = 0;

  return atom;
}

extern void stts_exit(struct stts_t* atom)
{
  if(atom->table_)
  {
    free(atom->table_);
  }
  free(atom);
}

extern unsigned int stts_get_sample(struct stts_t const* stts, uint64_t time)
{
  unsigned int stts_index = 0;
  unsigned int stts_count;

  unsigned int ret = 0;
  uint64_t time_count = 0;

  for(; stts_index != stts->entries_; ++stts_index)
  {
    unsigned int sample_count = stts->table_[stts_index].sample_count_;
    unsigned int sample_duration = stts->table_[stts_index].sample_duration_;
    if(time_count + (uint64_t)sample_duration * (uint64_t)sample_count >= time)
    {
      stts_count = (unsigned int)((time - time_count + sample_duration - 1) / sample_duration);
      time_count += (uint64_t)stts_count * (uint64_t)sample_duration;
      ret += stts_count;
      break;
    }
    else
    {
      time_count += (uint64_t)sample_duration * (uint64_t)sample_count;
      ret += sample_count;
    }
  }
  return ret;
}

extern uint64_t stts_get_time(struct stts_t const* stts, unsigned int sample)
{
  uint64_t ret = 0;
  unsigned int stts_index = 0;
  unsigned int sample_count = 0;
  
  for(;;)
  {
    unsigned int table_sample_count = stts->table_[stts_index].sample_count_;
    unsigned int table_sample_duration = stts->table_[stts_index].sample_duration_;
    if(sample_count + table_sample_count > sample)
    {
      unsigned int stts_count = (sample - sample_count);
      ret += (uint64_t)stts_count * (uint64_t)table_sample_duration;
      break;
    }
    else
    {
      sample_count += table_sample_count;
      ret += (uint64_t)table_sample_count * (uint64_t)table_sample_duration;
      stts_index++;
    }
  }
  return ret;
}

extern uint64_t stts_get_duration(struct stts_t const* stts)
{
  uint64_t duration = 0;
  unsigned int i;
  for(i = 0; i != stts->entries_; ++i)
  {
    unsigned int sample_count = stts->table_[i].sample_count_;
    unsigned int sample_duration = stts->table_[i].sample_duration_;
    duration += (uint64_t)sample_duration * (uint64_t)sample_count;
  }

  return duration;
}

extern unsigned int stts_get_samples(struct stts_t const* stts)
{
  unsigned int samples = 0;
  unsigned int entries = stts->entries_;
  unsigned int i;
  for(i = 0; i != entries; ++i)
  {
    unsigned int sample_count = stts->table_[i].sample_count_;
//  unsigned int sample_duration = stts->table_[i].sample_duration_;
    samples += sample_count;
  }

  return samples;
}

extern struct stss_t* stss_init()
{
  struct stss_t* atom = (struct stss_t*)malloc(sizeof(struct stss_t));
  atom->sample_numbers_ = 0;

  return atom;
}

extern void stss_exit(struct stss_t* atom)
{
  if(atom->sample_numbers_)
  {
    free(atom->sample_numbers_);
  }
  free(atom);
}

extern unsigned int stss_get_nearest_keyframe(struct stss_t const* stss,
                                              unsigned int sample)
{
  // scan the sync samples to find the key frame that precedes the sample number
  unsigned int i;
  unsigned int table_sample = 0;
  for(i = 0; i != stss->entries_; ++i)
  {
    table_sample = stss->sample_numbers_[i];
    if(table_sample >= sample)
      break;
  }
  if(table_sample == sample)
    return table_sample;
  else
    return stss->sample_numbers_[i - 1];
}


extern struct stsc_t* stsc_init()
{
  struct stsc_t* atom = (struct stsc_t*)malloc(sizeof(struct stsc_t));
  atom->table_ = 0;

  return atom;
}

extern void stsc_exit(struct stsc_t* atom)
{
  if(atom->table_)
  {
    free(atom->table_);
  }
  free(atom);
}

extern struct stsz_t* stsz_init()
{
  struct stsz_t* atom = (struct stsz_t*)malloc(sizeof(struct stsz_t));
  atom->sample_sizes_ = 0;

  return atom;
}

extern void stsz_exit(struct stsz_t* atom)
{
  if(atom->sample_sizes_)
  {
    free(atom->sample_sizes_);
  }
  free(atom);
}

extern struct stco_t* stco_init()
{
  struct stco_t* atom = (struct stco_t*)malloc(sizeof(struct stco_t));
  atom->chunk_offsets_ = 0;

  return atom;
}

extern void stco_exit(struct stco_t* atom)
{
  if(atom->chunk_offsets_)
  {
    free(atom->chunk_offsets_);
  }
  free(atom);
}

extern struct ctts_t* ctts_init()
{
  struct ctts_t* atom = (struct ctts_t*)malloc(sizeof(struct ctts_t));
  atom->version_ = 0;
  atom->flags_ = 0;
  atom->entries_ = 0;
  atom->table_ = 0;

  return atom;
}

extern void ctts_exit(struct ctts_t* atom)
{
  if(atom->table_)
  {
    free(atom->table_);
  }
  free(atom);
}

extern unsigned int ctts_get_samples(struct ctts_t const* ctts)
{
  unsigned int samples = 0;
  unsigned int entries = ctts->entries_;
  unsigned int i;
  for(i = 0; i != entries; ++i)
  {
    unsigned int sample_count = ctts->table_[i].sample_count_;
//  unsigned int sample_offset = ctts->table_[i].sample_offset_;
    samples += sample_count;
  }

  return samples;
}

extern uint64_t moov_time_to_trak_time(uint64_t t, long moov_time_scale,
                                       long trak_time_scale)
{
  return t * (uint64_t)trak_time_scale / moov_time_scale;
}

extern uint64_t trak_time_to_moov_time(uint64_t t, long moov_time_scale,
                                       long trak_time_scale)
{
  return t * (uint64_t)moov_time_scale / trak_time_scale;
}

// End Of File

