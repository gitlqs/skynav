#include <ros/ros.h>
#include <skynav_msgs/current_pose.h>
#include <skynav_msgs/current_velocity.h>
#include <skynav_msgs/waypoint_check.h>
#include <string.h>
#include <list>
#include <boost/algorithm/string.hpp>
#include <std_msgs/UInt8.h>
#include <nav_msgs/Path.h>
#include <geometry_msgs/Quaternion.h>
#include <geometry_msgs/Pose.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/Twist.h>
#include <math.h>

// defines
#define MOTION_VELOCITY                	0.2                      // speed in m/s
#define MAX_VELOCITY					1.0						
#define TURN_VELOCITY					0.3						// turn speed in rad/s
#define ANGLE_ERROR_FIRST		  		M_PI / 180 * 20 		// rads // first stage angle check
#define ANGLE_ERROR_ALLOWED             M_PI / 180 * 1          // rads	// second stage angle check for precision // if too small, the robot may rotate infinitely
#define DISTANCE_ERROR_ALLOWED          0.1                    // in meters

#define ROBOT_WAYPOINT_ACCURACY         true                   // if true, robot will continue trying to reach target goal within error values until proceeding to next target
#define CONSECUTIVE_PATHS				true					//if true, each received path will be added to the current path. if false, current path is erased, and new path is the new way to go

enum NAVIGATION_STATE { 			//TODO move this into a shared container (skynav_msgs perhaps?)
    NAV_READY = 0,					//check for new path recieved
    NAV_MOVING = 1,					//calculate path to next waypoint, turn, accel, drive and decel.
    NAV_AVOIDING = 2,				//reroute past detected object
    NAV_POSE_REACHED = 3,			//(intermediate) waypoint has been reached, check
    NAV_ERROR = 4,					
    NAV_UNREACHABLE = 5,			
    NAV_OBSTACLE_DETECTED = 6,		//obstacle has been detected
    NAV_CHECKING_OBSTACLE = 7,		//Depricated?!
    NAV_STOP = 8					//stop everything before continue with whatever
};

enum MOVEMENT_STATE{
	MOV_READY = 0,					//calculate path to next waypoint
	MOV_TURN = 1,					//turn appropriate angle
	MOV_ACCEL = 2,					//accelerate to certain velocity
	MOV_STEADY = 3,					//drive steady at certain velocity
	MOV_DECEL = 4,					//decelerate until full stop
	MOV_REACHED = 5					//done with moving
};

using namespace geometry_msgs;
using namespace std;

// VARS
ros::NodeHandle* mNode;
ros::NodeHandle* mNodeSLAM;

ros::ServiceClient servClientCurrentPose, servClientWaypointCheck, servClientCurrentVelocity;
ros::Publisher pubCmdVel, pubNavigationState, pubTargetPoseStamped;

list<PoseStamped>* mCurrentPath = new list<PoseStamped>;
list<PoseStamped>* mOriginalPath = new list<PoseStamped>;
double mEndOrientation = 0;

bool path_init=false;	//boolean if there is a current path initiated

ros::Timer mCmdVelTimeout;
uint8_t mNavigationState = NAV_READY; 	// initial navigation_state
uint8_t mMovementState = MOV_READY;		// initial movement_state


//callback to change navigationstate on an interrupt from outside
void subNavigationStateInterruptCallback(const std_msgs::UInt8::ConstPtr& msg) 
{	
	mNavigationState = msg->data;
	ROS_WARN("nav_state interrupted, changed to (%u)", msg->data);
    pubNavigationState.publish(msg);
	return;
}

//function to change navigationstate from within this node, and publish current state to the outside
void pubNavigationStateHelper(NAVIGATION_STATE state) 
{
	// set the local state to ensure there is no delay
    mNavigationState = state;

    std_msgs::UInt8 pubmsg;
    pubmsg.data = state;
    pubNavigationState.publish(pubmsg);
}

//remove the current known path
void clearPath() 
{	
    mCurrentPath->clear(); 
    mOriginalPath->clear();
    path_init = false;
}

