//
// Copyright (c) 2020 CNRS
// Authors: Joseph Mirabel
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

#include <hpp/core/path-planner/bi-rrt-star.hh>

#include <queue>

#include <hpp/core/configuration-shooter.hh>
#include <hpp/core/config-validations.hh>
#include <hpp/core/path-projector.hh>
#include <hpp/core/path-validation.hh>
#include <hpp/core/path-validation-report.hh>
#include <hpp/core/problem.hh>
#include <hpp/core/roadmap.hh>
#include <hpp/core/edge.hh>

namespace hpp {
  namespace core {
    namespace pathPlanner {
      BiRrtStarPtr_t BiRrtStar::create (const Problem& problem)
      {
        BiRrtStarPtr_t shPtr (new BiRrtStar (problem));
        shPtr->init (shPtr);
        return shPtr;
      }

      BiRrtStarPtr_t BiRrtStar::createWithRoadmap (const Problem& problem,
                                                 const RoadmapPtr_t& roadmap)
      {
        BiRrtStarPtr_t shPtr (new BiRrtStar (problem, roadmap));
        shPtr->init (shPtr);
        return shPtr;
      }

      BiRrtStar::BiRrtStar (const Problem& problem) :
        Parent_t (problem),
        gamma_ (1.),
        extendMaxLength_ (1.),
        toRoot_(2)
      {}

      BiRrtStar::BiRrtStar (const Problem& problem, const RoadmapPtr_t& roadmap) :
        Parent_t (problem, roadmap),
        gamma_ (1.),
        extendMaxLength_ (1.),
        toRoot_(2)
      {}

      void BiRrtStar::init (const BiRrtStarWkPtr_t& weak)
      {
        Parent_t::init (weak);
        weak_ = weak;
      }

      // ----------- Algorithm ---------------------------------------------- //

      typedef std::pair<bool, PathPtr_t> ValidatedPath_t;

      typedef std::map<NodePtr_t, EdgePtr_t> ParentMap_t;
      value_type computeCost(const ParentMap_t& map, NodePtr_t n)
      {
        typedef ParentMap_t::const_iterator It_t;
        value_type c = 0;
        for (It_t current = map.find(n); current->second; 
            current = map.find(current->second->from())) {
          if (current == map.end())
            throw std::logic_error("this node has no parent.");
          c += current->second->path()->length();
        }
        return c;
      }

      void setParent(ParentMap_t& map, NodePtr_t n, EdgePtr_t e)
      {
        if (e) {
          assert(e->to() == n);
          if (map.find(e->from()) == map.end()) {
            std::cout << "\n" << e->from()->configuration()->transpose() << std::endl;
            throw std::logic_error("could not find node from of edge in parent map.");
          }
        }
        map[n] = e;
      }

      struct WeighedNode_t {
        NodePtr_t node;
        EdgePtr_t parent;
        value_type cost;
        bool operator<( const WeighedNode_t& other ) const { return cost < other.cost; }
        WeighedNode_t(NodePtr_t node, EdgePtr_t parent, value_type cost)
          : node(node), parent(parent), cost(cost) {}
      };
      typedef std::priority_queue<WeighedNode_t> Queue_t;

      ParentMap_t computeParentMap(NodePtr_t root)
      {
        typedef std::map<NodePtr_t, WeighedNode_t> Visited_t;
        typedef Visited_t::iterator ItV_t;
        Visited_t visited;

        Queue_t queue;
        queue.push(WeighedNode_t(root, EdgePtr_t(), 0));

        while (!queue.empty()) {
          WeighedNode_t current (queue.top());
          queue.pop();

          std::pair<ItV_t, bool> res (visited.insert(std::make_pair(current.node, current)));
          bool addChildren (res.second);
          if (!addChildren) {
            // Not inserted because already visited. Check if path is better.
            // Normally, it is not possible that the children of current are
            // visited before all the best way of reaching current is found.
            if (res.first->second.cost > current.cost) {
              res.first->second.cost = current.cost;
              res.first->second.parent = current.parent;
              // Re-add to priority queue.
              addChildren = true;
            }
          }
          if (addChildren) {
            const Edges_t& edges = current.node->outEdges();
            for (Edges_t::const_iterator _edge = edges.begin();
                _edge != edges.end(); ++_edge) {
              EdgePtr_t edge (*_edge);
              queue.push(WeighedNode_t(edge->to(), edge,
                    current.cost + edge->path()->length()));
            }
          }
        }

        ParentMap_t result;
        for (ItV_t _v = visited.begin(); _v != visited.end(); ++_v)
          result[_v->first] = _v->second.parent;
        return result;
      }

