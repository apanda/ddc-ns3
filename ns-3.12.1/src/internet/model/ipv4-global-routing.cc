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
#include "ns3/channel.h"
#include "ns3/node.h"
#include "ns3/boolean.h"
#include "ns3/udp-socket-factory.h" 
#include "ns3/inet-socket-address.h"   
#include "ipv4-global-routing.h"
#include "global-route-manager.h"
#include "global-router-interface.h"

#define ILLINOIS 1
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
    .AddTraceSource("ReceivedTtl",
                    "TTL of packets destined for this node",
                    MakeTraceSourceAccessor (&Ipv4GlobalRouting::m_receivedTtl))
  ;
  return tid;
}

Ipv4GlobalRouting::Ipv4GlobalRouting () 
  : m_randomEcmpRouting (false),
    m_respondToInterfaceEvents (false),
    m_messageSequence (0),
    m_reanimationTimer (Timer::REMOVE_ON_DESTROY),
    m_simulationEndTime(Seconds(60.0 * 60.0 * 24 * 7))
{
  NS_LOG_FUNCTION_NOARGS ();
}

Ipv4GlobalRouting::~Ipv4GlobalRouting ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

void
Ipv4GlobalRouting::DoStart ()
{
  static const Ipv4Address loopback ("127.0.0.1");
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++) {
    Ipv4Address address = m_ipv4->GetAddress(i, 0).GetLocal();
    if (address == loopback) {
      continue;
    }
    Ptr<Socket> socket = Socket::CreateSocket (m_ipv4->GetNetDevice(i)->GetNode(), 
                                   UdpSocketFactory::GetTypeId ());
    socket->SetAllowBroadcast (true);
    socket->SetRecvCallback (MakeCallback (&Ipv4GlobalRouting::RecvDdcHealing, this));
    InetSocketAddress inetAddr (address, RAD_PORT);
    if (socket->Bind (inetAddr)) {
      NS_FATAL_ERROR("Could not bind socket");
    }
    socket->BindToNetDevice (m_ipv4->GetNetDevice(i));
    m_addressForSocket[socket] = m_ipv4->GetAddress(i, 0);
    m_socketForAddress[address] = socket;
    m_socketForInterface[i] = socket;
    m_interfaceForSocket[socket] = i;
  }
  // For the Illinois algorithm we have no healing yet
#ifndef ILLINOIS
  m_reanimationTimer.SetFunction(&Ipv4GlobalRouting::CheckIfLinksReanimated, this);
  m_reanimationTimer.Schedule (MilliSeconds(100)); 
#endif
}

void
Ipv4GlobalRouting::RecvDdcHealing (Ptr<Socket> socket)
{
  Ptr<Packet> receivedPacket;
  Address sourceAddress;
  receivedPacket = socket->RecvFrom(sourceAddress);
  
  InetSocketAddress inetSourceAddr = InetSocketAddress::ConvertFrom (sourceAddress);
  Ipv4Address senderIfaceAddr = inetSourceAddr.GetIpv4 ();
  Ipv4Address receiverIfaceAddr = m_addressForSocket[socket].GetLocal();
  uint32_t iface = m_interfaceForSocket[socket];
  NS_ASSERT (receiverIfaceAddr != Ipv4Address ());

  PacketHeader header;
  receivedPacket->RemoveHeader(header);

  MessageHeader messageHeader;
  receivedPacket->RemoveHeader(messageHeader);
  
  if (messageHeader.GetMessageType() == MessageHeader::REQUEST_METRIC) {
    /// Receive a request for metric
    ReceiveHealingRequest (iface, messageHeader);
  }
  else if (messageHeader.GetMessageType() == MessageHeader::RESPONSE_METRIC) {
    /// Receive a response to a previous request
    ReceiveHealingResponse (iface, messageHeader);
  }
}

void 
Ipv4GlobalRouting::SetDistance (Ipv4Address address, 
                                uint32_t distance)
{
  m_distances[address] = distance;
}

uint32_t
Ipv4GlobalRouting::GetDistance (Ipv4Address address)
{
  return m_distances[address];
}

void
Ipv4GlobalRouting::SetIfaceToOutput(Ipv4Address address, uint32_t iif)
{
  if (m_stateMachines[address][iif] != None && m_stateMachines[address][iif] != Dead) {
    return;
  }
  m_originalStates[address][iif] = Output;
  m_outputInterfaces[address].push_back(iif);
  m_stateMachines[address][iif] = Output;
  NS_LOG_INFO ("Setting state for " << address << " interface " << iif << " to output");
}

void
Ipv4GlobalRouting::SetIfaceToInput(Ipv4Address address, uint32_t iif)
{
  if (m_stateMachines[address][iif] != None) {
    return;
  }
  m_originalStates[address][iif] = Input;
  m_inputInterfaces[address].push_back(iif);
  m_goodToReverse[address].push_back(iif);
  m_stateMachines[address][iif] = Input;
  NS_LOG_INFO ("Setting state for " << address << " interface " << iif << " to input");
}

void 
Ipv4GlobalRouting::ClassifyInterfaces()
{
  std::vector<Ipv4Address> addresses;
  // First find all addresses
  for (DistanceMetricI it = m_distances.begin(); it != m_distances.end(); it++) {
    addresses.push_back(it->first);
    NS_LOG_INFO("Found address " << it->first);
  }
  for (int i = 0; i < (int)m_ipv4->GetNInterfaces(); i++) {
    Ptr<NetDevice> dev = m_ipv4->GetNetDevice(i);
    if (! dev->IsPointToPoint()) {
      continue;
    }
    NS_ASSERT(dev != 0);
    Ptr<Channel> channel = dev->GetChannel();
    NS_ASSERT(channel != 0);
    Ptr<NetDevice> other = 0;
    
    if (channel->GetDevice(0) == dev) {
      other = channel->GetDevice(1);
    }
    else {
      other = channel->GetDevice(0);
    }
    NS_ASSERT(other != 0);

    Ptr<Node> us = dev->GetNode();
    Ptr<Node> them = other->GetNode();
    Ptr<Ipv4GlobalRouting> otherIpv4 = them->GetObject<GlobalRouter>()->GetRoutingProtocol();
    NS_ASSERT(otherIpv4 != 0);
    for (std::vector<Ipv4Address>::iterator it = addresses.begin();
              it != addresses.end();
              it++) {
      uint32_t distance = GetDistance(*it);
      uint32_t otherDistance = otherIpv4->GetDistance(*it);
      Ipv4Address address = *it;
      //address = VerifyAndUpdateAddress(address);
      //if (address == Ipv4Address()) {
      //  continue;
      //}
      if (m_stateMachines.find(address) == m_stateMachines.end() ||
          m_stateMachines[address].size() < m_ipv4->GetNInterfaces()) {
        m_stateMachines[address] = std::vector<DdcState>(m_ipv4->GetNInterfaces());
        m_originalStates[address] = std::vector<DdcState>(m_ipv4->GetNInterfaces());
        m_goodToReverse[address] = std::list<uint32_t>();
        m_remoteSeq[address] = std::vector<uint8_t>(m_ipv4->GetNInterfaces());
        m_localSeq[address] = std::vector<uint8_t>(m_ipv4->GetNInterfaces());
        for (int ii = 0; ii < (int)m_ipv4->GetNInterfaces(); ii++) {
          m_stateMachines[address][ii] = None;
          m_originalStates[address][ii] = None;
          m_remoteSeq[address][ii] = 0;
          m_localSeq[address][ii] = 0;
        }
      }
      if (m_stateMachines[address][i] != None) {
        continue;
      }
      if (distance < otherDistance) {
        SetIfaceToInput(address, i);
      }
      else if (distance > otherDistance) {
        SetIfaceToOutput(address, i);
      }
      else {
        if (us->GetId() < them->GetId()) {
          SetIfaceToInput(address, i);
        }
        else {
          NS_ASSERT(us->GetId() > them->GetId());
          SetIfaceToOutput(address, i);
        }
      }
    }
  }
  
  for (std::vector<Ipv4Address>::iterator it = addresses.begin();
            it != addresses.end();
            it++) {
    Ipv4Address address = *it;
    address = VerifyAndUpdateAddress(address);
    if (address == Ipv4Address()) {
      continue;
    }
    NS_ASSERT((m_inputInterfaces[address].size() + m_outputInterfaces[address].size()) == m_ipv4->GetNInterfaces() - 1);
    m_originalInputs[address].insert(m_originalInputs[address].begin(), m_inputInterfaces[address].begin(), m_inputInterfaces[address].end());
    m_originalOutputs[address].insert(m_originalOutputs[address].begin(), m_outputInterfaces[address].begin(), m_outputInterfaces[address].end());
  }
  for (uint32_t j = 0; j < m_ipv4->GetNInterfaces (); j++)
  {
    for (uint32_t i = 0; i < m_ipv4->GetNAddresses (j); i++)
    {
      Ipv4InterfaceAddress iaddr = m_ipv4->GetAddress (j, i);
      Ipv4Address addr = iaddr.GetLocal ();
      m_localAddresses[addr] = true;
    }
  }
}