//update the current path.
//if CONSECUTIVE_WAYPOINTS is active, the newly received path will be added to the current path
void subCheckedWaypointsCallback(const nav_msgs::Path::ConstPtr& msg) 
{		
	ROS_INFO("path received by motion control");

	if(CONSECUTIVE_PATHS && path_init)
	{
		mCurrentPath->insert(mCurrentPath->end(), msg->poses.begin(), msg->poses.end());
		mOriginalPath->insert(mOriginalPath->end(), msg->poses.begin(), msg->poses.end());
		mEndOrientation = mOriginalPath->back().pose.orientation.z;
		return;
	} 
	else if(path_init) 
	{	
		pubNavigationStateHelper(NAV_STOP);	
	}
	
	mCurrentPath = new list<PoseStamped>(msg->poses.begin(), msg->poses.end());
	mOriginalPath = new list<PoseStamped>(msg->poses.begin(), msg->poses.end());
	mEndOrientation = mOriginalPath->back().pose.orientation.z;
	path_init = true;
}

//update the current movement_state
void setMovementState(MOVEMENT_STATE moveState) 
{	
    mMovementState = moveState;
}

//calculate distance between two point with use of pythagoras
double calcDistance(Point a, Point b)
{	
	return sqrt(pow((a.x - b.x),2) + pow((a.y - b.y),2));
}

void publishCmdVel(Twist twist)	
{		
	if(twist.linear.x > MOTION_VELOCITY){
		ROS_ERROR("Attempt to send cmdvel %f, which is higher than max intended velocity. sending max intended velocity of: %f",twist.linear.x,MOTION_VELOCITY);
		twist.linear.x = MOTION_VELOCITY;
	}	
	
	pubCmdVel.publish(twist);

	// this timer is to timeout the target location
	//mCmdVelTimeout = mNode->createTimer(ros::Duration(t), cmdVelTimeoutCallback); 
}

void cmdVelTimeoutCallback(const ros::TimerEvent&) 
{	
    mCmdVelTimeout.stop();
    pubNavigationStateHelper(NAV_READY); 
    
    Twist twist_stop;
    pubCmdVel.publish(twist_stop);
    
    ROS_INFO("stopped timer");
}

Pose getCurrentPose() 
{	
	try{
		Pose currentPose;

		skynav_msgs::current_pose poseService;

		if (servClientCurrentPose.call(poseService)) {

			currentPose = poseService.response.pose;

		} else {
			ROS_ERROR("Failed to call current_pose service from motion control");
			ros::shutdown();
		}
		return currentPose;
	}catch(exception& e){
		ROS_ERROR("exception caught: %s",e.what());
		ros::shutdown();
	}
}

Twist getCurrentVelocity() 
{	
	try{
		Twist currentVelocity;

		skynav_msgs::current_velocity velService;

		if (servClientCurrentVelocity.call(velService)) {

			currentVelocity = velService.response.velocity;
		} else {
			ROS_ERROR("Failed to call current_velocity service from motion control");
			ros::shutdown();
		}
		return currentVelocity;
	}catch(exception& e){
		ROS_ERROR("exception caught: %s",e.what());
		ros::shutdown();
	}
}

bool posesEqual(Pose currentPose, Pose targetPose) {
	
    if (calcDistance(targetPose.position,currentPose.position)< DISTANCE_ERROR_ALLOWED) {
        return true;
    }
    return false;
}

//semi depricated function as this is integrated into nav_moving state
void motionTurn(const double theta) {	
	
	ros::Rate minimumSleep(1000); //1ms
	Pose currentPose = getCurrentPose();
	
	Twist twist;
	
	if(fmod(currentPose.orientation.z - theta + (M_PI*2), M_PI*2) <= M_PI)	{
		twist.angular.z = -TURN_VELOCITY;
	} else {
		twist.angular.z = TURN_VELOCITY;
	}
	
	publishCmdVel(twist);

	
	while(currentPose.orientation.z > theta + ANGLE_ERROR_ALLOWED || currentPose.orientation.z < theta - ANGLE_ERROR_ALLOWED)	{
		currentPose = getCurrentPose();

		minimumSleep.sleep();
	}
	
	Twist twist_stop; // stop moving
	publishCmdVel(twist_stop);

}

