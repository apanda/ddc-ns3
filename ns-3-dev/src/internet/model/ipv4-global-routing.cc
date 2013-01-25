// -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*-
//
// Copyright (c) 2008 University of Washington
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation;
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//

#include <vector>
#include <iomanip>
#include "ns3/names.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/net-device.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv4-routing-table-entry.h"
#include "ns3/boolean.h"
#include "ipv4-global-routing.h"
#include "global-route-manager.h"

NS_LOG_COMPONENT_DEFINE ("Ipv4GlobalRouting");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (Ipv4GlobalRouting);

TypeId 
Ipv4GlobalRouting::GetTypeId (void)
{ 
  static TypeId tid = TypeId ("ns3::Ipv4GlobalRouting")
    .SetParent<Object> ()
    .AddAttribute ("RandomEcmpRouting",
                   "Set to true if packets are randomly routed among ECMP; set to false for using only one route consistently",
                   BooleanValue (false),
                   MakeBooleanAccessor (&Ipv4GlobalRouting::m_randomEcmpRouting),
                   MakeBooleanChecker ())
    .AddAttribute ("RespondToInterfaceEvents",
                   "Set to true if you want to dynamically recompute the global routes upon Interface notification events (up/down, or add/remove address)",
                   BooleanValue (false),
                   MakeBooleanAccessor (&Ipv4GlobalRouting::m_respondToInterfaceEvents),
                   MakeBooleanChecker ())
    .AddAttribute ("ReverseOutputToInputDelay",
                   "Delay reversing output to input",
                   TimeValue (Seconds (0)),
                   MakeTimeAccessor (&Ipv4GlobalRouting::m_reverseOutputToInputDelay),
                   MakeTimeChecker ())
    .AddAttribute ("ReverseInputToOutputDelay",
                   "Delay reversing input to output",
                   TimeValue (Seconds (0)),
                   MakeTimeAccessor (&Ipv4GlobalRouting::m_reverseInputToOutputDelay),
                   MakeTimeChecker())
    .AddAttribute ("AllowReversal",
                   "Allow DDC reversals",
                   BooleanValue (true),
                   MakeBooleanAccessor (&Ipv4GlobalRouting::m_allowReversal),
                   MakeBooleanChecker ())
  ;
  return tid;
}

Ipv4GlobalRouting::Ipv4GlobalRouting () 
  : m_randomEcmpRouting (false),
    m_respondToInterfaceEvents (false)
{
  NS_LOG_FUNCTION_NOARGS ();
}

Ipv4GlobalRouting::~Ipv4GlobalRouting ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

void 
Ipv4GlobalRouting::AddHostRouteTo (Ipv4Address dest, 
                                   Ipv4Address nextHop, 
                                   uint32_t interface)
{
  //NS_LOG_FUNCTION (dest << nextHop << interface);
  Ipv4RoutingTableEntry *route = new Ipv4RoutingTableEntry ();
  *route = Ipv4RoutingTableEntry::CreateHostRouteTo (dest, nextHop, interface);
  m_hostRoutes.push_back (route);
  InitializeDestination(dest);
  m_vnodeState[0].m_directions[dest][interface] = Out;
  m_vnodeState[0].m_outputs[dest].push(PriorityInterface(m_priorities[dest][interface], interface));
  
}

void 
Ipv4GlobalRouting::AddHostRouteTo (Ipv4Address dest, 
                                   uint32_t interface)
{
  //NS_LOG_FUNCTION (dest << interface);
  Ipv4RoutingTableEntry *route = new Ipv4RoutingTableEntry ();
  *route = Ipv4RoutingTableEntry::CreateHostRouteTo (dest, interface);
  m_hostRoutes.push_back (route);
  InitializeDestination(dest);
  m_vnodeState[0].m_directions[dest][interface] = Out;
  m_vnodeState[0].m_outputs[dest].push(PriorityInterface(m_priorities[dest][interface], interface));
}

void 
Ipv4GlobalRouting::AddNetworkRouteTo (Ipv4Address network, 
                                      Ipv4Mask networkMask, 
                                      Ipv4Address nextHop, 
                                      uint32_t interface)
{
  //NS_LOG_FUNCTION (network << networkMask << nextHop << interface);
  Ipv4RoutingTableEntry *route = new Ipv4RoutingTableEntry ();
  *route = Ipv4RoutingTableEntry::CreateNetworkRouteTo (network,
                                                        networkMask,
                                                        nextHop,
                                                        interface);
  m_networkRoutes.push_back (route);
  if (networkMask == Ipv4Mask(0xffffffff)) {
    InitializeDestination(network);
    m_vnodeState[0].m_directions[network][interface] = Out;
    m_vnodeState[0].m_outputs[network].push(PriorityInterface(m_priorities[network][interface], interface));
  }
}

void 
Ipv4GlobalRouting::AddNetworkRouteTo (Ipv4Address network, 
                                      Ipv4Mask networkMask, 
                                      uint32_t interface)
{
  //NS_LOG_FUNCTION (network << networkMask << interface);
  Ipv4RoutingTableEntry *route = new Ipv4RoutingTableEntry ();
  *route = Ipv4RoutingTableEntry::CreateNetworkRouteTo (network,
                                                        networkMask,
                                                        interface);
  m_networkRoutes.push_back (route);
  if (networkMask == Ipv4Mask(0xffffffff)) {
    InitializeDestination(network);
    m_vnodeState[0].m_directions[network][interface] = Out;
    m_vnodeState[0].m_outputs[network].push(PriorityInterface(m_priorities[network][interface], interface));
  }
}

void 
Ipv4GlobalRouting::AddASExternalRouteTo (Ipv4Address network, 
                                         Ipv4Mask networkMask,
                                         Ipv4Address nextHop,
                                         uint32_t interface)
{
  //NS_LOG_FUNCTION (network << networkMask << nextHop);
  Ipv4RoutingTableEntry *route = new Ipv4RoutingTableEntry ();
  *route = Ipv4RoutingTableEntry::CreateNetworkRouteTo (network,
                                                        networkMask,
                                                        nextHop,
                                                        interface);
  m_ASexternalRoutes.push_back (route);
}

