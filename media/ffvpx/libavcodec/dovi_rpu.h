/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Stubs for dovi_rpu.{c,h} */

enum {
    FF_DOVI_WRAP_NAL        = 1 << 0, ///< wrap inside NAL RBSP
    FF_DOVI_WRAP_T35        = 1 << 1, ///< wrap inside T.35+EMDF
    FF_DOVI_COMPRESS_RPU    = 1 << 2, ///< enable compression for this RPU
};

typedef struct AVCtx AVContext;

//typedef struct AVDOVICConfRecord {
//} AVDOVIDecoderConfigurationRecord;
//
typedef struct AVDOVIDecoderConfigurationRecord {
    uint8_t dv_version_major;
    uint8_t dv_version_minor;
    uint8_t dv_profile;
    uint8_t dv_level;
    uint8_t rpu_present_flag;
    uint8_t el_present_flag;
    uint8_t bl_present_flag;
    uint8_t dv_bl_signal_compatibility_id;
    uint8_t dv_md_compression;
} AVDOVIDecoderConfigurationRecord;

typedef struct DOVICtx {
  int dv_profile;
  void* logctx;
  int operating_point;
  AVDOVIDecoderConfigurationRecord cfg;
  #define FF_DOVI_AUTOMATIC -1
    int enable;
} DOVIContext;

typedef struct AVDOVIMetadata {
    /**
     * Offset in bytes from the beginning of this structure at which the
     * respective structs start.
     */
    size_t header_offset;   /* AVDOVIRpuDataHeader */
    size_t mapping_offset;  /* AVDOVIDataMapping */
    size_t color_offset;    /* AVDOVIColorMetadata */

    size_t ext_block_offset; /* offset to start of ext blocks array */
    size_t ext_block_size; /* size per element */
    int num_ext_blocks; /* number of extension blocks */

    /* static limit on num_ext_blocks, derived from bitstream limitations */
#define AV_DOVI_MAX_EXT_BLOCKS 32
} AVDOVIMetadata;



static void ff_dovi_ctx_unref(DOVIContext* ctx) {}
static void ff_dovi_update_cfg(DOVIContext* ctx,
                               AVDOVIDecoderConfigurationRecord* record) {}
static int ff_dovi_rpu_parse(DOVIContext* ctx, uint8_t* buf, size_t len,
                             int err_recognition) {
  return 0;
}
static int ff_dovi_attach_side_data(DOVIContext* ctx, AVFrame* frame) {
  return 0;
}

static int ff_dovi_configure(DOVIContext *s, AVCodecContext *avctx) {
  return 0;
}

static int ff_dovi_rpu_generate(DOVIContext *s, const AVDOVIMetadata *metadata,
                                int flags, uint8_t **out_rpu, int *out_size) {
  return 0;
}
