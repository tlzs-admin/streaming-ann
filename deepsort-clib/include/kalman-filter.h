#ifndef DEEPSORT_KALMAN_FILTER_H_
#define DEEPSORT_KALMAN_FILTER_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @ingroup kalman_filter
  * @defgroup measurement Measurement (observation)
  * @{
  * deepsort_measurement: 
  *  Bounding box coordinates (x, y, a, h) with 
  *    center position (x, y),
  *    aspect ratio a, 
  *    and height h.
  * 
  * @}
 **/
typedef union deepsort_measurement
{
	float values[4];
	struct {
		float x, y; // center_pos
		float a;
		float h;
	};
}deepsort_measurement_t;

typedef struct deepsort_kalman_filter_state
{
	union {
		float mean[8];
		struct {
			deepsort_measurement_t measurement;
			struct { float vx, vy, va, vh; }; // respective velocities
		};
	};
	float covariance[8][8];
}kalman_filter_state_t;


typedef struct deepsort_kalman_filter_measurement_state
{
	union {
		float mean[4];
		struct {
			deepsort_measurement_t measurement;
		};
	};
	float covariance[4][8];
}measurement_state_t;


typedef struct deepsort_kalman_filter
{
	float motion_mat[8][8];
	float update_mat[4][8];
	
	/**
	 * https://github.com/nwojke/deep_sort/blob/master/deep_sort/          kalman_filter.py : line 49
	 * 
	 * Motion and observation uncertainty are chosen relative to the       current
	 * state estimate. These weights control the amount of uncertainty in
	 * the model. This is a bit hacky.
	**/
	struct {
		float position;
		float velocity;
	}std_weight;
}kalman_filter_t;








#ifdef __cplusplus
}
#endif
#endif