uint32_t 
Ipv4GlobalRouting::GetNRoutes (void) const
{
  //NS_LOG_FUNCTION_NOARGS ();
  uint32_t n = 0;
  n += m_hostRoutes.size ();
  n += m_networkRoutes.size ();
  n += m_ASexternalRoutes.size ();
  return n;
}

Ipv4RoutingTableEntry *
Ipv4GlobalRouting::GetRoute (uint32_t index) const
{
  //NS_LOG_FUNCTION (index);
  if (index < m_hostRoutes.size ())
    {
      uint32_t tmp = 0;
      for (HostRoutesCI i = m_hostRoutes.begin (); 
           i != m_hostRoutes.end (); 
           i++) 
        {
          if (tmp  == index)
            {
              return *i;
            }
          tmp++;
        }
    }
  index -= m_hostRoutes.size ();
  uint32_t tmp = 0;
  if (index < m_networkRoutes.size ())
    {
      for (NetworkRoutesCI j = m_networkRoutes.begin (); 
           j != m_networkRoutes.end ();
           j++)
        {
          if (tmp == index)
            {
              return *j;
            }
          tmp++;
        }
    }
  index -= m_networkRoutes.size ();
  tmp = 0;
  for (ASExternalRoutesCI k = m_ASexternalRoutes.begin (); 
       k != m_ASexternalRoutes.end (); 
       k++) 
    {
      if (tmp == index)
        {
          return *k;
        }
      tmp++;
    }
  NS_ASSERT (false);
  // quiet compiler.
  return 0;
}
void 
Ipv4GlobalRouting::RemoveRoute (uint32_t index)
{
  //NS_LOG_FUNCTION (index);
  if (index < m_hostRoutes.size ())
    {
      uint32_t tmp = 0;
      for (HostRoutesI i = m_hostRoutes.begin (); 
           i != m_hostRoutes.end (); 
           i++) 
        {
          if (tmp  == index)
            {
              //NS_LOG_LOGIC ("Removing route " << index << "; size = " << m_hostRoutes.size ());
              delete *i;
              m_hostRoutes.erase (i);
              //NS_LOG_LOGIC ("Done removing host route " << index << "; host route remaining size = " << m_hostRoutes.size ());
              return;
            }
          tmp++;
        }
    }
  index -= m_hostRoutes.size ();
  uint32_t tmp = 0;
  for (NetworkRoutesI j = m_networkRoutes.begin (); 
       j != m_networkRoutes.end (); 
       j++) 
    {
      if (tmp == index)
        {
          //NS_LOG_LOGIC ("Removing route " << index << "; size = " << m_networkRoutes.size ());
          delete *j;
          m_networkRoutes.erase (j);
          //NS_LOG_LOGIC ("Done removing network route " << index << "; network route remaining size = " << m_networkRoutes.size ());
          return;
        }
      tmp++;
    }
  index -= m_networkRoutes.size ();
  tmp = 0;
  for (ASExternalRoutesI k = m_ASexternalRoutes.begin (); 
       k != m_ASexternalRoutes.end ();
       k++)
    {
      if (tmp == index)
        {
          //NS_LOG_LOGIC ("Removing route " << index << "; size = " << m_ASexternalRoutes.size ());
          delete *k;
          m_ASexternalRoutes.erase (k);
          //NS_LOG_LOGIC ("Done removing network route " << index << "; network route remaining size = " << m_networkRoutes.size ());
          return;
        }
      tmp++;
    }
  NS_ASSERT (false);
}

void
Ipv4GlobalRouting::DoDispose (void)
{
  //NS_LOG_FUNCTION_NOARGS ();
  for (HostRoutesI i = m_hostRoutes.begin (); 
       i != m_hostRoutes.end (); 
       i = m_hostRoutes.erase (i)) 
    {
      delete (*i);
    }
  for (NetworkRoutesI j = m_networkRoutes.begin (); 
       j != m_networkRoutes.end (); 
       j = m_networkRoutes.erase (j)) 
    {
      delete (*j);
    }
  for (ASExternalRoutesI l = m_ASexternalRoutes.begin (); 
       l != m_ASexternalRoutes.end ();
       l = m_ASexternalRoutes.erase (l))
    {
      delete (*l);
    }

  Ipv4RoutingProtocol::DoDispose ();
}

// Formatted like output of "route -n" command
void
Ipv4GlobalRouting::PrintRoutingTable (Ptr<OutputStreamWrapper> stream) const
{
  std::ostream* os = stream->GetStream ();
  if (GetNRoutes () > 0)
    {
      *os << "Destination     Gateway         Genmask         Flags Metric Ref    Use Iface" << std::endl;
      for (uint32_t j = 0; j < GetNRoutes (); j++)
        {
          std::ostringstream dest, gw, mask, flags;
          Ipv4RoutingTableEntry route = GetRoute (j);
          dest << route.GetDest ();
          *os << std::setiosflags (std::ios::left) << std::setw (16) << dest.str ();
          gw << route.GetGateway ();
          *os << std::setiosflags (std::ios::left) << std::setw (16) << gw.str ();
          mask << route.GetDestNetworkMask ();
          *os << std::setiosflags (std::ios::left) << std::setw (16) << mask.str ();
          flags << "U";
          if (route.IsHost ())
            {
              flags << "H";
            }
          else if (route.IsGateway ())
            {
              flags << "G";
            }
          *os << std::setiosflags (std::ios::left) << std::setw (6) << flags.str ();
          // Metric not implemented
          *os << "-" << "      ";
          // Ref ct not implemented
          *os << "-" << "      ";
          // Use not implemented
          *os << "-" << "   ";
          if (Names::FindName (m_ipv4->GetNetDevice (route.GetInterface ())) != "")
            {
              *os << Names::FindName (m_ipv4->GetNetDevice (route.GetInterface ()));
            }
          else
            {
              *os << route.GetInterface ();
            }
          *os << std::endl;
        }
    }
}

