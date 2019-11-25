#include <emmintrin.h>

#include "qcmsint.h"
#include "transform_util.h"

/* pre-shuffled: just load these into XMM reg instead of load-scalar/shufps sequence */
static const ALIGN float floatScaleX4[4] =
    { FLOATSCALE, FLOATSCALE, FLOATSCALE, FLOATSCALE};
static const ALIGN float clampMaxValueX4[4] =
    { CLAMPMAXVAL, CLAMPMAXVAL, CLAMPMAXVAL, CLAMPMAXVAL};

template <size_t kRIndex, size_t kGIndex, size_t kBIndex, size_t kAIndex = NO_A_INDEX>
static void qcms_transform_data_template_lut_sse2(const qcms_transform *transform,
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
    vec_r = _mm_load_ss(&igtbl_r[src[kRIndex]]);
    vec_g = _mm_load_ss(&igtbl_g[src[kGIndex]]);
    vec_b = _mm_load_ss(&igtbl_b[src[kBIndex]]);
    if (kAIndex != NO_A_INDEX) {
        alpha = src[kAIndex];
    }
    src += components;

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
        vec_r = _mm_load_ss(&igtbl_r[src[kRIndex]]);
        vec_g = _mm_load_ss(&igtbl_g[src[kGIndex]]);
        vec_b = _mm_load_ss(&igtbl_b[src[kBIndex]]);
        src += components;

        /* use calc'd indices to output RGB values */
        dest[kRIndex] = otdata_r[output[0]];
        dest[kGIndex] = otdata_g[output[1]];
        dest[kBIndex] = otdata_b[output[2]];
        dest += components;
    }

    /* handle final (maybe only) pixel */

    vec_r = _mm_shuffle_ps(vec_r, vec_r, 0);
    vec_g = _mm_shuffle_ps(vec_g, vec_g, 0);
    vec_b = _mm_shuffle_ps(vec_b, vec_b, 0);

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

void qcms_transform_data_rgb_out_lut_sse2(const qcms_transform *transform,
                                          const unsigned char *src,
                                          unsigned char *dest,
                                          size_t length)
{
  qcms_transform_data_template_lut_sse2<RGBA_R_INDEX, RGBA_G_INDEX, RGBA_B_INDEX>(transform, src, dest, length);
}

void qcms_transform_data_rgba_out_lut_sse2(const qcms_transform *transform,
                                           const unsigned char *src,
                                           unsigned char *dest,
                                           size_t length)
{
  qcms_transform_data_template_lut_sse2<RGBA_R_INDEX, RGBA_G_INDEX, RGBA_B_INDEX, RGBA_A_INDEX>(transform, src, dest, length);
}

void qcms_transform_data_bgra_out_lut_sse2(const qcms_transform *transform,
                                           const unsigned char *src,
                                           unsigned char *dest,
                                           size_t length)
{
  qcms_transform_data_template_lut_sse2<BGRA_R_INDEX, BGRA_G_INDEX, BGRA_B_INDEX, BGRA_A_INDEX>(transform, src, dest, length);
}

#define CLU_OFFSET(x, y, z) \
	offset_##x[0] + offset_##y[1] + offset_##z[2]

