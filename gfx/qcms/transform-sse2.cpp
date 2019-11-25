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

// Using lcms' tetra interpolation algorithm.
template <size_t kRIndex, size_t kGIndex, size_t kBIndex, size_t kAIndex = NO_A_INDEX>
static void qcms_transform_data_template_tetra_clut_sse2(const qcms_transform *transform, const unsigned char *src, unsigned char *dest, size_t length) {
	const unsigned int components = A_INDEX_COMPONENTS(kAIndex);
	unsigned int i;
	int xy_len = 1;
	int x_len = transform->grid_size;
	int len = x_len * x_len;
	float* r_table = transform->r_clut;
	float* g_table = transform->g_clut;
	float* b_table = transform->b_clut;
	float c0_r, c1_r, c2_r, c3_r;
	float c0_g, c1_g, c2_g, c3_g;
	float c0_b, c1_b, c2_b, c3_b;
	float clut_r, clut_g, clut_b;
	for (i = 0; i < length; i++) {
		unsigned char in_r = src[kRIndex];
		unsigned char in_g = src[kGIndex];
		unsigned char in_b = src[kBIndex];
		unsigned char in_a;
		if (kAIndex != NO_A_INDEX) {
			in_a = src[kAIndex];
		}
		src += components;
		float linear_r = in_r/255.0f, linear_g=in_g/255.0f, linear_b = in_b/255.0f;

		int x = in_r * (transform->grid_size-1) / 255;
		int y = in_g * (transform->grid_size-1) / 255;
		int z = in_b * (transform->grid_size-1) / 255;
		int x_n = int_div_ceil(in_r * (transform->grid_size-1), 255);
		int y_n = int_div_ceil(in_g * (transform->grid_size-1), 255);
		int z_n = int_div_ceil(in_b * (transform->grid_size-1), 255);
		float rx = linear_r * (transform->grid_size-1) - x;
		float ry = linear_g * (transform->grid_size-1) - y;
		float rz = linear_b * (transform->grid_size-1) - z;

		c0_r = CLU(r_table, x, y, z);
		c0_g = CLU(g_table, x, y, z);
		c0_b = CLU(b_table, x, y, z);

		if( rx >= ry ) {
			if (ry >= rz) { //rx >= ry && ry >= rz
				c1_r = CLU(r_table, x_n, y, z) - c0_r;
				c2_r = CLU(r_table, x_n, y_n, z) - CLU(r_table, x_n, y, z);
				c3_r = CLU(r_table, x_n, y_n, z_n) - CLU(r_table, x_n, y_n, z);
				c1_g = CLU(g_table, x_n, y, z) - c0_g;
				c2_g = CLU(g_table, x_n, y_n, z) - CLU(g_table, x_n, y, z);
				c3_g = CLU(g_table, x_n, y_n, z_n) - CLU(g_table, x_n, y_n, z);
				c1_b = CLU(b_table, x_n, y, z) - c0_b;
				c2_b = CLU(b_table, x_n, y_n, z) - CLU(b_table, x_n, y, z);
				c3_b = CLU(b_table, x_n, y_n, z_n) - CLU(b_table, x_n, y_n, z);
			} else {
				if (rx >= rz) { //rx >= rz && rz >= ry
					c1_r = CLU(r_table, x_n, y, z) - c0_r;
					c2_r = CLU(r_table, x_n, y_n, z_n) - CLU(r_table, x_n, y, z_n);
					c3_r = CLU(r_table, x_n, y, z_n) - CLU(r_table, x_n, y, z);
					c1_g = CLU(g_table, x_n, y, z) - c0_g;
					c2_g = CLU(g_table, x_n, y_n, z_n) - CLU(g_table, x_n, y, z_n);
					c3_g = CLU(g_table, x_n, y, z_n) - CLU(g_table, x_n, y, z);
					c1_b = CLU(b_table, x_n, y, z) - c0_b;
					c2_b = CLU(b_table, x_n, y_n, z_n) - CLU(b_table, x_n, y, z_n);
					c3_b = CLU(b_table, x_n, y, z_n) - CLU(b_table, x_n, y, z);
				} else { //rz > rx && rx >= ry
					c1_r = CLU(r_table, x_n, y, z_n) - CLU(r_table, x, y, z_n);
					c2_r = CLU(r_table, x_n, y_n, z_n) - CLU(r_table, x_n, y, z_n);
					c3_r = CLU(r_table, x, y, z_n) - c0_r;
					c1_g = CLU(g_table, x_n, y, z_n) - CLU(g_table, x, y, z_n);
					c2_g = CLU(g_table, x_n, y_n, z_n) - CLU(g_table, x_n, y, z_n);
					c3_g = CLU(g_table, x, y, z_n) - c0_g;
					c1_b = CLU(b_table, x_n, y, z_n) - CLU(b_table, x, y, z_n);
					c2_b = CLU(b_table, x_n, y_n, z_n) - CLU(b_table, x_n, y, z_n);
					c3_b = CLU(b_table, x, y, z_n) - c0_b;
				}
			}
		} else {
			if (rx >= rz) { //ry > rx && rx >= rz
				c1_r = CLU(r_table, x_n, y_n, z) - CLU(r_table, x, y_n, z);
				c2_r = CLU(r_table, x, y_n, z) - c0_r;
				c3_r = CLU(r_table, x_n, y_n, z_n) - CLU(r_table, x_n, y_n, z);
				c1_g = CLU(g_table, x_n, y_n, z) - CLU(g_table, x, y_n, z);
				c2_g = CLU(g_table, x, y_n, z) - c0_g;
				c3_g = CLU(g_table, x_n, y_n, z_n) - CLU(g_table, x_n, y_n, z);
				c1_b = CLU(b_table, x_n, y_n, z) - CLU(b_table, x, y_n, z);
				c2_b = CLU(b_table, x, y_n, z) - c0_b;
				c3_b = CLU(b_table, x_n, y_n, z_n) - CLU(b_table, x_n, y_n, z);
			} else {
				if (ry >= rz) { //ry >= rz && rz > rx
					c1_r = CLU(r_table, x_n, y_n, z_n) - CLU(r_table, x, y_n, z_n);
					c2_r = CLU(r_table, x, y_n, z) - c0_r;
					c3_r = CLU(r_table, x, y_n, z_n) - CLU(r_table, x, y_n, z);
					c1_g = CLU(g_table, x_n, y_n, z_n) - CLU(g_table, x, y_n, z_n);
					c2_g = CLU(g_table, x, y_n, z) - c0_g;
					c3_g = CLU(g_table, x, y_n, z_n) - CLU(g_table, x, y_n, z);
					c1_b = CLU(b_table, x_n, y_n, z_n) - CLU(b_table, x, y_n, z_n);
					c2_b = CLU(b_table, x, y_n, z) - c0_b;
					c3_b = CLU(b_table, x, y_n, z_n) - CLU(b_table, x, y_n, z);
				} else { //rz > ry && ry > rx
					c1_r = CLU(r_table, x_n, y_n, z_n) - CLU(r_table, x, y_n, z_n);
					c2_r = CLU(r_table, x, y_n, z_n) - CLU(r_table, x, y, z_n);
					c3_r = CLU(r_table, x, y, z_n) - c0_r;
					c1_g = CLU(g_table, x_n, y_n, z_n) - CLU(g_table, x, y_n, z_n);
					c2_g = CLU(g_table, x, y_n, z_n) - CLU(g_table, x, y, z_n);
					c3_g = CLU(g_table, x, y, z_n) - c0_g;
					c1_b = CLU(b_table, x_n, y_n, z_n) - CLU(b_table, x, y_n, z_n);
					c2_b = CLU(b_table, x, y_n, z_n) - CLU(b_table, x, y, z_n);
					c3_b = CLU(b_table, x, y, z_n) - c0_b;
				}
			}
		}

		clut_r = c0_r + c1_r*rx + c2_r*ry + c3_r*rz;
		clut_g = c0_g + c1_g*rx + c2_g*ry + c3_g*rz;
		clut_b = c0_b + c1_b*rx + c2_b*ry + c3_b*rz;

		dest[kRIndex] = clamp_u8(clut_r*255.0f);
		dest[kGIndex] = clamp_u8(clut_g*255.0f);
		dest[kBIndex] = clamp_u8(clut_b*255.0f);
		if (kAIndex != NO_A_INDEX) {
			dest[kAIndex] = in_a;
		}
		dest += components;
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
