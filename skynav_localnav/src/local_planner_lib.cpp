#include "local_planner_lib.h"

using namespace std;
using namespace geometry_msgs;
using namespace sensor_msgs;

//truncate values (in meters) to certain precision
float truncateValue(const float value)
{
	return floorf(value*1000)/1000; //mm
}


double pclCalcDistance(pcl::PointXYZ a, pcl::PointXYZ b)
{
	return sqrt(pow((a.x - b.x),2) + pow((a.y - b.y),2));
}


//compare both points to each other
bool pclCompare_Point(pcl::PointXYZ p, pcl::PointXYZ q)
{
	if((p.x == q.x) && (p.y = q.y))
	{
		return true;
	}
	return false;
}


pcl::PCLPointCloud2 pclConvex_hull(pcl::PCLPointCloud2& inputCloud)
{
	pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_in(new pcl::PointCloud<pcl::PointXYZ>);
	pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_out(new pcl::PointCloud<pcl::PointXYZ>);
	pcl::ConvexHull<pcl::PointXYZ> chull;
	pcl::PCLPointCloud2 outputCloud;	
	
	pcl::fromPCLPointCloud2(inputCloud, *cloud_in);
	
	chull.setInputCloud (cloud_in);
	chull.setDimension(2);
	//chull.setDimension(3);
	chull.reconstruct (*cloud_out);
	
	pcl::toPCLPointCloud2(*cloud_out, outputCloud);
		
	return outputCloud;
}


pcl::PCLPointCloud2 pclConcave_hull(pcl::PCLPointCloud2& inputCloud)
{
	pcl::ConcaveHull<pcl::PointXYZ> chull;
	pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_in(new pcl::PointCloud<pcl::PointXYZ>);
	pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_out(new pcl::PointCloud<pcl::PointXYZ>);
	pcl::PCLPointCloud2 outputCloud;	
	
	pcl::fromPCLPointCloud2(inputCloud, *cloud_in);
	
	chull.setInputCloud (cloud_in);
	chull.setAlpha (0.2);
	chull.setDimension(2);
	chull.reconstruct (*cloud_out);
	
	pcl::toPCLPointCloud2(*cloud_in, outputCloud);

	return outputCloud;
}


