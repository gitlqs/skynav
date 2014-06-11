#include <ros/ros.h>

#include <sensor_msgs/PointCloud.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/LaserScan.h>
#include <sensor_msgs/point_cloud_conversion.h>

#include <tf/transform_listener.h>
#include <laser_geometry/laser_geometry.h>

using namespace std;
using namespace sensor_msgs;

ros::Publisher pubCloud, pubCloud_old ;
ros::Subscriber laserReceiver;

tf::TransformListener* mTransformListener;
laser_geometry::LaserProjection mLaserProjector;


//receive a sensor_msgs::laserscan from the robot laser, and translate to the right frame and project to a sensor_msgs::PointCloud2.
void subLaserScanCallback(const LaserScan::ConstPtr& scan_in) 
{
	//transform to the right frame
	if(!mTransformListener->waitForTransform(scan_in->header.frame_id, "/map", scan_in->header.stamp + ros::Duration().fromSec(scan_in->ranges.size()*scan_in->time_increment), ros::Duration(1.0)))	
	{
		return;
	}	
	
	//project to a sensor_msgs::PointCloud2 message 
	//for usage of the pcl library and forward compatibility
	PointCloud2 pointCloud;
	try
	{
		mLaserProjector.transformLaserScanToPointCloud("/map", *scan_in, pointCloud, *mTransformListener);
	}
	catch (tf::TransformException& e)
	{
		ROS_ERROR("obstacle_detector - %s", e.what());
	}

	if (pointCloud.width * pointCloud.height >0) 
	{		
		pointCloud.header.stamp = ros::Time::now();
        pointCloud.header.frame_id = "/map";
        
        pubCloud.publish(pointCloud);	
    }	
}

//In process of beeing DEPRECATED!
//receive laserscan data and convert to sensor_msgs::pointcloud
void subLaserScanCallback2(const sensor_msgs::LaserScan::ConstPtr& scan_in)
{

	if(!mTransformListener->waitForTransform(scan_in->header.frame_id, "/map", scan_in->header.stamp + ros::Duration().fromSec(scan_in->ranges.size()*scan_in->time_increment), ros::Duration(1.0)))
	{
		return;
	}	

	PointCloud pointCloud;
	try
	{
		mLaserProjector.transformLaserScanToPointCloud("/map", *scan_in, pointCloud, *mTransformListener);
	}
	catch (tf::TransformException& e)
	{
		ROS_ERROR("obstacle_detector - %s", e.what());
	}

	if (pointCloud.points.size() > 0) 
	{
        pointCloud.header.stamp = ros::Time::now();
        pointCloud.header.frame_id = "/map";

        pubCloud_old.publish(pointCloud);        
    }
}



int main(int argc, char **argv) {

    ros::init(argc, argv, "laser_receiver");

    ros::NodeHandle n("/control");
    ros::NodeHandle n_robot("/x80sv");
    
    //pubs
	pubCloud = n.advertise<sensor_msgs::PointCloud2>("cloud", 1024);
    pubCloud_old = n.advertise<sensor_msgs::PointCloud>("sensor_data",1024);
	
    //subs
    ros::Subscriber subLaser = n.subscribe("laser_scan", 10, subLaserScanCallback);
    //ros::Subscriber subLaser2 = n.subscribe("laser_scan", 10, subLaserScanCallback2);

    
	mTransformListener = new tf::TransformListener();
	
	while(ros::ok())
	{
		ros::spin();
	}
    return 0;
}

