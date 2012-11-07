/******************************************************************************
 * Copyright (c) 2012 Sergey Alexandrov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/

#include <string>

#include <ros/ros.h>
#include <ros/console.h>
#include <nodelet/nodelet.h>
#include <image_transport/image_transport.h>

#include <boost/thread.hpp>

#include "pmd_camboard_nano.h"
#include "pmd_exceptions.h"

class DriverNodelet : public nodelet::Nodelet
{

public:

  virtual ~DriverNodelet()
  {
    // Make sure we interrupt initialization (if it happened to still execute).
    init_thread_.interrupt();
    init_thread_.join();
  }

private:

  virtual void onInit()
  {
    // We will be retrying to open camera until it is open, which may block the
    // thread. Nodelet::onInit() should not block, hence spawning a new thread
    // to do initialization.
    init_thread_ = boost::thread(boost::bind(&DriverNodelet::onInitImpl, this));
  }

  void onInitImpl()
  {
    ros::NodeHandle& nh = getNodeHandle();
    ros::NodeHandle& pn = getPrivateNodeHandle();

    // Retrieve parameters from server
    std::string device_serial;
    double open_camera_retry_period;
    double update_rate;
    pn.param<std::string>("depth_frame_id", depth_frame_id_, "/pmd_depth_optical_frame");
    pn.param<std::string>("device_serial", device_serial, "");
    pn.param<double>("open_camera_retry_period", open_camera_retry_period, 3);
    pn.param<double>("update_rate", update_rate, 30);

    // Open camera
    while (!camera_)
    {
      try
      {
        camera_ = boost::make_shared<PMDCamboardNano>(device_serial);
        NODELET_INFO("Opened PMD camera with serial number \"%s\"", camera_->getSerialNumber().c_str());
      }
      catch (PMDCameraNotOpenedException& e)
      {
        if (device_serial != "")
          NODELET_INFO("Unable to open PMD camera with serial number %s...", device_serial.c_str());
        else
          NODELET_INFO("Unable to open PMD camera...");
      }
      boost::this_thread::sleep(boost::posix_time::seconds(open_camera_retry_period));
    }

    // Advertise topics
    ros::NodeHandle depth_nh(nh, "depth");
    image_transport::ImageTransport depth_it(depth_nh);
    depth_publisher_ = depth_it.advertiseCamera("image", 1);

    // Setup periodic callback to get new data from the camera
    update_timer_ = nh.createTimer(ros::Rate(30).expectedCycleTime(), &DriverNodelet::updateCallback, this);

    // TODO: setup dynamic reconfigure server
  }

  void updateCallback(const ros::TimerEvent& event)
  {
    // Get new depth data and camera info
    ros::Time ts = ros::Time::now();
    sensor_msgs::CameraInfoPtr info = camera_->getCameraInfo();
    sensor_msgs::ImagePtr depth = camera_->getDepthImage();
    info->header.stamp = ts;
    depth->header.stamp = ts;
    info->header.frame_id = depth_frame_id_;
    depth->header.frame_id = depth_frame_id_;
    // Publish both
    depth_publisher_.publish(depth, info);
  }

private:

  PMDCamboardNano::Ptr camera_;
  boost::thread init_thread_;
  ros::Timer update_timer_;
  image_transport::CameraPublisher depth_publisher_;
  std::string depth_frame_id_;

};

// Register as a nodelet
#include <pluginlib/class_list_macros.h>
PLUGINLIB_DECLARE_CLASS (pmd_camboard_nano, driver, DriverNodelet, nodelet::Nodelet);