//recursive bug algorithm for object avoidance 
pclOptionPoint pclRecursiveBug(const pcl::PointXYZ currentPos, const pcl::PointXYZ targetPos, const pcl::PointXYZ collisionPoint, const pcl::PCLPointCloud2 objectPC_input)
{
	if(objectPC_input.width * objectPC_input.height == 0)
	{
		ROS_WARN("Object size is zero, stopping recursive bug");
		return pclOptionPoint();	//return false;
	}
	bool foundNew = false;
		
	pcl::PointXYZ nwTarget;
	pcl::PointXYZ obstacleExtremeLeft;
	pcl::PointXYZ obstacleExtremeRight;
	pcl::PointXYZ shortestExtremeToTarget;
	pcl::PointXYZ intersection = collisionPoint;
	pcl::PointXYZ laser_coord;
	pcl::PointCloud<pcl::PointXYZ> objectPC;
	pcl::fromPCLPointCloud2(objectPC_input, objectPC);

	
	//double laserDist = 4;
	double laserDist = pclCalcDistance(currentPos, targetPos);	//TODO hack for the first colission check when path is received by waypoint_filter.

	double angleTowardTarget = atan2((targetPos.y - currentPos.y),(targetPos.x - currentPos.x));

	double A1,B1,C1; 	
	double A2,B2,C2;
	double determant;
	bool intersectFound;

	//left side of scan radius
	for(int i = 0; i<=180; ++i)
	{
		float angle = (M_PI*i)/180;
		intersectFound = false;

		laser_coord.x = (currentPos.x + (cos(angleTowardTarget + angle) * laserDist));
		laser_coord.y = (currentPos.y + (sin(angleTowardTarget + angle) * laserDist));
		
		//calculate intersection point					
		//line function for path Ax+By=C
		A1 = laser_coord.y-currentPos.y;
		B1 = currentPos.x - laser_coord.x;
		C1 = A1*currentPos.x+B1*currentPos.y;
		
		for(int i = 0, j = 1; j<objectPC.points.size(); ++i,++j)
		{
			pcl::PointXYZ pObstacle1 = objectPC.points.at(i);
			pcl::PointXYZ pObstacle2 = objectPC.points.at(j);

			//line function for object edge Ax+By=C
			A2 = pObstacle2.y - pObstacle1.y;
			B2 = pObstacle1.x-pObstacle2.x;
			C2 = A2*pObstacle1.x+B2*pObstacle1.y;				
						
			determant = A1*B2 - A2*B1;
			if(determant == 0)
			{
				continue;						//point has been processed. jump to next edge of the object
			}
			intersection.x = (B2*C1 - B1*C2)/determant;
			intersection.y = (A1*C2 - A2*C1)/determant;
			
			//check if intersection of lines occur within obstacle boundaries;
			if(	min(pObstacle1.x,pObstacle2.x) <= intersection.x && intersection.x <= max(pObstacle1.x, pObstacle2.x) 
			 && min(pObstacle1.y,pObstacle2.y) <= intersection.y && intersection.y <= max(pObstacle1.y, pObstacle2.y))
			 {	
				//check if intersection of lines occur within path boundaries;
				if(	min(currentPos.x,laser_coord.x) <= intersection.x && intersection.x <= max(currentPos.x, laser_coord.x) 
				 && min(currentPos.y,laser_coord.y) <= intersection.y && intersection.y <= max(currentPos.y, laser_coord.y))
				 {
					//path collides with object						
					intersectFound = true;				
					break;	
				}						
			}	
		}
		
		if (!intersectFound)
		{
			ROS_INFO("left  extreme at (%f, %f)", obstacleExtremeLeft.x, obstacleExtremeLeft.y);
			break; // stop at first empty, previous hit was the extreme
		}	
		obstacleExtremeLeft = intersection;
		
	}
	
	//right side of scan radius
	for(int i = 0; i<=180; ++i)
	{
		float angle = (M_PI*i)/180;
		intersectFound = false;

		laser_coord.x = (currentPos.x + (cos(angleTowardTarget - angle) * laserDist));
		laser_coord.y = (currentPos.y + (sin(angleTowardTarget - angle) * laserDist));

		//calculate intersection point					
		//line function for path Ax+By=C
		A1 = laser_coord.y-currentPos.y;
		B1 = currentPos.x - laser_coord.x;
		C1 = A1*currentPos.x+B1*currentPos.y;
		
		for(int i = 0, j = 1; j<objectPC.points.size(); ++i,++j)
		{
			pcl::PointXYZ pObstacle1 = objectPC.points.at(i);
			pcl::PointXYZ pObstacle2 = objectPC.points.at(j);

			//line function for object edge Ax+By=C
			A2 = pObstacle2.y - pObstacle1.y;
			B2 = pObstacle1.x-pObstacle2.x;
			C2 = A2*pObstacle1.x+B2*pObstacle1.y;				
						
			determant = A1*B2 - A2*B1;
			if(determant == 0)
			{
				continue;						//point has been processed. jump to next edge of the object
			}
			intersection.x = (B2*C1 - B1*C2)/determant;
			intersection.y = (A1*C2 - A2*C1)/determant;	

			//check if intersection of lines occur within obstacle boundaries;
			if(	min(pObstacle1.x,pObstacle2.x) <= intersection.x && intersection.x <= max(pObstacle1.x, pObstacle2.x) 
			 && min(pObstacle1.y,pObstacle2.y) <= intersection.y && intersection.y <= max(pObstacle1.y, pObstacle2.y))
			 {	
				//check if intersection of lines occur within path boundaries;
				if(	min(currentPos.x,laser_coord.x) <= intersection.x && intersection.x <= max(currentPos.x, laser_coord.x) 
				 && min(currentPos.y,laser_coord.y) <= intersection.y && intersection.y <= max(currentPos.y, laser_coord.y))
				 {
					//path collides with object						
					intersectFound = true;
					break;
				}						
			}	
		}
		
		if (!intersectFound)
		{
			ROS_INFO("right  extreme at (%f, %f)", obstacleExtremeRight.x, obstacleExtremeRight.y);
			break; // stop at first empty, previous hit was the extreme
		}	
		obstacleExtremeRight = intersection;		
	}
	
	//check for errors with calculated extremes //TODO more checks
	if((pclCompare_Point(obstacleExtremeLeft,collisionPoint)) && (pclCompare_Point(obstacleExtremeRight,collisionPoint)))
	{
		ROS_ERROR("no extremes found besides collisionpoint itself");
		return pclOptionPoint();	//return false;
	}
	
	//calc path via left extreme   
    double extremeLeftPathLength  = pclCalcDistance(targetPos, obstacleExtremeLeft) + pclCalcDistance(currentPos, obstacleExtremeLeft);
	//calc path via right extreme    
	double extremeRightPathLength = pclCalcDistance(targetPos, obstacleExtremeRight) + pclCalcDistance(currentPos, obstacleExtremeRight);
	
	int angleSign;
	//determine extreme closest to target, 
	if (extremeLeftPathLength < extremeRightPathLength)
	{
		shortestExtremeToTarget=obstacleExtremeLeft;
		angleSign = 1;
	}else
	{
		shortestExtremeToTarget = obstacleExtremeRight;
		//apply offset sign
		angleSign = -1;
	}
	
	//calculate new waypoint based on robot size and trigenomitry functions in 7 steps
	double offsetRadius = 0.5 * ROBOTRADIUS; //roughly the distance the robot-center needs to keep from objects to avoid colission
	double distNewP;
	double distNewP2;
	double angleCollision;
	double angleNewP;
	double angleEx;
	double angleExNewP;
	
	//I determine distances between known and detected points
	double distCollision = pclCalcDistance(currentPos,collisionPoint);
	double distExtreme = pclCalcDistance(currentPos, shortestExtremeToTarget);
	double distExtrCollis = pclCalcDistance(collisionPoint, shortestExtremeToTarget);
	double distNwPCollis = (distExtrCollis + offsetRadius);
	
	double distCollision2 = pow(distCollision,2);
	double distExtreme2 = pow(distExtreme,2);
	double distExtrCollis2 = pow(distExtrCollis,2);
	double distNwPCollis2 = pow(distNwPCollis,2); 

	//II determine the angle between the edges at the colission
	if(distExtrCollis == 0){ //prevent nan values
		angleCollision = 1;
	}else{
		angleCollision = acos( (distExtrCollis2 + distCollision2 - distExtreme2 ) / (2 * (distExtrCollis * distExtreme) ) );
	}
	
	//III determine the distance to the new waypoint
	distNewP = sqrt(distNwPCollis2 + distCollision2 - (2 * distNwPCollis * distCollision * cos(angleCollision)) );
	distNewP2 = pow(distNewP,2);
	
	//IV determine the angle between the current position and the extreme of the object
	angleEx = atan2( (shortestExtremeToTarget.y - currentPos.y), (shortestExtremeToTarget.x - currentPos.x) );
	
	//V determine the angle between the edges to extreme and the new waypoint
	angleExNewP = acos( (distExtreme2 + distNewP2 - pow(offsetRadius,2))/(2 * (distExtreme * distNewP) ) ) * angleSign;
	
	//VI calculate the angle to the new waypoint
	if(distExtrCollis == 0){ //prevent nan values
		angleNewP = angleExNewP;
	}else{
		angleNewP = angleEx + angleExNewP;
	}
	
	//Hack calculate the point further along the same angle, for colission detection when the object is wider than known at the moment
	distNewP = distNewP;
	
	//VII calculate x and y coordinates of the new waypoint, based on distance and angle from current pos
	nwTarget.x = truncateValue(	currentPos.x + cos(angleNewP) * distNewP	);
	nwTarget.y = truncateValue(	currentPos.y + sin(angleNewP) * distNewP	);

	if(isnan(nwTarget.x) || isnan(nwTarget.y)){
		return pclOptionPoint();		
		ROS_ERROR("Targetpose is not valid, skipping targetPose!"); 	
	}
	return pclOptionPoint(nwTarget);	//return true with new target	
}


