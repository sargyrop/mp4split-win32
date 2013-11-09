/*******************************************************************************
 mp4_io.h - A library for general MPEG4 I/O.

 Copyright (C) 2007-2009 CodeShop B.V.
 http://www.code-shop.com

 For licensing see the LICENSE file
******************************************************************************/ 

#ifndef MP4_IO_H_AKW
#define MP4_IO_H_AKW

#include "mod_streaming_export.h"

#ifndef _MSC_VER
#include <inttypes.h>
#else
#include "inttypes.h"
#endif
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef UNUSED
#elif defined(__GNUC__)
# define UNUSED(x) UNUSED_ ## x __attribute__((unused))
#elif defined(__LCLINT__)
# define UNUSED(x) /*@unused@*/ x
#else
# define UNUSED(x) x
#endif

#ifdef WIN32
#define ftello _ftelli64
#define fseeko _fseeki64
// #define strdup _strdup
#endif

#define ATOM_PREAMBLE_SIZE 8

#define MAX_TRACKS 8

#define FOURCC(a, b, c, d) ((uint32_t)(a) << 24) + \
                           ((uint32_t)(b) << 16) + \
                           ((uint32_t)(c) << 8) + \
                           ((uint32_t)(d))

#define MP4_INFO(fmt, ...) \
  if(mp4_context->verbose_ > 2) \
  { \
    log_trace("%s.%d: (info) "fmt, remove_path(__FILE__), __LINE__, __VA_ARGS__); \
  }

#define MP4_WARNING(fmt, ...) \
  if(mp4_context->verbose_ > 1) \
  { \
    log_trace("%s.%d: (warning) "fmt, remove_path(__FILE__), __LINE__, __VA_ARGS__); \
  }

#define MP4_ERROR(fmt, ...) \
  if(mp4_context->verbose_ > 0) \
  { \
    log_trace("%s.%d: (error) "fmt, remove_path(__FILE__), __LINE__, __VA_ARGS__); \
  }

MOD_STREAMING_DLL_LOCAL extern uint64_t atoi64(const char* val);

MOD_STREAMING_DLL_LOCAL extern const char* remove_path(const char *path);
MOD_STREAMING_DLL_LOCAL extern void log_trace(const char* fmt, ...);

MOD_STREAMING_DLL_LOCAL extern unsigned int read_8(unsigned char const* buffer);
MOD_STREAMING_DLL_LOCAL extern unsigned char* write_8(unsigned char* buffer, unsigned int v);
MOD_STREAMING_DLL_LOCAL extern uint16_t read_16(unsigned char const* buffer);
MOD_STREAMING_DLL_LOCAL extern unsigned char* write_16(unsigned char* buffer, unsigned int v);
MOD_STREAMING_DLL_LOCAL extern unsigned int read_24(unsigned char const* buffer);
MOD_STREAMING_DLL_LOCAL extern unsigned char* write_24(unsigned char* buffer, unsigned int v);
MOD_STREAMING_DLL_LOCAL extern uint32_t read_32(unsigned char const* buffer);
MOD_STREAMING_DLL_LOCAL extern unsigned char* write_32(unsigned char* buffer, uint32_t v);
MOD_STREAMING_DLL_LOCAL extern uint64_t read_64(unsigned char const* buffer);
MOD_STREAMING_DLL_LOCAL extern unsigned char* write_64(unsigned char* buffer, uint64_t v);
MOD_STREAMING_DLL_LOCAL extern uint32_t read_n(unsigned char const* buffer, unsigned int n);
MOD_STREAMING_DLL_LOCAL extern unsigned char* write_n(unsigned char* buffer, unsigned int n, uint32_t v);

struct mp4_atom_t
{
  uint32_t type_;
  uint32_t short_size_;
  uint64_t size_;
  uint64_t start_;
  uint64_t end_;
};
typedef struct mp4_atom_t mp4_atom_t;

