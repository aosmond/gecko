#include <emmintrin.h>

#include "qcmsint.h"
#include "transform_util.h"
#include <stdio.h>
#include <unistd.h>

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
template <size_t kRIndex, size_t kGIndex, size_t kBIndex, size_t kAIndex = NO_A_INDEX>
static void qcms_transform_data_template_tetra_clut_sse2(const qcms_transform *transform, const unsigned char *src, unsigned char *dest, size_t length) {
	fprintf(stderr, "[AO] pid = %lu\n", size_t(getpid()));
	const unsigned int components = A_INDEX_COMPONENTS(kAIndex);
	unsigned int i;
	int xy_len = 3;
	int x_len = 3 * transform->grid_size;
	int len = 3 * transform->grid_size * transform->grid_size;
	float* r_table = transform->r_clut;
	float* g_table = transform->g_clut;
	float* b_table = transform->b_clut;

	uint32_t offset_l[4];
	uint32_t offset_h[4];
	uint32_t debug[4];
	float debugf[4];

	unsigned char in_a;
	int p0, p1, p2, p3;

	bool first = true;
	__m128i px, xx, xn;
	__m128 rr, rrcmp;
	//__m128 ra0, ra1, rb0, rb1, rc0, rc1;
	__m128 cr0, cr1, cg0, cg1, cb0, cb1;
	for (i = 0; i < length; i++) {
		// Load in a single pixel and place each component into the
		// lower 16-bit words. 
		px = _mm_loadu_si32(src);
		if (kAIndex != NO_A_INDEX) {
			in_a = src[kAIndex];
		}

		if (first) {
			fprintf(stderr, "[AO] src = 0x%02X%02X%02X%02X\n", src[3], src[2], src[1], src[0]);
		}

		// Make each component 16-bits wide.
		px = _mm_unpacklo_epi8(px, _mm_setzero_si128());

		if (first) {
			_mm_storeu_si128((__m128i*)&debug[0], px);
			fprintf(stderr, "[AO] unpack px = 0x%08X %08X %08X %08X\n", debug[3], debug[2], debug[1], debug[0]);
		}

		if (kBIndex == BGRA_B_INDEX) {
			// XX R G B as 32 bit words.
			px = _mm_unpacklo_epi16(px, _mm_setzero_si128());
			// XX B G R as 32 bit words.
			px = _mm_shuffle_epi32(px, _MM_SHUFFLE(3, 0, 1, 2));
			// 0 0 0 0 XX B G R as 32 bit words.
			px = _mm_packs_epi32(px, _mm_setzero_si128());
		}

		if (first) {
			_mm_storeu_si128((__m128i*)&debug[0], px);
			fprintf(stderr, "[AO] bgra fixup px = 0x%08X %08X %08X %08X\n", debug[3], debug[2], debug[1], debug[0]);
		}

		// xx = in_c * (transform->grid - 1 ), placed into 16-bit words
		px = _mm_mullo_epi16(px, _mm_set1_epi16(transform->grid_size - 1));
		// xx = as 32-bit words
		px = _mm_unpacklo_epi16(px, _mm_setzero_si128());

		if (first) {
			_mm_storeu_si128((__m128i*)&debug[0], px);
			fprintf(stderr, "[AO] px mul = 0x%08X %08X %08X %08X\n", debug[3], debug[2], debug[1], debug[0]);
		}

		// rr = (in_c * (transform->grid_size - 1)) / 255.0f 
		rr = _mm_cvtepi32_ps(px);

		if (first) {
			_mm_storeu_ps(&debugf[0], rr);
			fprintf(stderr, "[AO] rr cvt = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
		}

		rr = _mm_div_ps(rr, _mm_set1_ps(255.0f));

		if (first) {
			_mm_storeu_ps(&debugf[0], rr);
			fprintf(stderr, "[AO] rr div = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
		}

		// Compute the floor by truncation.
		//   XX z y x
		xx = _mm_cvttps_epi32(rr);

		if (first) {
			_mm_storeu_si128((__m128i*)&debug[0], xx);
			fprintf(stderr, "[AO] xx = 0x%08X %08X %08X %08X\n", debug[3], debug[2], debug[1], debug[0]);
		}

		// Compute the ceil by truncation and addition.
		//   XX zn yn xn
		xn = _mm_cvttps_epi32(_mm_add_ps(rr, _mm_set1_ps(254.0f / 255.0f)));

		if (first) {
			_mm_storeu_si128((__m128i*)&debug[0], xn);
			fprintf(stderr, "[AO] xn = 0x%08X %08X %08X %08X\n", debug[3], debug[2], debug[1], debug[0]);
		}

		// Calculate fractional component. rr = XX rz ry rx
		rr = _mm_sub_ps(rr, _mm_cvtepi32_ps(xx));

		if (first) {
			_mm_storeu_ps(&debugf[0], rr);
			fprintf(stderr, "[AO] rr frac = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
		}

		// XX, z * xy_len, y * x_len, x * len
		_mm_storeu_si128((__m128i*)&offset_l[0], xx);
		_mm_storeu_si128((__m128i*)&offset_h[0], xn);

		// Compare components.
		//  (XX >= XX) (rx >= rz) (ry >= rz) (rx >= ry)
		rrcmp  = _mm_cmpge_ps(
			_mm_shuffle_ps(rr, rr, _MM_SHUFFLE(0, 0, 1, 0)), // XX rx ry rx
			_mm_shuffle_ps(rr, rr, _MM_SHUFFLE(0, 2, 2, 1))  // XX rz rz ry
		);

		if (first) {
			__m128i d = _mm_cvtps_epi32(rrcmp);
			_mm_storeu_si128((__m128i*)&debug[0], d);
			fprintf(stderr, "[AO] rrcmp = 0x%08X %08X %08X %08X\n", debug[3], debug[2], debug[1], debug[0]);
		}

		offset_l[0] *= len;
		offset_h[0] *= len;
		offset_l[1] *= x_len;
		offset_h[1] *= x_len;
		offset_l[2] *= xy_len;
		offset_h[2] *= xy_len;

		if (first) {
			fprintf(stderr, "[AO] xx %08X %08X %08X\n", offset_l[2], offset_l[1], offset_l[0]);
			fprintf(stderr, "[AO] xn %08X %08X %08X\n", offset_h[2], offset_h[1], offset_h[0]);
		}

		p0 = CLU_OFFSET(l, l, l);
		p3 = CLU_OFFSET(h, h, h);

		if (first) {
		        int mask = _mm_movemask_ps(rrcmp);
			fprintf(stderr, "[AO] mask = %d\n", mask);
		}
		switch (_mm_movemask_ps(rrcmp)) {
			case 8: // rx < ry && ry < rz && rx < rz
				p1 = CLU_OFFSET(l, h, h);
				p2 = CLU_OFFSET(l, l, h);
				cr0 = _mm_set_ps(r_table[p0], r_table[p2], r_table[p1], r_table[p3]);
				cr1 = _mm_set_ps(0.0f, r_table[p0], r_table[p2], r_table[p1]);
				cg0 = _mm_set_ps(g_table[p0], g_table[p2], g_table[p1], g_table[p3]);
				cg1 = _mm_set_ps(0.0f, g_table[p0], g_table[p2], g_table[p1]);
				cb0 = _mm_set_ps(b_table[p0], b_table[p2], b_table[p1], b_table[p3]);
				cb1 = _mm_set_ps(0.0f, b_table[p0], b_table[p2], b_table[p1]);
				break;
			case 10: // rx < ry && ry >= rz && rx < rz
				p1 = CLU_OFFSET(l, h, h);
				p2 = CLU_OFFSET(l, h, l);
				cr0 = _mm_set_ps(r_table[p0], r_table[p1], r_table[p2], r_table[p3]);
				cr1 = _mm_set_ps(0.0f, r_table[p2], r_table[p0], r_table[p1]);
				cg0 = _mm_set_ps(g_table[p0], g_table[p1], g_table[p2], g_table[p3]);
				cg1 = _mm_set_ps(0.0f, g_table[p2], g_table[p0], g_table[p1]);
				cb0 = _mm_set_ps(b_table[p0], b_table[p1], b_table[p2], b_table[p3]);
				cb1 = _mm_set_ps(0.0f, b_table[p2], b_table[p0], b_table[p1]);
				break;
			case 12: // rx < ry && ry < rz && rx >= rz
			case 14: // rx < ry && ry >= rz && rx >= rz
				p1 = CLU_OFFSET(h, h, l);
				p2 = CLU_OFFSET(l, h, l);
				cr0 = _mm_set_ps(r_table[p0], r_table[p3], r_table[p2], r_table[p1]);
				cr1 = _mm_set_ps(0.0f, r_table[p1], r_table[p0], r_table[p2]);
				cg0 = _mm_set_ps(g_table[p0], g_table[p3], g_table[p2], g_table[p1]);
				cg1 = _mm_set_ps(0.0f, g_table[p1], g_table[p0], g_table[p2]);
				cb0 = _mm_set_ps(b_table[p0], b_table[p3], b_table[p2], b_table[p1]);
				cb1 = _mm_set_ps(0.0f, b_table[p1], b_table[p0], b_table[p2]);
				break;
			case 9: // rx >= ry && ry < rz && rx < rz
				p1 = CLU_OFFSET(h, l, h);
				p2 = CLU_OFFSET(l, l, h);
				cr0 = _mm_set_ps(r_table[p0], r_table[p2], r_table[p3], r_table[p1]);
				cr1 = _mm_set_ps(0.0f, r_table[p0], r_table[p1], r_table[p2]);
				cg0 = _mm_set_ps(g_table[p0], g_table[p2], g_table[p3], g_table[p1]);
				cg1 = _mm_set_ps(0.0f, g_table[p0], g_table[p1], g_table[p2]);
				cb0 = _mm_set_ps(b_table[p0], b_table[p2], b_table[p3], b_table[p1]);
				cb1 = _mm_set_ps(0.0f, b_table[p0], b_table[p1], b_table[p2]);
				break;
			case 13: // rx >= ry && ry < rz && rx >= rz
				p1 = CLU_OFFSET(h, l, l);
				p2 = CLU_OFFSET(h, l, h);
				cr0 = _mm_set_ps(r_table[p0], r_table[p2], r_table[p3], r_table[p1]);
				cr1 = _mm_set_ps(0.0f, r_table[p1], r_table[p2], r_table[p0]);
				cg0 = _mm_set_ps(g_table[p0], g_table[p2], g_table[p3], g_table[p1]);
				cg1 = _mm_set_ps(0.0f, g_table[p1], g_table[p2], g_table[p0]);
				cb0 = _mm_set_ps(b_table[p0], b_table[p2], b_table[p3], b_table[p1]);
				cb1 = _mm_set_ps(0.0f, b_table[p1], b_table[p2], b_table[p0]);
				break;
			default: // Can never happen
			case 11: // rx >= ry && ry >= rz && rx < rz
			case 15: // rx >= ry && ry >= rz && rx >= rz
				p1 = CLU_OFFSET(h, l, l);
				p2 = CLU_OFFSET(h, h, l);
				cr0 = _mm_set_ps(r_table[p0], r_table[p3], r_table[p2], r_table[p1]);
				cr1 = _mm_set_ps(0.0f, r_table[p2], r_table[p1], r_table[p0]);
				cg0 = _mm_set_ps(g_table[p0], g_table[p3], g_table[p2], g_table[p1]);
				cg0 = _mm_set_ps(0.0f, g_table[p2], g_table[p1], g_table[p0]);
				cb1 = _mm_set_ps(b_table[p0], b_table[p3], b_table[p2], b_table[p1]);
				cb1 = _mm_set_ps(0.0f, b_table[p2], b_table[p1], b_table[p0]);
				break;
		}

		if (first) {
			fprintf(stderr, "[AO] pn %08X %08X %08X %08X\n", p3, p2, p1, p0);
		}

		// ra0 = 0, c0_b, c0_g, c0_r
		// rb0 = 0, c1_b, c1_g, c1_r
		// rc0 = 0, c2_b, c2_g, c2_r
		// rd0 = 0, c3_b, c3_g, c3_r
		//
		// rb0 *= (rx, rx, rx, rx)
		// rc0 *= (ry, ry, ry, ry)
		// rd0 *= (rz, rz, rz, rz)
		//
		// ra0 += rb0
		// ra0 += rc0
		// ra0 += rd0
		//
		// ra *= (255.0f, 255.0, 255.0f, 255.0f)
		// ra += (0.5f, 0.5f, 0.5f, 0.5f, 0.5f)
		// ra0 = min(ra0, 255.0f)
		// ra0 = max(ra0, 0.0f)

		// highest register needs to be 1.0
		rr = _mm_add_ps(rr, _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f));

		if (first) {
			_mm_storeu_ps(&debugf[0], rr);
			fprintf(stderr, "[AO] rr mul = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
		}

		if (first) {
			_mm_storeu_ps(&debugf[0], cr0);
			fprintf(stderr, "[AO] cr0 = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
			_mm_storeu_ps(&debugf[0], cr1);
			fprintf(stderr, "[AO] cr1 = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
			_mm_storeu_ps(&debugf[0], cg0);
			fprintf(stderr, "[AO] cg0 = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
			_mm_storeu_ps(&debugf[0], cg1);
			fprintf(stderr, "[AO] cg1 = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
			_mm_storeu_ps(&debugf[0], cb0);
			fprintf(stderr, "[AO] cb0 = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
			_mm_storeu_ps(&debugf[0], cb1);
			fprintf(stderr, "[AO] cb1 = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
		}

		// clut_r = c0_r + c1_r*rx + c2_r*ry + c3_r*rz;
		// clut_g = c0_g + c1_g*rx + c2_g*ry + c3_g*rz;
		// clut_b = c0_b + c1_b*rx + c2_b*ry + c3_b*rz;
		cr0 = _mm_sub_ps(cr0, cr1);
		cg0 = _mm_sub_ps(cg0, cg1);
		cb0 = _mm_sub_ps(cb0, cb1);

		if (first) {
			_mm_storeu_ps(&debugf[0], cr0);
			fprintf(stderr, "[AO] cr sub = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
			_mm_storeu_ps(&debugf[0], cg0);
			fprintf(stderr, "[AO] cg sub = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
			_mm_storeu_ps(&debugf[0], cb0);
			fprintf(stderr, "[AO] cb sub = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
		}

		cr0 = _mm_mul_ps(cr0, rr);
		cg0 = _mm_mul_ps(cg0, rr);
		cb0 = _mm_mul_ps(cb0, rr);

		if (first) {
			_mm_storeu_ps(&debugf[0], cr0);
			fprintf(stderr, "[AO] cr * rr = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
			_mm_storeu_ps(&debugf[0], cg0);
			fprintf(stderr, "[AO] cg * rr = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
			_mm_storeu_ps(&debugf[0], cb0);
			fprintf(stderr, "[AO] cb * rr = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
		}

		// Add bottom two 32-bit words to upper two 32-bit words.
		cr0 = _mm_add_ps(cr0, _mm_movehl_ps(cr0, cr0));
		cg0 = _mm_add_ps(cg0, _mm_movehl_ps(cg0, cg0));
		cb0 = _mm_add_ps(cb0, _mm_movehl_ps(cb0, cb0));

		// Add lowest 32-bit word to second lowest 32-bit word.
		cr0 = _mm_add_ss(cr0, _mm_shuffle_ps(cr0, cr0, _MM_SHUFFLE(0, 0, 0, 1)));
		cg0 = _mm_add_ss(cg0, _mm_shuffle_ps(cg0, cg0, _MM_SHUFFLE(0, 0, 0, 1)));
		cb0 = _mm_add_ss(cb0, _mm_shuffle_ps(cb0, cb0, _MM_SHUFFLE(0, 0, 0, 1)));

		if (first) {
			_mm_storeu_ps(&debugf[0], cr0);
			fprintf(stderr, "[AO] cr sum = %f\n", debugf[0]);
			_mm_storeu_ps(&debugf[0], cg0);
			fprintf(stderr, "[AO] cg sum = %f\n", debugf[0]);
			_mm_storeu_ps(&debugf[0], cb0);
			fprintf(stderr, "[AO] cb sum = %f\n", debugf[0]);
		}

		// Combine lowest 32-bit words from each component register into 128-bits.
		// XX clut_g clut_r clut_r
		rr = _mm_shuffle_ps(cr0, cg0, _MM_SHUFFLE(0, 0, 0, 0));
		// XX clut_r clut_g clut_r
		rr = _mm_shuffle_ps(rr, rr, _MM_SHUFFLE(0, 0, 2, 0));
		// XX clut_b clut_g clut_r
		rr = _mm_shuffle_ps(rr, cb0, _MM_SHUFFLE(0, 0, 1, 0));

		if (first) {
			_mm_storeu_ps(&debugf[0], rr);
			fprintf(stderr, "[AO] clut rgb = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
		}

		// Clamp components from 0 - 255
		rr = _mm_mul_ps(rr, _mm_set1_ps(255.0f));
		if (first) {
			_mm_storeu_ps(&debugf[0], rr);
			fprintf(stderr, "[AO] clut mul 1 = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
			cr0 = _mm_set1_ps(255.0f);
			_mm_storeu_ps(&debugf[0], cr0);
			fprintf(stderr, "[AO] clut mul 2 = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
		}
		rr = _mm_add_ps(rr, _mm_set1_ps(0.5f));
		if (first) {
			_mm_storeu_ps(&debugf[0], rr);
			fprintf(stderr, "[AO] clut add = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
		}
		rr = _mm_min_ps(rr, _mm_set1_ps(255.0f));
		if (first) {
			_mm_storeu_ps(&debugf[0], rr);
			fprintf(stderr, "[AO] clut min = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
		}
		rr = _mm_max_ps(rr, _mm_set1_ps(0.0f));

		if (first) {
			_mm_storeu_ps(&debugf[0], rr);
			fprintf(stderr, "[AO] clut max = %f %f %f %f\n", debugf[3], debugf[2], debugf[1], debugf[0]);
		}

		// convert final result into RGB component values
		xx = _mm_cvtps_epi32(rr);
		_mm_storeu_si128((__m128i*)&offset_l[0], xx);
		if (first) {
			fprintf(stderr, "[AO] rgb %08X %08X %08X\n", offset_l[2], offset_l[1], offset_l[0]);
		}

		dest[kRIndex] = offset_l[0];
		dest[kGIndex] = offset_l[1];
		dest[kBIndex] = offset_l[2];
		if (kAIndex != NO_A_INDEX) {
			dest[kAIndex] = in_a;
		}

		if (first) {
			fprintf(stderr, "[AO] dst = 0x%02X%02X%02X%02X\n", dest[3], dest[2], dest[1], dest[0]);
		}

		src += components;
		dest += components;
		first = false;
	}
}

void qcms_transform_data_tetra_clut_rgb_sse2(const qcms_transform *transform,
                                             const unsigned char *src,
                                             unsigned char *dest,
                                             size_t length)
{
  qcms_transform_data_template_tetra_clut_sse2<RGBA_R_INDEX, RGBA_G_INDEX, RGBA_B_INDEX>(transform, src, dest, length);
}

void qcms_transform_data_tetra_clut_rgba_sse2(const qcms_transform *transform,
                                              const unsigned char *src,
                                              unsigned char *dest,
                                              size_t length)
{
  qcms_transform_data_template_tetra_clut_sse2<RGBA_R_INDEX, RGBA_G_INDEX, RGBA_B_INDEX, RGBA_A_INDEX>(transform, src, dest, length);
}

void qcms_transform_data_tetra_clut_bgra_sse2(const qcms_transform *transform,
                                              const unsigned char *src,
                                              unsigned char *dest,
                                              size_t length)
{
  qcms_transform_data_template_tetra_clut_sse2<BGRA_R_INDEX, BGRA_G_INDEX, BGRA_B_INDEX, BGRA_A_INDEX>(transform, src, dest, length);
}