Ptr<Ipv4Route>
Ipv4GlobalRouting::RouteOutput (Ptr<Packet> p, Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr)
{

//
// First, see if this is a multicast packet we have a route for.  If we
// have a route, then send the packet down each of the specified interfaces.
//
  if (header.GetDestination ().IsMulticast ())
    {
      //NS_LOG_LOGIC ("Multicast destination-- returning false");
      return 0; // Let other routing protocols try to handle this
    }
  // TODO:  Configurable option to enable RFC 1222 Strong End System Model
  // Right now, we will be permissive and allow a source to send us
  // a packet to one of our other interface addresses; that is, the
  // destination unicast address does not match one of the iif addresses,
  // but we check our other interfaces.  This could be an option
  // (to remove the outer loop immediately below and just check iif).
  for (uint32_t j = 0; j < m_ipv4->GetNInterfaces (); j++)
    {
      for (uint32_t i = 0; i < m_ipv4->GetNAddresses (j); i++)
        {
          Ipv4InterfaceAddress iaddr = m_ipv4->GetAddress (j, i);
          Ipv4Address addr = iaddr.GetLocal ();
          if (addr.IsEqual (header.GetDestination ()))
            {
              Ptr<Ipv4Route> route;
              route = Create<Ipv4Route>();
              route->SetDestination(header.GetDestination());
              route->SetGateway(header.GetDestination());
              Ptr<NetDevice> netdev = m_ipv4->GetNetDevice(0);
              route->SetOutputDevice(netdev);
              route->SetSource (m_ipv4->GetAddress (0, 0).GetLocal ());
              return route;
            }
        }
    }
//
// See if this is a unicast packet we have a route for.
//
  Ptr<Ipv4Route> rtentry;
  header.SetVnode(m_localVnode[header.GetDestination()]);
  StandardReceive(header.GetDestination(), header, rtentry, sockerr, 0);
  NS_LOG_LOGIC ("Unicast destination- looking up");
  return rtentry;
}

bool 
Ipv4GlobalRouting::RouteInput  (Ptr<const Packet> p, Ipv4Header &header, Ptr<const NetDevice> idev,
                                UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                                LocalDeliverCallback lcb, ErrorCallback ecb)
{ 

  NS_LOG_FUNCTION (this << p << header << header.GetSource () << header.GetDestination () << idev);
  // Check if input device supports IP
  NS_ASSERT (m_ipv4->GetInterfaceForDevice (idev) >= 0);
  uint32_t iif = m_ipv4->GetInterfaceForDevice (idev);

  if (header.GetDestination ().IsMulticast ())
    {
      //NS_LOG_LOGIC ("Multicast destination-- returning false");
      return false; // Let other routing protocols try to handle this
    }

  if (header.GetDestination ().IsBroadcast ())
    {
      NS_LOG_LOGIC ("For me (Ipv4Addr broadcast address)");
      // TODO:  Local Deliver for broadcast
      // TODO:  Forward broadcast
    }

  // TODO:  Configurable option to enable RFC 1222 Strong End System Model
  // Right now, we will be permissive and allow a source to send us
  // a packet to one of our other interface addresses; that is, the
  // destination unicast address does not match one of the iif addresses,
  // but we check our other interfaces.  This could be an option
  // (to remove the outer loop immediately below and just check iif).
  for (uint32_t j = 0; j < m_ipv4->GetNInterfaces (); j++)
    {
      for (uint32_t i = 0; i < m_ipv4->GetNAddresses (j); i++)
        {
          Ipv4InterfaceAddress iaddr = m_ipv4->GetAddress (j, i);
          Ipv4Address addr = iaddr.GetLocal ();
          if (addr.IsEqual (header.GetDestination ()))
            {
              if (j == iif)
                {
                  NS_LOG_LOGIC ("For me (destination " << addr << " match)");
                }
              else
                {
                  NS_LOG_LOGIC ("For me (destination " << addr << " match) on another interface " << header.GetDestination ());
                }
              lcb (p, header, iif);
              return true;
            }
          if (header.GetDestination ().IsEqual (iaddr.GetBroadcast ()))
            {
              NS_LOG_LOGIC ("For me (interface broadcast address)");
              lcb (p, header, iif);
              return true;
            }
          NS_LOG_LOGIC ("Address "<< addr << " not a match");
        }
    }
  // Check if input device supports IP forwarding
  if (m_ipv4->IsForwarding (iif) == false)
    {
      NS_LOG_LOGIC ("Forwarding disabled for this interface");
      ecb (p, header, Socket::ERROR_NOROUTETOHOST);
      return false;
    }

  Ipv4Address destination = header.GetDestination ();
  uint8_t vnode = header.GetVnode();
  Socket::SocketErrno error;
  Ptr<Ipv4Route> route = 0;
  NS_LOG_LOGIC ("Received for vnode = " << (uint32_t)vnode);
  if (m_vnodeState[vnode].m_directions[destination][iif] == In) {
    // This assertion is now approved
    NS_ASSERT(m_vnodeState[vnode].m_remoteSeq[destination][iif] == header.GetSeq());
    NS_LOG_LOGIC ("Received along an input port");
    StandardReceive(destination, header, route, error, iif);
    if (route != 0) {
      ucb(route, p, header);
      return true;
    }
    else {
      ecb (p, header, error);
      return false;
    }
  }
  else {
    if (m_vnodeState[vnode].m_directions[destination][iif] == Out) {
      NS_LOG_LOGIC ("Received on output port");
      if (header.GetSeq() == m_vnodeState[vnode].m_remoteSeq[destination][iif]) {
        // Send packet back (maybe)
        NS_LOG_LOGIC ("Bouncing back, header seq = "<<header.GetSeq() << " Remote = " << (uint32_t)(m_vnodeState[vnode].m_remoteSeq[destination][iif])
                      << " local = " << (uint32_t)(m_vnodeState[vnode].m_localSeq[destination][iif]));
        CreateRoutingEntry(vnode, iif, destination, header, route);
        ucb(route, p, header);
        return true;
      }
      else {
        NS_LOG_LOGIC("Reversing output to input");
        // TODO Add delay here, it is pretty easy in this case
        // ReverseOutputToInput(vnode, destination, iif);
        NS_LOG_LOGIC("Reversing output to input eventually");
        if (!m_reverseOutputToInputDelay.IsZero()) {
          Simulator::Schedule(m_reverseOutputToInputDelay, &Ipv4GlobalRouting::ReverseOutputToInput, this, vnode, destination, iif);
        }
        else {
          ReverseOutputToInput(vnode, destination, iif);
        }
        StandardReceive(destination, header, route, error, iif);
        if (route != 0) {
          ucb(route, p, header);
          return true;
        }
        else {
          ecb (p, header, error);
          return false;
        }
      }

    }
    else {
      m_vnodeState[vnode].m_directions[destination][iif] = In;
      m_vnodeState[vnode].m_remoteSeq[destination][iif] = header.GetSeq();
      NS_LOG_LOGIC ("Received on an uncategorized port");
      StandardReceive(destination, header, route, error, iif);
      if (route != 0) {
        ucb(route, p, header);
        return true;
      }
      else {
        ecb (p, header, error);
        return false;
      }
    }
  }
}