struct mp4_context_t;
MOD_STREAMING_DLL_LOCAL extern
int mp4_atom_read_header(struct mp4_context_t const* mp4_context,
                         FILE* infile, mp4_atom_t* atom);
MOD_STREAMING_DLL_LOCAL extern
int mp4_atom_write_header(unsigned char* outbuffer,
                          mp4_atom_t const* atom);

struct unknown_atom_t
{
  void* atom_;
  struct unknown_atom_t* next_;
};
typedef struct unknown_atom_t unknown_atom_t;
MOD_STREAMING_DLL_LOCAL extern unknown_atom_t* unknown_atom_init();
MOD_STREAMING_DLL_LOCAL extern void unknown_atom_exit(unknown_atom_t* atom);

struct moov_t
{
  struct unknown_atom_t* unknown_atoms_;
  struct mvhd_t* mvhd_;
  unsigned int tracks_;
  struct trak_t* traks_[MAX_TRACKS];
};
typedef struct moov_t moov_t;
MOD_STREAMING_DLL_LOCAL extern moov_t* moov_init();
MOD_STREAMING_DLL_LOCAL extern void moov_exit(moov_t* atom);

struct mvhd_t
{
  unsigned int version_;
  unsigned int flags_;
  uint64_t creation_time_;
  uint64_t modification_time_;
  uint32_t timescale_;
  uint64_t duration_;
  uint32_t rate_;
  uint16_t volume_;
  uint16_t reserved1_;
  uint32_t reserved2_[2];
  uint32_t matrix_[9];
  uint32_t predefined_[6];
  uint32_t next_track_id_;
};
typedef struct mvhd_t mvhd_t;
MOD_STREAMING_DLL_LOCAL extern mvhd_t* mvhd_init();
MOD_STREAMING_DLL_LOCAL extern mvhd_t* mvhd_copy(mvhd_t const* rhs);
MOD_STREAMING_DLL_LOCAL extern void mvhd_exit(mvhd_t* atom);

struct trak_t
{
  struct unknown_atom_t* unknown_atoms_;
  struct tkhd_t* tkhd_;
  struct mdia_t* mdia_;

  /* temporary indices */
  unsigned int chunks_size_;
  struct chunks_t* chunks_;

  unsigned int samples_size_;
  struct samples_t* samples_;
};
typedef struct trak_t trak_t;
MOD_STREAMING_DLL_LOCAL extern trak_t* trak_init();
MOD_STREAMING_DLL_LOCAL extern void trak_exit(trak_t* trak);

struct tkhd_t
{
  unsigned int version_;
  unsigned int flags_;
  uint64_t creation_time_;
  uint64_t modification_time_;
  uint32_t track_id_;
  uint32_t reserved_;
  uint64_t duration_;
  uint32_t reserved2_[2];
  uint16_t layer_;
  uint16_t predefined_;
  uint16_t volume_;
  uint16_t reserved3_;
  uint32_t matrix_[9];
  uint32_t width_;
  uint32_t height_;
};
typedef struct tkhd_t tkhd_t;
MOD_STREAMING_DLL_LOCAL extern tkhd_t* tkhd_init();
MOD_STREAMING_DLL_LOCAL extern tkhd_t* tkhd_copy(tkhd_t const* rhs);
MOD_STREAMING_DLL_LOCAL extern void tkhd_exit(tkhd_t* tkhd);

struct mdia_t
{
  struct unknown_atom_t* unknown_atoms_;
  struct mdhd_t* mdhd_;
  struct hdlr_t* hdlr_;
  struct minf_t* minf_;
};
typedef struct mdia_t mdia_t;
MOD_STREAMING_DLL_LOCAL extern mdia_t* mdia_init();
MOD_STREAMING_DLL_LOCAL extern void mdia_exit(mdia_t* atom);

