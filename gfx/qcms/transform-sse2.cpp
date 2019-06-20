#include <emmintrin.h>

#include "qcmsint.h"

/* pre-shuffled: just load these into XMM reg instead of load-scalar/shufps sequence */
#define FLOATSCALE  (float)(PRECACHE_OUTPUT_SIZE)
#define CLAMPMAXVAL ( ((float) (PRECACHE_OUTPUT_SIZE - 1)) / PRECACHE_OUTPUT_SIZE )
static const ALIGN(16) float floatScaleX4[4] =
    { FLOATSCALE, FLOATSCALE, FLOATSCALE, FLOATSCALE};
static const ALIGN(16) float clampMaxValueX4[4] =
    { CLAMPMAXVAL, CLAMPMAXVAL, CLAMPMAXVAL, CLAMPMAXVAL};

template <bool aSwapRB, bool aHasAlpha, bool aInplace>
static void qcms_transform_data_template_lut_sse2(const qcms_transform *transform,
                                                  const unsigned char *src,
                                                  unsigned char *dest,
                                                  size_t length)
{
    /* aligned output buffer to copy result into */
    uint32_t ALIGN(16) output[4];

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
    const __m128 mat0  = _mm_load_ps(mat[0]);
    const __m128 mat1  = _mm_load_ps(mat[1]);
    const __m128 mat2  = _mm_load_ps(mat[2]);

    /* these values don't change, either */
    const __m128 max   = _mm_load_ps(clampMaxValueX4);
    const __m128 min   = _mm_setzero_ps();
    const __m128 scale = _mm_load_ps(floatScaleX4);

    /* pixel layout constants */
    const size_t kRIndex = GET_R_INDEX(aSwapRB);
    const size_t kGIndex = GET_G_INDEX(aSwapRB);
    const size_t kBIndex = GET_B_INDEX(aSwapRB);
    const size_t kAIndex = GET_A_INDEX(aSwapRB);
    const size_t kComponents = GET_COMPONENTS(aHasAlpha);

    /* working variables */
    __m128 vec_r, vec_g, vec_b, result;
    size_t i;
    unsigned char alpha;

    /* CYA */
    if (!length)
        return;

    /* one pixel is handled outside of the loop */
    length--;

    /* setup for transforming 1st pixel */
    if (aInplace) {
        vec_r = _mm_load_ss(&igtbl_r[dest[kRIndex]]);
        vec_g = _mm_load_ss(&igtbl_g[dest[kGIndex]]);
        vec_b = _mm_load_ss(&igtbl_b[dest[kBIndex]]);
    } else {
        vec_r = _mm_load_ss(&igtbl_r[src[kRIndex]]);
        vec_g = _mm_load_ss(&igtbl_g[src[kGIndex]]);
        vec_b = _mm_load_ss(&igtbl_b[src[kBIndex]]);
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
        /* position values from gamma tables */
        vec_r = _mm_shuffle_ps(vec_r, vec_r, 0);
        vec_g = _mm_shuffle_ps(vec_g, vec_g, 0);
        vec_b = _mm_shuffle_ps(vec_b, vec_b, 0);

        /* gamma * matrix */
        vec_r = _mm_mul_ps(vec_r, mat0);
        vec_g = _mm_mul_ps(vec_g, mat1);
        vec_b = _mm_mul_ps(vec_b, mat2);

        /* store alpha for this pixel; load alpha for next */
        if (aHasAlpha && !aInplace) {
            dest[kAIndex] = alpha;
            alpha = src[kAIndex];
        }

        /* crunch, crunch, crunch */
        vec_r  = _mm_add_ps(vec_r, _mm_add_ps(vec_g, vec_b));
        vec_r  = _mm_max_ps(min, vec_r);
        vec_r  = _mm_min_ps(max, vec_r);
        result = _mm_mul_ps(vec_r, scale);

        /* store calc'd output tables indices */
        _mm_store_si128((__m128i*)output, _mm_cvtps_epi32(result));

        /* load gamma values for next loop while store completes */
        if (aInplace) {
            vec_r = _mm_load_ss(&igtbl_r[dest[kRIndex + kComponents]]);
            vec_g = _mm_load_ss(&igtbl_g[dest[kGIndex + kComponents]]);
            vec_b = _mm_load_ss(&igtbl_b[dest[kBIndex + kComponents]]);
        } else {
            vec_r = _mm_load_ss(&igtbl_r[src[kRIndex]]);
            vec_g = _mm_load_ss(&igtbl_g[src[kGIndex]]);
            vec_b = _mm_load_ss(&igtbl_b[src[kBIndex]]);
            src += kComponents;
        }

        /* use calc'd indices to output RGB values */
        dest[kRIndex] = otdata_r[output[0]];
        dest[kGIndex] = otdata_g[output[1]];
        dest[kBIndex] = otdata_b[output[2]];
        dest += kComponents;
    }

    /* handle final (maybe only) pixel */

    vec_r = _mm_shuffle_ps(vec_r, vec_r, 0);
    vec_g = _mm_shuffle_ps(vec_g, vec_g, 0);
    vec_b = _mm_shuffle_ps(vec_b, vec_b, 0);

    vec_r = _mm_mul_ps(vec_r, mat0);
    vec_g = _mm_mul_ps(vec_g, mat1);
    vec_b = _mm_mul_ps(vec_b, mat2);

    if (aHasAlpha && !aInplace) {
        dest[kAIndex] = alpha;
    }

    vec_r  = _mm_add_ps(vec_r, _mm_add_ps(vec_g, vec_b));
    vec_r  = _mm_max_ps(min, vec_r);
    vec_r  = _mm_min_ps(max, vec_r);
    result = _mm_mul_ps(vec_r, scale);

    _mm_store_si128((__m128i*)output, _mm_cvtps_epi32(result));

    dest[kRIndex] = otdata_r[output[0]];
    dest[kGIndex] = otdata_g[output[1]];
    dest[kBIndex] = otdata_b[output[2]];
}

void qcms_transform_data_rgb_out_lut_sse2(const qcms_transform *transform,
                                          const unsigned char *src,
                                          unsigned char *dest,
                                          size_t length)
{
  qcms_transform_data_template_lut_sse2<false, false, false>(transform, src, dest, length);
}

void qcms_transform_data_rgba_out_lut_sse2(const qcms_transform *transform,
                                           const unsigned char *src,
                                           unsigned char *dest,
                                           size_t length)
{
  qcms_transform_data_template_lut_sse2<false, true, src == dest>(transform, src, dest, length);
}

void qcms_transform_data_bgra_out_lut_sse2(const qcms_transform *transform,
                                           const unsigned char *src,
                                           unsigned char *dest,
                                           size_t length)
{
  qcms_transform_data_template_lut_sse2<false, true, src == dest>(transform, src, dest, length);
}
