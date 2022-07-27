/*
 * kalman-filter.c
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

#include "kalman-filter.h"
#include "matrix_f.h"
 
/**********************************************************************
 * @defgroup kalman_filter Kalman Filter
 */

/**
 * 0.95 quantile of the chi-square distribution with N degrees of freedom (contains values for N=1, ..., 9).
 * Sample code of the calculation: 
 * 
#include <gsl/gsl_cdf.h>
void init_chi2pinv() {
    for(int i = 1; i < 10; ++i) s_chi2pinv95[i] = gsl_cdf_chisq_Pinv(0.95, i);
}

 * 
***********************************************************************/ 
const float s_chi2pinv95[10] = {
	[0] =  0.0f, // not used
	[1] =  3.841459f,
	[2] =  5.991465f,
	[3] =  7.814728f,
	[4] =  9.487729f,
	[5] = 11.070498f,
	[6] = 12.591587f,
	[7] = 14.067140f,
	[8] = 15.507313f,
	[9] = 16.918978f,
};


/**
 * Create track from unassociated measurement.
 */
int kalman_filter_initialize(kalman_filter_t *kf, 
	const deepsort_measurement_t measurement, 
	kalman_filter_state_t *state)
{
	assert(state);
	memset(state, 0, sizeof(*state));
	
	state->measurement = measurement;
	float std[8] = {
		[0] = 2.0 * kf->std_weight.position * measurement.values[3],
		[1] = 2.0 * kf->std_weight.position * measurement.values[3],
		[2] = 1e-2,
		[3] = 2.0 * kf->std_weight.position * measurement.values[3],
		
		[4] = 10.0 * kf->std_weight.velocity * measurement.values[3],
		[5] = 10.0 * kf->std_weight.velocity * measurement.values[3],
		[6] = 1e-5,
		[7] = 10.0 * kf->std_weight.velocity * measurement.values[3],
	};
	for(int row = 0; row < 8; ++row) {
		// covariance = np.diag(np.square(std))
		state->covariance[row][row] = std[row] * std[row];
	}
	return 0;
}

int kalman_filter_predict(kalman_filter_t * kf, kalman_filter_state_t * state)
{
	float std[8] = {
		[0] = 2.0 * kf->std_weight.position * state->mean[3],
		[1] = 2.0 * kf->std_weight.position * state->mean[3],
		[2] = 1e-2,
		[3] = 2.0 * kf->std_weight.position * state->mean[3],
		
		[4] = 10.0 * kf->std_weight.velocity * state->mean[3],
		[5] = 10.0 * kf->std_weight.velocity * state->mean[3],
		[6] = 1e-5,
		[7] = 10.0 * kf->std_weight.velocity * state->mean[3],
	};
	
	// current_mean = motion_mat * prev_mean
	struct matrix_f motion_mat = {
		.data = (float *)kf->motion_mat,
		.m = 8, .n = 8,
		.order = CblasRowMajor,
	};
	float * mean = matrix_mv(state->mean, &motion_mat, 0, state->mean);
	assert(mean && mean == state->mean);
	
/************************************************************************/
	/* A: state transition matrix ==> (motion_mat)
	* P: state covariance matrix ==> (state->covariance)
	* B: input transition matrix 
	* Q: input covariance matrix  
	*
	*/
	/* Predict next covariance using system dynamics and input */
	/* P = A*P*A' + B*Q*B'                                     */
/************************************************************************/

	// * BQB' = matrix::diag[[ square(std[]) ]]
	float B_Q_BT_data[8][8] = { 0 };
	for(int i = 0; i < 8; ++i) B_Q_BT_data[i][i] = (std[i]*std[i]);
	
	float temp_data[8*8] = { 0 }; 
	struct matrix_f tmp = {
		.data = temp_data,
		.m = 8, .n = 8, 
		.order = CblasRowMajor
	};
	struct matrix_f covariance = {
		.data = (float *)state->covariance,
		.m = 8, .n = 8, 
		.order = CblasColMajor
		
	};
	
	matrix_mm(&tmp, &motion_mat, 0, &covariance, 0);
	memcpy(state->covariance, B_Q_BT_data, sizeof(state->covariance));
	matrix_f_mm(&covariance, 1.0f, &tmp, 0, &motion_mat, 1, 1.0f);
	return 0;
}

/* Project state distribution to measurement space. */
int kalman_filter_project(kalman_filter_t *kf, 
	const kalman_filter_state_t * state,
	measurement_state_t * measurement_state)
{
	memset(measurement_state, 0, sizeof(*measurement_state));
	float std[4] = {
		[0] = kf->std_weight.position * state->mean[3],
		[1] = kf->std_weight.position * state->mean[3],
		[2] = 1e-1,
		[3] = kf->std_weight.position * state->mean[3],
	};
	
	struct matrix_f update_mat = {
		.data = (float *)kf->update_mat,
		.m = 4, .n = 8,
		.order = CblasRowMajor
	};
	
	matrix_mv(measurement_state->mean, &update_mat, 0, state->mean);
	
	// * BQB' = matrix::diag[[ square(std[]) ]]
	float B_Q_BT_data[4][8] = { 0 };
	for(int i = 0; i < 4; ++i) B_Q_BT_data[i][i] = (std[i]*std[i]);
	
	float temp_data[8*8] = { 0 }; 
	struct matrix_f tmp = {
		.data = temp_data,
		.m = 8, .n = 8, 
		.order = CblasRowMajor
	};
	struct matrix_f state_cov = {
		.data = (float *)state->covariance,
		.m = 8, .n = 8, 
		.order = CblasColMajor
		
	};
	struct matrix_f measurement_cov = {
		.data = (float *)measurement_state->covariance,
		.m = 4, .n = 8, 
		.order = CblasColMajor
	};
	
	matrix_mm(&tmp, &update_mat, 0, &state_cov, 0);
	memcpy(measurement_state->covariance, B_Q_BT_data, sizeof(measurement_state->covariance));
	matrix_f_mm(&measurement_cov, 1.0f, &tmp, 0, &update_mat, 1, 1.0f);
	
	return 0;
}

int kalman_filter_update(kalman_filter_t *kf, kalman_filter_state_t * state, deepsort_measurement_t * measurement)
{
	// TODO: 
	return -1;
}


#if defined(_STAND_ALONE) 
#include "matrix_f.c"

int main(int argc, char **argv)
{
	// todo ...
	return 0;
}
#endif
