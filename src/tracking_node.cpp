
/******************************************************************************
*
* The MIT License (MIT)
*
* Copyright (c) 2018 Bluewhale Robot
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
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Author: Randoms
*******************************************************************************/
#include "tracking_node.h"

using namespace cv;
using namespace std;
using namespace XiaoqiangTrack;

sensor_msgs::Image last_frame;
XiaoqiangTrack::Tracker *tracker = NULL;
Rect2d body_rect;
ros::Publisher image_pub;
ros::Publisher target_pub;
std::mutex update_track_mutex;
bool track_ok_flag = false;


sensor_msgs::Image get_one_frame()
{
    return last_frame;
}

void update_frame(const sensor_msgs::ImageConstPtr &new_frame)
{
    last_frame = *new_frame;
    cv_bridge::CvImagePtr cv_ptr = cv_bridge::toCvCopy(new_frame, "bgr8");
    cv::Mat cv_image = cv_ptr->image;
    if (tracker == NULL)
        return;
    unique_lock<mutex> lock(update_track_mutex);
    track_ok_flag = tracker->updateFrame(cv_image, body_rect);
    cv::rectangle(cv_image, body_rect, cv::Scalar(0, 255, 0));
    image_pub.publish(cv_ptr->toImageMsg());
    xiaoqiang_track::TrackTarget target;
    target.x = body_rect.x + body_rect.width / 2;
    target.y = body_rect.y + body_rect.height / 2;
    if(!track_ok_flag){
        // send stop
        target.x = 0;
        target.y = 0;
    }
    target_pub.publish(target);
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "xiaoqiang_track_node");
    ros::AsyncSpinner spinner(4);
    spinner.start();
    ROS_INFO_STREAM("OK1");
    ros::NodeHandle private_nh("~");
    ros::Publisher talk_pub = private_nh.advertise<std_msgs::String>("text", 10);
    image_pub = private_nh.advertise<sensor_msgs::Image>("processed_image", 10);
    target_pub = private_nh.advertise<xiaoqiang_track::TrackTarget>("target", 10);
    ros::Subscriber image_sub = private_nh.subscribe("image", 10, update_frame);
    BaiduTrack client;
    std::string tracker_type;
    ros::param::param<std::string>("~tracker", tracker_type, "");
    tracker = new XiaoqiangTrack::Tracker(tracker_type);

    // 告诉用户站在前面
    std_msgs::String words;
    words.data = "请站在我前面";
    talk_pub.publish(words);
    // 提醒用户调整好距离
    sensor_msgs::Image frame = get_one_frame();
    body_rect.x = -1;
    body_rect.y = -1;
    ROS_INFO_STREAM("OK3");
    while (!ros::isShuttingDown())
    {
        if (frame.data.size() != 0)
        {
            ROS_INFO_STREAM("OK4");
            std::vector<int> rect = client.getBodyRect(frame);
            if (rect.size() == 0)
            {
                words.data = "我没有看到人,请站到我前面";
                talk_pub.publish(words);
                sleep(4);
            }
            else if (rect[0] + rect[2] / 2 > 440 || rect[0] - rect[2] / 2 < 200)
            {
                body_rect.x = rect[0];
                body_rect.y = rect[1];
                body_rect.width = rect[2];
                body_rect.height = rect[3];
                ROS_INFO_STREAM(body_rect);
                words.data = "请站到镜头中间来";
                talk_pub.publish(words);
                sleep(4);
            }
            else
            {
                body_rect.x = rect[0];
                body_rect.y = rect[1];
                body_rect.width = rect[2];
                body_rect.height = rect[3];
                words.data = "我看到人了,开始追踪";
                talk_pub.publish(words);
                ROS_INFO_STREAM(body_rect);
                sleep(4);
                break;
            }
        }
        sleep(4);
        frame = get_one_frame();
    }

    // 告诉用户可以开始走了
    sensor_msgs::Image tracking_frame = get_one_frame();
    cv_bridge::CvImagePtr cv_ptr = cv_bridge::toCvCopy(tracking_frame, "bgr8");
    cv::Mat cv_image = cv_ptr->image;
    tracker->initTracker(cv_image, body_rect);
    while (ros::ok())
    {
        sleep(1);
        if(body_rect.width < 300 && body_rect.height < 300 && track_ok_flag && body_rect.height > body_rect.width){
            continue;
        }
        tracking_frame = get_one_frame();
        ROS_INFO_STREAM("Body check frame");
        ROS_INFO_STREAM(tracking_frame.header.stamp);
        std::vector<int> rect = client.getBodyRect(tracking_frame);
        if (rect.size() == 0)
        {
            words.data = "我没有看到人,请站到我前面";
            talk_pub.publish(words);
            sleep(4);
        }
        else
        {
            {
                ROS_INFO_STREAM("rect: " << rect[0] << " " << rect[1] << " " << rect[2] << " " << rect[3]);
                unique_lock<mutex> lock(update_track_mutex);
                body_rect.x = rect[0];
                body_rect.y = rect[1];
                body_rect.width = rect[2];
                body_rect.height = rect[3];
                ROS_INFO_STREAM(body_rect);
                ROS_INFO_STREAM("track frame");
                ROS_INFO_STREAM(tracking_frame.header.stamp);
                cv_bridge::CvImagePtr cv_ptr = cv_bridge::toCvCopy(tracking_frame, "bgr8");
                cv::Mat cv_image = cv_ptr->image;
                tracker->reset(cv_image, body_rect);
            }
            words.data = "我看到人了,更新追踪位置";
            talk_pub.publish(words);
            ROS_INFO_STREAM(body_rect);
            sleep(4);
        }
    }
}