void
Ipv4GlobalRouting::Reset()
{
  for (StateMachines::iterator it = m_originalStates.begin();
       it != m_originalStates.end();
       it++) {
     m_stateMachines[it->first].clear();
     m_stateMachines[it->first].insert(m_stateMachines[it->first].begin(), it->second.begin(), it->second.end());
     m_inputInterfaces[it->first].clear();
     m_inputInterfaces[it->first].insert(m_inputInterfaces[it->first].begin(), m_originalInputs[it->first].begin(), m_originalInputs[it->first].end());
     m_goodToReverse[it->first].clear();
     m_goodToReverse[it->first].insert(m_goodToReverse[it->first].begin(), m_originalInputs[it->first].begin(), m_originalInputs[it->first].end());
     m_outputInterfaces[it->first].clear();
     m_outputInterfaces[it->first].insert(m_outputInterfaces[it->first].begin(), m_originalOutputs[it->first].begin(), m_originalOutputs[it->first].end());
     for (int i = 0; i < (int)m_ipv4->GetNInterfaces(); i++) {
       m_remoteSeq[it->first][i] = 0;
       m_localSeq[it->first][i] = 0;
     }
  }
}

Ipv4Address
Ipv4GlobalRouting::VerifyAndUpdateAddress (Ipv4Address address)
{
  if (m_stateMachines.find(address) != m_stateMachines.end()) {
     return address;
  }
  
  NS_LOG_LOGIC (" Number of m_networkRoutes" << m_networkRoutes.size ());
  for (NetworkRoutesI j = m_networkRoutes.begin (); 
       j != m_networkRoutes.end (); 
       j++) 
    {
      Ipv4Mask mask = (*j)->GetDestNetworkMask ();
      Ipv4Address entry = (*j)->GetDestNetwork ();
     
      if (mask.IsMatch (address, entry)) 
        {
          if (entry == Ipv4Address("0.0.0.0")) {
            if((*j)->GetGateway() != Ipv4Address() && (*j)->GetGateway() != Ipv4Address("0.0.0.0")) {
                if (m_stateMachines.find((*j)->GetGateway()) != m_stateMachines.end()) {
                  return address;
                }
                else {
                  return Ipv4Address();
                }
            }
            else {
              return Ipv4Address();
            }
          }
          else {
            NS_LOG_WARN("Returning address " << entry);
            return entry;
          }
       }
    }
  // What address?
  NS_ASSERT(false);
  return Ipv4Address();
}

void
Ipv4GlobalRouting::FillInMetric (MessageHeader& message)
{
  std::list<MessageHeader::MetricListEntry>& list = message.GetMetricList();
  for (DistanceMetricI it = m_distances.begin(); it != m_distances.end(); it++)
  {
    if (!m_outputInterfaces[it->first].empty()) {
      list.push_back(MessageHeader::MetricListEntry(it->first, it->second));
    }
    else if (m_localAddresses.find(it->first) != m_localAddresses.end()) {
      NS_ASSERT(it->second == 0);
      list.push_back(MessageHeader::MetricListEntry(it->first, it->second));
    }
    else {
      list.push_back(MessageHeader::MetricListEntry(it->first, 255));
    }
  }
  for (LocalAddressI it = m_localAddresses.begin(); it != m_localAddresses.end(); it++)
  {
    list.push_back(MessageHeader::MetricListEntry(it->first, 0));
  }
}

void Ipv4GlobalRouting::CommonBuildPacket (uint32_t iface, MessageHeader& message)
{
  Ptr<Socket> sock = m_socketForInterface[iface];
  Ipv4Address originAddress = m_ipv4->GetAddress(iface, 0).GetLocal();
  message.SetValidTime(255);
  message.SetOriginator(originAddress);
  message.SetTTL(255);
  message.SetHops(0);
  message.SetMessageSequence(m_messageSequence);
  m_messageSequence++;
  FillInMetric(message);
}

void Ipv4GlobalRouting::SendMetricRequest (uint32_t iface)
{
  Ptr<Socket> sock = m_socketForInterface[iface];
  MessageHeader message;
  message.SetMessageType(MessageHeader::REQUEST_METRIC);
  CommonBuildPacket (iface, message);
  SendMessage(sock, message);
}

void Ipv4GlobalRouting::SendMetricResponse (uint32_t iface)
{
  Ptr<Socket> sock = m_socketForInterface[iface];
  MessageHeader message;
  message.SetMessageType(MessageHeader::RESPONSE_METRIC);
  CommonBuildPacket (iface, message);
  SendMessage(sock, message);
}

void Ipv4GlobalRouting::SendMessage (Ptr<Socket>& socket, MessageHeader& message)
{
  Ptr<Packet> packet = Create<Packet>();
  packet->AddHeader(message);
  PacketHeader header;
  header.SetPacketLength(header.GetSerializedSize() + packet->GetSize ());
  packet->AddHeader(header);
  
  Ipv4Address bcastAddress = m_addressForSocket[socket].GetLocal().GetSubnetDirectedBroadcast(m_addressForSocket[socket].GetMask());
  socket->SendTo(packet, 0, InetSocketAddress(bcastAddress, RAD_PORT)); 
}

void Ipv4GlobalRouting::ReceiveHealingRequest (uint32_t iface, 
                                               MessageHeader& message)
{
  NS_LOG_FUNCTION_NOARGS ();
  SendMetricResponse (iface);
}