struct mdhd_t
{
  unsigned int version_;
  unsigned int flags_;
  uint64_t creation_time_;
  uint64_t modification_time_;
  uint32_t timescale_;
  uint64_t duration_;
  unsigned int language_[3];
  uint16_t predefined_;
};
typedef struct mdhd_t mdhd_t;
MOD_STREAMING_DLL_LOCAL extern struct mdhd_t* mdhd_init();
MOD_STREAMING_DLL_LOCAL extern mdhd_t* mdhd_copy(mdhd_t const* rhs);
MOD_STREAMING_DLL_LOCAL extern void mdhd_exit(struct mdhd_t* mdhd);

struct hdlr_t
{
  unsigned int version_;
  unsigned int flags_;
  uint32_t predefined_;
  uint32_t handler_type_;
  uint32_t reserved1_;
  uint32_t reserved2_;
  uint32_t reserved3_;
  char* name_;
};
typedef struct hdlr_t hdlr_t;
MOD_STREAMING_DLL_LOCAL extern hdlr_t* hdlr_init();
MOD_STREAMING_DLL_LOCAL extern hdlr_t* hdlr_copy(hdlr_t const* rhs);
MOD_STREAMING_DLL_LOCAL extern void hdlr_exit(hdlr_t* atom);

struct minf_t
{
  struct unknown_atom_t* unknown_atoms_;
  struct vmhd_t* vmhd_;
  struct smhd_t* smhd_;
  struct dinf_t* dinf_;
  struct stbl_t* stbl_;
};
typedef struct minf_t minf_t;
MOD_STREAMING_DLL_LOCAL extern minf_t* minf_init();
MOD_STREAMING_DLL_LOCAL extern void minf_exit(minf_t* atom);

struct vmhd_t
{
  unsigned int version_;
  unsigned int flags_;
  uint16_t graphics_mode_;
  uint16_t opcolor_[3];
};
typedef struct vmhd_t vmhd_t;
MOD_STREAMING_DLL_LOCAL extern vmhd_t* vmhd_init();
MOD_STREAMING_DLL_LOCAL extern vmhd_t* vmhd_copy(vmhd_t* rhs);
MOD_STREAMING_DLL_LOCAL extern void vmhd_exit(vmhd_t* atom);

struct smhd_t
{
  unsigned int version_;
  unsigned int flags_;
  uint16_t balance_;
  uint16_t reserved_;
};
typedef struct smhd_t smhd_t;
MOD_STREAMING_DLL_LOCAL extern smhd_t* smhd_init();
MOD_STREAMING_DLL_LOCAL extern smhd_t* smhd_copy(smhd_t* rhs);
MOD_STREAMING_DLL_LOCAL extern void smhd_exit(smhd_t* atom);

struct dinf_t
{
  struct dref_t* dref_;
};
typedef struct dinf_t dinf_t;
MOD_STREAMING_DLL_LOCAL extern dinf_t* dinf_init();
MOD_STREAMING_DLL_LOCAL extern dinf_t* dinf_copy(dinf_t* rhs);
MOD_STREAMING_DLL_LOCAL extern void dinf_exit(dinf_t* atom);

struct dref_table_t
{
  unsigned int flags_;          // 0x000001 is self contained
  char* name_;                  // name is a URN
  char* location_;              // location is a URL
};
typedef struct dref_table_t dref_table_t;
MOD_STREAMING_DLL_LOCAL extern void dref_table_init(dref_table_t* entry);
MOD_STREAMING_DLL_LOCAL extern void dref_table_assign(dref_table_t* lhs, dref_table_t const* rhs);
MOD_STREAMING_DLL_LOCAL extern void dref_table_exit(dref_table_t* entry);

struct dref_t
{
  unsigned int version_;
  unsigned int flags_;
  unsigned int entry_count_;
  dref_table_t* table_;
};
typedef struct dref_t dref_t;
MOD_STREAMING_DLL_LOCAL extern dref_t* dref_init();
MOD_STREAMING_DLL_LOCAL extern dref_t* dref_copy(dref_t const* rhs);
MOD_STREAMING_DLL_LOCAL extern void dref_exit(dref_t* atom);

