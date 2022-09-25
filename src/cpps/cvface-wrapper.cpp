/*
 * cvface-wrapper.cpp
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

#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/face.hpp>

#include "cv-wrapper.h"

#define CV_DNN_FACE_DETECTOR_DEFAULT_MODEL  "models/res10_300x300_ssd_iter_140000_fp16.caffemodel"
#define CV_DNN_FACE_DETECTOR_DEFAULT_CONFIG "models/res10_300x300_ssd-deploy.prototxt"
#define CV_DNN_FACE_RECOG_DEFAULT_MODEL     "models/openface.nn4.small2.v1.t7"
#define CV_DNN_FACE_LANDMARK_DEFAULT_MODEL  "models/face_landmark_model.dat"

struct cv_dnn_face_private
{
protected:
	cv_dnn_face_private() = delete;

public:
	cv_dnn_face_private(struct cv_dnn_face *face, const char * det_cfg, const char *det_model, const char *recog_model = NULL, int enable_recog = 0, int enable_landmark = 0);
	virtual ~cv_dnn_face_private();
	
	struct cv_dnn_face *face;
	cv::dnn::Net detector;
	std::vector<cv::String> output_names;
	const char * output_name;
	
	int recog_enabled;
	cv::dnn::Net recog;
	
	int landmark_enabled;
	cv::Ptr<cv::face::FacemarkKazemi> landmark;
};
cv_dnn_face_private::cv_dnn_face_private(struct cv_dnn_face *_face, const char * det_cfg, const char *det_model, const char *recog_model, int enable_recog, int enable_landmark)
{
	if(NULL == det_cfg) det_cfg = CV_DNN_FACE_DETECTOR_DEFAULT_CONFIG;
	if(NULL == det_model) det_model = CV_DNN_FACE_DETECTOR_DEFAULT_MODEL;
	
	detector = cv::dnn::readNetFromCaffe(det_cfg, det_model);
	output_names = detector.getUnconnectedOutLayersNames();
	int num_names = output_names.size();
	assert(num_names > 0);
	output_name = output_names[0].c_str();
	assert(output_name && 0 == strcasecmp(output_name, "detection_out"));
	
	detector.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
	detector.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
	
	recog_enabled = enable_recog;
	if(recog_enabled) {
		if(NULL == recog_model) recog_model = CV_DNN_FACE_RECOG_DEFAULT_MODEL;
		recog = cv::dnn::readNetFromTorch(recog_model);
	}
	
	landmark_enabled = enable_landmark;
	if(landmark_enabled) {
		cv::face::FacemarkKazemi::Params params;
		landmark = cv::face::FacemarkKazemi::create(params);
		landmark->loadModel(CV_DNN_FACE_LANDMARK_DEFAULT_MODEL);
	}
	this->face = _face;
}
cv_dnn_face_private::~cv_dnn_face_private(void)
{
	
}




/************************************************************************
 * cv_dnn_face :
************************************************************************/
static ssize_t cv_dnn_face_detect(struct cv_dnn_face *face, 
		cvmat_t _frame, 
		double confidence_threshold, 
		struct face_detection ** p_detections,
		struct face_landmark ** p_landmarks,
		struct face_feature ** p_features
	)
{
	static const cv::Scalar mean = cv::Scalar(103.94, 116.78, 123.68);
	static const cv::Size input_size = cv::Size(300, 300);
	
	if(confidence_threshold <= 0.0) confidence_threshold = 0.5;	// default threshold

	cv::Mat &frame = CV_MAT(_frame);
	cv::Mat gray_frame;
	cv::cvtColor(frame, gray_frame, cv::COLOR_BGR2GRAY);
		
	int width = frame.cols;
	int height = frame.rows;
	assert(width > 1 && height > 1);
	
	cv::Mat resized, blob, output;
	cv::resize(frame, resized, input_size);
	cv::dnn::blobFromImage(resized, blob, 1.0, input_size, mean, 
		false, // swap R and B
		false, // crop
		CV_32F);
	
	struct cv_dnn_face_private * priv = (struct cv_dnn_face_private *)face->priv;
	cv::dnn::Net &detector = priv->detector;
	
	detector.setInput(blob, "data");
	detector.forward(output, priv->output_name);
	
	// Network produces output blob with a shape [1x1xNx7] where N is a number of
	// detections and an every detection is a vector of values
	// [batchId, classId, confidence, left, top, right, bottom]
	ssize_t total_detections = output.size[2];
	
	if(total_detections <= 0) return total_detections;
	
	cv::Mat caffe_results(output.size[2], output.size[3], CV_32F, (float *)output.data);
	struct face_detection *detections = *p_detections;
	detections = (struct face_detection *)realloc(detections, total_detections * sizeof(*detections));
	assert(detections);
	*p_detections = detections;
	
	ssize_t num_detections = 0;
	std::vector<cv::Rect> bboxes;
	for(ssize_t i = 0; i < total_detections; ++i) 
	{
		float x1, y1, x2, y2;
		float confidence = caffe_results.at<float>(i, 2);
		if(confidence < confidence_threshold) continue;
		detections[i].confidence = confidence;
		
		x1 = caffe_results.at<float>(i, 3);
		y1 = caffe_results.at<float>(i, 4);
		x2 = caffe_results.at<float>(i, 5);
		y2 = caffe_results.at<float>(i, 6);
		
		detections[i].x = x1;
		detections[i].y = y1;
		detections[i].cx = x2 - x1;
		detections[i].cy = y2 - y1;
		
		if(detections[i].cx < 0) {
			detections[i].cx = x1 - x2;
			detections[i].x = x2;
		}
		
		if(detections[i].cy < 0) {
			detections[i].cy = y1 - y2;
			detections[i].y = x2;
		}
		
		int x = detections[i].x * width;
		int y = detections[i].y * height;
		int cx = detections[i].cx * width;
		int cy = detections[i].cy * height;
		bboxes.push_back(cv::Rect(x, y, cx, cy));
		++num_detections;
	}
	if(num_detections > 0) {
		detections = (struct face_detection *)realloc(detections, num_detections * sizeof(*detections));
		assert(detections);
	}
	*p_detections = detections;
	if(num_detections <= 0) return num_detections;
	
	if(p_landmarks && priv->landmark_enabled) {
		cv::Ptr<cv::face::FacemarkKazemi> &landmark = priv->landmark;
		std::vector<std::vector<cv::Point2f>> shapes;
		landmark->fit(gray_frame, bboxes, shapes);
		
		int num_shapes = shapes.size();
		assert(num_shapes == num_detections);
		
		struct face_landmark * marks = (struct face_landmark *)calloc(num_shapes, sizeof(*marks));
		assert(marks);
		
		for(int i = 0; i < num_shapes; ++i) 
		{
			std::vector<cv::Point2f> &points = shapes[i];
			int num_points = points.size();
			assert(num_points == CV_DNN_FACE_LANDMARK_NUM_POINTS);
			
			for(ssize_t ii = 0; ii < num_points; ++ii) {
				// convert to relative coordinates
				marks[i].points[ii].x = points[ii].x / width;
				marks[i].points[ii].y = points[ii].y / height;
			}
		}
		*p_landmarks = marks;
	}
	
	if(p_features && priv->recog_enabled) {
		cv::dnn::Net &face_recog = priv->recog;
		
		struct face_feature * features = (struct face_feature *)calloc(num_detections, sizeof(*features));
		assert(features);
		
		for(ssize_t i = 0; i < num_detections; ++i) {
			cv::Rect & bbox = bboxes[i];
			cv::Mat face_blob, results;
			
			cv::Mat clipped_face = frame(bbox);
			cv::resize(clipped_face, clipped_face, cv::Size(96,96));
			cv::dnn::blobFromImage(clipped_face, face_blob, 
				1.0/255.0, cv::Size(96,96), 
				cv::Scalar(0, 0, 0, 0), true, false);
			face_recog.setInput(face_blob);
			face_recog.forward(results);
			
			int num_features = results.rows;
			int feature_vec_size = results.cols;
			assert(results.type() == CV_32F);
			assert(num_features == 1 && feature_vec_size == CV_DNN_FACE_FEATURE_SIZE);
			
			memcpy(features[i].vec, results.data, sizeof(features[i].vec));
		}
		
		*p_features = features;
	}
	
	
	return num_detections;
}