void Ipv4GlobalRouting::ReceiveHealingResponse (uint32_t iface,
                                                MessageHeader& message)
{
  NS_LOG_FUNCTION_NOARGS ();
  m_isInterfaceDead[iface] = true;
  std::list<MessageHeader::MetricListEntry>& metric = message.GetMetricList();
  for (std::list<MessageHeader::MetricListEntry>::iterator it = metric.begin();
       it != metric.end(); 
       it++) {
    Ipv4Address addr = it->address;
    if (m_localAddresses.find(addr) != m_localAddresses.end()) {
      continue;
    }
    switch (m_stateMachines[addr][iface]) {
      case Input: {
        m_inputInterfaces[addr].remove(iface);
        break;
      }
      case Output: {
        m_outputInterfaces[addr].remove(iface);
        break;
      }
      case ReverseInput: {
        m_reverseInputInterfaces[addr].remove(iface);
        break;
      }
      case ReverseOutput: {
        m_reverseOutputInterfaces[addr].remove(iface);
        break;
      }
      case ReverseInputPrimed: {
        m_reverseInputInterfaces[addr].remove(iface);
        break;
      }
      case NewInput: {
        m_inputInterfaces[addr].remove(iface);
        break;
      }
      case Dead:
      case None:
        break;
    }
    // 255 here always signifies infinite distance
    if (m_outputInterfaces[addr].empty() && it->metric < 255) {
      // Unconditionally make this an output port, there's nothing better we can do.
      // Also establish metric on this. In reality this might not be a great idea, given
      // that a RI port might do significantly better, but this is ok for now.
      m_stateMachines[addr][iface] = Output;
      SetDistance(addr, it->metric + 1);
      m_outputInterfaces[addr].push_back(iface);
    }
    else if (it->metric < GetDistance(addr)) {
      m_stateMachines[addr][iface] = Output;
      m_outputInterfaces[addr].push_back(iface);
      SetDistance(addr, it->metric + 1);
    }
    else if (it->metric > GetDistance(addr)) {
      m_stateMachines[addr][iface] = Input;
      m_inputInterfaces[addr].push_back(iface);
    }
    else if (addr.Get() < message.GetOriginator().Get()) {
      m_stateMachines[addr][iface] = Input;
      m_inputInterfaces[addr].push_back(iface);
    }
    else {
      m_stateMachines[addr][iface] = Output;
      m_outputInterfaces[addr].push_back(iface);
    }
  }
}

void 
Ipv4GlobalRouting::AddHostRouteTo (Ipv4Address dest, 
                                   Ipv4Address nextHop, 
                                   uint32_t interface)
{
  NS_LOG_FUNCTION (dest << nextHop << interface);
  Ipv4RoutingTableEntry *route = new Ipv4RoutingTableEntry ();
  *route = Ipv4RoutingTableEntry::CreateHostRouteTo (dest, nextHop, interface);
  m_hostRoutes.push_back (route);
  if (m_stateMachines.find(dest) == m_stateMachines.end()) {
    m_stateMachines[dest] = std::vector<DdcState>(m_ipv4->GetNInterfaces());
    m_originalStates[dest] = std::vector<DdcState>(m_ipv4->GetNInterfaces());
    m_goodToReverse[dest] = std::list<uint32_t>();
    m_remoteSeq[dest] = std::vector<uint8_t>(m_ipv4->GetNInterfaces());
    m_localSeq[dest] = std::vector<uint8_t>(m_ipv4->GetNInterfaces());
    for (int i = 0; i < (int)m_ipv4->GetNInterfaces(); i++) {
      m_stateMachines[dest][i] = None;
      m_originalStates[dest][i] = None;
      m_remoteSeq[dest][i] = 0;
      m_localSeq[dest][i] = 0;
    }
  }
  m_outputInterfaces[dest].push_back(interface);
  m_stateMachines[dest][interface] = Output;
  m_originalStates[dest][interface] = Output;
}


void 
Ipv4GlobalRouting::AddHostRouteTo (Ipv4Address dest, 
                                   uint32_t interface)
{
  NS_LOG_FUNCTION (dest << interface);
  Ipv4RoutingTableEntry *route = new Ipv4RoutingTableEntry ();
  *route = Ipv4RoutingTableEntry::CreateHostRouteTo (dest, interface);
  m_hostRoutes.push_back (route);
  if (m_stateMachines.find(dest) == m_stateMachines.end()) {
    m_stateMachines[dest] = std::vector<DdcState>(m_ipv4->GetNInterfaces());
    m_originalStates[dest] = std::vector<DdcState>(m_ipv4->GetNInterfaces());
    m_goodToReverse[dest] = std::list<uint32_t>();
    m_remoteSeq[dest] = std::vector<uint8_t>(m_ipv4->GetNInterfaces());
    m_localSeq[dest] = std::vector<uint8_t>(m_ipv4->GetNInterfaces());
    for (int i = 0; i < (int)m_ipv4->GetNInterfaces(); i++) {
      m_stateMachines[dest][i] = None;
      m_originalStates[dest][i] = None;
      m_remoteSeq[dest][i] = 0;
      m_localSeq[dest][i] = 0;
    }
  }
  m_outputInterfaces[dest].push_back(interface);
  m_stateMachines[dest][interface] = Output;
  m_originalStates[dest][interface] = Output;
}

void 
Ipv4GlobalRouting::AddNetworkRouteTo (Ipv4Address network, 
                                      Ipv4Mask networkMask, 
                                      Ipv4Address nextHop, 
                                      uint32_t interface)
{
  NS_LOG_FUNCTION (network << networkMask << nextHop << interface);
  Ipv4RoutingTableEntry *route = new Ipv4RoutingTableEntry ();
  *route = Ipv4RoutingTableEntry::CreateNetworkRouteTo (network,
                                                        networkMask,
                                                        nextHop,
                                                        interface);
  if (m_stateMachines.find(network) == m_stateMachines.end()) {
    m_stateMachines[network] = std::vector<DdcState>(m_ipv4->GetNInterfaces());
    m_originalStates[network] = std::vector<DdcState>(m_ipv4->GetNInterfaces());
    m_goodToReverse[network] = std::list<uint32_t>();
    m_remoteSeq[network] = std::vector<uint8_t>(m_ipv4->GetNInterfaces());
    m_localSeq[network] = std::vector<uint8_t>(m_ipv4->GetNInterfaces());
    for (int i = 0; i < (int)m_ipv4->GetNInterfaces(); i++) {
      m_stateMachines[network][i] = None;
      m_originalStates[network][i] = None;
      m_remoteSeq[network][i] = 0;
      m_localSeq[network][i] = 0;
    }
  }
  m_outputInterfaces[network].push_back(interface);
  m_stateMachines[network][interface] = Output;
  m_originalStates[network][interface] = Output;
  m_networkRoutes.push_back (route);
}

void 
Ipv4GlobalRouting::AddNetworkRouteTo (Ipv4Address network, 
                                      Ipv4Mask networkMask, 
                                      uint32_t interface)
{
  NS_LOG_FUNCTION (network << networkMask << interface);
  Ipv4RoutingTableEntry *route = new Ipv4RoutingTableEntry ();
  *route = Ipv4RoutingTableEntry::CreateNetworkRouteTo (network,
                                                        networkMask,
                                                        interface);
  if (m_stateMachines.find(network) == m_stateMachines.end()) {
    m_stateMachines[network] = std::vector<DdcState>(m_ipv4->GetNInterfaces());
    m_originalStates[network] = std::vector<DdcState>(m_ipv4->GetNInterfaces());
    m_goodToReverse[network] = std::list<uint32_t>();
    m_remoteSeq[network] = std::vector<uint8_t>(m_ipv4->GetNInterfaces());
    m_localSeq[network] = std::vector<uint8_t>(m_ipv4->GetNInterfaces());
    for (int i = 0; i < (int)m_ipv4->GetNInterfaces(); i++) {
      m_stateMachines[network][i] = None;
      m_originalStates[network][i] = None;
      m_remoteSeq[network][i] = 0;
      m_localSeq[network][i] = 0;
    }
  }
  m_outputInterfaces[network].push_back(interface);
  m_stateMachines[network][interface] = Output;
  m_originalStates[network][interface] = Output;
  m_networkRoutes.push_back (route);
}