void navigate() 
{
    switch (mNavigationState) {

        case NAV_READY: 	
        {						
            if (!mCurrentPath->size() == 0){ 
				ROS_WARN("NAV_READY");
				
				PoseStamped absoluteTargetPose = mCurrentPath->front(); // always use index 0, if target reached, delete 0 and use the new 0
				
				absoluteTargetPose.header.frame_id = "/map";
				absoluteTargetPose.header.stamp = ros::Time::now();

				pubTargetPoseStamped.publish(absoluteTargetPose);          
				ROS_INFO("target pose: (%f, %f)", absoluteTargetPose.pose.position.x, absoluteTargetPose.pose.position.y);
				setMovementState(MOV_READY);
				pubNavigationStateHelper(NAV_MOVING);
			}else{
				//IDLE
				 return; 
			}
        }
		break;
        case NAV_POSE_REACHED:
        {	
			Pose currentPose = getCurrentPose();
			PoseStamped targetPose = mCurrentPath->front();
			ROS_WARN("NAV_POSE_REACHED");
			
			if (ROBOT_WAYPOINT_ACCURACY) 
			{				
				if (posesEqual(currentPose, targetPose.pose)) 
				{ 
					ROS_INFO("Location: (%f,%f)", currentPose.position.x, currentPose.position.y);
					mCurrentPath->pop_front();
					
				}else{
					ROS_ERROR("target not quite reached; compensating");						
				}				
			}else{
				ROS_INFO("nav pose reached unchecked, continue"); 
				mCurrentPath->pop_front();					
			}
			
			if(mCurrentPath->empty()){
				ROS_INFO("Waypoint list empty, target reached");                
                //motionTurn(mEndOrientation); //function is blocking when mEndOrientation is not reached
                pubNavigationStateHelper(NAV_STOP);
                break;
			}
                       
			pubNavigationStateHelper(NAV_READY);
        }
        break;
		case NAV_MOVING:
        {			
			ROS_WARN("NAV_MOVING");
			ros::Rate minimumSleep(1000); 	//1ms
			double rateHz = 0.2;
			ros::Rate rate(1 / rateHz);
			
			Pose currentPose;			
			Pose originalPose;				
			PoseStamped absoluteTargetPose;
			Pose relativeTargetPose;
			Twist currentVelocity;
			
			double theta;								//relative angle to target
			float breakDist;							//distance for robot to decel 
			double steadyVelocity = MOTION_VELOCITY;
			
			Twist twist_stop;
			
			bool in_motion = false;						//"is currently busy"  boolean
			setMovementState(MOV_READY);
			
			while(mNavigationState == NAV_MOVING){ 		//while(!interrupted){
				currentPose = getCurrentPose();
				currentVelocity = getCurrentVelocity();
				
				switch(mMovementState){
					case MOV_READY:
					{
						//retrieve abs_target pose
						absoluteTargetPose = mCurrentPath->front(); // always use index 0, if target reached, delete 0 and use the new 0
						//retrieve current pose
						originalPose = currentPose;  
						
						if (posesEqual(currentPose, absoluteTargetPose.pose)) {
							ROS_INFO("already at target pose, skipping");
							pubNavigationStateHelper(NAV_POSE_REACHED);
							break;
						}

						theta =  atan2(absoluteTargetPose.pose.position.y - currentPose.position.y, absoluteTargetPose.pose.position.x - currentPose.position.x); // calc turn towards next point
			
						setMovementState(MOV_TURN);			
					}
					break;
					case MOV_TURN:
					{
						if(!in_motion){
							//ROS_INFO("mov_turn");
							Twist twist;
							if(fmod(currentPose.orientation.z - theta + (M_PI*2), M_PI*2) <= M_PI)	{
								twist.angular.z = -TURN_VELOCITY;
							} else {
								twist.angular.z = TURN_VELOCITY;
							}
							publishCmdVel(twist);
							in_motion = true;
						}						
						//method for turning more precise. first turn a rough angle, then turn the remaining, on a slower rate
						if(!(currentPose.orientation.z < theta + ANGLE_ERROR_FIRST && currentPose.orientation.z > theta - ANGLE_ERROR_FIRST))	{
							//do nothing, continue turning
						}else{	
							publishCmdVel(twist_stop); //stop movement
							rate.sleep();

							currentPose = getCurrentPose();
							Twist twist;
							
							//if the angle already falls within accepted error margin, continue
							if(currentPose.orientation.z > theta + ANGLE_ERROR_ALLOWED || currentPose.orientation.z < theta - ANGLE_ERROR_ALLOWED)	{
								
								if(fmod(currentPose.orientation.z - theta + (M_PI*2), M_PI*2) <= M_PI)	{
									twist.angular.z = -0.5*TURN_VELOCITY;
								} else {
									twist.angular.z =  0.5*TURN_VELOCITY;
								}
								publishCmdVel(twist);
								
								//TODO //WARNING: FUNCTION IS BLOCKING!
								while(currentPose.orientation.z > theta + ANGLE_ERROR_ALLOWED || currentPose.orientation.z < theta - ANGLE_ERROR_ALLOWED)	{
									//do nothing, continue turning
									currentPose = getCurrentPose();
									minimumSleep.sleep();
								}
							}
							publishCmdVel(twist_stop); //stop movement
							
							in_motion = false;
							
							rate.sleep();
							setMovementState(MOV_ACCEL);
						}
					}
					break;
					case MOV_ACCEL:
					{
						//ROS_INFO("mov_accel");

						//relative target pose is a point along the relative x of the robot.
						relativeTargetPose.position.x = calcDistance(absoluteTargetPose.pose.position, currentPose.position);
						relativeTargetPose.position.y = 0;				
						
						if (relativeTargetPose.position.x <= DISTANCE_ERROR_ALLOWED){
							ROS_WARN("target lies within treshold distance, skipping %fm",relativeTargetPose.position.x);
							setMovementState(MOV_REACHED);
							break;
						}
						in_motion = true;							

						Twist twist;
											
						for(double s = MOTION_VELOCITY / 16; s <= MOTION_VELOCITY; s *= 2)	{	
							
							twist.linear.x = s;
							publishCmdVel(twist);
							
							rate.sleep();
						}
						in_motion = false;
						setMovementState(MOV_STEADY);
					}
					break;
					case MOV_STEADY:
					{
						if(!in_motion){
							//ROS_INFO("mov_steady");
							in_motion = true;
						}
													
						//calculate breaking distance
						breakDist = 0;						
						for(int i=0; i<=4;++i){
								breakDist+= (abs(currentVelocity.linear.x / (pow(2,i)))*0.2);
						}
							
						double dist_traversed = relativeTargetPose.position.x - (calcDistance(currentPose.position,originalPose.position));
						if(dist_traversed > breakDist){
							//do nothing, continue moving
						}else{
							steadyVelocity = currentVelocity.linear.x;
							setMovementState(MOV_DECEL);
							in_motion = false;
						}	
					}
					break;
					case MOV_DECEL:
					{
						//ROS_INFO("mov_decel");
						Twist twist;
						for(double s = steadyVelocity; s >= steadyVelocity / 16; s /= 2)	{
			
							twist.linear.x = s;							
							publishCmdVel(twist);
							
							rate.sleep();
						}						
						publishCmdVel(twist_stop);
						in_motion = false;
						setMovementState(MOV_REACHED);	
					}
					break;
					case MOV_REACHED:
					{
						//ROS_INFO("mov_reached");
						pubNavigationStateHelper(NAV_POSE_REACHED); // turn was within error value, so was distance. so pose should be reached within error values
					}
					break;		
					default:
					{
						ROS_ERROR("unrecognized movement state found");
						
					break;
					}		
				}
				ros::spinOnce();	//check for callbacks
				minimumSleep.sleep();
			}		
		}
		break;
		case NAV_OBSTACLE_DETECTED:
		{	
			ROS_WARN("NAV_OBSTACLE_DETECTED");	
			Twist twist_stop;	
			float curVel = getCurrentVelocity().linear.x;
			if(abs(curVel) > 0.25*MOTION_VELOCITY){
				ROS_INFO("current velocity is: %f ,breaking", curVel);
				double rateHz = 0.2;
				ros::Rate rate(1 / rateHz);
				Twist twist;
				for(double s = curVel; s >= curVel / 16; s /= 2)	{
	
					twist.linear.x = s;							
					publishCmdVel(twist);
					
					rate.sleep();
				}
			}
			publishCmdVel(twist_stop);
			pubNavigationStateHelper(NAV_AVOIDING);
        }
		break;
        case NAV_AVOIDING:
		{	
			ROS_WARN("NAV_AVOIDING");
			ros::Duration(1).sleep();	//sleep 1 second for the sensors to calibrate		
			Pose currentPose = getCurrentPose();
			Pose absTarget = mCurrentPath->front().pose;

			skynav_msgs::waypoint_check srv;
			srv.request.currentPos = currentPose.position;
			srv.request.targetPos  = absTarget.position;
			
			//re-check obstruction of the path and calculate the detour. 
			//TODO create a check if new path is inefficient. in that case, dont push_front
			if(servClientWaypointCheck.call(srv)){
				if(srv.response.pathChanged){
					PoseStamped nwWaypoint;
					nwWaypoint.pose.position = srv.response.newPos;
					mCurrentPath->push_front(nwWaypoint);	// be careful here!	
					
					//todo check if path from nwWaypoint to next doesnt collide.. if true, calculate new nwWaypoint and push after current nwWaypoint

				}else{
					ROS_INFO("False alarm");
				}
			}else{
				ROS_ERROR("failed to call waypoint check service from motion_control NAV_AVOIDING state ");
			}
			pubNavigationStateHelper(NAV_READY);
        }   
		break;      
        case NAV_CHECKING_OBSTACLE: //DEPRECATED
        {	
			ROS_WARN("NAV_OBSTACLE_DETECTED");
		}
		break;
        case NAV_ERROR:
        {
			ROS_WARN("NAV_ERROR");
            clearPath();
            pubNavigationStateHelper(NAV_STOP);            
        }
        break;
        case NAV_UNREACHABLE:
        {
            ROS_WARN("NAV_UNREACHABLE");
			ROS_INFO("Could not reach target pose");
			clearPath();
			pubNavigationStateHelper(NAV_STOP);  
        }
        break;
        case NAV_STOP:
        {
			ROS_WARN("NAV_STOP");
			double currentVelocity = getCurrentVelocity().linear.x;
			Twist twist_stop;
			if(abs(currentVelocity) > 0.25*MOTION_VELOCITY){
				Twist twist;
				   ros::Rate rate(5); // each 200ms

				for(double s = currentVelocity; s >= currentVelocity / 16; s /= 2)	{

					twist.linear.x = s;							
					publishCmdVel(twist);
					
					rate.sleep();
				}
			}						
			publishCmdVel(twist_stop);			
			
			pubNavigationStateHelper(NAV_READY);            
        }
        break;
        default:
            ROS_ERROR("unrecognized navigation state found");
    }
}

