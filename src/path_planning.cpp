 /*
 * Copyright 2017 Ayush Gaud
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ros/ros.h"
#include <octomap_msgs/Octomap.h>
#include <octomap_msgs/conversions.h>
#include <octomap_ros/conversions.h>
#include <octomap/octomap.h>
#include <message_filters/subscriber.h>
#include "visualization_msgs/Marker.h"
#include <trajectory_msgs/MultiDOFJointTrajectory.h>
#include <nav_msgs/Odometry.h>
#include <nav_msgs/Path.h>
#include <geometry_msgs/Pose.h>

#include <ompl/base/spaces/SE3StateSpace.h>
#include <ompl/base/spaces/SE3StateSpace.h>
#include <ompl/base/OptimizationObjective.h>
#include <ompl/base/objectives/PathLengthOptimizationObjective.h>
#include <ompl/geometric/planners/rrt/RRTstar.h>
#include <ompl/geometric/planners/rrt/InformedRRTstar.h>
#include <ompl/geometric/SimpleSetup.h>
//eric_wang:Constrained State Space
//#include <ompl/base/spaces/constraint/ConstrainedStateSpace.h>

#include <ompl/config.h>
#include <iostream>

#include "fcl/config.h"
#include "fcl/octree.h"
#include "fcl/traversal/traversal_node_octree.h"
#include "fcl/collision.h"
#include "fcl/broadphase/broadphase.h"
#include "fcl/math/transform.h"

namespace ob = ompl::base;
namespace og = ompl::geometric;


// Declear some global variables

//ROS publishers
//ros::Publisher vis_pub;
//ros::Publisher traj_pub;

class planner {
public:
	void init_start(void)
	{
		if(!set_start)
			std::cout << "Initialized" << std::endl;
		set_start = true;
	}
	void setStart(double x, double y, double z)
	{
		ob::ScopedState<ob::SE3StateSpace> start(space);
		start->setXYZ(x,y,z);
		start->as<ob::SO3StateSpace::StateType>(1)->setIdentity();
		pdef->clearStartStates();
		pdef->addStartState(start);

        //eric_wang:
        std::cout<<"Satrt point set to: "<< x << " " << y << " " << z << std::endl;
        }
	void setGoal(double x, double y, double z)
	{
		if(prev_goal[0] != x || prev_goal[1] != y || prev_goal[2] != z)
		{
			ob::ScopedState<ob::SE3StateSpace> goal(space);
			goal->setXYZ(x,y,z);
			prev_goal[0] = x;
			prev_goal[1] = y;
			prev_goal[2] = z;
			goal->as<ob::SO3StateSpace::StateType>(1)->setIdentity();
			pdef->clearGoal();
			pdef->setGoalState(goal);
			std::cout << "Goal point set to: " << x << " " << y << " " << z << std::endl;
			if(set_start)
				plan();

		}
	}
	void updateMap(std::shared_ptr<fcl::CollisionGeometry> map)
	{
		tree_obj = map;
	}
	// Constructor
	planner(void)
	{
        //eric_wang:
        vis_pub = nh.advertise<visualization_msgs::Marker>( "visualization_marker", 1);
        traj_pub = nh.advertise<nav_msgs::Path>("waypoints",1);
        //ros::Subscriber octree_sub = nh.subscribe<octomap_msgs::Octomap>("/octomap_binary", 1, boost::bind(&octomapCallback, _1, &planner_object));

        //eric_wang:set the size of the Quadcopter
		Quadcopter = std::shared_ptr<fcl::CollisionGeometry>(new fcl::Box(0.3, 0.3, 0.1));
		fcl::OcTree* tree = new fcl::OcTree(std::shared_ptr<const octomap::OcTree>(new octomap::OcTree(0.1)));
		tree_obj = std::shared_ptr<fcl::CollisionGeometry>(tree);

        //eric_wang:the state space of the solution
		space = ob::StateSpacePtr(new ob::SE3StateSpace());

		// create a start state
		ob::ScopedState<ob::SE3StateSpace> start(space);

		// create a goal state
		ob::ScopedState<ob::SE3StateSpace> goal(space);

		// set the bounds for the R^3 part of SE(3)
		ob::RealVectorBounds bounds(3);

        bounds.setLow(0,-100);
        bounds.setHigh(0,100);
        bounds.setLow(1,-100);
        bounds.setHigh(1,100);
        bounds.setLow(2,0);
        bounds.setHigh(2,100);

        //eric_wang:add the bounds to the state space
		space->as<ob::SE3StateSpace>()->setBounds(bounds);

		// construct an instance of  space information from this state space
		si = ob::SpaceInformationPtr(new ob::SpaceInformation(space));

		start->setXYZ(0,0,0);
		start->as<ob::SO3StateSpace::StateType>(1)->setIdentity();
		// start.random();

		goal->setXYZ(0,0,0);
		prev_goal[0] = 0;
		prev_goal[1] = 0;
		prev_goal[2] = 0;
		goal->as<ob::SO3StateSpace::StateType>(1)->setIdentity();
		// goal.random();

	    // set state validity checking for this space
		si->setStateValidityChecker(std::bind(&planner::isStateValid, this, std::placeholders::_1 ));

		// create a problem instance
		pdef = ob::ProblemDefinitionPtr(new ob::ProblemDefinition(si));

		// set the start and goal states
		pdef->setStartAndGoalStates(start, goal);

	    // set Optimizattion objective
		pdef->setOptimizationObjective(planner::getPathLengthObjWithCostToGo(si));

		std::cout << "Initialized: " << std::endl;
	}
	// Destructor
	~planner()
	{
	}
	void replan(void)
	{
		if(path_smooth != NULL && set_start)
		{
			std::cout << "Total Points:" << path_smooth->getStateCount () << std::endl;
			if(path_smooth->getStateCount () <= 2)
				plan();
			else
			{
				for (std::size_t idx = 0; idx < path_smooth->getStateCount (); idx++)
				{
					if(!replan_flag)
						replan_flag = !isStateValid(path_smooth->getState(idx));
					else
						break;

				}
				if(replan_flag)
					plan();
				else
					std::cout << "Replanning not required" << std::endl;
			}
		}
	}

	void plan(void)
	{

	    // create a planner for the defined space
        //ob::PlannerPtr plan(new og::InformedRRTstar(si));
        og::RRTstar* rrt = new og::RRTstar(si);

        //eric_wang: Set the range the planner is supposd to use
        rrt->setRange(0.2);

        ob::PlannerPtr plan(rrt);


	    // set the problem we are trying to solve for the planner
		plan->setProblemDefinition(pdef);

	    // perform setup steps for the planner
		plan->setup();

	    // print the settings for this space
        //eric_wang:
        std::cout<<"******print the setting for this space:******"<<std::endl;
		si->printSettings(std::cout);

	    // print the problem settings
        //eric_wang:
        std::cout<<"******print the problem settings:******"<<std::endl;
		pdef->print(std::cout);

	    // attempt to solve the problem within one second of planning time
        ob::PlannerStatus solved = plan->solve(10);


		if (solved)
		{
	        // get the goal representation from the problem definition (not the same as the goal state)
	        // and inquire about the found path
			std::cout << "Found solution:" << std::endl;
			ob::PathPtr path = pdef->getSolutionPath();
			og::PathGeometric* pth = pdef->getSolutionPath()->as<og::PathGeometric>();
            pth->printAsMatrix(std::cout);
	        // print the path to screen
            path->print(std::cout);
            //trajectory_msgs::MultiDOFJointTrajectory msg;
            //trajectory_msgs::MultiDOFJointTrajectoryPoint point_msg;
            //eric_wang:
            nav_msgs::Path msg;
            geometry_msgs::PoseStamped pose;

			msg.header.stamp = ros::Time::now();
            msg.header.frame_id = "odom";
            //msg.joint_names.clear();
            //msg.points.clear();
            //msg.joint_names.push_back("Quadcopter");

			for (std::size_t path_idx = 0; path_idx < pth->getStateCount (); path_idx++)
			{
				const ob::SE3StateSpace::StateType *se3state = pth->getState(path_idx)->as<ob::SE3StateSpace::StateType>();

	            // extract the first component of the state and cast it to what we expect
				const ob::RealVectorStateSpace::StateType *pos = se3state->as<ob::RealVectorStateSpace::StateType>(0);

	            // extract the second component of the state and cast it to what we expect
				const ob::SO3StateSpace::StateType *rot = se3state->as<ob::SO3StateSpace::StateType>(1);

//				point_msg.time_from_start.fromSec(ros::Time::now().toSec());
//				point_msg.transforms.resize(1);

//				point_msg.transforms[0].translation.x= pos->values[0];
//				point_msg.transforms[0].translation.y = pos->values[1];
//				point_msg.transforms[0].translation.z = pos->values[2];

//				point_msg.transforms[0].rotation.x = rot->x;
//				point_msg.transforms[0].rotation.y = rot->y;
//				point_msg.transforms[0].rotation.z = rot->z;
//				point_msg.transforms[0].rotation.w = rot->w;
                pose.pose.position.x = pos->values[0];
                pose.pose.position.y = pos->values[1];
                pose.pose.position.z = pos->values[2];

                pose.pose.orientation.x = rot->x;
                pose.pose.orientation.y = rot->y;
                pose.pose.orientation.z = rot->z;
                pose.pose.orientation.w = rot->w;

                msg.poses.push_back(pose);


			}
            //eric_wang:
            std::cout<<"publish the <nav_msgs::path>msgs"<<std::endl;
            traj_pub.publish(msg);


	        //Path smoothing using bspline

			og::PathSimplifier* pathBSpline = new og::PathSimplifier(si);
			path_smooth = new og::PathGeometric(dynamic_cast<const og::PathGeometric&>(*pdef->getSolutionPath()));
			pathBSpline->smoothBSpline(*path_smooth,3);
			// std::cout << "Smoothed Path" << std::endl;
			// path_smooth.print(std::cout);


			//Publish path as markers

			visualization_msgs::Marker marker;

			marker.action = visualization_msgs::Marker::DELETEALL;
            //vis_pub.publish(marker);

			for (std::size_t idx = 0; idx < path_smooth->getStateCount (); idx++)
			{
	                // cast the abstract state type to the type we expect
				const ob::SE3StateSpace::StateType *se3state = path_smooth->getState(idx)->as<ob::SE3StateSpace::StateType>();

	            // extract the first component of the state and cast it to what we expect
				const ob::RealVectorStateSpace::StateType *pos = se3state->as<ob::RealVectorStateSpace::StateType>(0);

	            // extract the second component of the state and cast it to what we expect
				const ob::SO3StateSpace::StateType *rot = se3state->as<ob::SO3StateSpace::StateType>(1);

                marker.header.frame_id = "odom";
                marker.header.stamp = ros::Time::now();
				marker.ns = "path";
				marker.id = idx;
				marker.type = visualization_msgs::Marker::CUBE;
                marker.action = visualization_msgs::Marker::ADD;
				marker.pose.position.x = pos->values[0];
				marker.pose.position.y = pos->values[1];
				marker.pose.position.z = pos->values[2];
				marker.pose.orientation.x = rot->x;
				marker.pose.orientation.y = rot->y;
				marker.pose.orientation.z = rot->z;
				marker.pose.orientation.w = rot->w;
				marker.scale.x = 0.15;
				marker.scale.y = 0.15;
				marker.scale.z = 0.15;
				marker.color.a = 1.0;
				marker.color.r = 0.0;
				marker.color.g = 1.0;
				marker.color.b = 0.0;
                vis_pub.publish(marker);
                ros::Duration(0.01).sleep();
                marker.lifetime = ros::Duration();
				std::cout << "Published marker: " << idx << std::endl;
			}

			// Clear memory
			pdef->clearSolutionPaths();
			replan_flag = false;

		}
		else
			std::cout << "No solution found" << std::endl;
    }

    //ros nodehandle
    ros::NodeHandle nh;

private:

	// construct the state space we are planning in
	ob::StateSpacePtr space;

	// construct an instance of  space information from this state space
	ob::SpaceInformationPtr si;
	// create a problem instance
	ob::ProblemDefinitionPtr pdef;

	// goal state
	double prev_goal[3];

    //ROS publisher
    ros::Publisher vis_pub;
    ros::Publisher traj_pub;

    //Ros node
    //ros::NodeHandle nh;

	og::PathGeometric* path_smooth = NULL;

	bool replan_flag = false;

	std::shared_ptr<fcl::CollisionGeometry> Quadcopter;

	std::shared_ptr<fcl::CollisionGeometry> tree_obj;

	// Flag for initialization
	bool set_start = false;

	bool isStateValid(const ob::State *state)
	{
	    // cast the abstract state type to the type we expect
		const ob::SE3StateSpace::StateType *se3state = state->as<ob::SE3StateSpace::StateType>();

	    // extract the first component of the state and cast it to what we expect
		const ob::RealVectorStateSpace::StateType *pos = se3state->as<ob::RealVectorStateSpace::StateType>(0);

	    // extract the second component of the state and cast it to what we expect
		const ob::SO3StateSpace::StateType *rot = se3state->as<ob::SO3StateSpace::StateType>(1);

		fcl::CollisionObject treeObj((tree_obj));
		fcl::CollisionObject aircraftObject(Quadcopter);

	    // check validity of state defined by pos & rot
		fcl::Vec3f translation(pos->values[0],pos->values[1],pos->values[2]);
		fcl::Quaternion3f rotation(rot->w, rot->x, rot->y, rot->z);
		aircraftObject.setTransform(rotation, translation);
		fcl::CollisionRequest requestType(1,false,1,false);
		fcl::CollisionResult collisionResult;
		fcl::collide(&aircraftObject, &treeObj, requestType, collisionResult);

		return(!collisionResult.isCollision());
	}

	// Returns a structure representing the optimization objective to use
	// for optimal motion planning. This method returns an objective which
	// attempts to minimize the length in configuration space of computed
	// paths.
	ob::OptimizationObjectivePtr getThresholdPathLengthObj(const ob::SpaceInformationPtr& si)
	{
		ob::OptimizationObjectivePtr obj(new ob::PathLengthOptimizationObjective(si));
		// obj->setCostThreshold(ob::Cost(1.51));
		return obj;
	}

	ob::OptimizationObjectivePtr getPathLengthObjWithCostToGo(const ob::SpaceInformationPtr& si)
	{
		ob::OptimizationObjectivePtr obj(new ob::PathLengthOptimizationObjective(si));
		obj->setCostToGoHeuristic(&ob::goalRegionCostToGo);
		return obj;
	}

};


void octomapCallback(const octomap_msgs::Octomap::ConstPtr &msg, planner* planner_ptr)
{


    //loading octree from binary
     // const std::string filename = "/home/rrc/power_plant.bt";
     // octomap::OcTree temp_tree(0.1);
     // temp_tree.readBinary(filename);
     // fcl::OcTree* tree = new fcl::OcTree(std::shared_ptr<const octomap::OcTree>(&temp_tree));


    // convert octree to collision object
    octomap::OcTree* tree_oct = dynamic_cast<octomap::OcTree*>(octomap_msgs::msgToMap(*msg));
    fcl::OcTree* tree = new fcl::OcTree(std::shared_ptr<const octomap::OcTree>(tree_oct));

    // Update the octree used for collision checking
    planner_ptr->updateMap(std::shared_ptr<fcl::CollisionGeometry>(tree));
    planner_ptr->replan();
}

void odomCb(const nav_msgs::Odometry::ConstPtr &msg, planner* planner_ptr)
{
	planner_ptr->setStart(msg->pose.pose.position.x, msg->pose.pose.position.y, msg->pose.pose.position.z);
	planner_ptr->init_start();
}

void startCb(const geometry_msgs::PointStamped::ConstPtr &msg, planner* planner_ptr)
{
	planner_ptr->setStart(msg->point.x, msg->point.y, msg->point.z);
	planner_ptr->init_start();
}

void goalCb(const geometry_msgs::PointStamped::ConstPtr &msg, planner* planner_ptr)
{
	planner_ptr->setGoal(msg->point.x, msg->point.y, msg->point.z);
}

int main(int argc, char **argv)
{
	ros::init(argc, argv, "octomap_planner");
    //ros::NodeHandle n;
	planner planner_object;

    ros::Subscriber octree_sub = planner_object.nh.subscribe<octomap_msgs::Octomap>("/octomap_binary", 1, boost::bind(&octomapCallback, _1, &planner_object));
    ROS_INFO("Subcribe the octomap_messages");
    //ros::Subscriber odom_sub = n.subscribe<nav_msgs::Odometry>("/bebop2/odometry_sensor1/odometry", 1, boost::bind(&odomCb, _1, &planner_object));
    //eric_wang:pulish the goal point with the "Publish Point" in Rviz;
    ros::Subscriber goal_sub = planner_object.nh.subscribe<geometry_msgs::PointStamped>("/clicked_point", 1, boost::bind(&goalCb, _1, &planner_object));
    //ros::Subscriber start_sub = n.subscribe<geometry_msgs::PointStamped>("/start/clicked_point", 1, boost::bind(&goalCb, _1, &planner_object));
    planner_object.setStart(0, 0, 0);
    planner_object.init_start();
    //planner_object.setGoal(-5, -5, 2);
    //vis_pub = n.advertise<visualization_msgs::Marker>( "visualization_marker", 100);
    //traj_pub = n.advertise<trajectory_msgs::MultiDOFJointTrajectory>("waypoints",1);

	std::cout << "OMPL version: " << OMPL_VERSION << std::endl;

	ros::spin();

	return 0;
}