void 
Ipv4GlobalRouting::AddASExternalRouteTo (Ipv4Address network, 
                                         Ipv4Mask networkMask,
                                         Ipv4Address nextHop,
                                         uint32_t interface)
{
  NS_LOG_FUNCTION (network << networkMask << nextHop);
  Ipv4RoutingTableEntry *route = new Ipv4RoutingTableEntry ();
  *route = Ipv4RoutingTableEntry::CreateNetworkRouteTo (network,
                                                        networkMask,
                                                        nextHop,
                                                        interface);
  m_ASexternalRoutes.push_back (route);
}

Ipv4GlobalRouting::RouteVec_t 
Ipv4GlobalRouting::FindEqualCostPaths (Ipv4Address dest, Ptr<NetDevice> oif)
{
  NS_LOG_FUNCTION_NOARGS ();
  NS_LOG_LOGIC ("Looking for route for destination " << dest);
  RouteVec_t allRoutes;

  NS_LOG_LOGIC ("Number of m_hostRoutes = " << m_hostRoutes.size ());
  bool foundHostRoute = false;
  for (HostRoutesCI i = m_hostRoutes.begin (); 
       i != m_hostRoutes.end (); 
       i++) 
    {
      NS_ASSERT ((*i)->IsHost ());
      if ((*i)->GetDest ().IsEqual (dest)) 
        {
          if (oif != 0)
            {
              if (oif != m_ipv4->GetNetDevice ((*i)->GetInterface ()))
                {
                  NS_LOG_LOGIC ("Not on requested interface, skipping");
                  continue;
                }
            }
          // Don't send packets on ports which are RO
          if (m_stateMachines[dest][(*i)->GetInterface()] != Output) {
            foundHostRoute = true;
            NS_LOG_LOGIC("Skipping interface " << *i << " since state is " <<m_stateMachines[dest][(*i)->GetInterface()]);
            continue;
          }
          allRoutes.push_back (*i);
          foundHostRoute = true;
          NS_LOG_LOGIC (allRoutes.size () << " Found global host route " << *i); 
          NS_LOG_LOGIC(dest << " can use " << m_outputInterfaces[dest].size() << " interfaces " << " and an additional " << m_reverseInputInterfaces[dest].size() << " RI interfaces"); 
        }
    }
  // @apanda Technically if we found a route, even if the route now involved an RO thing, we should still let
  // things be. Not doing this would mean leaving OSPF entirely at times, and we really don't want to
  // do that
  if (!foundHostRoute) // if no host route is found
    {
      NS_LOG_LOGIC (" Number of m_networkRoutes" << m_networkRoutes.size ());
      for (NetworkRoutesI j = m_networkRoutes.begin (); 
           j != m_networkRoutes.end (); 
           j++) 
        {
          Ipv4Mask mask = (*j)->GetDestNetworkMask ();
          Ipv4Address entry = (*j)->GetDestNetwork ();
          if (mask.IsMatch (dest, entry)) 
            {
              if (oif != 0)
                {
                  if (oif != m_ipv4->GetNetDevice ((*j)->GetInterface ()))
                    {
                      NS_LOG_LOGIC ("Not on requested interface, skipping");
                      continue;
                    }
                }
              allRoutes.push_back (*j);
              NS_LOG_LOGIC (allRoutes.size () << "Found global network route" << *j);
            }
        }
    }
  if (!foundHostRoute && allRoutes.size () == 0)  // consider external if no host/network found
    {
      for (ASExternalRoutesI k = m_ASexternalRoutes.begin ();
           k != m_ASexternalRoutes.end ();
           k++)
        {
          Ipv4Mask mask = (*k)->GetDestNetworkMask ();
          Ipv4Address entry = (*k)->GetDestNetwork ();
          if (mask.IsMatch (dest, entry))
            {
              NS_LOG_LOGIC ("Found external route" << *k);
              if (oif != 0)
                {
                  if (oif != m_ipv4->GetNetDevice ((*k)->GetInterface ()))
                    {
                      NS_LOG_LOGIC ("Not on requested interface, skipping");
                      continue;
                    }
                }
              allRoutes.push_back (*k);
              break;
            }
        }
    }
  return allRoutes;
}

void
Ipv4GlobalRouting::CheckIfLinksReanimated() {
  NS_LOG_FUNCTION_NOARGS();
  std::list<uint32_t> reanimate;
  for (sgi::hash_map<uint32_t, bool>::iterator it = m_isInterfaceDead.begin(); it != m_isInterfaceDead.end(); it++) {
    if (it->second) {
      Ptr<NetDevice> device = 0;
      device = m_ipv4->GetNetDevice (it->first);
      if (device->IsLinkUp()) {
        reanimate.push_back(it->first);
      }
    }
  }
  while (!reanimate.empty()) {
    uint32_t interface = reanimate.front();
    reanimate.pop_front();
    m_isInterfaceDead[interface] = false;
    SendMetricRequest(interface);
  }
  Time upStep = MilliSeconds(100);                                                                                                                                                                           
  Time tAbsolute = Simulator::Now() + upStep;                                                                                                                                                                                            
  if (tAbsolute < m_simulationEndTime) {
      m_reanimationTimer.Schedule (MilliSeconds(100)); 
  }
}

void
Ipv4GlobalRouting::SetStopTime(Time time) {
    m_simulationEndTime = time;
}

void
Ipv4GlobalRouting::AdvanceStateMachine(Ipv4Address address, uint32_t iface, DdcAction action) {
  NS_LOG_FUNCTION_NOARGS();
  switch (m_stateMachines[address][iface]) {
    case NewInput: {
      switch (action) {
        case Send:
          m_stateMachines[address][iface] = Input;
          break;
        default:
          NS_ASSERT(false);
      }
    }
    case Input: {
      switch (action) {
        case NoPath:
          m_stateMachines[address][iface] = ReverseInput;
          m_inputInterfaces[address].remove(iface);
          m_reverseInputInterfaces[address].push_back(iface);
          NS_LOG_LOGIC("Setting " << iface << " for address " << address << " to RI");
          break;
        case Receive:
          break;
        case DetectFailure:
          m_stateMachines[address][iface] = Dead;
          m_inputInterfaces[address].remove(iface);
          m_deadInterfaces[address].push_back(iface);
          m_isInterfaceDead[iface] = true;
          NS_LOG_ERROR("0 Setting " << iface << " for address " << address << " to dead");
          break;
        case Send:
          break; // Send just means sending on behalf of, an acceptable situation
        default:
          NS_ASSERT(false);
      }
      break;
    }
    case ReverseOutput: {
      switch (action) {
        case NoPath:
          break; // RouteUsingDdc takes care of this
        case Receive:
          break; // Expected
        case DetectFailure:
          m_stateMachines[address][iface] = Dead;
          m_reverseOutputInterfaces[address].remove(iface);
          m_deadInterfaces[address].push_back(iface);
          m_isInterfaceDead[iface] = true;
          NS_LOG_ERROR("1 Setting " << iface << " for address " << address << " to dead");
          break;
        case Send:
          break; 
        default:
          NS_ASSERT(false);
      }
      break;
    }
    case Output: {
      switch (action) {
        case Receive:
          m_stateMachines[address][iface] = ReverseOutput;
          m_outputInterfaces[address].remove(iface);
          m_reverseOutputInterfaces[address].push_back(iface);
          NS_LOG_LOGIC("Setting " << iface << " for address " << address << " to RO");
          break;
        case DetectFailure:
          m_stateMachines[address][iface] = Dead;
          m_outputInterfaces[address].remove(iface);
          m_deadInterfaces[address].push_back(iface);
          m_isInterfaceDead[iface] = true;
          NS_LOG_ERROR("2 Setting " << iface << " for address " << address << " to dead");
          break;
        case Send:
          break;
        case NoPath:
          NS_ASSERT(false);
        default:
          NS_ASSERT(false);
      }
      break;
    }
    case ReverseInput: {
      switch (action) {
        case Receive:
          m_stateMachines[address][iface] = ReverseInputPrimed;
          NS_LOG_LOGIC("Setting " << iface << " for address " << address << " to RI'");
          break;
        case DetectFailure:
          m_stateMachines[address][iface] = Dead;
          m_reverseInputInterfaces[address].remove(iface);
          m_deadInterfaces[address].push_back(iface);
          m_isInterfaceDead[iface] = true;
          NS_LOG_ERROR("3 Setting " << iface << " for address " << address << " to dead");
          break;
        case Send:
          break;
        case NoPath: 
          NS_ASSERT(false);
        default: 
          NS_ASSERT(false);
      }
      break;
    }
    case ReverseInputPrimed: {
      switch (action) {
        case Receive:
          m_stateMachines[address][iface] = NewInput;
          m_reverseInputInterfaces[address].remove(iface);
          m_inputInterfaces[address].push_back(iface);
          NS_LOG_LOGIC("Setting " << iface << " for address " << address << " to I from RI'");
          break;
        case DetectFailure:
          m_stateMachines[address][iface] = Dead;
          m_reverseInputInterfaces[address].remove(iface);
          m_deadInterfaces[address].push_back(iface);
          m_isInterfaceDead[iface] = true;
          NS_LOG_ERROR("4 Setting " << iface << " for address " << address << " to dead");
          break;
        case Send:
          break;
        case NoPath:
          NS_ASSERT(false);
        default:
          NS_ASSERT(false);
      }
      break;
    }
    case Dead: {
      switch (action) {
        case Receive:
        case Send:
        case NoPath:
          break;
        case DetectFailure:
          NS_LOG_ERROR("About to die iface is " << iface << " address is " << address);
          NS_ASSERT(false);
        default:
          NS_ASSERT(false);
      }
      break;
    }
    default:
      NS_ASSERT(false);
  }
}

