#include <stdio.h>

#include <ros/ros.h>

#include "actionProvider.h"

using namespace std;

int main(int argc, char ** argv)
{
    ros::init(argc, argv, "action_provider");

    actionProvider ap("action_provider");

    printf("\n");
    ROS_INFO("READY! Waiting for a service message..\n");

    ros::spin();
    return 0;
}
