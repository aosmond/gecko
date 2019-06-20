#include <arm_neon.h>

#include "qcmsint.h"

#define FLOATSCALE  (float)(PRECACHE_OUTPUT_SIZE)
#define CLAMPMAXVAL ( ((float) (PRECACHE_OUTPUT_SIZE - 1)) / PRECACHE_OUTPUT_SIZE )
static const float floatScale = FLOATSCALE;
static const float clampMaxValue = CLAMPMAXVAL;

template <bool aSwapRB, bool aHasAlpha, bool aInplace>
static void qcms_transform_data_template_lut_neon(const qcms_transform *transform,
                                                  const unsigned char *src,
                                                  unsigned char *dest,
                                                  size_t length)
{
    /* deref *transform now to avoid it in loop */
    const float *igtbl_r = transform->input_gamma_table_r;
    const float *igtbl_g = transform->input_gamma_table_g;
    const float *igtbl_b = transform->input_gamma_table_b;

    /* deref *transform now to avoid it in loop */
    const uint8_t *otdata_r = &transform->output_table_r->data[0];
    const uint8_t *otdata_g = &transform->output_table_g->data[0];
    const uint8_t *otdata_b = &transform->output_table_b->data[0];

    /* input matrix values never change */
    const float (*mat)[4] = transform->matrix;
    const float32x4_t mat0  = vld1q_f32(mat[0]);
    const float32x4_t mat1  = vld1q_f32(mat[1]);
    const float32x4_t mat2  = vld1q_f32(mat[2]);

    /* these values don't change, either */
    const float32x4_t max   = vld1q_dup_f32(&clampMaxValue);
    const float32x4_t min   = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float32x4_t scale = vld1q_dup_f32(&floatScale);

    /* pixel layout constants */
    const size_t kRIndex = GET_R_INDEX(aSwapRB);
    const size_t kGIndex = GET_G_INDEX(aSwapRB);
    const size_t kBIndex = GET_B_INDEX(aSwapRB);
    const size_t kAIndex = GET_A_INDEX(aSwapRB);
    const size_t kComponents = GET_COMPONENTS_SIZE(aHasAlpha);

    /* working variables */
    float32x4_t vec_r, vec_g, vec_b;
    int32x4_t result;
    unsigned char alpha;
    size_t i;

    /* CYA */
    if (!length)
        return;

    /* one pixel is handled outside of the loop */
    length--;

    /* setup for transforming 1st pixel */
    if (aInplace) {
        vec_r = vld1q_dup_f32(&igtbl_r[dest[kRIndex]]);
        vec_g = vld1q_dup_f32(&igtbl_g[dest[kGIndex]]);
        vec_b = vld1q_dup_f32(&igtbl_b[dest[kBIndex]]);
    } else {
        vec_r = vld1q_dup_f32(&igtbl_r[src[kRIndex]]);
        vec_g = vld1q_dup_f32(&igtbl_g[src[kGIndex]]);
        vec_b = vld1q_dup_f32(&igtbl_b[src[kBIndex]]);
    }
    if (aHasAlpha && !aInplace) {
        alpha = src[kAIndex];
    }
    if (!aInplace) {
        src += kComponents;
    }

    /* transform all but final pixel */

    for (i=0; i<length; i++)
    {
        /* gamma * matrix */
        vec_r = vmulq_f32(vec_r, mat0);
        vec_g = vmulq_f32(vec_g, mat1);
        vec_b = vmulq_f32(vec_b, mat2);

        /* store alpha for this pixel; load alpha for next */
        if (aHasAlpha && !aInplace) {
            dest[kAIndex] = alpha;
            alpha = src[kAIndex];
        }

        /* crunch, crunch, crunch */
        vec_r  = vaddq_f32(vec_r, vaddq_f32(vec_g, vec_b));
        vec_r  = vmaxq_f32(min, vec_r);
        vec_r  = vminq_f32(max, vec_r);
        result = vcvtq_s32_f32(vmulq_f32(vec_r, scale));

        /* use calc'd indices to output RGB values */
        dest[kRIndex] = otdata_r[vgetq_lane_s32(result, 0)];
        dest[kGIndex] = otdata_g[vgetq_lane_s32(result, 1)];
        dest[kBIndex] = otdata_b[vgetq_lane_s32(result, 2)];

        /* load for next loop while store completes */
        if (aInplace) {
            vec_r = vld1q_dup_f32(&igtbl_r[src[kRIndex]]);
            vec_g = vld1q_dup_f32(&igtbl_g[src[kGIndex]]);
            vec_b = vld1q_dup_f32(&igtbl_b[src[kBIndex]]);
            src += kComponents;
        } else {
            vec_r = vld1q_dup_f32(&igtbl_r[dest[kRIndex + kComponents]]);
            vec_g = vld1q_dup_f32(&igtbl_g[dest[kGIndex + kComponents]]);
            vec_b = vld1q_dup_f32(&igtbl_b[dest[kBIndex + kComponents]]);
        }
        dest += kComponents;
    }

    /* handle final (maybe only) pixel */

    vec_r = vmulq_f32(vec_r, mat0);
    vec_g = vmulq_f32(vec_g, mat1);
    vec_b = vmulq_f32(vec_b, mat2);

    if (aHasAlpha && !aInplace) {
        dest[kAIndex] = alpha;
    }

    vec_r  = vaddq_f32(vec_r, vaddq_f32(vec_g, vec_b));
    vec_r  = vmaxq_f32(min, vec_r);
    vec_r  = vminq_f32(max, vec_r);
    result = vcvtq_s32_f32(vmulq_f32(vec_r, scale));

    dest[kRIndex] = otdata_r[vgetq_lane_s32(result, 0)];
    dest[kGIndex] = otdata_g[vgetq_lane_s32(result, 1)];
    dest[kBIndex] = otdata_b[vgetq_lane_s32(result, 2)];
}

void qcms_transform_data_rgb_out_lut_neon(const qcms_transform *transform,
                                          const unsigned char *src,
                                          unsigned char *dest,
                                          size_t length)
{
  qcms_transform_data_template_lut_neon<false, false, false>(transform, src, dest, length);
}

void qcms_transform_data_rgba_out_lut_neon(const qcms_transform *transform,
                                           const unsigned char *src,
                                           unsigned char *dest,
                                           size_t length)
{
  if (src == dest) {
    qcms_transform_data_template_lut_neon<false, true, true>(transform, src, dest, length);
  } else {
    qcms_transform_data_template_lut_neon<false, true, false>(transform, src, dest, length);
  }
}

void qcms_transform_data_bgra_out_lut_neon(const qcms_transform *transform,
                                           const unsigned char *src,
                                           unsigned char *dest,
                                           size_t length)
{
  if (src == dest) {
    qcms_transform_data_template_lut_neon<true, true, true>(transform, src, dest, length);
  } else {
    qcms_transform_data_template_lut_neon<true, true, false>(transform, src, dest, length);
  }
}
