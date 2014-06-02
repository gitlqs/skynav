#include <ros/ros.h>

#include <skynav_msgs/waypoint_check.h>
#include <skynav_msgs/current_pose.h>
#include <skynav_msgs/Object.h>
#include <skynav_msgs/PointCloudVector.h>


#include <std_msgs/UInt8.h>

#include "local_planner_lib.h"

using namespace std;

ros::Publisher pubObjectOutlines, pubNavigationState;
ros::Subscriber subNavigationState, subAbsoluteTargetPose, subPointCloudSensorData, subPCVector;

ros::ServiceServer servServerWaypointCheck;
ros::ServiceClient servClientCurrentPose;

vector<PointCloud> mObstacles;					//vector of pointclouds that make up the different objects
vector<PointCloud> mObjectOutlines;			//vector of outlines for known objects, calculated by the convex / concave hull function

vector<pcl::PCLPointCloud2> mClusters;		//set of clusters(objects)

PoseStamped mCurrentAbsoluteTargetPose;			//the current next target pose, as published by motion_control
uint8_t mControl_NavigationState;				//the state which motion_control is in.

boost::mutex mMutex;


//request the current pose
Pose getCurrentPose() {
	try{
		Pose currentPose;

		skynav_msgs::current_pose poseService;

		if (servClientCurrentPose.call(poseService)) {

			currentPose = poseService.response.pose;

		} else {
			ROS_ERROR("Failed to call current_pose service from local_planner");
			ros::shutdown();
		}
		return currentPose;
	}catch(exception& e){
		ROS_ERROR("exception caught: %s",e.what());
		ros::shutdown();
	}
}


//receive a pcl::PCLPointCloud2 and perform some actions on it
void subPointCloudDataCallback(const pcl::PCLPointCloud2ConstPtr& msg)
{
	//TODO
}


void subPCVectorCallback(const skynav_msgs::PointCloudVector::ConstPtr& msg)
{
	boost::mutex::scoped_lock lock(mMutex);

	//TODO
	mClusters.clear();
	mObstacles.clear();
	
	for(vector<sensor_msgs::PointCloud2>::const_iterator it = msg->clouds.begin();it!=msg->clouds.end();++it){
		 sensor_msgs::PointCloud pc;
		 sensor_msgs::convertPointCloud2ToPointCloud((*it),pc);
		 
		 mObstacles.push_back(pc);
	}
	
	//ROS_INFO("received clusters %d", mObstacles.size());
}





//publish the outer hull pointclouds
void publishHull(){
	if(!mObjectOutlines.empty()){
		for(vector<PointCloud>::iterator outlineIt = mObjectOutlines.begin(); outlineIt!= mObjectOutlines.end(); ++outlineIt){
			pubObjectOutlines.publish((*outlineIt) );
		}	
	}
}

//function to call outline determination for each object
void hullFunction(){	
	mObjectOutlines.clear();	//clear the outlines and replace with new data
	
	for(vector<PointCloud>::iterator it = mObstacles.begin(); it!= mObstacles.end(); ++it){
		if((*it).points.size()>10){
			mObjectOutlines.push_back(concave_hull((*it)));
		}else if ((*it).points.size()>5){
			mObjectOutlines.push_back(convex_hull((*it)));
		} else if((*it).points.size()>=2){
			mObjectOutlines.push_back((*it));
		}
	}
	//publish the outer hulls of the known objects
	publishHull();
}

void subNavigationStateCallback(const std_msgs::UInt8& msg ){
	mControl_NavigationState = msg.data;
	//ROS_INFO("localnav NavigationState: %d", msg.data);	
}

void subAbsoluteTargetPoseCallback(const PoseStamped& msg){
	mCurrentAbsoluteTargetPose = msg;
	//ROS_INFO("Current Localnav target pose: (%f,%f)", msg.pose.position.x, msg.pose.position.y);
}

// receive the two coordinates that make up the current path and check for colission with known objects.
// objects consist of a set of coordinates that determine the CONVEX outline of the object.
// the line (edge) between two consecutive coordinates can be checked for colission with the path  
// if the path is free, set pathChanged on true and return, otherwise call the recursive bug algorithm 
// and return a new coordinate for the reroute and and return true 	// TODO margin, eg robot size
bool servServerWaypointCheckCallback(skynav_msgs::waypoint_check::Request &req, skynav_msgs::waypoint_check::Response &resp) {

	hullFunction();	//Only call when asking for colission.
	
	if(mObjectOutlines.empty()){
		resp.pathChanged = 0;
		return true;	//dont calculate anything and return
	}
	//if colissioncheck is true, new waypoint is returned, else there is no colission	
	optionPoint newPoint;
	if((newPoint = waypointCheck(req.currentPos, req.targetPos, mObjectOutlines, true))){		//call for colissioncheck with recursiveBug active
		resp.pathChanged = 1;
		resp.newPos = *newPoint;
		ROS_INFO("New waypoint at(%f,%f)",resp.newPos.x, resp.newPos.y);
		return true;
	}
	resp.pathChanged = 0;
	return true;
}

//send a new NAVIGATION_STATE as interrupt to motion_control
void publishInterruptNavigationState(const std_msgs::UInt8 pubmsg){
	mControl_NavigationState = pubmsg.data;
	pubNavigationState.publish(pubmsg);
	
	return;	
}

// call colissioncheck function. if colission occurs, publish interrupt navigationstate for motion_control 
void collisionCheck(){
	
	hullFunction();	//Only call when asking for colissioncheck. 
	
	if(mObjectOutlines.empty()){
		return;				// dont calculate anything and return
	}
	
	Pose currentPose = getCurrentPose();
	Pose targetPose = mCurrentAbsoluteTargetPose.pose;

	optionPoint collision;
	if((collision = waypointCheck(currentPose.position, targetPose.position, mObjectOutlines, false))){
		
		if(calcDistance(currentPose.position, *collision) <= (0.5*MAX_SENSORDIST) ){
			std_msgs::UInt8 msg;		
			msg.data = 6;		//TODO replace with NAVIGATION_STATE state;
			publishInterruptNavigationState(msg);
			//ROS_INFO("Collision at: %f,%f", (*collision).x, (*collision).y );
		}
	}	 
	return;
}
 
 
int main(int argc, char **argv) {
    ros::init(argc, argv, "local_planner");

    ros::NodeHandle n("/localnav");
    ros::NodeHandle n_control("/control");
    ros::NodeHandle n_slam("/slam");

    //pubs
    pubObjectOutlines = n.advertise<PointCloud>("objectOutlines", 10);
    pubNavigationState = n_control.advertise<std_msgs::UInt8>("navigation_state_interrupt", 0);    
    
    //subs
	subNavigationState= n_control.subscribe("navigation_state",0,subNavigationStateCallback);
	subAbsoluteTargetPose = n_control.subscribe("target_pose",0,subAbsoluteTargetPoseCallback);
	subPCVector = n.subscribe("pointcloudVector",1, subPCVectorCallback);
    
	//subPointCloudSensorData = n.subscribe("pointCloudData",1,subPointCloudDataCallback);

	//services
    servServerWaypointCheck = n.advertiseService("path_check", servServerWaypointCheckCallback);
    
    
	servClientCurrentPose = n_slam.serviceClient<skynav_msgs::current_pose>("current_pose");

	ros::Rate loop_rate(1);

	while(ros::ok){
		
		ros::spinOnce();

		if(mControl_NavigationState == 1){
			collisionCheck();
			loop_rate.sleep();
		}	
	}

    return 0;
}