struct stbl_t
{
  struct unknown_atom_t* unknown_atoms_;
  struct stsd_t* stsd_;         // sample description
  struct stts_t* stts_;         // decoding time-to-sample
  struct stss_t* stss_;         // sync sample
  struct stsc_t* stsc_;         // sample-to-chunk
  struct stsz_t* stsz_;         // sample size
  struct stco_t* stco_;         // chunk offset
  struct ctts_t* ctts_;         // composition time-to-sample
};
typedef struct stbl_t stbl_t;
MOD_STREAMING_DLL_LOCAL extern stbl_t* stbl_init();
MOD_STREAMING_DLL_LOCAL extern void stbl_exit(stbl_t* atom);
MOD_STREAMING_DLL_LOCAL extern
unsigned int stbl_get_nearest_keyframe(stbl_t const* stbl, unsigned int sample);

struct stsd_t
{
  unsigned int version_;
  unsigned int flags_;
  uint32_t entries_;
  struct sample_entry_t* sample_entries_;
};
typedef struct stsd_t stsd_t;
MOD_STREAMING_DLL_LOCAL extern stsd_t* stsd_init();
MOD_STREAMING_DLL_LOCAL extern stsd_t* stsd_copy(stsd_t const* rhs);
MOD_STREAMING_DLL_LOCAL extern void stsd_exit(stsd_t* atom);

struct sample_entry_t
{
  unsigned int len_;
  uint32_t fourcc_;
  unsigned char* buf_;

  unsigned int codec_private_data_length_;
  unsigned char* codec_private_data_;

  // avcC
  unsigned int nal_unit_length_;
  unsigned int sps_length_;
  unsigned char* sps_;
  unsigned int pps_length_;
  unsigned char* pps_;

  // sound (WAVEFORMATEX) structure
  uint16_t wFormatTag;
  uint16_t nChannels;
  uint32_t nSamplesPerSec;
  uint32_t nAvgBytesPerSec;
  uint16_t nBlockAlign;
  uint16_t wBitsPerSample;

  unsigned int samplerate_hi_;
  unsigned int samplerate_lo_;

  // esds
  unsigned int max_bitrate_;
  unsigned int avg_bitrate_;
};
typedef struct sample_entry_t sample_entry_t;
MOD_STREAMING_DLL_LOCAL extern
void sample_entry_init(sample_entry_t* sample_entry);
MOD_STREAMING_DLL_LOCAL extern
void sample_entry_assign(sample_entry_t* lhs, sample_entry_t const* rhs);
MOD_STREAMING_DLL_LOCAL extern
void sample_entry_exit(sample_entry_t* sample_entry);

struct stts_t
{
  unsigned int version_;
  unsigned int flags_;
  uint32_t entries_;
  struct stts_table_t* table_;
};
typedef struct stts_t stts_t;
MOD_STREAMING_DLL_LOCAL extern stts_t* stts_init();
MOD_STREAMING_DLL_LOCAL extern void stts_exit(stts_t* atom);
MOD_STREAMING_DLL_LOCAL extern unsigned int stts_get_sample(stts_t const* stts, uint64_t time);
MOD_STREAMING_DLL_LOCAL extern uint64_t stts_get_time(stts_t const* stts, unsigned int sample);
MOD_STREAMING_DLL_LOCAL extern uint64_t stts_get_duration(stts_t const* stts);
MOD_STREAMING_DLL_LOCAL extern unsigned int stts_get_samples(stts_t const* stts);

struct stts_table_t
{
  uint32_t sample_count_;
  uint32_t sample_duration_;
};
typedef struct stts_table_t stts_table_t;

struct stss_t
{
  unsigned int version_;
  unsigned int flags_;
  uint32_t entries_;
  uint32_t* sample_numbers_;
};
typedef struct stss_t stss_t;
MOD_STREAMING_DLL_LOCAL extern stss_t* stss_init();
MOD_STREAMING_DLL_LOCAL extern void stss_exit(stss_t* atom);
MOD_STREAMING_DLL_LOCAL extern
unsigned int stss_get_nearest_keyframe(stss_t const* stss, unsigned int sample);