void 
Ipv4GlobalRouting::SetAllLinksGoodToReverse (Ipv4Address destination)
{
  for (int i = 0; i < (int)m_stateMachines[destination].size(); i++) {
    if (m_stateMachines[destination][i] != Dead && 
        m_stateMachines[destination][i] != None) {
      NS_ASSERT(m_stateMachines[destination][i] == Input ||
                m_stateMachines[destination][i] == Output);
      m_goodToReverse[destination].push_back(i);
    }
  }
}

void
Ipv4GlobalRouting::PopulateGoodToReverse (Ipv4Address destination)
{
  m_goodToReverse[destination].clear();
  m_goodToReverse[destination].insert(m_goodToReverse[destination].begin(),
            m_inputInterfaces[destination].begin(),
            m_inputInterfaces[destination].end());
}

/// Standard methods for the Illinois algorithm                                                                                                                                                                                   
Ptr<Ipv4Route> 
Ipv4GlobalRouting::StandardReceive (Ipv4Header &header)
{
  Ipv4Address destination = VerifyAndUpdateAddress(header.GetDestination());
  NS_LOG_INFO("StandardReceive, destination = " << header.GetDestination());
  if (destination == Ipv4Address()) {
    return NULL;
  }
  Ptr<Ipv4Route> rtentry = 0;
  if (m_outputInterfaces[destination].empty()) {
    NS_LOG_LOGIC("Reversing because of lack of output ports");
    if (m_goodToReverse[destination].empty()) {
      SetAllLinksGoodToReverse(destination);
    }
    while (!m_goodToReverse[destination].empty()) {
      ReverseInToOut(destination, m_goodToReverse[destination].front()); 
    }
    PopulateGoodToReverse(destination);
  }
  rtentry = TryRouteThroughOutputInterfaces(header);
  if (rtentry == 0) {
    NS_ASSERT(m_outputInterfaces[destination].empty());
    if (!m_inputInterfaces[destination].empty()) {
      return StandardReceive(header);
    }
  }
  return rtentry;
}

Ptr<Ipv4Route>
Ipv4GlobalRouting::TryRouteThroughOutputInterfaces (Ipv4Header &header)
{
  Ptr<Ipv4Route> rtentry = 0;
  Ipv4Address destination = VerifyAndUpdateAddress(header.GetDestination());
  if (destination == Ipv4Address()) {
    return NULL;
  }
  while (rtentry == 0 && !m_outputInterfaces[destination].empty()) {
    rtentry = SendOnOutLink(m_outputInterfaces[destination].front(), header);
    if (rtentry == 0) {
      m_stateMachines[destination][m_outputInterfaces[destination].front()] = Dead;
      m_isInterfaceDead[m_outputInterfaces[destination].front()] = true;
      m_outputInterfaces[destination].pop_front(); 
    }
  }
  return rtentry;
}

Ptr<Ipv4Route> 
Ipv4GlobalRouting::SendOnOutLink (uint32_t link, Ipv4Header &header)
{
  Ipv4Address destination = VerifyAndUpdateAddress(header.GetDestination());
  if (destination == Ipv4Address()) {
    return NULL;
  }
  uint32_t pseq = (uint32_t)m_localSeq[destination][link];
  Ptr<NetDevice> device = m_ipv4->GetNetDevice (link);
  Ptr<Ipv4Route> rtentry = 0;
  if (device->IsLinkUp()) {
    NS_LOG_LOGIC("Setting packet sequence number (hopefully) to " << pseq);
    header.SetDdcInformation(pseq);
    rtentry = Create<Ipv4Route> ();
    rtentry->SetDestination(header.GetDestination());
    rtentry->SetGateway(destination);
    rtentry->SetOutputDevice(device);
    rtentry->SetSource (m_ipv4->GetAddress(link, 0).GetLocal());
  }
  return rtentry;
}

void 
Ipv4GlobalRouting::ReverseOutToIn (Ipv4Address dest, uint32_t link)
{
  NS_ASSERT(m_stateMachines[dest][link] == Output);
  m_stateMachines[dest][link] = Input;
  NS_LOG_LOGIC("Setting remote seq for " << dest << " link " << link << " to " << ((m_remoteSeq[dest][link] + 1) & 1));
  m_remoteSeq[dest][link] = ((m_remoteSeq[dest][link] + 1) & 1);
  m_outputInterfaces[dest].remove(link);
  m_inputInterfaces[dest].push_back(link);
}

void 
Ipv4GlobalRouting::ReverseInToOut (Ipv4Address dest, uint32_t link)
{
  NS_ASSERT(m_stateMachines[dest][link] == Input);
  m_stateMachines[dest][link] = Output;
  NS_LOG_LOGIC("Setting local seq for " << dest << " link " << link << " to " << ((m_localSeq[dest][link] + 1) & 1));
  m_localSeq[dest][link] = ((m_localSeq[dest][link] + 1) & 1);
  m_goodToReverse[dest].remove(link);
  m_inputInterfaces[dest].remove(link);
  m_outputInterfaces[dest].push_back(link);
}

Ptr<Ipv4Route>
Ipv4GlobalRouting::TryRouteThroughInterfaces (Interfaces interfaces, Ipv4Address dest)
{
  NS_LOG_FUNCTION_NOARGS();
  Ptr<Ipv4Route> rtentry = 0;
  // Route through here
  Ptr<NetDevice> device = 0;
  while (!interfaces[dest].empty()) {
    device = m_ipv4->GetNetDevice (interfaces[dest].front());
    if (device->IsLinkUp()) {
      break;
    }
    else {
      AdvanceStateMachine(dest, interfaces[dest].front(), DetectFailure);
      device = 0;
    }
    interfaces[dest].pop_front();
  }
  if (device != 0) {
    rtentry = Create<Ipv4Route> ();
    rtentry->SetDestination(dest);
    rtentry->SetGateway(dest);
    rtentry->SetOutputDevice(device);
    rtentry->SetSource (m_ipv4->GetAddress (interfaces[dest].front(), 0).GetLocal ());
    return rtentry;
  }
  return rtentry;
}