void 
Ipv4GlobalRouting::NotifyInterfaceUp (uint32_t i)
{
  //NS_LOG_FUNCTION (this << i);
  if (m_respondToInterfaceEvents && Simulator::Now ().GetSeconds () > 0)  // avoid startup events
    {
      GlobalRouteManager::DeleteGlobalRoutes ();
      GlobalRouteManager::BuildGlobalRoutingDatabase ();
      GlobalRouteManager::InitializeRoutes ();
    }
  if (Simulator::Now ().GetSeconds() > 0) {
    for (InterfacePriorities::iterator it = m_priorities.begin();
         it != m_priorities.end();
         it++) {
        Ipv4Address dest = (*it).first;
        uint32_t vnode = m_localVnode[dest];
        m_vnodeState[vnode].m_directions[dest][i] = Unknown;
        m_vnodeState[vnode].m_localSeq[dest][i] = 0;
        m_vnodeState[vnode].m_remoteSeq[dest][i] = 0;
        m_ttl[dest][i] = 0;
        m_vnodeState[vnode].m_inputs[dest].push_back(i);
        m_vnodeState[vnode].m_to_reverse[dest].push_back(i);
    }
  }
}

void 
Ipv4GlobalRouting::NotifyInterfaceDown (uint32_t i)
{
  NS_LOG_FUNCTION (this << i);
  if (m_respondToInterfaceEvents && Simulator::Now ().GetSeconds () > 0)  // avoid startup events
    {
      GlobalRouteManager::DeleteGlobalRoutes ();
      GlobalRouteManager::BuildGlobalRoutingDatabase ();
      GlobalRouteManager::InitializeRoutes ();
    }
}

void 
Ipv4GlobalRouting::NotifyAddAddress (uint32_t interface, Ipv4InterfaceAddress address)
{
  //NS_LOG_FUNCTION (this << interface << address);
  if (m_respondToInterfaceEvents && Simulator::Now ().GetSeconds () > 0)  // avoid startup events
    {
      GlobalRouteManager::DeleteGlobalRoutes ();
      GlobalRouteManager::BuildGlobalRoutingDatabase ();
      GlobalRouteManager::InitializeRoutes ();
    }
}

void 
Ipv4GlobalRouting::NotifyRemoveAddress (uint32_t interface, Ipv4InterfaceAddress address)
{
  //NS_LOG_FUNCTION (this << interface << address);
  if (m_respondToInterfaceEvents && Simulator::Now ().GetSeconds () > 0)  // avoid startup events
    {
      GlobalRouteManager::DeleteGlobalRoutes ();
      GlobalRouteManager::BuildGlobalRoutingDatabase ();
      GlobalRouteManager::InitializeRoutes ();
    }
}

void 
Ipv4GlobalRouting::SetIpv4 (Ptr<Ipv4> ipv4)
{
  //NS_LOG_FUNCTION (this << ipv4);
  NS_ASSERT (m_ipv4 == 0 && ipv4 != 0);
  m_ipv4 = ipv4;
}

// @apanda
bool
Ipv4GlobalRouting::PrimitiveAEO (Ipv4Address dest)
{
  //NS_LOG_FUNCTION (this << dest);
  m_aeoRequested[dest] = true;
  bool success = LocalLock(dest);
  //NS_LOG_LOGIC("Acquiring lock " << success);
  // NS_ASSERT_MSG(success, "Could not acquire lock");
  if (success) {
    m_aeoRequested[dest] = false;
    uint8_t newVnode = (m_localVnode[dest] + 1) % 2;
    ClearVnode(newVnode, dest);
    for (uint32_t i = 1; i < m_ipv4->GetNInterfaces(); i++) {
      if (m_vnodeState[newVnode].m_directions[dest][i] != Out) { 
        if (m_vnodeState[newVnode].m_directions[dest][i] != Dead) {  
          m_vnodeState[newVnode].m_outputs[dest].push(PriorityInterface(m_priorities[dest][i], i));
          m_vnodeState[newVnode].m_directions[dest][i] = Out;
          m_vnodeState[newVnode].m_localSeq[dest][i] = 0;
          m_vnodeState[newVnode].m_remoteSeq[dest][i] = 0;
          // Reset TTL during AEO operation, this makes sense since AEO is
          // primarily a control plane primitive, and is called in order, and
          // sets true directions
          m_ttl[dest][i] = 0;
          LocalSetRemoteVnode(dest,  i, newVnode);
        }
      }
    }
    m_localVnode[dest] = newVnode;
    LocalUnlock(dest);
    return true;
  }
  return false;
}

// @apanda
void
Ipv4GlobalRouting::SetInterfacePriority (
    Ipv4Address dest,
    uint32_t interface,
    uint32_t priority) {
  //NS_LOG_LOGIC (this << "setting interface " << interface << " priority to " << priority);
  m_priorities[dest][interface] = priority;
  m_vnodeState[0].m_prioritized_links[dest].push(PriorityInterface(priority, interface));
}

