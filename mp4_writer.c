/*******************************************************************************
 mp4_writer.c - A library for writing MPEG4.

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

#include "mp4_writer.h"
#include "mp4_io.h"
#include <stdlib.h>
#include <string.h>

static unsigned char* atom_writer_unknown(unknown_atom_t* atoms,
                                          unsigned char* buffer)
{
  while(atoms)
  {
    size_t size = read_32((const unsigned char*)atoms->atom_);
    memcpy(buffer, atoms->atom_, size);
    buffer += size;
    atoms = atoms->next_;
  }

  return buffer;
}

extern unsigned char* atom_writer(struct unknown_atom_t* unknown_atoms,
                                  atom_write_list_t* atom_write_list,
                                  unsigned int atom_write_list_size,
                                  unsigned char* buffer)
{
  unsigned i;
  const int write_box64 = 0;

  if(unknown_atoms)
  {
    buffer = atom_writer_unknown(unknown_atoms, buffer);
  }

  for(i = 0; i != atom_write_list_size; ++i)
  {
    if(atom_write_list[i].source_ != 0)
    {
      unsigned char* atom_start = buffer;
      // atom size
      if(write_box64)
      {
        write_32(buffer, 1); // box64
      }
      buffer += 4;

      // atom type
      buffer = write_32(buffer, atom_write_list[i].type_);
      if(write_box64)
      {
        buffer += 8; // box64
      }

      // atom payload
      buffer = atom_write_list[i].writer_(atom_write_list[i].source_, buffer);

      if(write_box64)
        write_64(atom_start + 8, buffer - atom_start);
      else
        write_32(atom_start, (uint32_t)(buffer - atom_start));
    }
  }

  return buffer;
}

static unsigned char* tkhd_write(void const* atom, unsigned char* buffer)
{
  tkhd_t const* tkhd = (tkhd_t const*)atom;
  unsigned int i;

  buffer = write_8(buffer, tkhd->version_);
  buffer = write_24(buffer, tkhd->flags_);

  if(tkhd->version_ == 0)
  {
    buffer = write_32(buffer, (uint32_t)tkhd->creation_time_);
    buffer = write_32(buffer, (uint32_t)tkhd->modification_time_);
    buffer = write_32(buffer, tkhd->track_id_);
    buffer = write_32(buffer, tkhd->reserved_);
    buffer = write_32(buffer, (uint32_t)tkhd->duration_);
  }
  else
  {
    buffer = write_64(buffer, tkhd->creation_time_);
    buffer = write_64(buffer, tkhd->modification_time_);
    buffer = write_32(buffer, tkhd->track_id_);
    buffer = write_32(buffer, tkhd->reserved_);
    buffer = write_64(buffer, tkhd->duration_);
  }

  buffer = write_32(buffer, tkhd->reserved2_[0]);
  buffer = write_32(buffer, tkhd->reserved2_[1]);
  buffer = write_16(buffer, tkhd->layer_);
  buffer = write_16(buffer, tkhd->predefined_);
  buffer = write_16(buffer, tkhd->volume_);
  buffer = write_16(buffer, tkhd->reserved3_);

  for(i = 0; i != 9; ++i)
  {
    buffer = write_32(buffer, tkhd->matrix_[i]);
  }

  buffer = write_32(buffer, tkhd->width_);
  buffer = write_32(buffer, tkhd->height_);

  return buffer;
}

static unsigned char* mdhd_write(void const* atom, unsigned char* buffer)
{
  mdhd_t const* mdhd = (mdhd_t const*)atom;

  buffer = write_8(buffer, mdhd->version_);
  buffer = write_24(buffer, mdhd->flags_);

  if(mdhd->version_ == 0)
  {
    buffer = write_32(buffer, (uint32_t)mdhd->creation_time_);
    buffer = write_32(buffer, (uint32_t)mdhd->modification_time_);
    buffer = write_32(buffer, mdhd->timescale_);
    buffer = write_32(buffer, (uint32_t)mdhd->duration_);
  }
  else
  {
    buffer = write_64(buffer, mdhd->creation_time_);
    buffer = write_64(buffer, mdhd->modification_time_);
    buffer = write_32(buffer, mdhd->timescale_);
    buffer = write_64(buffer, mdhd->duration_);
  }

  buffer = write_16(buffer,
                    ((mdhd->language_[0] - 0x60) << 10) +
                    ((mdhd->language_[1] - 0x60) << 5) +
                    ((mdhd->language_[2] - 0x60) << 0));

  buffer = write_16(buffer, mdhd->predefined_);

  return buffer;
}

static unsigned char* vmhd_write(void const* atom, unsigned char* buffer)
{
  vmhd_t const* vmhd = (vmhd_t const*)atom;
  unsigned int i;

  buffer = write_8(buffer, vmhd->version_);
  buffer = write_24(buffer, vmhd->flags_);
  buffer = write_16(buffer, vmhd->graphics_mode_);
  for(i = 0; i != 3; ++i)
  {
    buffer = write_16(buffer, vmhd->opcolor_[i]);
  }

  return buffer;
}

static unsigned char* smhd_write(void const* atom, unsigned char* buffer)
{
  smhd_t const* smhd = (smhd_t const*)atom;

  buffer = write_8(buffer, smhd->version_);
  buffer = write_24(buffer, smhd->flags_);

  buffer = write_16(buffer, smhd->balance_);
  buffer = write_16(buffer, smhd->reserved_);

  return buffer;
}

static unsigned char* dref_write(void const* atom, unsigned char* buffer)
{
  unsigned int i;
  dref_t const* dref = (dref_t const*)atom;

  buffer = write_8(buffer, dref->version_);
  buffer = write_24(buffer, dref->flags_);
  buffer = write_32(buffer, dref->entry_count_);

  for(i = 0; i != dref->entry_count_; ++i)
  {
    dref_table_t* entry = &dref->table_[i];
    if(entry->flags_ == 0x000001)
    {
      write_32(buffer + 0, 12);
      write_32(buffer + 4, FOURCC('u', 'r', 'l', ' '));
      write_32(buffer + 8, entry->flags_);
      buffer += 12;
    }
    else
    {
    // TODO: implement urn and url
    }
  }

  return buffer;
}

static unsigned char* dinf_write(void const* atom, unsigned char* buffer)
{
  dinf_t const* dinf = (dinf_t const*)atom;
  atom_write_list_t atom_write_list[] = {
    { FOURCC('d', 'r', 'e', 'f'), dinf->dref_, &dref_write },
  };

  buffer = atom_writer(NULL,
                       atom_write_list,
                       sizeof(atom_write_list) / sizeof(atom_write_list[0]),
                       buffer);

  return buffer;
}

static unsigned char* hdlr_write(void const* atom, unsigned char* buffer)
{
  hdlr_t const* hdlr = (hdlr_t const*)atom;
  buffer = write_8(buffer, hdlr->version_);
  buffer = write_24(buffer, hdlr->flags_);

  buffer = write_32(buffer, hdlr->predefined_);
  buffer = write_32(buffer, hdlr->handler_type_);
  buffer = write_32(buffer, hdlr->reserved1_);
  buffer = write_32(buffer, hdlr->reserved2_);
  buffer = write_32(buffer, hdlr->reserved3_);
  if(hdlr->name_)
  {
    char const* p;
    if(hdlr->predefined_ == FOURCC('m', 'h', 'l', 'r'))
    {
      buffer = write_8(buffer, (unsigned int)(strlen(hdlr->name_)));
    }

    for(p = hdlr->name_; *p; ++p)
      buffer = write_8(buffer, *p);
  }

  return buffer;
}

static unsigned char* stsd_write(void const* atom, unsigned char* buffer)
{
  stsd_t const* stsd = (stsd_t const*)atom;
  unsigned int i;

  buffer = write_8(buffer, stsd->version_);
  buffer = write_24(buffer, stsd->flags_);
  buffer = write_32(buffer, stsd->entries_);
  for(i = 0; i != stsd->entries_; ++i)
  {
    sample_entry_t* sample_entry = &stsd->sample_entries_[i];
    unsigned int j = 0;
    buffer = write_32(buffer, sample_entry->len_ + 8);
    buffer = write_32(buffer, sample_entry->fourcc_);
    for(j = 0; j != sample_entry->len_; ++j)
    {
      buffer = write_8(buffer, sample_entry->buf_[j]);
    }
  }

  return buffer;
}

static unsigned char* stts_write(void const* atom, unsigned char* buffer)
{
  stts_t const* stts = (stts_t const*)atom;
  unsigned int i;

  buffer = write_8(buffer, stts->version_);
  buffer = write_24(buffer, stts->flags_);
  buffer = write_32(buffer, stts->entries_);
  for(i = 0; i != stts->entries_; ++i)
  {
    buffer = write_32(buffer, stts->table_[i].sample_count_);
    buffer = write_32(buffer, stts->table_[i].sample_duration_);
  }

  return buffer;
}

static unsigned char* stss_write(void const* atom, unsigned char* buffer)
{
  stss_t const* stss = (stss_t const*)atom;
  unsigned int i;

  buffer = write_8(buffer, stss->version_);
  buffer = write_24(buffer, stss->flags_);
  buffer = write_32(buffer, stss->entries_);
  for(i = 0; i != stss->entries_; ++i)
  {
    buffer = write_32(buffer, stss->sample_numbers_[i]);
  }

  return buffer;
}

static unsigned char* stsc_write(void const* atom, unsigned char* buffer)
{
  stsc_t const* stsc = (stsc_t const*)atom;
  unsigned int i;

  buffer = write_8(buffer, stsc->version_);
  buffer = write_24(buffer, stsc->flags_);
  buffer = write_32(buffer, stsc->entries_);
  for(i = 0; i != stsc->entries_; ++i)
  {
    buffer = write_32(buffer, stsc->table_[i].chunk_ + 1);
    buffer = write_32(buffer, stsc->table_[i].samples_);
    buffer = write_32(buffer, stsc->table_[i].id_);
  }

  return buffer;
}

static unsigned char* stsz_write(void const* atom, unsigned char* buffer)
{
  stsz_t const* stsz = (stsz_t const*)atom;
  unsigned int i;
  unsigned int entries = stsz->sample_size_ ? 0 : stsz->entries_;

  buffer = write_8(buffer, stsz->version_);
  buffer = write_24(buffer, stsz->flags_);
  buffer = write_32(buffer, stsz->sample_size_);
  buffer = write_32(buffer, entries);
  for(i = 0; i != entries; ++i)
  {
    buffer = write_32(buffer, stsz->sample_sizes_[i]);
  }

  return buffer;
}

static unsigned char* stco_write(void const* atom, unsigned char* buffer)
{
  stco_t const* stco = (stco_t const*)atom;
  unsigned int i;

  // newly generated stco (patched inplace)
  ((stco_t*)stco)->stco_inplace_ = buffer;

  buffer = write_8(buffer, stco->version_);
  buffer = write_24(buffer, stco->flags_);
  buffer = write_32(buffer, stco->entries_);
  for(i = 0; i != stco->entries_; ++i)
  {
    buffer = write_32(buffer, (uint32_t)(stco->chunk_offsets_[i]));
  }

  return buffer;
}

static unsigned char* ctts_write(void const* atom, unsigned char* buffer)
{
  ctts_t const* ctts = (ctts_t const*)atom;
  unsigned int i;

  buffer = write_8(buffer, ctts->version_);
  buffer = write_24(buffer, ctts->flags_);
  buffer = write_32(buffer, ctts->entries_);
  for(i = 0; i != ctts->entries_; ++i)
  {
    buffer = write_32(buffer, (uint32_t)(ctts->table_[i].sample_count_));
    buffer = write_32(buffer, (uint32_t)(ctts->table_[i].sample_offset_));
  }

  return buffer;
}

static unsigned char* stbl_write(void const* atom, unsigned char* buffer)
{
  stbl_t const* stbl = (stbl_t const*)atom;
  atom_write_list_t atom_write_list[] = {
    { FOURCC('s', 't', 's', 'd'), stbl->stsd_, &stsd_write },
    { FOURCC('s', 't', 't', 's'), stbl->stts_, &stts_write },
    { FOURCC('s', 't', 's', 's'), stbl->stss_, &stss_write },
    { FOURCC('s', 't', 's', 'c'), stbl->stsc_, &stsc_write },
    { FOURCC('s', 't', 's', 'z'), stbl->stsz_, &stsz_write },
    { FOURCC('s', 't', 'c', 'o'), stbl->stco_, &stco_write },
    { FOURCC('c', 't', 't', 's'), stbl->ctts_, &ctts_write },
  };

  buffer = atom_writer(stbl->unknown_atoms_,
                       atom_write_list,
                       sizeof(atom_write_list) / sizeof(atom_write_list[0]),
                       buffer);

  return buffer;
}

static unsigned char* minf_write(void const* atom, unsigned char* buffer)
{
  minf_t const* minf = (minf_t const*)atom;
  atom_write_list_t atom_write_list[] = {
    { FOURCC('v', 'm', 'h', 'd'), minf->vmhd_, &vmhd_write },
    { FOURCC('s', 'm', 'h', 'd'), minf->smhd_, &smhd_write },
    { FOURCC('d', 'i', 'n', 'f'), minf->dinf_, &dinf_write },
    { FOURCC('s', 't', 'b', 'l'), minf->stbl_, &stbl_write }
  };

  buffer = atom_writer(minf->unknown_atoms_,
                       atom_write_list,
                       sizeof(atom_write_list) / sizeof(atom_write_list[0]),
                       buffer);

  return buffer;
}

static unsigned char* mdia_write(void const* atom, unsigned char* buffer)
{
  mdia_t const* mdia = (mdia_t const*)atom;
  atom_write_list_t atom_write_list[] = {
    { FOURCC('m', 'd', 'h', 'd'), mdia->mdhd_, &mdhd_write },
    { FOURCC('h', 'd', 'l', 'r'), mdia->hdlr_, &hdlr_write },
    { FOURCC('m', 'i', 'n', 'f'), mdia->minf_, &minf_write }
  };

  buffer = atom_writer(mdia->unknown_atoms_,
                       atom_write_list,
                       sizeof(atom_write_list) / sizeof(atom_write_list[0]),
                       buffer);

  return buffer;
}

static unsigned char* trak_write(void const* atom, unsigned char* buffer)
{
  trak_t const* trak = (trak_t const*)atom;
  atom_write_list_t atom_write_list[] = {
    { FOURCC('t', 'k', 'h', 'd'), trak->tkhd_, &tkhd_write },
    { FOURCC('m', 'd', 'i', 'a'), trak->mdia_, &mdia_write }
  };

  buffer = atom_writer(trak->unknown_atoms_,
                       atom_write_list,
                       sizeof(atom_write_list) / sizeof(atom_write_list[0]),
                       buffer);

  return buffer;
}

static unsigned char* mvhd_write(void const* atom, unsigned char* buffer)
{
  mvhd_t const* mvhd = (mvhd_t const*)atom;
  unsigned int i;

  buffer = write_8(buffer, mvhd->version_);
  buffer = write_24(buffer, mvhd->flags_);

  if(mvhd->version_ == 0)
  {
    buffer = write_32(buffer, (uint32_t)mvhd->creation_time_);
    buffer = write_32(buffer, (uint32_t)mvhd->modification_time_);
    buffer = write_32(buffer, mvhd->timescale_);
    buffer = write_32(buffer, (uint32_t)mvhd->duration_);
  }
  else
  {
    buffer = write_64(buffer, mvhd->creation_time_);
    buffer = write_64(buffer, mvhd->modification_time_);
    buffer = write_32(buffer, mvhd->timescale_);
    buffer = write_64(buffer, mvhd->duration_);
  }

  buffer = write_32(buffer, mvhd->rate_);
  buffer = write_16(buffer, mvhd->volume_);
  buffer = write_16(buffer, mvhd->reserved1_);
  buffer = write_32(buffer, mvhd->reserved2_[0]);
  buffer = write_32(buffer, mvhd->reserved2_[1]);

  for(i = 0; i != 9; ++i)
  {
    buffer = write_32(buffer, mvhd->matrix_[i]);
  }

  for(i = 0; i != 6; ++i)
  {
    buffer = write_32(buffer, mvhd->predefined_[i]);
  }

  buffer = write_32(buffer, mvhd->next_track_id_);

  return buffer;
}

extern void moov_write(struct moov_t* atom, unsigned char* buffer)
{
  unsigned i;

  unsigned char* atom_start = buffer;

  atom_write_list_t atom_write_list[] = {
    { FOURCC('m', 'v', 'h', 'd'), atom->mvhd_, &mvhd_write },
  };

  // atom size
  buffer += 4;

  // atom type
  buffer = write_32(buffer, FOURCC('m', 'o', 'o', 'v'));

  buffer = atom_writer(atom->unknown_atoms_,
                       atom_write_list,
                       sizeof(atom_write_list) / sizeof(atom_write_list[0]),
                       buffer);

  for(i = 0; i != atom->tracks_; ++i)
  {
    atom_write_list_t trak_atom_write_list[] = {
      { FOURCC('t', 'r', 'a', 'k'), atom->traks_[i], &trak_write },
    };
    buffer = atom_writer(0,
                         trak_atom_write_list,
                         sizeof(trak_atom_write_list) / sizeof(trak_atom_write_list[0]),
                         buffer);
  }
  write_32(atom_start, (uint32_t)(buffer - atom_start));
}

// End Of File