struct stsc_t
{
  unsigned int version_;
  unsigned int flags_;
  uint32_t entries_;
  struct stsc_table_t* table_;
};
typedef struct stsc_t stsc_t;
MOD_STREAMING_DLL_LOCAL extern stsc_t* stsc_init();
MOD_STREAMING_DLL_LOCAL extern void stsc_exit(stsc_t* atom);

struct stsc_table_t
{
  uint32_t chunk_;
  uint32_t samples_;
  uint32_t id_;
};
typedef struct stsc_table_t stsc_table_t;

struct stsz_t
{
  unsigned int version_;
  unsigned int flags_;
  uint32_t sample_size_;
  uint32_t entries_;
  uint32_t* sample_sizes_;
};
typedef struct stsz_t stsz_t;
MOD_STREAMING_DLL_LOCAL extern stsz_t* stsz_init();
MOD_STREAMING_DLL_LOCAL extern void stsz_exit(stsz_t* atom);

struct stco_t
{
  unsigned int version_;
  unsigned int flags_;
  uint32_t entries_;
  uint64_t* chunk_offsets_;

  void* stco_inplace_;          // newly generated stco (patched inplace)
};
typedef struct stco_t stco_t;
MOD_STREAMING_DLL_LOCAL extern stco_t* stco_init();
MOD_STREAMING_DLL_LOCAL extern void stco_exit(stco_t* atom);

struct ctts_t
{
  unsigned int version_;
  unsigned int flags_;
  uint32_t entries_;
  struct ctts_table_t* table_;
};
typedef struct ctts_t ctts_t;
MOD_STREAMING_DLL_LOCAL extern ctts_t* ctts_init();
MOD_STREAMING_DLL_LOCAL extern void ctts_exit(ctts_t* atom);
MOD_STREAMING_DLL_LOCAL extern unsigned int ctts_get_samples(ctts_t const* ctts);

struct ctts_table_t
{
  uint32_t sample_count_;
  uint32_t sample_offset_;
};
typedef struct ctts_table_t ctts_table_t;

struct samples_t
{
  uint64_t pts_;                // decoding/presentation time
  unsigned int size_;           // size in bytes
  uint64_t pos_;                // byte offset
  unsigned int cto_;            // composition time offset

  unsigned int is_ss_:1;        // sync sample
  unsigned int is_smooth_ss_:1; // sync sample for smooth streaming
};
typedef struct samples_t samples_t;

struct chunks_t
{
  unsigned int sample_;         // number of the first sample in the chunk
  unsigned int size_;           // number of samples in the chunk
  int id_;                      // not used
  uint64_t pos_;                // start byte position of chunk
};
typedef struct chunks_t chunks_t;

MOD_STREAMING_DLL_LOCAL extern
uint64_t moov_time_to_trak_time(uint64_t t, long moov_time_scale,
                                long trak_time_scale);
MOD_STREAMING_DLL_LOCAL extern
uint64_t trak_time_to_moov_time(uint64_t t, long moov_time_scale,
                                long trak_time_scale);

struct mp4_context_t
{
  char* filename_;
  FILE* infile;

  int verbose_;

  // the atoms as found in the stream
  mp4_atom_t ftyp_atom;
  mp4_atom_t moov_atom;
  mp4_atom_t mdat_atom;
  mp4_atom_t mfra_atom;

  // the actual binary data
  unsigned char* moov_data;
  unsigned char* mfra_data;

  // the parsed atoms
  moov_t* moov;
};
typedef struct mp4_context_t mp4_context_t;

MOD_STREAMING_DLL_LOCAL extern
mp4_context_t* mp4_open(const char* filename, int64_t filesize, int mfra_only, int verbose);

MOD_STREAMING_DLL_LOCAL extern void mp4_close(mp4_context_t* mp4_context);

#ifdef __cplusplus
} /* extern C definitions */
#endif

#endif // MP4_IO_H_AKW

// End Of File

