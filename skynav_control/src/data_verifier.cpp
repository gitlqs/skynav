#include <ros/ros.h>
#include <geometry_msgs/Pose.h>
#include <sensor_msgs/Range.h>
#include <sensor_msgs/LaserScan.h>
#include <skynav_msgs/current_pose.h>
#include <std_msgs/UInt8.h>

using namespace sensor_msgs;
using namespace geometry_msgs;
using namespace std;

ros::Publisher pubSensors;
ros::Publisher pubLaser;

ros::ServiceClient servClientCurrentPose;

uint8_t mControl_NavigationState = 0;

Pose getCurrentPose() 
{

    Pose currentPose;

    skynav_msgs::current_pose poseService;

    if (servClientCurrentPose.call(poseService)) {
        currentPose = poseService.response.pose;
    } else {
        ROS_ERROR("Failed to call current_pose service from data varifier");
    }
    return currentPose;
}

void subNavigationStateCallback(const std_msgs::UInt8& msg ){
	mControl_NavigationState = msg.data;
}

/*
void subSensorsCallback(const skynav_msgs::RangeDefinedArray::ConstPtr& msg) 
{
    
    // check limits here
    
    pubSensors.publish(msg);
}
*/

//filter the (raw)LaserScan data
void subFilterLaserCallback(const LaserScan::ConstPtr& scan) 
{
	LaserScan scan_filtered;
	scan_filtered.header.frame_id = scan->header.frame_id;
	scan_filtered.header.stamp = scan->header.stamp;

	scan_filtered.angle_min = scan->angle_min;
	scan_filtered.angle_max = scan->angle_max;
	scan_filtered.angle_increment = scan->angle_increment;
	scan_filtered.scan_time = scan->scan_time;
	scan_filtered.range_min = scan->range_min;
	scan_filtered.range_max = scan->range_max;	
	
	for(uint i = 0; i < scan->ranges.size(); ++i)	{
		
		if(i>30 && i<150){	//cap the laser so the robot wont see itself
			scan_filtered.ranges.push_back( 0 );
			scan_filtered.intensities.push_back( 0 );
			continue;
		}
		
		//filter the outer edge of the laser range out of the results because of outer edge ghost data
		if( scan->ranges.at(i) > (scan->range_max - 0.1) || scan->ranges.at(i) < scan->range_min)	{	
			scan_filtered.ranges.push_back( 0 );
			scan_filtered.intensities.push_back( 0 );
			continue;
		}
		
		scan_filtered.ranges.push_back( scan->ranges.at(i) );
		scan_filtered.intensities.push_back( scan->intensities.at(i) );
	}
    
    pubLaser.publish(scan_filtered);

}

int main(int argc, char **argv) 
{

    ros::init(argc, argv, "data_verifier");

    ros::NodeHandle n("/control");
    ros::NodeHandle n_robot;
    ros::NodeHandle n_SLAM("/slam");
    
    //pubs
    // pubSensors = n.advertise<skynav_msgs::RangeDefinedArray>("sensors", 1024);
    pubLaser = n.advertise<sensor_msgs::LaserScan>("laser_scan", 1024);    
	
    //subs
    // ros::Subscriber subSensors = n_robot.subscribe("sensors", 1024, subSensorsCallback);    
	ros::Subscriber subLaser = n_robot.subscribe("/laser/scan", 1024, subFilterLaserCallback);	
	//ros::Subscriber	subNavigationState= n.subscribe("navigation_state",0,subNavigationStateCallback);

	//services
    servClientCurrentPose = n_SLAM.serviceClient<skynav_msgs::current_pose>("current_pose");
    
	while(ros::ok())
	{
		ros::spin();
	}

    return 0;
}

