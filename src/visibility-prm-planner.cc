//
// Copyright (c) 2014 CNRS
// Authors: Mylene Campana
//
// This file is part of hpp-core
// hpp-core is free software: you can redistribute it
// and/or modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation, either version
// 3 of the License, or (at your option) any later version.
//
// hpp-core is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the implied warranty
// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Lesser Public License for more details.  You should have
// received a copy of the GNU Lesser General Public License along with
// hpp-core  If not, see
// <http://www.gnu.org/licenses/>.

#include <hpp/util/debug.hh>
#include <hpp/pinocchio/device.hh>
#include <hpp/pinocchio/configuration.hh>
#include <hpp/core/config-projector.hh>
#include <hpp/core/config-validations.hh>
#include <hpp/core/connected-component.hh>
#include <hpp/core/path-validation.hh>
#include <hpp/core/problem.hh>
#include <hpp/core/roadmap.hh>
#include <hpp/core/visibility-prm-planner.hh>
#include <hpp/core/configuration-shooter.hh>

#include <stdio.h>
#include <time.h>

namespace hpp {
  namespace core {
    using pinocchio::displayConfig;

    VisibilityPrmPlannerPtr_t VisibilityPrmPlanner::createWithRoadmap
    (const Problem& problem, const RoadmapPtr_t& roadmap)
    {
      VisibilityPrmPlanner* ptr = new VisibilityPrmPlanner (problem, roadmap);
      return VisibilityPrmPlannerPtr_t (ptr);
    }

    VisibilityPrmPlannerPtr_t VisibilityPrmPlanner::create 
    (const Problem& problem)
    {
      VisibilityPrmPlanner* ptr = new VisibilityPrmPlanner (problem);
      return VisibilityPrmPlannerPtr_t (ptr);
    }

    VisibilityPrmPlanner::VisibilityPrmPlanner (const Problem& problem):
      PathPlanner (problem)
    {
    }

    VisibilityPrmPlanner::VisibilityPrmPlanner (const Problem& problem,
						const RoadmapPtr_t& roadmap) :
      PathPlanner (problem, roadmap)
    {
    }

    void VisibilityPrmPlanner::init (const VisibilityPrmPlannerWkPtr_t& weak)
    {
      PathPlanner::init (weak);
      weakPtr_ = weak;
    }

    bool VisibilityPrmPlanner::visibleFromCC (const Configuration_t q, 
					      const ConnectedComponentPtr_t cc){
      PathPtr_t validPart;
      bool found = false; 
      value_type length = std::numeric_limits <value_type>::infinity ();
      PathValidationPtr_t pathValidation (problem ().pathValidation ());
      SteeringMethodPtr_t sm (problem ().steeringMethod ());
      RoadmapPtr_t r (roadmap ());
      DelayedEdge_t delayedEdge;

      for (NodeVector_t::const_iterator n_it = cc->nodes ().begin (); 
	   n_it != cc->nodes ().end (); ++n_it){
	if(nodeStatus_ [*n_it]){// only iterate on guard nodes
	  ConfigurationPtr_t qCC = (*n_it)->configuration ();
	  PathPtr_t path = (*sm) (q, *qCC);
	  PathValidationReportPtr_t report;
	  if (path && pathValidation->validate (path, false, validPart,
						report)){
	    // q and qCC see each other
	    if (path->length () < length) {
	      length = path->length ();
	      // Save shortest edge
	      delayedEdge = DelayedEdge_t (*n_it,
                  boost::make_shared<Configuration_t>(q),
                  path->reverse ());
	    }
	    found = true;
	  }
	}
      }
      if (found) {
	// Store shortest delayed edge in list
	delayedEdges_.push_back (delayedEdge);
	return true;
      }
      else return false;
    }

    void VisibilityPrmPlanner::applyConstraints (const Configuration_t& qFrom,
        const Configuration_t& qTo, Configuration_t& qout)
    {
      ConstraintPtr_t constraints (problem ().constraints ());
      if (constraints) {
	ConfigProjectorPtr_t configProjector (constraints->configProjector ());
	if (configProjector) {
          constrApply_ = false; // while apply has not successed
	  configProjector->projectOnKernel (qFrom, qTo, qout);
	  if (constraints->apply (qout)) {
	    constrApply_ = true;
	    return;
	  }
	}
      }
      qout = qTo;
    }

    void VisibilityPrmPlanner::oneStep ()
    {
      DevicePtr_t robot (problem ().robot ());
      ConfigurationShooterPtr_t configurationShooter (problem().configurationShooter());
      ConfigValidationsPtr_t configValidations (problem ().configValidations());
      RoadmapPtr_t r (roadmap ());
      value_type count; // number of times q has been seen
      constrApply_ = true; // stay true if no constraint in Problem
      Configuration_t q_init (*(r->initNode ()->configuration ())),
                      q_proj (robot->configSize()),
                      q_rand (robot->configSize());

      /* Initialization of guard status */
      nodeStatus_ [r->initNode ()] = true; // init node is guard
      for (NodeVector_t::const_iterator itg = r->goalNodes ().begin();
	   itg != r->goalNodes ().end (); ++itg) {
	nodeStatus_ [*itg] = true; // goal nodes are guards
      }

      // Shoot random config as long as not collision-free
      ValidationReportPtr_t report;
      do {
	configurationShooter->shoot (q_rand);
	applyConstraints(q_init, q_rand, q_proj);
	robot->currentConfiguration (q_proj);
	robot->computeForwardKinematics ();
      } while (!configValidations->validate (q_proj, report) ||
	       !constrApply_);
      count = 0;

      for (ConnectedComponents_t::const_iterator itcc =
	     r->connectedComponents ().begin ();
	   itcc != r->connectedComponents ().end (); ++itcc) {
	ConnectedComponentPtr_t cc = *itcc;
	if (visibleFromCC (q_proj, cc)) { 
	  // delayedEdges_ will completed if visible
	  count++; // count how many times q has been seen
	}
      }
	
      if (count == 0){ // q not visible from anywhere
	NodePtr_t newNode = r->addNode (q_proj); // add q as a guard node
	nodeStatus_ [newNode] = true;
	hppDout(info, "q is a guard node: " << displayConfig (q_proj));
      }
      if (count > 1){ // q visible several times
	// Insert delayed edges from list and add q as a connection node
	for (DelayedEdges_t::const_iterator itEdge = delayedEdges_.begin ();
	     itEdge != delayedEdges_.end (); ++itEdge) {
	  const NodePtr_t& near = itEdge-> get <0> ();
	  const ConfigurationPtr_t& q_new = itEdge-> get <1> ();
	  const PathPtr_t& validPath = itEdge-> get <2> ();
	  NodePtr_t newNode = r->addNode (q_new);
	  nodeStatus_ [newNode] = false;
	  r->addEdge (near, newNode, validPath);
	  r->addEdge (newNode, near, validPath->reverse());
	  hppDout(info, "connection between q1: " 
		  << displayConfig (*(near->configuration ())) 
		  << "and q2: " << displayConfig (*q_new));
	}
      }
      delayedEdges_.clear ();
    }

  } // namespace core
} // namespace hpp