// @apanda
bool
Ipv4GlobalRouting::FindOutputPort (uint8_t vnode, Ipv4Address addr, uint32_t &link, uint32_t iif)
{
  //NS_LOG_FUNCTION (this << addr);
  if (m_vnodeState[vnode].m_outputs[addr].empty()) {
      NS_LOG_LOGIC("Outputs empty");
      return false;
  }
  do {
    PriorityInterface interface = m_vnodeState[vnode].m_outputs[addr].top();
    link = interface.second;
    if (link == iif && m_vnodeState[vnode].m_outputs[addr].size() > 1) {
      m_vnodeState[vnode].m_outputs[addr].pop();
      const PriorityInterface iface2 = m_vnodeState[vnode].m_outputs[addr].top();
      m_vnodeState[vnode].m_outputs[addr].push(interface);
      interface = iface2;
    }
    if (m_vnodeState[vnode].m_directions[addr][link] == Out && m_ipv4->GetNetDevice(link)->IsLinkUp()) {
      NS_LOG_LOGIC("Returning output link " << link << "(priority = " << interface.first << ")");
      return true;
    }
    else {
      m_vnodeState[vnode].m_outputs[addr].pop();
    }
  } while (!m_vnodeState[vnode].m_outputs[addr].empty());
  NS_LOG_LOGIC("Found no output");
  return false; 
}

// @apanda
bool
Ipv4GlobalRouting::FindHighPriorityLink(uint8_t vnode, Ipv4Address addr, uint32_t &link)
{
  //NS_LOG_FUNCTION (this << addr);
  if (m_vnodeState[vnode].m_prioritized_links[addr].empty()) {
      NS_LOG_LOGIC("No links of any sort");
      return false;
  }
  do {
    const PriorityInterface interface = m_vnodeState[vnode].m_prioritized_links[addr].top();
    link = interface.second;
    if (m_ipv4->GetNetDevice(link)->IsLinkUp()) {
      NS_LOG_LOGIC("Returning output link " << link << "(priority = " << interface.first << ")");
      return true;
    }
    else {
      m_vnodeState[vnode].m_prioritized_links[addr].pop();
    }
  } while (!m_vnodeState[vnode].m_prioritized_links[addr].empty());
  NS_LOG_LOGIC("Found no output");
  return false; 
}

// @apanda
void
Ipv4GlobalRouting::InitializeDestination (Ipv4Address dest)
{
  //NS_LOG_FUNCTION (this << dest);
  //NS_LOG_LOGIC("Initializing stuff for dest = " << dest << " at node " << m_ipv4->GetNetDevice(0)->GetNode()->GetId());
  //NS_LOG_LOGIC("Number of interfaces = " << m_ipv4->GetNInterfaces());
  if (m_vnodeState[0].m_directions.find(dest) != m_vnodeState[0].m_directions.end()) {
  }
  else {
    m_aeoRequested.insert(AeoRequests::value_type(dest, false));
    m_locks.insert(LockStatus::value_type(dest, std::vector<bool>(m_ipv4->GetNInterfaces(), false)));
    m_held.insert(LockHeld::value_type(dest, false));
    m_lockCounts.insert(LockCounts::value_type(dest, 0));
    m_localVnode.insert(VnodeNumbers::value_type(dest, 0));
    m_remoteVnode.insert(RemoteVnodeNumbers::value_type(dest, std::vector<uint8_t>(m_ipv4->GetNInterfaces(), 0)));
    m_priorities.insert(InterfacePriorities::value_type(dest, std::vector<uint32_t>(m_ipv4->GetNInterfaces(), 0)));  
    m_ttl.insert(InterfacePriorities::value_type(dest, std::vector<uint32_t>(m_ipv4->GetNInterfaces(), 0)));  
    m_heartbeatSequence.insert(CurrentSequence::value_type(dest, 0));
    m_heartbeatState.insert(HeartbeatState::value_type(dest, std::vector<bool>(m_ipv4->GetNInterfaces(), false)));
    for (int i = 0; i < 2; i++) {
      m_vnodeState[i].m_directions.insert(InterfaceDirection::value_type(dest, std::vector<LinkDirection>(m_ipv4->GetNInterfaces(), Unknown)));
      m_vnodeState[i].m_inputs.insert(DestinationListInterfaceQueues::value_type(dest, std::list<uint32_t>()));
      m_vnodeState[i].m_outputs.insert(DestinationPriorityInterfaceQueues::value_type(dest, InterfaceQueue())); 
      m_vnodeState[i].m_localSeq.insert(SequenceNumbers::value_type(dest, std::vector<uint8_t>(m_ipv4->GetNInterfaces(), 0)));
      m_vnodeState[i].m_remoteSeq.insert(SequenceNumbers::value_type(dest, std::vector<uint8_t>(m_ipv4->GetNInterfaces(), 0)));
      m_vnodeState[i].m_to_reverse.insert(DestinationListInterfaceQueues::value_type(dest, std::list<uint32_t>()));
      m_vnodeState[i].m_prioritized_links.insert(DestinationPriorityInterfaceQueues::value_type(dest, InterfaceQueue())); 
    }
  }
}

// @apanda
void 
Ipv4GlobalRouting::ReverseInputToOutput (uint8_t vnode, Ipv4Address addr, uint32_t link) 
{
  if (m_vnodeState[vnode].m_directions[addr][link] != In) {
    return;
  }
  //NS_LOG_FUNCTION (this << vnode << addr << link);
  m_reversalCallback(link, addr);
  m_ttl[addr][link]++;
  m_vnodeState[vnode].m_directions[addr][link] = Out;
  m_vnodeState[vnode].m_inputs[addr].remove(link);
  m_vnodeState[vnode].m_outputs[addr].push(PriorityInterface(m_priorities[addr][link], link));
  m_vnodeState[vnode].m_localSeq[addr][link] = ((m_vnodeState[vnode].m_localSeq[addr][link] + 1) & 0x1);
}

// @apanda
void 
Ipv4GlobalRouting::ReverseOutputToInput (uint8_t vnode, Ipv4Address addr, uint32_t link) 
{
  if (m_vnodeState[vnode].m_directions[addr][link] != Out) {
    return;
  }
  NS_ASSERT(m_vnodeState[vnode].m_directions[addr][link] == Out);
  NS_LOG_FUNCTION (this << vnode << addr << link);
  m_reversalCallback(link, addr);
  m_ttl[addr][link]++;
  m_vnodeState[vnode].m_directions[addr][link] = In;
  m_vnodeState[vnode].m_inputs[addr].push_front(link);
  m_vnodeState[vnode].m_remoteSeq[addr][link] = ((m_vnodeState[vnode].m_remoteSeq[addr][link] + 1) & 0x1);
}

