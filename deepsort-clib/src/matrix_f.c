/*
 * matrix_f.c
 * 
 * Copyright 2022 chehw <hongwei.che@gmail.com>
 * 
 * The MIT License (MIT)
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy 
 * of this software and associated documentation files (the "Software"), to deal 
 * in the Software without restriction, including without limitation the rights 
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
 * copies of the Software, and to permit persons to whom the Software is 
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all 
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS 
 * IN THE SOFTWARE.
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <cblas-openblas64.h>

#include "matrix_f.h"

struct matrix_f * matrix_f_init(struct matrix_f * matrix,  
	int m, int n, 
	int stride, 
	const float *init_data)
{
	if(NULL == matrix) matrix = calloc(1, sizeof(*matrix));
	else memset(matrix, 0, sizeof(*matrix));
	
	if(0 == stride) stride = n;
	size_t data_size = sizeof(matrix->data) * stride * m;
	
	float *data = malloc(data_size);
	assert(data);

	matrix->data = data;
	matrix->m = m;
	matrix->n = n;
	matrix->order = CblasRowMajor;
	matrix->free_data = free;

	if(init_data) memcpy(matrix->data, init_data, data_size);
	return matrix;
}
void matrix_f_cleanup(struct matrix_f * matrix)
{
	if(NULL == matrix) return;
	if(matrix->free_data) matrix->free_data(matrix->data);
	memset(matrix, 0, sizeof(*matrix));
	return;
}

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
	const float beta)
{
	int order = C?C->order:CblasRowMajor;
	assert(order == A->order && order == B->order);
	
	int m, n, k;
	int A_rows = transpos_A?A->n:A->m;
	int A_cols = transpos_A?A->m:A->n;
	int B_rows = transpos_B?B->n:B->m;
	int B_cols = transpos_B?B->m:B->n;
	
	assert(A_cols == B_rows);
	m = A_rows;
	k = A_cols;
	n = B_cols;
	
	if(NULL == C || C->data == NULL || C->m <= 0 || C->n <= 0) {
		C = matrix_f_init(C, m, n, 0, NULL);
		C->order = order;
	}
	int lda = A->stride;
	int ldb = B->stride;
	int ldc = C->stride;
	if(0 == lda) lda = (A->order==CblasRowMajor)?A_cols:A_rows;
	if(0 == ldb) ldb = (B->order==CblasRowMajor)?B_cols:B_rows;
	if(0 == ldc) ldc = (C->order==CblasRowMajor)?C->n:C->m;
	
	transpos_A = transpos_A?CblasTrans:CblasNoTrans;
	transpos_B = transpos_B?CblasTrans:CblasNoTrans;
	
	
	printf("order: %s\n", (order == CblasColMajor)?"ColMajor":"RowMajor");
	cblas_sgemm(C->order, transpos_A, transpos_B, 
		m, n, k, 
		alpha, A->data, lda, 
		B->data, ldb, 
		beta, C->data, ldc);
	return C;
}

float * matrix_f_mv(float * y, 
	const float alpha, 
	const struct matrix_f * A, int transpos_A, 
	const float *x, 
	int ldx, 	// incx, default: 1
	const float beta,
	int ldy	// incy, default: 1
	)
{
	int m = transpos_A?A->n:A->m;
	int n = transpos_A?A->m:A->n;
	int lda = A->stride;
	
	if(lda == 0) lda = (A->order==CblasRowMajor)?n:m;
	if(ldx == 0) ldx = 1;
	if(ldy == 0) ldy = 1;

	if(NULL == y) y = calloc(ldy * m, sizeof(float));
	assert(y);
	
	transpos_A = transpos_A?CblasTrans:CblasNoTrans;
	cblas_sgemv(A->order, transpos_A, m, n,
		alpha, A->data, lda, 
		x, ldx, 
		beta, y, ldy);
	return y;
}

#define dump_matrix(matrix) matrix_f_dump(matrix, #matrix)

void matrix_f_dump(const struct matrix_f * matrix, const char * title)
{
	printf("======== %s(%s) ==========\n", __FUNCTION__, title);
	printf("  order: %s\n", matrix->order == CblasColMajor?"ColMajor":"RowMajor");
	printf("  m: %d, n: %d\n", matrix->m, matrix->n);
	
	int m = matrix->m;
	int n = matrix->n;
	
	if(matrix->order == CblasColMajor) {
		m = matrix->n;
		n = matrix->m;
	} 
	
	int stride = (matrix->stride>0)?matrix->stride:n;
	for(int i = 0; i < m; ++i) {
		printf("  | ");
		for(int j = 0; j < n; ++j) {
			printf(" %3g ", matrix->data[i * stride + j]);
		}
		printf(" |\n");
	}
	return;
}

#if defined(TEST_MATRIX_F_) && defined(_STAND_ALONE)
int main(int argc, char ** argv)
{
	/*********************************************
	 *       A      X     B     =     C
	 *  | 1  2  3 |   | 7  8  |   |  58   64 |
	 *  | 4  5  6 | X | 9  10 | = | 139  154 | 
	 *                | 11 12 |
	*********************************************/
	static const float A_data_row_major[2][3] = {	// row major
		[0] = { 1, 2, 3 },
		[1] = { 4, 5, 6 },
	};
	static const float A_data_col_major[3][2] = {	// col major
		[0] = { 1, 4 },
		[1] = { 2, 5 },
		[2] = { 3, 6 },
	};
	static const float B_data_row_major[3][2] = {
		[0] = {  7,  8 }, 
		[1] = {  9, 10 },
		[2] = { 11, 12 },
	};
	static const float B_data_col_major[2][3] = {
		[0] = { 7, 9, 11 },
		[1] = { 8, 10, 12 },
	};
	
	struct matrix_f A[1], B[1];
	memset(A, 0, sizeof(A));
	memset(B, 0, sizeof(B));
	
	A->order = CblasRowMajor;
	A->data = (float *)A_data_row_major;
	A->m = 2;
	A->n = 3;
	
	B->order = CblasRowMajor;
	B->data = (float *)B_data_row_major;
	B->m = 3;
	B->n = 2;
	
	
	dump_matrix(A);
	dump_matrix(B);
	
	struct matrix_f *C = matrix_f_mm(NULL, 1.0, A, 0, B, 0, 0.0);
	dump_matrix(C);
	matrix_f_cleanup(C);
	
	A->order = CblasColMajor;
	A->data = (float *)A_data_col_major;
	
	B->order = CblasColMajor;
	B->data = (float *)B_data_col_major;
	
	dump_matrix(A);
	dump_matrix(B);
	
	C->order = CblasColMajor;
	C = matrix_f_mm(C, 1.0, A, 0, B, 0, 0.0);
	dump_matrix(C);
	matrix_f_cleanup(C);
	
	
	const float *x = B_data_col_major[0];
	float y[2] = { 0 };
	matrix_f_mv(y, 1.0, A, 0, x, 1, 0, 1);
	printf("y_col0 = [ ");
	for(int i = 0; i < 2; ++i) {
		printf(" %3g ", y[i]);
	}
	printf(" ]\n");
	assert(y[0] == 58 && y[1] == 139);
	
	x = B_data_col_major[1];
	matrix_f_mv(y, 1.0, A, 0, x, 1, 0, 1);
	printf("y_col1 = [ ");
	for(int i = 0; i < 2; ++i) {
		printf(" %3g ", y[i]);
	}
	printf(" ]\n");
	assert(y[0] == 64 && y[1] == 154);
	
	free(C);
	return 0;
}
#endif