//function to determine if there is a colission, where, and (if needed) call for a new waypoint calculation.
//if recursiveBugNeeded is true, the return value is the new waypoint calculated with the recursivebug algorithm, if false: the return is the collisionpoint itself
pclOptionPoint pclWaypointCheck(const pcl::PointXYZ pPath1, const pcl::PointXYZ pPath2, Pcl2Vector outlinesInput, bool recursiveBugNeeded)
{	
	//line function for path Ax+By=C
	double A1 = pPath2.y-pPath1.y;
	double B1 = pPath1.x - pPath2.x;
	double C1 = A1*pPath1.x+B1*pPath1.y;
	
	double A2;
	double B2;
	double C2;
	double determant;
	
	pcl::PointXYZ intersection;
	pcl::PCLPointCloud2 relObjectConvert;
	vector<pcl::PointXYZ> colissions;
	pcl::PointCloud<pcl::PointXYZ> cloud;
	PclXYZVector outlines;
	PclXYZVector intersect_obstacles;

	bool collisionsFound=false;
	
	//convert pclpointcloud2 to pointcloud<pointxyz> for conveniance in the algorithm
	for(Pcl2Vector::iterator outlineIt = outlinesInput.begin(); outlineIt != outlinesInput.end(); ++outlineIt)
	{
		pcl::fromPCLPointCloud2((*outlineIt), cloud);
		outlines.push_back( cloud );		
	}	
	
	for (PclXYZVector::iterator outlineIt = outlines.begin(); outlineIt != outlines.end(); ++outlineIt) 
	{
		if((*outlineIt).points.size() >= 2)
		{
			for(int i = 0, j = 1; j<(*outlineIt).points.size(); ++i,++j)
			{
				pcl::PointXYZ pObstacle1 = (*outlineIt).points.at(i);
				pcl::PointXYZ pObstacle2 = (*outlineIt).points.at(j);
		
				////line function for object edge Ax+By=C
				A2 = pObstacle2.y - pObstacle1.y;
				B2 = pObstacle1.x-pObstacle2.x;
				C2 = A2*pObstacle1.x+B2*pObstacle1.y;				
							
				determant = A1*B2 - A2*B1;
				if(determant == 0)
				{
					//ROS_INFO("no intersections");	//Lines are parallel
					continue;						//point has been processed. jump to next edge of the object
				}
				intersection.x = (B2*C1 - B1*C2)/determant;
				intersection.y = (A1*C2 - A2*C1)/determant;					
				
				////check if intersection of lines occur within obstacle boundaries;
				if(	min(pObstacle1.x,pObstacle2.x) <= intersection.x && intersection.x <= max(pObstacle1.x, pObstacle2.x) 
					&& min(pObstacle1.y,pObstacle2.y) <= intersection.y && intersection.y <= max(pObstacle1.y, pObstacle2.y))
				{	
					//check if intersection of lines occur within path boundaries;
					if(	min(pPath1.x,pPath2.x) <= intersection.x && intersection.x <= max(pPath1.x, pPath2.x) 
					 && min(pPath1.y,pPath2.y) <= intersection.y && intersection.y <= max(pPath1.y, pPath2.y))
					 {
						//path collides with object						
						collisionsFound = true;
						colissions.push_back(intersection);
						intersect_obstacles.push_back((*outlineIt));
						continue;	//check for other colission points
					}						
				}							
			}
		}
	}
	
	if(collisionsFound)
	{
		////find relevant colission from set of colissions
		pcl::PointXYZ relColission;
		pcl::PointCloud<pcl::PointXYZ> relObject;
		double colDistance = 9999; // initiate distance at "far from robot" TODO set certain distance
		double newDist;

		for(int i =0; i<colissions.size(); ++i)
		{
			newDist = pclCalcDistance(pPath1,colissions.at(i));
			if( newDist < colDistance )
			{		
				colDistance = newDist;		
				relColission = colissions.at(i);
				relObject = intersect_obstacles.at(i);
				pcl::toPCLPointCloud2(relObject, relObjectConvert );
			}
		}		
		
		if(recursiveBugNeeded)
		{
			//calculate new Point newPoint with recursive bug algorithm
			pclOptionPoint newPoint;
			if((newPoint = pclRecursiveBug(pPath1, pPath2, relColission, relObjectConvert)))
			{
				//ROS_INFO("newpoint at (%f, %f)", (*newPoint).x, (*newPoint).y);
				return pclOptionPoint(newPoint);  //return true, with new waypoint 		
			}			
			ROS_ERROR("Collision detected, but no new waypoint could be calculated");
			return pclOptionPoint();  //return false 
		}
		//recursive bug is not neccesary, only collisionpoint is asked
		return pclOptionPoint(relColission); //return true with colissionpoint		
	}

	//no colissions found
	return pclOptionPoint();  //return false 	
}