      void BiRrtStar::startSolve ()
      {
        Parent_t::startSolve ();

        if (roadmap()->goalNodes().size() != 1)
          throw std::invalid_argument("there should be only one goal node.");

        extendMaxLength_ = problem().getParameter("BiRRT*/maxStepLength").floatValue();
        if (extendMaxLength_ <= 0) 
          extendMaxLength_ = std::sqrt(problem().robot()->numberDof());
        gamma_ = problem().getParameter("BiRRT*/gamma").floatValue();

        roots_[0] = roadmap()->initNode();
        roots_[1] = roadmap()->goalNodes()[0];

        setParent(toRoot_[0], roots_[0], EdgePtr_t());
        setParent(toRoot_[1], roots_[1], EdgePtr_t());
      }

      void BiRrtStar::oneStep ()
      {
        Configuration_t q = sample();

        if (roadmap()->connectedComponents().size() == 2) {
          if (extend(roots_[0], toRoot_[0], q)) {
            // in the unlikely event that extend connected the two graphs,
            // then one of the connected component is not valid.
            if (roots_[0]->connectedComponent() == roots_[1]->connectedComponent()) return;
            connect(roots_[1], toRoot_[1], q);
          }

          std::swap(roots_[0], roots_[1]);
          std::swap(toRoot_[0], toRoot_[1]);
        } else {
          if (toRoot_[1].find(roots_[0]) == toRoot_[1].end()) {
            // Fill parent map
            toRoot_[0] = computeParentMap(roots_[0]);
            toRoot_[1] = computeParentMap(roots_[1]);
          }

          assert(toRoot_[0].size() == toRoot_[1].size());
          assert(toRoot_[0].size() == roadmap()->nodes().size());
          improve(q);
        }
      }

      Configuration_t BiRrtStar::sample ()
      {
        ConfigurationShooterPtr_t shooter = problem().configurationShooter();
        Configuration_t q;
        shooter->shoot(q);
        return q;
      }

      bool validate(const Problem& problem, const PathPtr_t& path)
      {
        PathPtr_t validPart;
        PathValidationReportPtr_t report;
        return problem.pathValidation()
          ->validate (path, false, validPart, report);
      }

      PathPtr_t BiRrtStar::buildPath(const Configuration_t& q0, const Configuration_t& q1,
          value_type maxLength,
          bool validatePath)
      {
        PathPtr_t path = problem().steeringMethod()->steer(q0, q1);
        if (!path) return path;
        if (problem().pathProjector()) { // path projection
          PathPtr_t projected;
          problem().pathProjector()->apply (path, projected);
          if (!projected) return projected;
          path = projected;
        }

        if (maxLength > 0 && path->length() > maxLength) {
          const interval_t& I = path->timeRange();
          path = path->extract(I.first, I.first + maxLength);
        }

        if (!validatePath) return path;

        PathPtr_t validPart;
        PathValidationReportPtr_t report;
        problem().pathValidation()->validate (path, false, validPart, report);
        return validPart;
      }

      bool BiRrtStar::extend (NodePtr_t target, ParentMap_t& parentMap, Configuration_t& q)
      {
        ConnectedComponentPtr_t cc (target->connectedComponent());

        value_type dist;
        NodePtr_t near = roadmap()->nearestNode(q, cc, dist);
        if (dist < 1e-16)
          return false;

        PathPtr_t path = buildPath(*near->configuration(), q, extendMaxLength_, true);
        if (!path || path->length() < 1e-10) return false;
        q = path->end();

        value_type n ((value_type)roadmap()->nodes().size());
        NodeVector_t nearNodes = roadmap()->nodesWithinBall(q, cc,
            std::min(gamma_ * std::pow(std::log(n)/n, 1./(value_type)problem().robot()->numberDof()),
              extendMaxLength_));

        value_type cost_q (computeCost(parentMap, near) + path->length());
        std::vector<ValidatedPath_t> paths;
        paths.reserve(nearNodes.size());
        for (NodeVector_t::const_iterator _near = nearNodes.begin(); _near != nearNodes.end(); ++_near) {
          PathPtr_t near2new;
          if (*_near == near) {
            near2new = path;
            paths.push_back(ValidatedPath_t(true, near2new));
            continue;
          } else {
            near2new = buildPath(*(*_near)->configuration(), q, -1, false);
            paths.push_back(ValidatedPath_t(false, near2new));
          }
          if (!near2new) continue;

          value_type _cost_q = computeCost(parentMap, *_near) + near2new->length();
          if (_cost_q < cost_q) {
            paths.back().first = true;
            // Run path validation
            if (validate (problem(), near2new)) {
              // Path is valid and shorter.
              cost_q = _cost_q;
              near = *_near;
              path = near2new;
            } else
              paths.back().second.reset();
          }
        }

        NodePtr_t qnew = roadmap()->addNode(boost::make_shared<Configuration_t>(q));
        EdgePtr_t edge = roadmap()->addEdge(near, qnew, path);
        roadmap()->addEdge(qnew, near, path->reverse());
        assert(parentMap.find(near) != parentMap.end());
        setParent(parentMap, qnew, edge);

        for (std::size_t i = 0; i < nearNodes.size(); ++i) {
          if (nearNodes[i] == near || !paths[i].second) continue;

          value_type cost_q_near = cost_q + paths[i].second->length();
          if (cost_q_near < computeCost(parentMap, nearNodes[i])) {
            bool pathValid = paths[i].first;
            if (!pathValid) // If path validation has not been run
              pathValid = validate(problem(), paths[i].second);
            if (pathValid) {
              roadmap()->addEdge(nearNodes[i], qnew, paths[i].second);
              edge = roadmap()->addEdge(qnew, nearNodes[i], paths[i].second->reverse());
              setParent(parentMap, nearNodes[i], edge);
            }
          }
        }
        return true;
      }

