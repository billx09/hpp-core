//
// Copyright (c) 2014 CNRS
// Authors: Florent Lamiraux
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

#include <hpp/core/constraint.hh>

#include <boost/serialization/weak_ptr.hpp>
#include <hpp/util/serialization.hh>

#include <hpp/core/config-projector.hh>

namespace hpp {
  namespace core {
    bool Constraint::apply (ConfigurationOut_t configuration)
    {
      return impl_compute (configuration);
    }

    ConfigProjectorPtr_t Constraint::configProjector () const
    {
      return HPP_DYNAMIC_PTR_CAST(ConfigProjector, weak_.lock());
    }


    template<class Archive>
    void Constraint::serialize(Archive & ar, const unsigned int version)
    {
      (void) version;
      ar & BOOST_SERIALIZATION_NVP(name_);
      ar & BOOST_SERIALIZATION_NVP(weak_);
    }

    HPP_SERIALIZATION_IMPLEMENT(Constraint);
  } // namespace core
} // namespace core
