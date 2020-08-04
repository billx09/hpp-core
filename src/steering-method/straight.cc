// Copyright (c) 2019 CNRS
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

#include <hpp/core/steering-method/straight.hh>

#include <hpp/core/problem.hh>
#include <hpp/core/straight-path.hh>
#include <hpp/core/distance.hh>
#include <hpp/core/config-projector.hh>

namespace hpp {
  namespace core {
    namespace steeringMethod {
      PathPtr_t Straight::impl_compute (ConfigurationIn_t q1, ConfigurationIn_t q2) const
      {
        value_type length = (*problem_.distance()) (q1, q2);
        ConstraintPtr_t c;
        if (constraints() && constraints()->configProjector ()) {
          c = constraints()->copy ();
          c->configProjector()->rightHandSideFromConfig (q1); 
          c->configProjector()->lineSearchType (ConfigProjector::Backtracking);
        } else {
          c = constraints ();
        }
        PathPtr_t path = StraightPath::create
          (problem_.robot(), q1, q2, length, c);
        return path;
      }
    } // namespace steeringMethod
  } // namespace core
} // namespace hpp