// Call this when all options to find an open O port has failed, so we can make smarter decissions
Ptr<Ipv4Route>
Ipv4GlobalRouting::RouteThroughDdc(const Ipv4Header &header, Ptr<NetDevice> oif, Ptr<const NetDevice> idev)
{
  NS_LOG_FUNCTION_NOARGS();
  Ipv4Address address = header.GetDestination();
  Ptr<Ipv4Route> rtentry = 0;
  uint32_t iif = 0xffffffff;
  if (idev != 0) {
    iif = m_ipv4->GetInterfaceForDevice (idev);
  }

  rtentry = TryRouteThroughInterfaces(m_outputInterfaces, address);
  if (rtentry != 0) {
    NS_LOG_LOGIC("Routing through an O port");
    goto AdvanceAndExit;
  }
  else if (idev == 0) {
    NS_LOG_LOGIC("Failed to send throug output");
  }
  // This is just a result of sending from this node, not a routing issue
  if (idev == 0) {
    // Trigger special processing, just doing a send, which we will really never do, so just do the same thing new input
    // would do.
    // Already tried O, let's try RI
    NS_LOG_LOGIC("Could not send message");
    NS_LOG_LOGIC("I interfaces " << m_inputInterfaces[address].size());
    NS_LOG_LOGIC("RI interfaces " << m_reverseInputInterfaces[address].size());
    NS_LOG_LOGIC("O interfaces " << m_outputInterfaces[address].size());
    NS_LOG_LOGIC("RO interfaces " << m_reverseOutputInterfaces[address].size());
    NS_LOG_LOGIC("Total interfaces " << m_ipv4->GetNInterfaces());
    rtentry = TryRouteThroughInterfaces(m_reverseInputInterfaces, address);
    if (rtentry != 0) {
      NS_LOG_LOGIC("Sending along an RI path");
      goto AdvanceAndExit;
    }
    if (!m_reverseOutputInterfaces[address].empty()) {
      // Make all RO ports O ports
      // send to one of them
      while (!m_reverseOutputInterfaces[address].empty()) {
        uint32_t iface = m_reverseOutputInterfaces[address].front();
        m_reverseOutputInterfaces[address].pop_front();
        m_stateMachines[address][iface] = Output;
        m_outputInterfaces[address].push_back(iface);
        NS_LOG_LOGIC("Setting " << iface << " for address " << address << " to O");
      }
      rtentry = TryRouteThroughInterfaces(m_outputInterfaces, address);
      if (rtentry != 0) {
         NS_LOG_LOGIC("Sending along O");
         goto AdvanceAndExit;
      }
    }
    // Set all I -> RI, send to one of those
    NS_LOG_LOGIC("Setting all "  << m_inputInterfaces[address].size() <<  " I interfaces to RI");
    while (!m_inputInterfaces[address].empty()) {
      uint32_t iface = m_inputInterfaces[address].front();
      m_inputInterfaces[address].pop_front();
      m_stateMachines[address][iface] = ReverseInput;
      m_reverseInputInterfaces[address].push_back(iface);
      NS_LOG_LOGIC("Setting " << iface << " for address " << address << " to RI");
    }
    rtentry = TryRouteThroughInterfaces(m_reverseInputInterfaces, address);
    if (rtentry != 0) {
      NS_LOG_LOGIC("Sending along some new RI paths");
    }
    goto AdvanceAndExit;
  }
  switch (m_stateMachines[address][iif]) {
    case Dead:
    case Input: {
      AdvanceStateMachine(address, iif, NoPath);
      if (idev->IsLinkUp()) {
        rtentry = Create<Ipv4Route> ();
        rtentry->SetDestination (address);
        rtentry->SetGateway(header.GetSource());
        rtentry->SetOutputDevice(m_ipv4->GetNetDevice (iif));
        rtentry->SetSource (m_ipv4->GetAddress (iif, 0).GetLocal ());
        NS_LOG_LOGIC("Bouncing packet because input didn't find path");
        break;
      }
      else {
        NS_LOG_LOGIC("incoming interface is dead");
        AdvanceStateMachine(address, iif, DetectFailure);
        idev = 0;
        // At this point we know there are no O, so let's try setting all I to RI, and routing that way
        NS_LOG_LOGIC("Setting all I interfaces to RI");
        while (!m_inputInterfaces[address].empty()) {
          uint32_t iface = m_inputInterfaces[address].front();
          m_inputInterfaces[address].pop_front();
          m_stateMachines[address][iface] = ReverseInput;
          m_reverseInputInterfaces[address].push_back(iface);
          NS_LOG_LOGIC("Setting " << iface << " for address " << address << " to RI");
        }
        rtentry = TryRouteThroughInterfaces(m_reverseInputInterfaces, address);
        if (rtentry != 0) {
          NS_LOG_LOGIC("Sending along RI");
          break;
        }
        
        NS_LOG_LOGIC("Setting all RO interfaces to O");
        // OK, no O ports, no RI ports, we are in the only RO state
        while (!m_reverseOutputInterfaces[address].empty()) {
          uint32_t iface = m_reverseOutputInterfaces[address].front();
          m_reverseOutputInterfaces[address].pop_front();
          m_stateMachines[address][iface] = Output;
          m_outputInterfaces[address].push_back(iface);
          NS_LOG_LOGIC("Setting " << iface << " for address " << address << " to O");
        }
        rtentry = TryRouteThroughInterfaces(m_outputInterfaces, address);
        if (rtentry != 0) {
          NS_LOG_LOGIC("Sending along O");
        }
        break;
      }
    }
    case Output: {
      NS_ASSERT(false); // We have already advanced state machine for receives at this point, we cannot really
      // be in output and receive a packet
      break;
    }
    case ReverseOutput: {
      // At this point we know there are no O, so let's try setting all I to RI, and routing that way
      NS_LOG_LOGIC("Setting all I interfaces to RI");
      while (!m_inputInterfaces[address].empty()) {
        uint32_t iface = m_inputInterfaces[address].front();
        m_inputInterfaces[address].pop_front();
        m_stateMachines[address][iface] = ReverseInput;
        m_reverseInputInterfaces[address].push_back(iface);
        NS_LOG_LOGIC("Setting " << iface << " for address " << address << " to RI");
      }
      rtentry = TryRouteThroughInterfaces(m_reverseInputInterfaces, address);
      if (rtentry != 0) {
        NS_LOG_LOGIC("Sending along RI");
        break;
      }
      
      NS_LOG_LOGIC("Setting all RO interfaces to O");
      // OK, no O ports, no RI ports, we are in the only RO state
      while (!m_reverseOutputInterfaces[address].empty()) {
        uint32_t iface = m_reverseOutputInterfaces[address].front();
        m_reverseOutputInterfaces[address].pop_front();
        m_stateMachines[address][iface] = Output;
        m_outputInterfaces[address].push_back(iface);
        NS_LOG_LOGIC("Setting " << iface << " for address " << address << " to O");
      }
      if (idev->IsLinkUp()) {
        // Bounce packet back to sender
        rtentry = Create<Ipv4Route> ();
        rtentry->SetDestination (address);
        rtentry->SetGateway(header.GetSource());
        rtentry->SetOutputDevice(m_ipv4->GetNetDevice (iif));
        rtentry->SetSource (m_ipv4->GetAddress (iif, 0).GetLocal ());
        NS_LOG_LOGIC("Bouncing packet because RO logic changed to O");
        break;
      }
      else {
        rtentry = TryRouteThroughInterfaces(m_outputInterfaces, address);
        if (rtentry != 0) {
          NS_LOG_LOGIC("Sending along O");
        }
        break;
      }
    }
    case ReverseInput: {
      NS_ASSERT(false); // Must have made it to at least RI'
      break;
    }
    case NewInput: {
      AdvanceStateMachine(address, iif, Send);
      // Already tried O, let's try RI
      rtentry = TryRouteThroughInterfaces(m_reverseInputInterfaces, address);
      if (rtentry != 0) {
        NS_LOG_LOGIC("Sending along an RI path");
        break;
      }
      if (!m_reverseOutputInterfaces[address].empty()) {
        // Make all RO ports O ports
        // send to one of them
        while (!m_reverseOutputInterfaces[address].empty()) {
          uint32_t iface = m_reverseOutputInterfaces[address].front();
          m_reverseOutputInterfaces[address].pop_front();
          m_stateMachines[address][iface] = Output;
          m_outputInterfaces[address].push_back(iface);
          NS_LOG_LOGIC("Setting " << iface << " for address " << address << " to O");
        }
        rtentry = TryRouteThroughInterfaces(m_outputInterfaces, address);
        if (rtentry != 0) {
           NS_LOG_LOGIC("Sending along O");
           break;
        }
      }
      // Set all I -> RI, bounce back
      NS_LOG_LOGIC("Setting all I interfaces to RI");
      while (!m_inputInterfaces[address].empty()) {
        uint32_t iface = m_inputInterfaces[address].front();
        m_inputInterfaces[address].pop_front();
        m_stateMachines[address][iface] = ReverseInput;
        m_reverseInputInterfaces[address].push_back(iface);
        NS_LOG_LOGIC("Setting " << iface << " for address " << address << " to RI");
      }
      NS_ASSERT(m_stateMachines[address][iif] == ReverseInput);
      // Bounce packet back to sender
      rtentry = Create<Ipv4Route> ();
      rtentry->SetDestination (address);
      rtentry->SetGateway(header.GetSource());
      rtentry->SetOutputDevice(m_ipv4->GetNetDevice (iif));
      rtentry->SetSource (m_ipv4->GetAddress (iif, 0).GetLocal ());
      NS_LOG_LOGIC("Bouncing packet due to lack of paths for new I");
      break;
    }
    case ReverseInputPrimed: {
      NS_ASSERT(false); // We should never ever get here, LookupGlobal already bounced the packet
      break;
    }
    default:
      NS_ASSERT(false);
  }

AdvanceAndExit:
  if (idev != 0) {
    AdvanceStateMachine(address, iif, Send);
  }
  return rtentry;
}

