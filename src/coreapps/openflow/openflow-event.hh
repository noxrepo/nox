/* Copyright 2008 (C) Nicira, Inc.
 *
 * This file is part of NOX.
 *
 * NOX is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * NOX is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NOX.  If not, see <http://www.gnu.org/licenses/>.
 */
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
#ifndef OFP_MSG_EVENT_HH
#define OFP_MSG_EVENT_HH

#include <boost/shared_ptr.hpp>

#include "event.hh"
#include "openflow-datapath.hh"

namespace vigil
{
namespace openflow
{

class Openflow_event : public Event
{
public:
    Openflow_event(Openflow_datapath& dp_,
                   const v1::ofp_msg* msg_) : Event(msg_->name()), dp(dp_), msg(msg_) {}

    ~Openflow_event() { }

    Openflow_datapath& dp;

    const v1::ofp_msg* msg;
};

} // namespace openflow
} // namespace vigil

#endif  // -- OFP_MSG_EVENT_HH