// Using lcms' tetra interpolation algorithm.
template <size_t kSwapRB, size_t kAIndex = NO_A_INDEX>
static void qcms_transform_data_template_tetra_clut_sse2(const qcms_transform *transform, const unsigned char *src, unsigned char *dest, size_t length) {
	const unsigned int components = A_INDEX_COMPONENTS(kAIndex);
	unsigned int i;
	int xy_len = 1;
	int x_len = transform->grid_size;
	int len = x_len * x_len;
	float* r_table = kSwapRB ? transform->b_clut : transform->r_clut;
	float* g_table = transform->g_clut;
	float* b_table = kSwapRB ? transform->r_clut : transform->b_clut;

	uint32_t offset_l[4];
	uint32_t offset_h[4];

	unsigned char in_a;
	int p0, p1, p2, p3;

	__m128i px, xx, xn;
	__m128 rr, rrcmp;
	__m128 cr0, cr1, cg0, cg1, cb0, cb1;
	for (i = 0; i < length; i++) {
		// Load in a single pixel and place each component into the
		// lower 16-bit words. 
		px = _mm_loadu_si32(src);
		if (kAIndex != NO_A_INDEX) {
			in_a = src[kAIndex];
		}

		src += components;
		px = _mm_unpacklo_epi8(px, px);
		px = _mm_srli_epi16(px, 8);

		// xx = in_c * (transform->grid - 1 ), placed into 32-bit words
		px = _mm_mullo_epi16(px, _mm_set1_epi16(transform->grid_size - 1));

		// rr = (in_c * (transform->grid_size - 1)) / 255.0f 
		rr = _mm_cvtepi32_ps(px);
		rr = _mm_div_ps(px, _mm_set1_ps(255.0f));

		// Effectively compute the floor by truncating the float.
		xx = _mm_cvttps_epi32(rr);
		xn = _mm_cvttps_epi32(_mm_add_ps(rr, _mm_set1_ps(254.0f / 255.0f)));

		// Calculate fractional component.
		rr = _mm_sub_ps(rr, _mm_cvtepi32_ps(xx));

		// Compare components.
		rrcmp  = _mm_cmpge_ps(_mm_shuffle_ps(rr, rr, _MM_SHUFFLE(0, 0, 1, 0)), _mm_shuffle_ps(rr, rr, _MM_SHUFFLE(0, 2, 2, 1)));

		// pack xyz back into 16-bit words
		xx = _mm_packs_epi32(xx, xx);
		xn = _mm_packs_epi32(xn, xn);

		// assumes RGBA (r = lowest 16-bit, g next, b next, alpha next)
		px = _mm_set_epi16(0, 0, 0, 0, 0, xy_len, x_len, len);

		// x * len, y * x_len, z * xy_len
		xx = _mm_mullo_epi16(xx, px);
		xn = _mm_mullo_epi16(xn, px);
		_mm_storeu_si128((__m128i*)&offset_l[0], xx);
		_mm_storeu_si128((__m128i*)&offset_h[0], xn);

		p0 = CLU_OFFSET(l, l, l);
		p3 = CLU_OFFSET(h, h, h);
		switch (_mm_movemask_ps(rrcmp)) {
			case 8: // rx < ry && ry < rz && rx < rz
				p1 = CLU_OFFSET(l, h, h);
				p2 = CLU_OFFSET(l, l, h);
				cr0 = _mm_set_ps(0.0f, r_table[p3], r_table[p1], r_table[p2]);
				cr1 = _mm_set_ps(0.0f, r_table[p1], r_table[p2], r_table[p0]);
				cg0 = _mm_set_ps(0.0f, g_table[p3], g_table[p1], g_table[p2]);
				cg1 = _mm_set_ps(0.0f, g_table[p1], g_table[p2], g_table[p0]);
				cb0 = _mm_set_ps(0.0f, b_table[p3], b_table[p1], b_table[p2]);
				cb1 = _mm_set_ps(0.0f, b_table[p1], b_table[p2], b_table[p0]);
				break;
			case 10: // rx < ry && ry >= rz && rx < rz
				p1 = CLU_OFFSET(l, h, h);
				p2 = CLU_OFFSET(l, h, l);
				cr0 = _mm_set_ps(0.0f, r_table[p3], r_table[p2], r_table[p1]);
				cr1 = _mm_set_ps(0.0f, r_table[p1], r_table[p0], r_table[p2]);
				cg0 = _mm_set_ps(0.0f, g_table[p3], g_table[p2], g_table[p1]);
				cg1 = _mm_set_ps(0.0f, g_table[p1], g_table[p0], g_table[p2]);
				cb0 = _mm_set_ps(0.0f, b_table[p3], b_table[p2], b_table[p1]);
				cb1 = _mm_set_ps(0.0f, b_table[p1], b_table[p0], b_table[p2]);
				break;
			case 12: // rx < ry && ry < rz && rx >= rz
			case 14: // rx < ry && ry >= rz && rx >= rz
				p1 = CLU_OFFSET(h, h, l);
				p2 = CLU_OFFSET(l, h, l);
				cr0 = _mm_set_ps(0.0f, r_table[p1], r_table[p2], r_table[p3]);
				cr1 = _mm_set_ps(0.0f, r_table[p2], r_table[p0], r_table[p1]);
				cg0 = _mm_set_ps(0.0f, g_table[p1], g_table[p2], g_table[p3]);
				cg1 = _mm_set_ps(0.0f, g_table[p2], g_table[p0], g_table[p1]);
				cb0 = _mm_set_ps(0.0f, b_table[p1], b_table[p2], b_table[p3]);
				cb1 = _mm_set_ps(0.0f, b_table[p2], b_table[p0], b_table[p1]);
				break;
			case 9: // rx >= ry && ry < rz && rx < rz
				p1 = CLU_OFFSET(h, l, h);
				p2 = CLU_OFFSET(l, l, h);
				cr0 = _mm_set_ps(0.0f, r_table[p1], r_table[p3], r_table[p2]);
				cr1 = _mm_set_ps(0.0f, r_table[p2], r_table[p1], r_table[p0]);
				cg0 = _mm_set_ps(0.0f, g_table[p1], g_table[p3], g_table[p2]);
				cg1 = _mm_set_ps(0.0f, g_table[p2], g_table[p1], g_table[p0]);
				cb0 = _mm_set_ps(0.0f, b_table[p1], b_table[p3], b_table[p2]);
				cb1 = _mm_set_ps(0.0f, b_table[p2], b_table[p1], b_table[p0]);
				break;
			case 13: // rx >= ry && ry < rz && rx >= rz
				p1 = CLU_OFFSET(h, l, l);
				p2 = CLU_OFFSET(h, l, h);
				cr0 = _mm_set_ps(0.0f, r_table[p1], r_table[p3], r_table[p2]);
				cr1 = _mm_set_ps(0.0f, r_table[p0], r_table[p2], r_table[p1]);
				cg0 = _mm_set_ps(0.0f, g_table[p1], g_table[p3], g_table[p2]);
				cg1 = _mm_set_ps(0.0f, g_table[p0], g_table[p2], g_table[p1]);
				cb0 = _mm_set_ps(0.0f, b_table[p1], b_table[p3], b_table[p2]);
				cb1 = _mm_set_ps(0.0f, b_table[p0], b_table[p2], b_table[p1]);
				break;
			default: // Can never happen
			case 11: // rx >= ry && ry >= rz && rx < rz
			case 15: // rx >= ry && ry >= rz && rx >= rz
				p1 = CLU_OFFSET(h, l, l);
				p2 = CLU_OFFSET(h, h, l);
				cr0 = _mm_set_ps(0.0f, r_table[p1], r_table[p2], r_table[p3]);
				cr1 = _mm_set_ps(0.0f, r_table[p0], r_table[p1], r_table[p2]);
				cg0 = _mm_set_ps(0.0f, g_table[p1], g_table[p2], g_table[p3]);
				cg0 = _mm_set_ps(0.0f, g_table[p1], g_table[p2], g_table[p3]);
				cb1 = _mm_set_ps(0.0f, b_table[p0], b_table[p1], b_table[p2]);
				cb1 = _mm_set_ps(0.0f, b_table[p0], b_table[p1], b_table[p2]);
				break;
		}

		// xx c0_r c0_g c0b
		// xx c1_r 
		// sum 

		// highest register needs to be 1.0
		rr = _mm_mul_ps(rr, _mm_set_ps(0.0, 1.0, 1.0, 1.0));
		rr = _mm_add_ps(rr, _mm_set_ps(1.0, 0.0, 0.0, 0.0));

		// clut_r = c0_r + c1_r*rx + c2_r*ry + c3_r*rz;
		// clut_g = c0_g + c1_g*rx + c2_g*ry + c3_g*rz;
		// clut_b = c0_b + c1_b*rx + c2_b*ry + c3_b*rz;
		cr0 = _mm_sub_ps(cr0, cr1);
		cg0 = _mm_sub_ps(cg0, cg1);
		cb0 = _mm_sub_ps(cb0, cb1);

		cr0 = _mm_mul_ps(cr0, rr);
		cg0 = _mm_mul_ps(cg0, rr);
		cb0 = _mm_mul_ps(cb0, rr);

		// add 32-bits into lowest 32-bit register
		cr0 = _mm_add_ps(cr0, _mm_movehl_ps(cr0, cr0));
		cg0 = _mm_add_ps(cg0, _mm_movehl_ps(cg0, cg0));
		cb0 = _mm_add_ps(cb0, _mm_movehl_ps(cb0, cb0));

		cr0 = _mm_add_ss(cr0, _mm_shuffle_ps(cr0, cr0, _MM_SHUFFLE(0, 0, 0, 1)));
		cg0 = _mm_add_ss(cg0, _mm_shuffle_ps(cg0, cg0, _MM_SHUFFLE(0, 0, 0, 1)));
		cb0 = _mm_add_ss(cb0, _mm_shuffle_ps(cb0, cb0, _MM_SHUFFLE(0, 0, 0, 1)));

		// combine components into single 128-bit register
		rr = _mm_shuffle_ps(cr0, cg0, _MM_SHUFFLE(0, 0, 0, 0));
		rr = _mm_shuffle_ps(rr, rr, _MM_SHUFFLE(0, 0, 2, 0));
		rr = _mm_shuffle_ps(rr, cb0, _MM_SHUFFLE(0, 0, 1, 0));

		// clamp components from 0 - 255
		rr = _mm_mul_ss(rr, _mm_set1_ps(255.0f));
		rr = _mm_add_ss(rr, _mm_set1_ps(0.5f));
		rr = _mm_min_ss(rr, _mm_set1_ps(255.0f));
		rr = _mm_max_ss(rr, _mm_set1_ps(0.0f));

		// convert final result into RGB component values
		xx = _mm_cvtps_epi32(rr);
		_mm_storeu_si128((__m128i*)&offset_l[0], xx);

		dest[kRIndex] = offset_l[0];
		dest[kGIndex] = offset_l[1];
		dest[kBIndex] = offset_l[2];
		if (kAIndex != NO_A_INDEX) {
			dest[kAIndex] = in_a;
		}

		src += components;
		dest += components;
	}
}

void qcms_transform_data_tetra_clut_rgb_sse2(const qcms_transform *transform,
                                             const unsigned char *src,
                                             unsigned char *dest,
                                             size_t length)
{
  qcms_transform_data_template_tetra_clut_sse2<false>(transform, src, dest, length);
}

void qcms_transform_data_tetra_clut_rgba_sse2(const qcms_transform *transform,
                                              const unsigned char *src,
                                              unsigned char *dest,
                                              size_t length)
{
  qcms_transform_data_template_tetra_clut_sse2<false, RGBA_A_INDEX>(transform, src, dest, length);
}

void qcms_transform_data_tetra_clut_bgra_sse2(const qcms_transform *transform,
                                              const unsigned char *src,
                                              unsigned char *dest,
                                              size_t length)
{
  qcms_transform_data_template_tetra_clut_sse2<true, BGRA_A_INDEX>(transform, src, dest, length);
}