// @apanda
void
Ipv4GlobalRouting::SendOnOutlink (uint8_t vnode, Ipv4Address addr, Ipv4Header& header, uint32_t link)
{
  NS_LOG_FUNCTION (this << addr);
  //NS_LOG_LOGIC("Setting sequence number to " << (uint32_t)m_vnodeState[vnode].m_localSeq[addr][link]);
  header.SetSeq(m_vnodeState[vnode].m_localSeq[addr][link]);
  //NS_LOG_LOGIC("Sequence number is " << header.GetSeq());
  //NS_LOG_LOGIC("Setting vnode number to " << (uint32_t)m_remoteVnode[addr][link]);
  header.SetVnode(m_remoteVnode[addr][link]);
}

// @apanda
void
Ipv4GlobalRouting::CreateRoutingEntry (uint8_t vnode, uint32_t link, Ipv4Address addr, Ipv4Header& header, Ptr<Ipv4Route> &route)
{
  SendOnOutlink(vnode, addr, header, link);
  route = Create<Ipv4Route>();
  route->SetDestination(header.GetDestination());
  route->SetGateway(header.GetDestination());
  Ptr<NetDevice> netdev = m_ipv4->GetNetDevice(link);
  route->SetOutputDevice(netdev);
  route->SetSource (m_ipv4->GetAddress (link, 0).GetLocal ());
}

// @apanda
void
Ipv4GlobalRouting::StandardReceive (Ipv4Address addr, Ipv4Header& header, 
                                Ptr<Ipv4Route> &route, Socket::SocketErrno &error, uint32_t iif)
{
  NS_LOG_FUNCTION (this << addr);
  route = 0;
  uint32_t link;
  uint8_t vnode = header.GetVnode();
  do {
    if (FindOutputPort(vnode, addr, link, iif)) {
      NS_LOG_LOGIC ("Choosing to use output port " << link);
      CreateRoutingEntry(vnode, link, addr, header, route);
      return;
    }
    if (m_allowReversal) {
      NS_LOG_LOGIC ("Reversing " << addr);
      ScheduleReversals(vnode, addr);
      
      if (m_vnodeState[vnode].m_outputs[addr].empty()) {
        NS_LOG_LOGIC ("Failed to find a link, so just using first high priority link " << addr);
        if (FindHighPriorityLink(vnode, addr, link)) {
          CreateRoutingEntry(vnode, link, addr, header, route);
          return;
        }
        else {
          error = Socket::ERROR_NOROUTETOHOST;
          NS_LOG_LOGIC("No path to " << addr);
          return;
        }
      }
    }
    else {
      error = Socket::ERROR_NOROUTETOHOST;
      return;
    }
  } while (!m_vnodeState[vnode].m_inputs[addr].empty() || !m_vnodeState[vnode].m_outputs[addr].empty());
}

// @apanda
void
Ipv4GlobalRouting::ScheduleReversals (uint8_t vnode, Ipv4Address addr)
{
  //NS_LOG_FUNCTION (this << addr);
  if (m_vnodeState[vnode].m_to_reverse[addr].empty()) {
    m_vnodeState[vnode].m_to_reverse[addr].insert(m_vnodeState[vnode].m_to_reverse[addr].begin(), 
                        m_vnodeState[vnode].m_inputs[addr].begin(), 
                        m_vnodeState[vnode].m_inputs[addr].end()); 
  }
  
  for (std::list<uint32_t>::iterator it = m_vnodeState[vnode].m_to_reverse[addr].begin();
      it != m_vnodeState[vnode].m_to_reverse[addr].end();
      it++) {
    // ReverseInputToOutput(vnode, addr, *it)
    NS_LOG_LOGIC("Scheduling reversal from input to output");
    if (!m_reverseInputToOutputDelay.IsZero()) {
      Simulator::Schedule(m_reverseInputToOutputDelay, &Ipv4GlobalRouting::ReverseInputToOutput, this, vnode, addr, *it);
    }
    else {
      ReverseInputToOutput(vnode, addr, *it);
    }
  }
  m_vnodeState[vnode].m_to_reverse.clear();
  m_vnodeState[vnode].m_to_reverse[addr].insert(m_vnodeState[vnode].m_to_reverse[addr].begin(), m_vnodeState[vnode].m_inputs[addr].begin(), m_vnodeState[vnode].m_inputs[addr].end()); 
}

// @apanda
void
Ipv4GlobalRouting::ClearVnode(uint8_t vnode, Ipv4Address dest)
{
  m_vnodeState[vnode].m_directions.erase(dest);
  m_vnodeState[vnode].m_directions.insert(InterfaceDirection::value_type(dest, std::vector<LinkDirection>(m_ipv4->GetNInterfaces(), Unknown)));
  m_vnodeState[vnode].m_inputs.erase(dest);
  m_vnodeState[vnode].m_inputs.insert(DestinationListInterfaceQueues::value_type(dest, std::list<uint32_t>()));
  m_vnodeState[vnode].m_outputs.erase(dest);
  m_vnodeState[vnode].m_outputs.insert(DestinationPriorityInterfaceQueues::value_type(dest, InterfaceQueue())); 
  m_vnodeState[vnode].m_localSeq.erase(dest);
  m_vnodeState[vnode].m_localSeq.insert(SequenceNumbers::value_type(dest, std::vector<uint8_t>(m_ipv4->GetNInterfaces(), 0)));
  m_vnodeState[vnode].m_remoteSeq.erase(dest);
  m_vnodeState[vnode].m_remoteSeq.insert(SequenceNumbers::value_type(dest, std::vector<uint8_t>(m_ipv4->GetNInterfaces(), 0)));
  m_vnodeState[vnode].m_to_reverse.erase(dest);
  m_vnodeState[vnode].m_to_reverse.insert(DestinationListInterfaceQueues::value_type(dest, std::list<uint32_t>()));
  m_vnodeState[vnode].m_prioritized_links.erase(dest);
  m_vnodeState[vnode].m_prioritized_links.insert(DestinationPriorityInterfaceQueues::value_type(dest, InterfaceQueue())); 
  for (uint32_t i = 1; i < m_ipv4->GetNInterfaces(); i++) {
    m_vnodeState[vnode].m_prioritized_links[dest].push(PriorityInterface(m_priorities[dest][i], i));  
  }
}

