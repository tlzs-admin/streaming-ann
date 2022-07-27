#ifndef MATRIX_F_H_
#define MATRIX_F_H_

#include <cblas-openblas64.h>

#ifdef __cplusplus
extern "C" {
#endif


struct matrix_f
{
	float * data;
	int m;	// num_rows
	int n;	// num_cols
	
	int stride;	// row size ( >= n ), default=n, 
	
	/* flags: */
	int order; // default: CBlasRowMajor
	void (* free_data)(void * data);
};
#define MATRIX_F_INITIALIZER(_data, _m, _n) { \
		.data = (float *)_data, \
		.m = _m, .n = _n, \
		.order = CblasRowMajor, } 

struct matrix_f * matrix_f_init(struct matrix_f * matrix,  
	int m, int n, 
	int stride, 
	const float *init_data);
void matrix_f_cleanup(struct matrix_f * matrix);


/**
 * matrix_f_mm: 
 * C = alpha * OP(A) * OP(B) + beta * C
 *     OP: Tranpose or NoTranpose
 *     OP(A): [ m * k ]
 *     OP(B): [ k * n ]
 *     C: [ m * n ]
 * @return : C
**/
struct matrix_f *matrix_f_mm(struct matrix_f *C, 
	const float alpha, 
	const struct matrix_f * A, int transpos_A, 
	const struct matrix_f * B, int transpos_B, 
	const float beta);

/* 
 * matrix_f_mv: 
 * y = alpha * OP(A) * x * beta * y 
 */ 
float * matrix_f_mv(float * y, 
	const float alpha, 
	const struct matrix_f * A, int transpos_A, 
	const float *x, 
	int ldx, 	// incx, default: 1
	const float beta,
	int ldy	// incy, default: 1
	);
	
#define matrix_mm(C, A, transpos_A, B, transpos_B) \
		matrix_f_mm(C, 1.0f, A, transpos_A, B, transpos_B, 0)

#define matrix_mv(y, A, transpos_A, x) \
		matrix_f_mv(y, 1.0f, A, transpos_A, x, 1, 0.0f, 1)


#ifdef __cplusplus
}
#endif
#endif
