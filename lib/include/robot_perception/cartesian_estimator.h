#ifndef __CARTESIAN_ESTIMATOR__
#define __CARTESIAN_ESTIMATOR__

#include <ros/ros.h>
#include <ros/console.h>

#include <opencv2/opencv.hpp>

#include <aruco/cameraparameters.h>
#include <aruco_ros/aruco_ros_utils.h>

#include "robot_utils/ros_thread_image.h"

class CartesianEstimator : public ROSThreadImage
{
private:
    image_transport::Publisher      img_pub;

    // Segmented object as a rotated rectangle
    cv::RotatedRect          obj_segm;

    // Camera parameters
    aruco::CameraParameters cam_param;

    std::vector<double> obj_size;

    // Rotation and translation matrices with respect to the camera
    cv::Mat Rvec, Tvec;

protected:
    /*
     * Function that will be spun out as a thread
     */
    void InternalThreadEntry();

    /**
     * Detects the object in the image
     */
    virtual void detectObject(const cv::Mat& _in, cv::Mat& _out) { return; };

    /**
     * Calculates the cartesian pose of the segmented object in the camera frame
     * given the rotated bounding box, the camera parameters and the real physical size of the object.
     *
     * @return true/false if success/failure
     */
    bool calcPoseCameraFrame();

    /**
     * Projects the cartesian pose of the segmented object from the camera frame to the root frame,
     * given the kinematics of the robot, and the pose in the camera frame
     *
     * @return true/false if success/failure
     */
    bool cameraToRootFramePose();

    /**
     * Draws a 3D axis in the object
     *
     * @param _in Image to draw the 3D axis into
     */
    void draw3dAxis(cv::Mat &_in);

public:
    CartesianEstimator(std::string name, std::vector<double> _obj_size);
    ~CartesianEstimator();

    /** GETTERS **/
    cv::RotatedRect getSegmentedObject() { return obj_segm; };

    /** SETTERS **/
    void setSegmentedObject(cv::RotatedRect _os) { obj_segm = _os; return; };
};

#endif