// @apanda
bool
Ipv4GlobalRouting::SimpleLock (Ipv4Address addr, uint32_t link)
{
  if (!m_held[addr]) {
    NS_ASSERT_MSG(!m_locks[addr][link], "Should not reaquire a lock");
    m_locks[addr][link] = true;
    m_lockCounts[addr] += 1;
    return true;
  }
  return false;
}

// @apanda
void
Ipv4GlobalRouting::SimpleUnlock (Ipv4Address addr, uint32_t link)
{
  NS_ASSERT_MSG(!m_held[addr], "If someone else thinks they have it, I better not hold it");
  NS_ASSERT_MSG(m_locks[addr][link], "Don't free something you don't hold");
  m_locks[addr][link] = false;
  m_lockCounts[addr] -= 1;
  if (m_aeoRequested[addr] && m_lockCounts[addr] == 0) {
    PrimitiveAEO(addr);
  }
}

// @apanda
bool 
Ipv4GlobalRouting::Lock (Ipv4Address addr, Ptr<NetDevice> link)
{
  return SimpleLock(addr, m_ipv4->GetInterfaceForDevice(link));
}

// @apanda
void 
Ipv4GlobalRouting::Unlock (Ipv4Address addr, Ptr<NetDevice> link)
{
  return SimpleUnlock(addr, m_ipv4->GetInterfaceForDevice(link));
}

//@apanda
void
Ipv4GlobalRouting::UpdateHeartbeat (uint32_t seq, Ipv4Address addr)
{
  m_heartbeatSequence[addr] = seq;
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++) {
    m_heartbeatState[addr][i] = false; 
  }
}

// @apanda
void
Ipv4GlobalRouting::CheckAndAEO (Ipv4Address addr, uint32_t iface)
{
  //NS_LOG_FUNCTION (this << addr << iface);
  if (m_heartbeatState[addr][0]) {
    NS_LOG_LOGIC("Already AEOd for this");
    // We have already AEOed, let's just get on with our life
    return;
  }

  bool seenPrevious = true;
  bool ifaceBefore = false;
  for (std::vector<uint32_t>::iterator it = m_reverseBefore[addr].begin();
       it != m_reverseBefore[addr].end(); it++) {
    ifaceBefore |= (*it == iface);
    seenPrevious &= (m_heartbeatState[addr][*it]);
  }
 NS_ASSERT_MSG(seenPrevious || ifaceBefore, "Cannot have someone later than us in the order hearbeating before us");
 if (seenPrevious) {
   m_heartbeatState[addr][0] = true;
   //NS_LOG_LOGIC("Actually reversing");
   PrimitiveAEO(addr);
 }

}

// @apanda
void
Ipv4GlobalRouting::ReceiveHeartbeat (uint32_t seq, Ipv4Address addr, Ptr<NetDevice> link)
{
  Ptr<Channel> channel = link->GetChannel();
  Ptr<NetDevice> other = (channel->GetDevice(0) == link ? channel->GetDevice(1) : channel->GetDevice(0));
  NS_ASSERT(other != link);
  Ptr<Node> otherNode = other->GetNode();
  //NS_LOG_LOGIC("Receiving a heartbeat at " <<  m_ipv4->GetNetDevice(0)->GetNode()->GetId() << " from " << otherNode->GetId() << " for " << addr);
  //NS_LOG_FUNCTION (this << seq << addr << link);
  if (seq != m_heartbeatSequence[addr]) {
    //NS_LOG_LOGIC("New heartbeat, maybe?");
    if (seq > m_heartbeatSequence[addr]) {
      UpdateHeartbeat(seq, addr);
    }
    else {
      // Spurious
      //NS_LOG_LOGIC("Discarding spurious heartbeat");
      return;
    }
  }
  m_heartbeatState[addr][m_ipv4->GetInterfaceForDevice(link)] = true;
  CheckAndAEO(addr, m_ipv4->GetInterfaceForDevice(link));
}

// @apanda
bool 
Ipv4GlobalRouting::LocalLock (Ipv4Address addr)
{
  NS_ASSERT_MSG(!m_held[addr], "No recursive locks");
  if (m_lockCounts[addr] == 0) {
    // The locking loop, we need to do this by sending data eventually
    for (uint32_t i = 1; i < m_ipv4->GetNInterfaces(); i++) {
      Ptr<NetDevice> device = m_ipv4->GetNetDevice(i);
      Ptr<Channel> channel = device->GetChannel();
      Ptr<NetDevice> other = (channel->GetDevice(0) == device ? channel->GetDevice(1) : channel->GetDevice(0));
      NS_ASSERT(other != device);
      Ptr<Node> otherNode = other->GetNode();
      Ptr<Ipv4GlobalRouting> rtr = otherNode->GetObject<GlobalRouter>()->GetRoutingProtocol();
      bool ret = rtr->Lock(addr, other);
      if (!ret) {
        for (uint32_t j = 1; j < i; j++) {
          Ptr<NetDevice> device = m_ipv4->GetNetDevice(j);
          Ptr<Channel> channel = device->GetChannel();
          Ptr<NetDevice> other = (channel->GetDevice(0) == device ? channel->GetDevice(1) : channel->GetDevice(0));
          NS_ASSERT(other != device);
          Ptr<Node> otherNode = other->GetNode();
          Ptr<Ipv4GlobalRouting> rtr = otherNode->GetObject<GlobalRouter>()->GetRoutingProtocol();
          rtr->Unlock(addr, other);
        }
        return false;
      }
    }
    m_held[addr] = true;
    return true;
  }
  return false;
}

