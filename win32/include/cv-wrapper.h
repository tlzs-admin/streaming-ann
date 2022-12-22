#ifndef CHLIB_CV_WRAPPER_H_
#define CHLIB_CV_WRAPPER_H_

#ifdef __cplusplus
#define CV_MAT_PTR(mat) (cv::Mat *)(mat)
#define CV_MAT(mat) (*CV_MAT_PTR(mat))
extern "C" {
#endif

typedef void * cvmat_t;

/************************************************************************
 * cvmat_t : (cv::Mat *)
************************************************************************/
cvmat_t cvmat_new(int width, int height, const unsigned char * bgra /* bgra or bgr */, int channels);
void cvmat_free(cvmat_t mat);
unsigned char * cvmat_get_data(cvmat_t mat);
int cvmat_get_size(cvmat_t _mat, int * width, int * height);
void cvmat_set(cvmat_t dst, cvmat_t src);
void cvmat_cvt_color(cvmat_t dst, cvmat_t src, int type);

cvmat_t cvmat_resize(cvmat_t dst, cvmat_t src, int dst_width, int dst_height, int interpolation /* default: INTER_LINEAR */);
cvmat_t cvmat_scale(cvmat_t dst, cvmat_t src, double factor_x, double factor_y, int interpolation /* default: INTER_LINEAR */);



/************************************************************************
 * cv_dnn_face :
************************************************************************/
struct face_detection
{
	union // base object
	{
		struct { float x, y, cx, cy; };
		struct { float x, y, cx, cy; } bbox;
	};
	float batch;
	float klass;
	float confidence;
};

#define CV_DNN_FACE_LANDMARK_NUM_POINTS (68)
struct face_landmark
{
	struct { 
		float x, y; 
	}points[CV_DNN_FACE_LANDMARK_NUM_POINTS];
};
int face_landmark_clear(struct face_landmark *mark);

#define CV_DNN_FACE_FEATURE_SIZE (128)
struct face_feature
{
	float vec[CV_DNN_FACE_FEATURE_SIZE];
};


struct cv_dnn_face
{
	void * priv; // struct cv_dnn_face_private;
	void * user_data;
	
	ssize_t (*detect)(struct cv_dnn_face *face, 
		cvmat_t frame, 
		double confidence_threshold, 
		struct face_detection ** p_detections,
		struct face_landmark ** p_landmarks,
		struct face_feature ** p_features
	);
};

struct cv_dnn_face * cv_dnn_face_init(struct cv_dnn_face *face,
	const char *det_cfg, const char *det_model, 
	const char *recog_model, 
	int enable_recog, int enable_landmark, 
	void *user_data);
void cv_dnn_face_cleanup(struct cv_dnn_face * face);


#ifdef __cplusplus
}
#endif
#endif