void emergency_stop()
{
	ROS_ERROR("FULL EMERGENCY STOP!");
	Twist twist_stop;
	publishCmdVel(twist_stop);
	clearPath();
	pubNavigationStateHelper(NAV_READY); 
}

void subEmergencyStopCallback(const std_msgs::UInt8::ConstPtr& msg)
{
	if (msg)
	{
		emergency_stop();
	}else{
		ROS_WARN("unknown emergency stop signal received");
	}
}

int main(int argc, char **argv) 
{
    ros::init(argc, argv, "motion_control");
    ROS_INFO("starting motion_control");
    
    
    ros::NodeHandle n = ros::NodeHandle("");
    ros::NodeHandle n_localnav("/localnav");
    
    mNode = new ros::NodeHandle("/control");
    mNodeSLAM = new ros::NodeHandle("/slam");

    //pubs
    pubCmdVel = n.advertise<Twist>("/cmd_vel", 1);
    pubNavigationState = mNode->advertise<std_msgs::UInt8>("navigation_state", 0);
    pubTargetPoseStamped = mNode->advertise<geometry_msgs::PoseStamped>("target_pose", 1);

    //subs
    ros::Subscriber subCheckedWaypoints = mNode->subscribe("checked_waypoints", 1, subCheckedWaypointsCallback);
    ros::Subscriber subNavigationState = mNode->subscribe("navigation_state_interrupt", 0, subNavigationStateInterruptCallback);
	ros::Subscriber subEmergencyStop = mNode->subscribe("emergencystop_interrupt",0,subEmergencyStopCallback);
	
    //services
    servClientCurrentPose = mNodeSLAM->serviceClient<skynav_msgs::current_pose>("current_pose");
    servClientCurrentVelocity = mNodeSLAM->serviceClient<skynav_msgs::current_velocity>("current_velocity");
    
    servClientWaypointCheck = n_localnav.serviceClient<skynav_msgs::waypoint_check>("path_check");

    ros::Rate loop_rate(10); // loop at 10hz

    while (ros::ok()) {

        ros::spinOnce(); 

		navigate();

        loop_rate.sleep();
    }
	
	emergency_stop(); // does not do anything?. needs to be called before ros shutdown
	
    delete mNode;
    delete mNodeSLAM;

    return 0;
}