// @apanda
void
Ipv4GlobalRouting::LocalUnlock (Ipv4Address addr)
{
  NS_ASSERT_MSG(m_held[addr], "Don't release unheld locks");
  for (uint32_t i = 1; i < m_ipv4->GetNInterfaces(); i++) {
    Ptr<NetDevice> device = m_ipv4->GetNetDevice(i);
    Ptr<Channel> channel = device->GetChannel();
    Ptr<NetDevice> other = (channel->GetDevice(0) == device ? channel->GetDevice(1) : channel->GetDevice(0));
    NS_ASSERT(other != device);
    Ptr<Node> otherNode = other->GetNode();
    Ptr<Ipv4GlobalRouting> rtr = otherNode->GetObject<GlobalRouter>()->GetRoutingProtocol();
    Simulator::ScheduleNow(&Ipv4GlobalRouting::Unlock, rtr, addr, other); //rtr->Unlock(addr, other);
  }
  m_held[addr] = false;
  for (std::vector<uint32_t>::iterator it = m_reverseAfter[addr].begin(); it != m_reverseAfter[addr].end(); it++) {
    Ptr<NetDevice> device = m_ipv4->GetNetDevice(*it);
    Ptr<Channel> channel = device->GetChannel();
    Ptr<NetDevice> other = (channel->GetDevice(0) == device ? channel->GetDevice(1) : channel->GetDevice(0));
    NS_ASSERT(other != device);
    Ptr<Node> otherNode = other->GetNode();
    Ptr<Ipv4GlobalRouting> rtr = otherNode->GetObject<GlobalRouter>()->GetRoutingProtocol();
    //NS_LOG_LOGIC("Sending heartbeat for " << addr << " to " << otherNode->GetId() << " from " <<  m_ipv4->GetNetDevice(0)->GetNode()->GetId());
    Simulator::ScheduleNow(&Ipv4GlobalRouting::ReceiveHeartbeat, rtr, m_heartbeatSequence[addr], addr, other); //rtr->ReceiveHeartbeat(addr, other);
  }
}

// @apanda
void 
Ipv4GlobalRouting::SetRemoteVnode (Ipv4Address addr, uint32_t interface, uint8_t vnode)
{
  NS_ASSERT_MSG(m_locks[addr][interface], "Don't set remote vnode without holding lock");
  m_remoteVnode[addr][interface] = vnode;
  if (m_vnodeState[m_localVnode[addr]].m_directions[addr][interface] != In) {
    m_vnodeState[m_localVnode[addr]].m_inputs[addr].push_front(interface);
  }
  m_vnodeState[m_localVnode[addr]].m_directions[addr][interface] = In;
  m_vnodeState[m_localVnode[addr]].m_localSeq[addr][interface] = 0;
  m_vnodeState[m_localVnode[addr]].m_remoteSeq[addr][interface] = 0;
}

// @apanda
void 
Ipv4GlobalRouting::SetRemoteVnode (Ipv4Address addr, Ptr<NetDevice> interface, uint8_t vnode)
{
  SetRemoteVnode(addr, m_ipv4->GetInterfaceForDevice(interface), vnode);
}

// @apanda
void 
Ipv4GlobalRouting::LocalSetRemoteVnode (Ipv4Address addr, uint32_t link, uint8_t vnode)
{
  Ptr<NetDevice> device = m_ipv4->GetNetDevice(link);
  Ptr<Channel> channel = device->GetChannel();
  Ptr<NetDevice> other = (channel->GetDevice(0) == device ? channel->GetDevice(1) : channel->GetDevice(0));
  NS_ASSERT(other != device);
  Ptr<Node> otherNode = other->GetNode();
  Ptr<Ipv4GlobalRouting> rtr = otherNode->GetObject<GlobalRouter>()->GetRoutingProtocol();
  rtr->SetRemoteVnode(addr, other, vnode);
}

// @apanda
void 
Ipv4GlobalRouting::SetReversalOrder (Ipv4Address addr, const std::list<uint32_t>& interfaces) 
{
  //NS_LOG_FUNCTION(this << addr);
  //NS_LOG_LOGIC("SetReversalOrder " << addr << " node = " << m_ipv4->GetNetDevice(0)->GetNode()->GetId());
  std::list<uint32_t>::const_iterator it;
  for (it = interfaces.begin(); 
       it != interfaces.end(); it++) {
    //NS_LOG_LOGIC("Vector " << *it);
  }

  for (it = interfaces.begin(); 
       it != interfaces.end() && (*it) != 0; it++) {
    //NS_LOG_LOGIC("Considering " << *it);
  }
  NS_ASSERT(it !=  interfaces.end());
  m_reverseBefore.insert(LinkOrder::value_type(addr, std::vector<uint32_t>(interfaces.begin(), it)));
  NS_ASSERT(*it == 0);
  it++;
  //NS_LOG_LOGIC("Now at iface = " << *it);
  if (it != interfaces.end()) {
    //NS_LOG_LOGIC("Interface = " << *it);
  }
  else {
    //NS_LOG_LOGIC("Inteface end of the line");
  }
  m_reverseAfter.insert(LinkOrder::value_type(addr, std::vector<uint32_t>(it, interfaces.end())));
}

// @apanda
void
Ipv4GlobalRouting::SendInitialHeartbeat (Ipv4Address addr)
{
  //NS_LOG_FUNCTION(this << addr);
  //NS_LOG_LOGIC("Initial heartbeat for address = " << addr << " from node = " << m_ipv4->GetNetDevice(0)->GetNode()->GetId());
  for (std::vector<uint32_t>::iterator it = m_reverseBefore[addr].begin();
       it != m_reverseBefore[addr].end(); it++) {
    //NS_LOG_LOGIC("REVERSE BEFORE NOT EMPTY: " << *it);
  }
  NS_ASSERT(m_reverseBefore[addr].empty());
  m_heartbeatSequence[addr]++;
  m_heartbeatState[addr][0] = true;
  PrimitiveAEO(addr);
}

// @apanda
void 
Ipv4GlobalRouting::AddReversalCallback (Callback<void, uint32_t, Ipv4Address> callback)
{
  m_reversalCallback.ConnectWithoutContext(callback);
}

} // namespace ns3