Ptr<Ipv4Route>
Ipv4GlobalRouting::LookupGlobal (const Ipv4Header &header, Ptr<NetDevice> oif, Ptr<const NetDevice> idev)
{
  Ipv4RoutingTableEntry* route;
  Ipv4Address dest = VerifyAndUpdateAddress(header.GetDestination());
  uint32_t iif = 0;
  Ptr<Ipv4Route> rtentry = 0;
  if (idev != 0) {
    iif = m_ipv4->GetInterfaceForDevice (idev);
    // We run state machine on receive before getting here, hence we shouldn't get here
    // with RI ever. This is however a soft dependency at them moment
    NS_ASSERT(m_stateMachines[dest][iif] != ReverseInput);
    if (m_stateMachines[dest][iif] == ReverseInputPrimed) {
      rtentry = Create<Ipv4Route> ();
      rtentry->SetDestination (dest);
      rtentry->SetGateway(header.GetSource());
      rtentry->SetOutputDevice(m_ipv4->GetNetDevice (iif));
      rtentry->SetSource (m_ipv4->GetAddress (iif, 0).GetLocal ());
      AdvanceStateMachine(dest, iif, Send);
      NS_LOG_LOGIC("Bouncing packet");
      return rtentry;
    }
  }
  else {
    NS_LOG_INFO("LookupGlobal from RouteOutput");
  }
  Ipv4GlobalRouting::RouteVec_t allRoutes = FindEqualCostPaths(dest, oif);
  // pick up one of the routes uniformly at random if random
  // ECMP routing is enabled, or always select the first route
  // consistently if random ECMP routing is disabled
  uint32_t selectIndex;
  while (!allRoutes.empty()) {
    if (m_randomEcmpRouting)
      {
        selectIndex = m_rand.GetInteger (0, allRoutes.size ()-1);
      }
    else 
      {
        selectIndex = 0;
      }
    uint32_t interface = allRoutes.at (selectIndex)->GetInterface();
    Ptr<NetDevice> device = m_ipv4->GetNetDevice (interface);
    if (device->IsLinkUp()) {
        break;
    }
    else {
        // Remove the interface as a valid output interface, it is clearly down
        AdvanceStateMachine(dest, interface, DetectFailure);
        allRoutes.erase(allRoutes.begin() + selectIndex);
    }
  }
  if (allRoutes.empty()) {
    // Couldn't find an output port,
    return RouteThroughDdc(header, oif, idev);
  }
  else {
    if (idev != 0) {
      AdvanceStateMachine(dest, iif, Send);
    }
    route = allRoutes.at (selectIndex);
    // create a Ipv4Route object from the selected routing table entry
    rtentry = Create<Ipv4Route> ();
    rtentry->SetDestination (route->GetDest ());
    // XXX handle multi-address case
    rtentry->SetSource (m_ipv4->GetAddress (route->GetInterface (), 0).GetLocal ());
    rtentry->SetGateway (route->GetGateway ());
    uint32_t interfaceIdx = route->GetInterface ();
    rtentry->SetOutputDevice (m_ipv4->GetNetDevice (interfaceIdx));
    return rtentry;
  }
}

uint32_t 
Ipv4GlobalRouting::GetNRoutes (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  uint32_t n = 0;
  n += m_hostRoutes.size ();
  n += m_networkRoutes.size ();
  n += m_ASexternalRoutes.size ();
  return n;
}

Ipv4RoutingTableEntry *
Ipv4GlobalRouting::GetRoute (uint32_t index) const
{
  NS_LOG_FUNCTION (index);
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
  NS_LOG_FUNCTION (index);
  if (index < m_hostRoutes.size ())
    {
      uint32_t tmp = 0;
      for (HostRoutesI i = m_hostRoutes.begin (); 
           i != m_hostRoutes.end (); 
           i++) 
        {
          if (tmp  == index)
            {
              NS_LOG_LOGIC ("Removing route " << index << "; size = " << m_hostRoutes.size ());
              delete *i;
              m_hostRoutes.erase (i);
              NS_LOG_LOGIC ("Done removing host route " << index << "; host route remaining size = " << m_hostRoutes.size ());
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
          NS_LOG_LOGIC ("Removing route " << index << "; size = " << m_networkRoutes.size ());
          delete *j;
          m_networkRoutes.erase (j);
          NS_LOG_LOGIC ("Done removing network route " << index << "; network route remaining size = " << m_networkRoutes.size ());

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
          NS_LOG_LOGIC ("Removing route " << index << "; size = " << m_ASexternalRoutes.size ());
          delete *k;
          m_ASexternalRoutes.erase (k);
          NS_LOG_LOGIC ("Done removing network route " << index << "; network route remaining size = " << m_networkRoutes.size ());
          return;
        }
      tmp++;
    }
  NS_ASSERT (false);
}

void
Ipv4GlobalRouting::DoDispose (void)
{
  NS_LOG_FUNCTION_NOARGS ();
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
  
  for (SocketToAddress::iterator it = m_addressForSocket.begin();
       it != m_addressForSocket.end();
       it++)
    {
      it->first->Close();
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
              NS_LOG_LOGIC("Sending message to self");
              sockerr = Socket::ERROR_NOTERROR;
              Ptr<NetDevice> device = m_ipv4->GetNetDevice (0);
              Ptr<Ipv4Route> rtentry = Create<Ipv4Route> ();
              rtentry->SetDestination(header.GetDestination());
              rtentry->SetGateway(header.GetDestination());
              rtentry->SetOutputDevice(device);
              rtentry->SetSource (m_ipv4->GetAddress (1, 0).GetLocal ());
              if (!m_receivedCallback.IsNull()) {
                m_receivedCallback(device->GetNode()->GetId());
              }
              return rtentry;
            }
        }
    }