struct cv_dnn_face * cv_dnn_face_init(struct cv_dnn_face *face, 
	const char *det_cfg, const char *det_model, 
	const char * recog_model,
	int enable_recog, int enable_landmark, void *user_data)
{
	if(NULL == face) face = (struct cv_dnn_face *)calloc(1, sizeof(*face));
	assert(face);
	
	face->user_data = user_data;
	face->detect = cv_dnn_face_detect;
	
	struct cv_dnn_face_private * priv = new cv_dnn_face_private(face, 
		det_cfg, det_model, recog_model, 
		enable_recog, enable_landmark);
	assert(priv);
	
	priv->landmark_enabled = enable_landmark;
	face->priv = priv;
	
	return face;
}
void cv_dnn_face_cleanup(struct cv_dnn_face * face)
{
	if(NULL == face) return;
	struct cv_dnn_face_private * priv = (struct cv_dnn_face_private *)face->priv;
	delete priv;
}

#if defined(TEST_CVFACE_WRAPPER_) && defined(_STAND_ALONE)
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <unistd.h>

int main(int argc, char **argv)
{
	cv::VideoCapture cap;
	
	
	cv::Mat _frame;
	
	struct cv_dnn_face face_ctx[1];
	memset(face_ctx, 0, sizeof(face_ctx));
	cv_dnn_face_init(face_ctx, NULL, NULL, NULL, 1, 1, NULL);
	
	
	bool ok = cap.open(0);
	assert(ok);
	
	const char * window_name = "face-demo";
	cv::namedWindow(window_name, cv::WINDOW_NORMAL | cv::WINDOW_KEEPRATIO);
	
	while(1) {
		if(!cap.isOpened()) break;
		
		cap >> _frame;
		if(_frame.rows < 1 || _frame.cols < 1) {
			sleep(1);
			continue;
		}
		
		cv::Mat frame = _frame;
		int width = frame.cols;
		int height = frame.rows;
		
		struct face_detection * dets = NULL;
		ssize_t num_faces = face_ctx->detect(face_ctx, (cvmat_t)&frame, 0.5, &dets, NULL, NULL);
		printf("num_faces: %ld, dets: %p\n", (long)num_faces, dets);
		
		for(int i = 0; i < num_faces; ++i)
		{
			int x = dets[i].x * width;
			int y = dets[i].y * height;
			int cx = dets[i].cx * width;
			int cy = dets[i].cy * height;
			
			cv::Point pt1(x, y);
			cv::Point pt2(x+cx, y+cy);
			cv::rectangle(frame, pt1, pt2, cv::Scalar(255, 255, 0), cv::LINE_8);
		}
		
		if(dets) free(dets);
		
		cv::imshow(window_name, frame);
		
		int key = cv::waitKey(1);
		if(key == 27 || key == 'q') break;
	}
	
	cv_dnn_face_cleanup(face_ctx);
	return 0;
}
#endif