      bool BiRrtStar::connect (NodePtr_t b, ParentMap_t& parentMap, const Configuration_t& q)
      {
        Configuration_t qnew;
        // while extend did not reach q
        while (roadmap()->connectedComponents().size() == 2) {
          qnew = q;
          if (!extend(b, parentMap, qnew)) // extend failed
            return false;
        }
        return true;
      }

      bool BiRrtStar::improve (const Configuration_t& q)
      {
        value_type dist;
        NodePtr_t near = roadmap()->nearestNode(q, dist);
        if (dist < 1e-16)
          return false;

        PathPtr_t path = buildPath(*near->configuration(), q, extendMaxLength_, true);
        if (!path || path->length() < 1e-10) return false;

        value_type n ((value_type)roadmap()->nodes().size());
        NodeVector_t nearNodes = roadmap()->nodesWithinBall(q, roots_[0]->connectedComponent(),
            std::min(gamma_ * std::pow(std::log(n)/n, 1./(value_type)problem().robot()->numberDof()),
              extendMaxLength_));

        NodePtr_t qnew = roadmap()->addNode(boost::make_shared<Configuration_t>(q));

        std::vector<ValidatedPath_t> paths;
        paths.reserve(nearNodes.size());

        for (int k = 0; k < 2; ++k) {
          paths.clear();

          PathPtr_t toqnew (path);

          value_type cost_q (computeCost(toRoot_[k], near) + toqnew->length());

          for (NodeVector_t::const_iterator _near = nearNodes.begin(); _near != nearNodes.end(); ++_near) {
            PathPtr_t near2new;
            if (*_near == near) {
              near2new = toqnew;
              paths.push_back(ValidatedPath_t(true, near2new));
              continue;
            } else {
              near2new = buildPath(*(*_near)->configuration(), q, -1, false);
              paths.push_back(ValidatedPath_t(false, near2new));
            }
            if (!near2new) continue;

            value_type _cost_q = computeCost(toRoot_[k], *_near) + near2new->length();
            if (_cost_q < cost_q) {
              paths.back().first = true;
              // Run path validation
              if (validate (problem(), near2new)) {
                // Path is valid and shorter.
                cost_q = _cost_q;
                near = *_near;
                toqnew = near2new;
              } else
                paths.back().second.reset();
            }
          }

          EdgePtr_t edge = roadmap()->addEdge(near, qnew, toqnew);
          roadmap()->addEdge(qnew, near, toqnew->reverse());
          assert(toRoot_[k].find(near) != toRoot_[k].end());
          setParent(toRoot_[k], qnew, edge);

          for (std::size_t i = 0; i < nearNodes.size(); ++i) {
            if (nearNodes[i] == near || !paths[i].second) continue;

            value_type cost_q_near = cost_q + paths[i].second->length();
            if (cost_q_near < computeCost(toRoot_[k], nearNodes[i])) {
              bool pathValid = paths[i].first;
              if (!pathValid) // If path validation has not been run
                pathValid = validate(problem(), paths[i].second);
              if (pathValid) {
                roadmap()->addEdge(nearNodes[i], qnew, paths[i].second);
                edge = roadmap()->addEdge(qnew, nearNodes[i], paths[i].second->reverse());
                assert(toRoot_[k].find(qnew) != toRoot_[k].end());
                setParent(toRoot_[k], nearNodes[i], edge);
              }
            }
          }
        }
        return true;
      }

      // ----------- Declare parameters ------------------------------------- //

      HPP_START_PARAMETER_DECLARATION(BiRrtStar)
      Problem::declareParameter(ParameterDescription (Parameter::FLOAT,
            "BiRRT*/maxStepLength",
            "The maximum step length when extending. If negative, uses sqrt(dimension)",
            Parameter(-1.)));
      Problem::declareParameter(ParameterDescription (Parameter::FLOAT,
            "BiRRT*/gamma",
            "",
            Parameter(1.)));
      HPP_END_PARAMETER_DECLARATION(BiRrtStar)
    } // namespace pathPlanner
  } // namespace core
} // namespace hpp

