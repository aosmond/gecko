#include <emmintrin.h>
#include <immintrin.h>

#include "qcmsint.h"

/* pre-shuffled: just load these into XMM reg instead of load-scalar/shufps sequence */
#define FLOATSCALE  (float)(PRECACHE_OUTPUT_SIZE)
#define CLAMPMAXVAL ( ((float) (PRECACHE_OUTPUT_SIZE - 1)) / PRECACHE_OUTPUT_SIZE )

#if 0
#define FLOATSCALE  (float)(PRECACHE_OUTPUT_SIZE)
#define CLAMPMAXVAL ( ((float) (PRECACHE_OUTPUT_SIZE - 1)) / PRECACHE_OUTPUT_SIZE )
static const ALIGN float floatScaleX4[4] =
    { FLOATSCALE, FLOATSCALE, FLOATSCALE, FLOATSCALE};
static const ALIGN float clampMaxValueX4[4] =
    { CLAMPMAXVAL, CLAMPMAXVAL, CLAMPMAXVAL, CLAMPMAXVAL};

template <size_t kRIndex, size_t kGIndex, size_t kBIndex, size_t kAIndex = NO_A_INDEX>
static void qcms_transform_data_template_lut_avx(const qcms_transform *transform,
                                                 const unsigned char *src,
                                                 unsigned char *dest,
                                                 size_t length)
{
    unsigned int i;
    const float (*mat)[4] = transform->matrix;
    char input_back[32];
    /* Ensure we have a buffer that's 16 byte aligned regardless of the original
     * stack alignment. We can't use __attribute__((aligned(16))) or __declspec(align(32))
     * because they don't work on stack variables. gcc 4.4 does do the right thing
     * on x86 but that's too new for us right now. For more info: gcc bug #16660 */
    float const * input = (float*)(((uintptr_t)&input_back[16]) & ~0xf);
    /* share input and output locations to save having to keep the
     * locations in separate registers */
    uint32_t const * output = (uint32_t*)input;

    /* deref *transform now to avoid it in loop */
    const float *igtbl_r = transform->input_gamma_table_r;
    const float *igtbl_g = transform->input_gamma_table_g;
    const float *igtbl_b = transform->input_gamma_table_b;

    /* deref *transform now to avoid it in loop */
    const uint8_t *otdata_r = &transform->output_table_r->data[0];
    const uint8_t *otdata_g = &transform->output_table_g->data[0];
    const uint8_t *otdata_b = &transform->output_table_b->data[0];

    /* input matrix values never change */
    const __m128 mat0  = _mm_load_ps(mat[0]);
    const __m128 mat1  = _mm_load_ps(mat[1]);
    const __m128 mat2  = _mm_load_ps(mat[2]);

    /* these values don't change, either */
    const __m128 max   = _mm_load_ps(clampMaxValueX4);
    const __m128 min   = _mm_setzero_ps();
    const __m128 scale = _mm_load_ps(floatScaleX4);
    const unsigned int components = A_INDEX_COMPONENTS(kAIndex);

    /* working variables */
    __m128 vec_r, vec_g, vec_b, result;
    unsigned char alpha;

    /* CYA */
    if (!length)
        return;

    /* one pixel is handled outside of the loop */
    length--;

    /* setup for transforming 1st pixel */
    vec_r = _mm_broadcast_ss(&igtbl_r[src[kRIndex]]);
    vec_g = _mm_broadcast_ss(&igtbl_g[src[kGIndex]]);
    vec_b = _mm_broadcast_ss(&igtbl_b[src[kBIndex]]);
    if (kAIndex != NO_A_INDEX) {
        alpha = src[kAIndex];
    }
    src += components;

    /* transform all but final pixel */

    for (i=0; i<length; i++)
    {
        /* gamma * matrix */
        vec_r = _mm_mul_ps(vec_r, mat0);
        vec_g = _mm_mul_ps(vec_g, mat1);
        vec_b = _mm_mul_ps(vec_b, mat2);

        /* store alpha for this pixel; load alpha for next */
        if (kAIndex != NO_A_INDEX) {
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
        vec_r = _mm_broadcast_ss(&igtbl_r[src[kRIndex]]);
        vec_g = _mm_broadcast_ss(&igtbl_g[src[kGIndex]]);
        vec_b = _mm_broadcast_ss(&igtbl_b[src[kBIndex]]);
        src += components;

        /* use calc'd indices to output RGB values */
        dest[kRIndex] = otdata_r[output[0]];
        dest[kGIndex] = otdata_g[output[1]];
        dest[kBIndex] = otdata_b[output[2]];
        dest += components;
    }

    /* handle final (maybe only) pixel */

    vec_r = _mm_mul_ps(vec_r, mat0);
    vec_g = _mm_mul_ps(vec_g, mat1);
    vec_b = _mm_mul_ps(vec_b, mat2);

    if (kAIndex != NO_A_INDEX) {
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
#endif

template <size_t kRIndex, size_t kGIndex, size_t kBIndex, size_t kAIndex = NO_A_INDEX>
static void qcms_transform_data_template_lut_avx(const qcms_transform *transform,
                                                 const unsigned char *src,
                                                 unsigned char *dest,
                                                 size_t length)
{
    unsigned int i;
    const float (*mat)[4] = transform->matrix;
    char input_back[64];
    /* Ensure we have a buffer that's 16 byte aligned regardless of the original
     * stack alignment. We can't use __attribute__((aligned(16))) or __declspec(align(32))
     * because they don't work on stack variables. gcc 4.4 does do the right thing
     * on x86 but that's too new for us right now. For more info: gcc bug #16660 */
    float const * input = (float*)(((uintptr_t)&input_back[32]) & ~0x1f);
    /* share input and output locations to save having to keep the
     * locations in separate registers */
    uint32_t const * output = (uint32_t*)input;

    /* deref *transform now to avoid it in loop */
    const float *igtbl_r = transform->input_gamma_table_r;

    /* deref *transform now to avoid it in loop */
    const uint8_t *otdata_r = &transform->output_table_r->data[0];
    const uint8_t *otdata_g = &transform->output_table_g->data[0];
    const uint8_t *otdata_b = &transform->output_table_b->data[0];

    /* input matrix values never change */
    const __m128 mat0s  = _mm_load_ps(mat[0]);
    const __m128 mat1s  = _mm_load_ps(mat[1]);
    const __m128 mat2s  = _mm_load_ps(mat[2]);
    const __m256 mat0   = _mm256_set_m128(mat0s, mat0s);
    const __m256 mat1   = _mm256_set_m128(mat1s, mat1s);
    const __m256 mat2   = _mm256_set_m128(mat2s, mat2s);

    /* these values don't change, either */
    const __m256 max   = _mm256_set1_ps(CLAMPMAXVAL);
    const __m256 min   = _mm256_setzero_ps();
    const __m256 scale = _mm256_set1_ps(FLOATSCALE);
    const __m128i load_and_mask = _mm_set1_epi32(0x000000ff);
    const __m128i load_or_mask = _mm_set_epi32(0x8000000, 0x8000100, 0x80000200, 0);
    const unsigned int components = A_INDEX_COMPONENTS(kAIndex);

    /* working variables */
    __m256 vec_r, vec_g, vec_b, result, tmp;
    __m128i px, px2;
    unsigned char alpha[2];

    /* CYA */
    if (!length)
        return;

    /* one pixel is handled outside of the loop */
    length -= 2;

    /* load two pixels into first 32-bit register of 128-bits */
    px = _mm_loadu_si32(src);
    px2 = _mm_loadu_si32(src + components);

    /* restructure into RRRRRRRR GGGGGGGG BBBBBBBB AAAAAAAA */
    px = _mm_unpacklo_epi8(px, px);
    px2 = _mm_unpacklo_epi8(px2, px2);
    px = _mm_unpacklo_epi8(px, px);
    px2 = _mm_unpacklo_epi8(px2, px2);

    /* mask into 800000RR 800001GG 800002BB 00000000 for the maskload below
     * high bit must be set, and lower bits are offset into lookup table */
    px = _mm_and_si128(px, load_and_mask);
    px2 = _mm_and_si128(px2, load_and_mask);
    px = _mm_or_si128(px, load_or_mask);
    px2 = _mm_or_si128(px2, load_or_mask);

    /* maskload such that LR1 LG1 LB1 0 LR2 LG2 LB2 0 */
    vec_g = _mm256_setr_m128(px, px2);
    vec_g = _mm256_maskload_ps(&igtbl_r[0], vec_g);

    /* set vec_r to LR1 LR1 LR1 LR1 LR2 LR2 LR2 LR2 */
    vec_r = _mm256_permute_ps(vec_g, kRIndex == 0 ? 0 : 0xAA);
    vec_b = _mm256_permute_ps(vec_g, kBIndex == 0 ? 0 : 0xAA /*10101010*/);
    vec_g = _mm256_permute_ps(vec_g, 0x55 /*01010101*/);

#if 0
    /* setup for transforming 1st pixel */
    tmp1 = _mm_broadcast_ss(&igtbl_r[src[kRIndex]]);
    tmp2 = _mm_broadcast_ss(&igtbl_r[src[kRIndex + components]]);
    vec_r = _mm256_setr_m128(tmp1, tmp2);
    tmp1 = _mm_broadcast_ss(&igtbl_g[src[kGIndex]]);
    tmp2 = _mm_broadcast_ss(&igtbl_g[src[kGIndex + components]]);
    vec_g = _mm256_setr_m128(tmp1, tmp2);
    tmp1 = _mm_broadcast_ss(&igtbl_b[src[kBIndex]]);
    tmp2 = _mm_broadcast_ss(&igtbl_b[src[kBIndex + components]]);
    vec_b = _mm256_setr_m128(tmp1, tmp2);
#endif

    if (kAIndex != NO_A_INDEX) {
        alpha[0] = src[kAIndex];
        alpha[1] = src[kAIndex + components];
    }
    src += 2 * components;

    /* transform all but final pixel */

    for (i=0; i<length-1; i += 2)
    {
        /* gamma * matrix */
        vec_r = _mm256_mul_ps(vec_r, mat0);
        vec_g = _mm256_mul_ps(vec_g, mat1);
        vec_b = _mm256_mul_ps(vec_b, mat2);

        /* store alpha for this pixel; load alpha for next */
        if (kAIndex != NO_A_INDEX) {
            dest[kAIndex] = alpha[0];
            dest[kAIndex + components] = alpha[1];
            alpha[0] = src[kAIndex];
            alpha[1] = src[kAIndex + components];
        }

        /* crunch, crunch, crunch */
        result = _mm256_add_ps(vec_r, _mm256_add_ps(vec_g, vec_b));
        result = _mm256_max_ps(min, result);
        result = _mm256_min_ps(max, result);
        result = _mm256_mul_ps(result, scale);

        /* store calc'd output tables indices */
        _mm256_store_si256((__m256i*)output, _mm256_cvtps_epi32(result));

        /* load gamma values for next loop while store completes */
#if 0
        tmp1 = _mm_broadcast_ss(&igtbl_r[src[kRIndex]]);
        tmp2 = _mm_broadcast_ss(&igtbl_r[src[kRIndex + components]]);
        vec_r = _mm256_setr_m128(tmp1, tmp2);
        tmp1 = _mm_broadcast_ss(&igtbl_g[src[kGIndex]]);
        tmp2 = _mm_broadcast_ss(&igtbl_g[src[kGIndex + components]]);
        vec_g = _mm256_setr_m128(tmp1, tmp2);
        tmp1 = _mm_broadcast_ss(&igtbl_b[src[kBIndex]]);
        tmp2 = _mm_broadcast_ss(&igtbl_b[src[kBIndex + components]]);
        vec_b = _mm256_setr_m128(tmp1, tmp2);
#else
#if 0
	tmp1 = _mm_loadu_si32(src);
	tmp2 = _mm_loadu_si32(src + components);
	tmp1 = _mm_shuffle_epi8(tmp1, _
	tmp = _mm256_setr_m128(tmp1, tmp2);
	tmp = _mm256_permute2f128_si256(_mm256_castsi128_si256(tmp1), _mm256_castsi128_si256(tmp2), 
	vec_r = _mm256_maskload_ps(&igtbl_r[0], _mm256_set_epi32(0, 0, 0, src[kRIndex + components], 0, 0, 0, src[kRIndex]));
	vec_g = _mm256_maskload_ps(&igtbl_g[0], _mm256_set_epi32(0, 0, 0, src[kGIndex + components], 0, 0, 0, src[kGIndex]));
	vec_b = _mm256_maskload_ps(&igtbl_b[0], _mm256_set_epi32(0, 0, 0, src[kBIndex + components], 0, 0, 0, src[kBIndex]));
	vec_r = _mm256_permute_ps(vec_r, 0);
	vec_g = _mm256_permute_ps(vec_g, 0);
	vec_b = _mm256_permute_ps(vec_b, 0);
#else
    px = _mm_loadu_si32(src);
    px2 = _mm_loadu_si32(src + components);
    px = _mm_unpacklo_epi8(px, px);
    px2 = _mm_unpacklo_epi8(px2, px2);
    px = _mm_unpacklo_epi8(px, px);
    px2 = _mm_unpacklo_epi8(px2, px2);
    px = _mm_and_si128(px, load_and_mask);
    px2 = _mm_and_si128(px2, load_and_mask);
    px = _mm_or_si128(px, load_or_mask);
    px2 = _mm_or_si128(px2, load_or_mask);
    vec_g = _mm256_setr_m128(px, px2);
    vec_g = _mm256_maskload_ps(&igtbl_r[0], vec_g);
    vec_r = _mm256_permute_ps(vec_g, kRIndex == 0 ? 0 : 0xAA);
    vec_b = _mm256_permute_ps(vec_g, kBIndex == 0 ? 0 : 0xAA /*10101010*/);
    vec_g = _mm256_permute_ps(vec_g, 0x55 /*01010101*/);
#endif
#endif
        src += 2 * components;

        /* use calc'd indices to output RGB values */
        dest[kRIndex] = otdata_r[output[0]];
        dest[kGIndex] = otdata_g[output[1]];
        dest[kBIndex] = otdata_b[output[2]];
        dest[kRIndex + components] = otdata_r[output[4]];
        dest[kGIndex + components] = otdata_g[output[5]];
        dest[kBIndex + components] = otdata_b[output[6]];
        dest += 2 * components;
    }

    /* handle final (maybe only) pixel */
    vec_r = _mm256_mul_ps(vec_r, mat0);
    vec_g = _mm256_mul_ps(vec_g, mat1);
    vec_b = _mm256_mul_ps(vec_b, mat2);

    if (kAIndex != NO_A_INDEX) {
        dest[kAIndex] = alpha[0];
        dest[kAIndex + components] = alpha[1];
    }

    vec_r  = _mm256_add_ps(vec_r, _mm256_add_ps(vec_g, vec_b));
    vec_r  = _mm256_max_ps(min, vec_r);
    vec_r  = _mm256_min_ps(max, vec_r);
    result = _mm256_mul_ps(vec_r, scale);

    _mm256_store_si256((__m256i*)output, _mm256_cvtps_epi32(result));

    dest[kRIndex] = otdata_r[output[0]];
    dest[kGIndex] = otdata_g[output[1]];
    dest[kBIndex] = otdata_b[output[2]];
    dest[kRIndex + components] = otdata_r[output[4]];
    dest[kGIndex + components] = otdata_g[output[5]];
    dest[kBIndex + components] = otdata_b[output[6]];
}

void qcms_transform_data_rgb_out_lut_avx(const qcms_transform *transform,
                                         const unsigned char *src,
                                         unsigned char *dest,
                                         size_t length)
{
  //qcms_transform_data_template_lut_avx<RGBA_R_INDEX, RGBA_G_INDEX, RGBA_B_INDEX>(transform, src, dest, length);
  qcms_transform_data_rgb_out_lut_sse2(transform, src, dest, length);
}

void qcms_transform_data_rgba_out_lut_avx(const qcms_transform *transform,
                                          const unsigned char *src,
                                          unsigned char *dest,
                                          size_t length)
{
  qcms_transform_data_template_lut_avx<RGBA_R_INDEX, RGBA_G_INDEX, RGBA_B_INDEX, RGBA_A_INDEX>(transform, src, dest, length);
}

void qcms_transform_data_bgra_out_lut_avx(const qcms_transform *transform,
                                          const unsigned char *src,
                                          unsigned char *dest,
                                          size_t length)
{
  qcms_transform_data_template_lut_avx<BGRA_R_INDEX, BGRA_G_INDEX, BGRA_B_INDEX, BGRA_A_INDEX>(transform, src, dest, length);
}
