/*
 * cv-wrapper.cpp
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


#include <iostream>

#include "opencv2/imgproc/types_c.h"	// CV_BGRA2BGR, ...
#include "opencv2/core/core_c.h"

#include "opencv2/core/core.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"


#include "cv-wrapper.h"

#ifdef __cplusplus
extern "C" {
typedef void * cvmat_t;
}
#endif


/************************************************************************
 * cv::Mat <--> cvmat_t
************************************************************************/
cvmat_t cvmat_new(int width, int height, const unsigned char * bgra /* bgra or bgr */, int channels)
{
	assert(channels == 3 || channels == 4);
	
	cv::Mat *mat = NULL;
	if(width <= 0 || height <= 0) return new cv::Mat(1, 1, CV_8UC3); // return a dummy cv::Mat object
	
	mat = new cv::Mat();
	if(bgra) {
		IplImage *image = cvCreateImageHeader(cvSize(width, height), 8, channels);
		assert(image);
		image->imageData = (char *)bgra;
		if(channels == 3) *mat = cv::cvarrToMat(image);
		else {
			cv::Mat tmp = cv::cvarrToMat(image);
			cv::cvtColor(tmp, *mat, cv::COLOR_BGRA2BGR);
		}
		cvReleaseImageHeader(&image);
	}
	return (cvmat_t)mat;
}

void cvmat_free(cvmat_t mat)
{
	if(mat) delete CV_MAT_PTR(mat);
}

void cvmat_set(cvmat_t dst, cvmat_t src)
{
	CV_MAT(dst) = CV_MAT(src);
}

void cvmat_cvt_color(cvmat_t dst, cvmat_t src, int type)
{
	cv::cvtColor(CV_MAT(src), CV_MAT(dst), type);
}

cvmat_t cvmat_resize(cvmat_t _dst, cvmat_t src, int dst_width, int dst_height, int interpolation /* default: INTER_LINEAR */)
{
	cv::Mat *dst = CV_MAT_PTR(_dst);
	if(NULL == dst) dst = new cv::Mat();
	cv::resize(CV_MAT(src), *dst, cv::Size(dst_width, dst_height), interpolation);
	
	return (cvmat_t)dst;
}

cvmat_t cvmat_scale(cvmat_t _dst, cvmat_t src, double factor_x, double factor_y, int interpolation /* default: INTER_LINEAR */)
{
	cv::Mat *dst = CV_MAT_PTR(_dst);
	if(NULL == dst) dst = new cv::Mat();
	cv::resize(CV_MAT(src), *dst, cv::Size(), factor_x, factor_y, interpolation);
	return (cvmat_t)dst;
}

unsigned char * cvmat_get_data(cvmat_t mat)
{
	assert(mat);
	return (unsigned char *)((cv::Mat *)mat)->data;
}
int cvmat_get_size(cvmat_t _mat, int * width, int * height)
{
	assert(_mat);
	cv::Mat &mat = CV_MAT(_mat);
	*width = mat.cols;
	*height = mat.rows;
	return 0;
}

#if defined(TEST_CVMAT_WRAPPER_) && defined(_STAND_ALONE)
#include <cairo/cairo.h>

#define MAKE_BGRA_PIXEL(b,g,r) ( (uint32_t)(b) | ((uint32_t)(g) << 8) | ((uint32_t)(r) << 16) | 0xFF000000)
static int img_utils_bgr_to_bgra(const unsigned char * bgr, int width, int height, unsigned char ** p_bgra)
{
	ssize_t num_pixels = width * height;
	unsigned char * bgra = *p_bgra;
	if(NULL == bgra) {
		bgra = (unsigned char *)malloc(num_pixels * 4);
		assert(bgra);
		*p_bgra = bgra;
	}
	uint32_t *dst = (uint32_t *)bgra;
	for(ssize_t i = 0; i < num_pixels; ++i, ++dst, bgr += 3)
	{
		*dst = MAKE_BGRA_PIXEL(bgr[0], bgr[1], bgr[2]);
	}
	return 0;
}

void test_cvmat(int argc, char ** argv);

int main(int argc, char **argv)
{
	test_cvmat(argc, argv);
	return 0;
}

void test_cvmat(int argc, char **argv)
{
	// load image
	const char * png_file = "face-landmark68.png";
	const char * output_file = "cv-output.jpg";
	cairo_surface_t * png = cairo_image_surface_create_from_png(png_file);
	assert(png);
	
	int width = cairo_image_surface_get_width(png);
	int height = cairo_image_surface_get_height(png);
	cairo_format_t fmt = cairo_image_surface_get_format(png);
	
	printf("fmt: %d\n", fmt);
	assert(fmt == CAIRO_FORMAT_ARGB32 || fmt == CAIRO_FORMAT_RGB24);
	
	unsigned char * image_data = cairo_image_surface_get_data(png);
	
	// convert to cvmat_t
	cvmat_t mat = cvmat_new(width, height, image_data, 4);
	assert(mat);
	cairo_surface_destroy(png);
	
	// verify cvmat image
	cv::imwrite(output_file, CV_MAT(mat));
	printf("cvmat_size: %d x %d\n", width, height);
	
	// test resize image
	cvmat_t resized = cvmat_scale(NULL, mat, 0.5, 0.5, CV_INTER_CUBIC);
	width = -1;
	height = -1;
	int rc = cvmat_get_size(resized, &width, &height);
	assert(0 == rc);
	printf("resized size: %d x %d\n", width, height);
	
	unsigned char * bgr = cvmat_get_data(resized);
	unsigned char * bgra = NULL;
	rc = img_utils_bgr_to_bgra(bgr, width, height, &bgra);
	assert(0 == rc && bgra);
	
	png = cairo_image_surface_create_for_data(bgra, CAIRO_FORMAT_RGB24, width, height, width * 4);
	assert(cairo_surface_status(png) == CAIRO_STATUS_SUCCESS);
	
	cairo_surface_write_to_png(png, "test-output.png");
	cairo_surface_destroy(png);
	
	cvmat_free(mat);
	cvmat_free(resized);
	free(bgra);
}
#endif