//
// First, see if this is a multicast packet we have a route for.  If we
// have a route, then send the packet down each of the specified interfaces.
//
  if (header.GetDestination ().IsMulticast ())
    {
      NS_LOG_LOGIC ("Multicast destination-- returning false");
      return 0; // Let other routing protocols try to handle this
    }
//
// See if this is a unicast packet we have a route for.
//
#ifndef ILLINOIS
  NS_LOG_LOGIC ("Unicast destination- looking up using DDC");
  Ptr<Ipv4Route> rtentry = LookupGlobal (header, oif);
#else
  if (!m_visitedCallback.IsNull()) {
    m_visitedCallback(m_ipv4->GetNetDevice(1)->GetNode()->GetId());
  }
  NS_LOG_LOGIC("Unicast destination- looking up using PRVLDDC");
  Ptr<Ipv4Route> rtentry = StandardReceive(header);
#endif

  if (rtentry)
    {
      sockerr = Socket::ERROR_NOTERROR;
    }
  else
    {
      sockerr = Socket::ERROR_NOROUTETOHOST;
      NS_LOG_ERROR ("IP dropping packet no route found to " << header.GetDestination());
      NS_LOG_ERROR ("   Dead interface " << m_deadInterfaces[header.GetDestination()].size());
      NS_LOG_ERROR ("   Total interfaces " << m_stateMachines[header.GetDestination()].size());
      for (uint32_t iif = 0; iif < m_stateMachines[header.GetDestination()].size(); iif++) {
        NS_LOG_ERROR("State for " << iif << " = " << m_stateMachines[header.GetDestination()][iif]);
      }
      if (!m_packetDropped.IsNull()) {
        m_packetDropped();
      }
    }
  return rtentry;
}

void 
Ipv4GlobalRouting::SetPacketDroppedCallback(Ipv4GlobalRouting::PacketDropped callback)
{
  m_packetDropped = callback;
}

void 
Ipv4GlobalRouting::SetReceivedCallback(ReceivedCallback receive)
{
  m_receivedCallback = receive;
}

void 
Ipv4GlobalRouting::SetVisitedCallback(VisitedCallback visited)
{
  m_visitedCallback = visited;
}

bool 
Ipv4GlobalRouting::RouteInput  (Ptr<const Packet> p, Ipv4Header &header, Ptr<const NetDevice> idev, UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                                LocalDeliverCallback lcb, ErrorCallback ecb)
{ 
  lcb = MakeNullCallback<void, Ptr<const Packet>, const Ipv4Header&, uint32_t>();
  NS_LOG_FUNCTION (this << p << header << header.GetSource () << header.GetDestination () << idev);
  // Check if input device supports IP
  if (header.GetTtl() == 1) {
    NS_LOG_WARN("About to drop because of TTL");
    if (!m_packetDropped.IsNull()) {
      m_packetDropped();
    }
    else {
        std::cout << "Dropping no way to report"<<std::endl;
    }
  }
  NS_ASSERT (m_ipv4->GetInterfaceForDevice (idev) >= 0);
  uint32_t iif = m_ipv4->GetInterfaceForDevice (idev);
  if (header.GetDestination ().IsMulticast ())
    {
      NS_LOG_LOGIC ("Multicast destination-- returning false");
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
              m_receivedTtl = header.GetTtl();
              if (!m_receivedCallback.IsNull()) {
                m_receivedCallback(idev->GetNode()->GetId());
              }
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
  
  //CheckIfLinksReanimated(header.GetDestination());
#ifndef ILLINOIS
  AdvanceStateMachine(header.GetDestination(), iif, Receive);
  // Next, try to find a route
  NS_LOG_LOGIC ("Unicast destination- looking up global route using DDC");
  Ptr<Ipv4Route> rtentry = LookupGlobal (header, 0, idev);
#else

  if (!m_visitedCallback.IsNull()) {
    m_visitedCallback(idev->GetNode()->GetId());
  }
  NS_LOG_LOGIC ("Unicast destination- looking up global route using PRVLDDC");
  Ipv4Address destination = VerifyAndUpdateAddress(header.GetDestination());
  Ptr<Ipv4Route> rtentry = 0;
  if (destination == Ipv4Address()) {
    return rtentry;
  }
  if (m_stateMachines[destination].size() < m_ipv4->GetNInterfaces()) {
    return rtentry;
  }
  NS_ASSERT (m_stateMachines[destination][iif] == Dead ||
            m_stateMachines[destination][iif] == Input ||
            m_stateMachines[destination][iif] == Output); 
  if (m_stateMachines[destination][iif] == Input) {
    NS_LOG_LOGIC(" header information = " << header.GetDdcInformation() <<
                 " m_remoteSeq information = " << (uint32_t)m_remoteSeq[destination][iif]);
    NS_ASSERT((header.GetDdcInformation() & 0x1) == (m_remoteSeq[destination][iif] & 0x1));
    rtentry = StandardReceive(header);
  }
  else if (m_stateMachines[destination][iif] == Output) {
    if ((header.GetDdcInformation() & 0x1) == (m_remoteSeq[destination][iif] & 0x1)) {
      rtentry = SendOnOutLink(iif, header);
      if (rtentry == 0) {
        rtentry = StandardReceive(header);
      }
    }
    else {
      NS_LOG_LOGIC("Reversing because of input on out port");
      ReverseOutToIn(destination, iif);
      rtentry = StandardReceive(header);
    }
  }
#endif
  if (rtentry != 0)
    {
      NS_LOG_LOGIC ("Found unicast destination- calling unicast callback, header.destination " << header.GetDestination());
      NS_LOG_LOGIC("Current ddc information is " << header.GetDdcInformation());
      ucb (rtentry, p, header);
      return true;
    }
  else
    {
      NS_LOG_ERROR ("IP dropping packet no route found to " << header.GetDestination());
      if (!m_packetDropped.IsNull()) {
        m_packetDropped();
      }
      return false; // Let other routing protocols try to handle this
                    // route request.
    }
}
void 
Ipv4GlobalRouting::NotifyInterfaceUp (uint32_t i)
{
  NS_LOG_ERROR("Notifying interface up");
  NS_LOG_FUNCTION (this << i);
  if (m_respondToInterfaceEvents && Simulator::Now ().GetSeconds () > 0)  // avoid startup events
    {
      GlobalRouteManager::DeleteGlobalRoutes ();
      GlobalRouteManager::BuildGlobalRoutingDatabase ();
      GlobalRouteManager::InitializeRoutes ();
    }
}

void 
Ipv4GlobalRouting::NotifyInterfaceDown (uint32_t i)
{
  NS_LOG_ERROR("Notifying interface down");
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
  NS_LOG_FUNCTION (this << interface << address);
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
  NS_LOG_FUNCTION (this << interface << address);
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
  NS_LOG_FUNCTION (this << ipv4);
  NS_ASSERT (m_ipv4 == 0 && ipv4 != 0);
  m_ipv4 = ipv4;
}


} // namespace ns3
