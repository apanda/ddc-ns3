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
//

#ifndef IPV4_GLOBAL_ROUTING_H
#define IPV4_GLOBAL_ROUTING_H

#include "ns3/sgi-hashmap.h"
#include <vector>
#include <list>
#include <map>
#include <stdint.h>
#include "ns3/ipv4-address.h"
#include "ns3/ipv4-header.h"
#include "ns3/ptr.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/random-variable.h"
#include "ns3/socket.h"
#include "ns3/timer.h"
#include "ns3/traced-value.h"
#include "ddc-headers.h"
#include "ns3/callback.h"

namespace ns3 {

class Packet;
class NetDevice;
class Ipv4Interface;
class Ipv4Address;
class Ipv4Header;
class Ipv4RoutingTableEntry;
class Ipv4MulticastRoutingTableEntry;
class Node;

/**
 * \brief Global routing protocol for IP version 4 stacks.
 *
 * In ns-3 we have the concept of a pluggable routing protocol.  Routing
 * protocols are added to a list maintained by the Ipv4L3Protocol.  Every 
 * stack gets one routing protocol for free -- the Ipv4StaticRouting routing
 * protocol is added in the constructor of the Ipv4L3Protocol (this is the 
 * piece of code that implements the functionality of the IP layer).
 *
 * As an option to running a dynamic routing protocol, a GlobalRouteManager
 * object has been created to allow users to build routes for all participating
 * nodes.  One can think of this object as a "routing oracle"; it has
 * an omniscient view of the topology, and can construct shortest path
 * routes between all pairs of nodes.  These routes must be stored 
 * somewhere in the node, so therefore this class Ipv4GlobalRouting
 * is used as one of the pluggable routing protocols.  It is kept distinct
 * from Ipv4StaticRouting because these routes may be dynamically cleared
 * and rebuilt in the middle of the simulation, while manually entered
 * routes into the Ipv4StaticRouting may need to be kept distinct.
 *
 * This class deals with Ipv4 unicast routes only.
 *
 * \see Ipv4RoutingProtocol
 * \see GlobalRouteManager
 */
class Ipv4GlobalRouting : public Ipv4RoutingProtocol
{
public:
  static TypeId GetTypeId (void);
/**
 * \brief Construct an empty Ipv4GlobalRouting routing protocol,
 *
 * The Ipv4GlobalRouting class supports host and network unicast routes.
 * This method initializes the lists containing these routes to empty.
 *
 * \see Ipv4GlobalRouting
 */
  Ipv4GlobalRouting ();
  virtual ~Ipv4GlobalRouting ();

  // These methods inherited from base class
  virtual Ptr<Ipv4Route> RouteOutput (Ptr<Packet> p, Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr);

  virtual bool RouteInput  (Ptr<const Packet> p, Ipv4Header &header, Ptr<const NetDevice> idev,
                            UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                            LocalDeliverCallback lcb, ErrorCallback ecb);
  virtual void NotifyInterfaceUp (uint32_t interface);
  virtual void NotifyInterfaceDown (uint32_t interface);
  virtual void NotifyAddAddress (uint32_t interface, Ipv4InterfaceAddress address);
  virtual void NotifyRemoveAddress (uint32_t interface, Ipv4InterfaceAddress address);
  virtual void SetIpv4 (Ptr<Ipv4> ipv4);
  virtual void PrintRoutingTable (Ptr<OutputStreamWrapper> stream) const;

/**
 * \brief Add a host route to the global routing table.
 *
 * \param dest The Ipv4Address destination for this route.
 * \param nextHop The Ipv4Address of the next hop in the route.
 * \param interface The network interface index used to send packets to the
 * destination.
 *
 * \see Ipv4Address
 */
  void AddHostRouteTo (Ipv4Address dest, 
                       Ipv4Address nextHop, 
                       uint32_t interface);
/**
 * \brief Add a host route to the global routing table.
 *
 * \param dest The Ipv4Address destination for this route.
 * \param interface The network interface index used to send packets to the
 * destination.
 *
 * \see Ipv4Address
 */
  void AddHostRouteTo (Ipv4Address dest, 
                       uint32_t interface);

/**
 * \brief Add a network route to the global routing table.
 *
 * \param network The Ipv4Address network for this route.
 * \param networkMask The Ipv4Mask to extract the network.
 * \param nextHop The next hop in the route to the destination network.
 * \param interface The network interface index used to send packets to the
 * destination.
 *
 * \see Ipv4Address
 */
  void AddNetworkRouteTo (Ipv4Address network, 
                          Ipv4Mask networkMask, 
                          Ipv4Address nextHop, 
                          uint32_t interface);

/**
 * \brief Add a network route to the global routing table.
 *
 * \param network The Ipv4Address network for this route.
 * \param networkMask The Ipv4Mask to extract the network.
 * \param interface The network interface index used to send packets to the
 * destination.
 *
 * \see Ipv4Address
 */
  void AddNetworkRouteTo (Ipv4Address network, 
                          Ipv4Mask networkMask, 
                          uint32_t interface);

/**
 * \brief Add an external route to the global routing table.
 *
 * \param network The Ipv4Address network for this route.
 * \param networkMask The Ipv4Mask to extract the network.
 * \param nextHop The next hop Ipv4Address
 * \param interface The network interface index used to send packets to the
 * destination.
 */
  void AddASExternalRouteTo (Ipv4Address network,
                             Ipv4Mask networkMask,
                             Ipv4Address nextHop,
                             uint32_t interface);

/**
 * \brief Get the number of individual unicast routes that have been added
 * to the routing table.
 *
 * \warning The default route counts as one of the routes.
 */
  uint32_t GetNRoutes (void) const;

/**
 * \brief Get a route from the global unicast routing table.
 *
 * Externally, the unicast global routing table appears simply as a table with
 * n entries.  The one subtlety of note is that if a default route has been set
 * it will appear as the zeroth entry in the table.  This means that if you
 * add only a default route, the table will have one entry that can be accessed
 * either by explicitly calling GetDefaultRoute () or by calling GetRoute (0).
 * 
 * Similarly, if the default route has been set, calling RemoveRoute (0) will
 * remove the default route.
 *
 * \param i The index (into the routing table) of the route to retrieve.  If
 * the default route has been set, it will occupy index zero.
 * \return If route is set, a pointer to that Ipv4RoutingTableEntry is returned, otherwise
 * a zero pointer is returned.
 *
 * \see Ipv4RoutingTableEntry
 * \see Ipv4GlobalRouting::RemoveRoute
 */
  Ipv4RoutingTableEntry *GetRoute (uint32_t i) const;

/**
 * \brief Remove a route from the global unicast routing table.
 *
 * Externally, the unicast global routing table appears simply as a table with
 * n entries.  The one subtlety of note is that if a default route has been set
 * it will appear as the zeroth entry in the table.  This means that if the
 * default route has been set, calling RemoveRoute (0) will remove the
 * default route.
 *
 * \param i The index (into the routing table) of the route to remove.  If
 * the default route has been set, it will occupy index zero.
 *
 * \see Ipv4RoutingTableEntry
 * \see Ipv4GlobalRouting::GetRoute
 * \see Ipv4GlobalRouting::AddRoute
 */
  void RemoveRoute (uint32_t i);

 // store all available routes that bring packets to their destination
  typedef std::vector<Ipv4RoutingTableEntry*> RouteVec_t;

  RouteVec_t FindEqualCostPaths (Ipv4Address dest, Ptr<NetDevice> oif = 0); 

  void SetDistance(Ipv4Address address, uint32_t distance);
  
  uint32_t GetDistance(Ipv4Address address);

  void SetIfaceToOutput (Ipv4Address address, uint32_t iif);

  void SetIfaceToInput (Ipv4Address address, uint32_t iif);

  void ClassifyInterfaces();

  void SetStopTime(Time time);

  typedef Callback<void> PacketDropped;
  void SetPacketDroppedCallback(PacketDropped callback);

  typedef Callback<void, uint32_t> ReceivedCallback;

  typedef Callback<void, uint32_t> VisitedCallback;
  void SetReceivedCallback(ReceivedCallback receive);
  void SetVisitedCallback(VisitedCallback visited);
  void Reset();
protected:
  void DoStart (void);
  void DoDispose (void);

private:
  /// Set to true if packets are randomly routed among ECMP; set to false for using only one route consistently
  bool m_randomEcmpRouting;
  /// Set to true if this interface should respond to interface events by globallly recomputing routes 
  bool m_respondToInterfaceEvents;
  /// A uniform random number generator for randomly routing packets among ECMP 
  UniformVariable m_rand;

  TracedValue<uint8_t> m_receivedTtl;

  enum DdcState
  {
    None,
    Input,
    ReverseInput,
    ReverseInputPrimed,
    NewInput,
    Output,
    ReverseOutput,
    Dead = 0xffff
  };

  enum DdcAction
  {
    Receive,
    NoPath,
    DetectFailure,
    Send
  };

  typedef std::list<Ipv4RoutingTableEntry *> HostRoutes;
  typedef std::list<Ipv4RoutingTableEntry *>::const_iterator HostRoutesCI;
  typedef std::list<Ipv4RoutingTableEntry *>::iterator HostRoutesI;
  typedef std::list<Ipv4RoutingTableEntry *> NetworkRoutes;
  typedef std::list<Ipv4RoutingTableEntry *>::const_iterator NetworkRoutesCI;
  typedef std::list<Ipv4RoutingTableEntry *>::iterator NetworkRoutesI;
  typedef std::list<Ipv4RoutingTableEntry *> ASExternalRoutes;
  typedef std::list<Ipv4RoutingTableEntry *>::const_iterator ASExternalRoutesCI;
  typedef std::list<Ipv4RoutingTableEntry *>::iterator ASExternalRoutesI;
  typedef sgi::hash_map<Ipv4Address, std::list<uint32_t>, Ipv4AddressHash> Interfaces;
  typedef sgi::hash_map<Ipv4Address, std::vector<DdcState>, Ipv4AddressHash> StateMachines;
  typedef sgi::hash_map<Ipv4Address, std::vector<uint8_t>, Ipv4AddressHash> SequenceNumbers;
  typedef sgi::hash_map<Ipv4Address, uint32_t, Ipv4AddressHash> DistanceMetric;
  typedef sgi::hash_map<Ipv4Address, bool, Ipv4AddressHash> LocalAddress;
  typedef sgi::hash_map<Ipv4Address, bool, Ipv4AddressHash>::iterator LocalAddressI;
  typedef sgi::hash_map<Ipv4Address, uint32_t, Ipv4AddressHash>::iterator DistanceMetricI;
  typedef std::map<Ptr<Socket>, Ipv4InterfaceAddress> SocketToAddress;
  typedef std::map<Ptr<Socket>, uint32_t> SocketToInterface;
  typedef sgi::hash_map<Ipv4Address, Ptr<Socket>, Ipv4AddressHash> AddressToSocket;
  typedef sgi::hash_map<uint32_t, Ptr<Socket> > InterfaceToSocket;
  typedef sgi::hash_map<uint32_t, bool> InterfaceStates;
  
  PacketDropped m_packetDropped;
  ReceivedCallback m_receivedCallback;
  VisitedCallback m_visitedCallback;
  StateMachines m_originalStates;
  Interfaces m_originalInputs;
  Interfaces m_originalOutputs;
  Interfaces m_inputInterfaces;
  Interfaces m_reverseInputInterfaces;
  Interfaces m_outputInterfaces;
  Interfaces m_reverseOutputInterfaces;
  Interfaces m_deadInterfaces;
  StateMachines m_stateMachines;
  DistanceMetric m_distances;
  SocketToAddress m_addressForSocket;
  AddressToSocket m_socketForAddress;
  InterfaceToSocket m_socketForInterface;
  SocketToInterface m_interfaceForSocket;
  LocalAddress m_localAddresses;
  InterfaceStates m_isInterfaceDead;
  SequenceNumbers m_remoteSeq;
  SequenceNumbers m_localSeq;
  Interfaces m_goodToReverse;
  static const uint16_t RAD_PORT = 698;
  uint16_t m_messageSequence;
  Timer m_reanimationTimer;
  Time m_simulationEndTime;

  HostRoutes m_hostRoutes;
  NetworkRoutes m_networkRoutes;
  ASExternalRoutes m_ASexternalRoutes; // External routes imported

  Ptr<Ipv4> m_ipv4;

  /// Standard methods for the Illinois algorithm
  Ptr<Ipv4Route> StandardReceive (Ipv4Header &header);
  Ptr<Ipv4Route> SendOnOutLink (uint32_t link, Ipv4Header &header);
  void ReverseOutToIn (Ipv4Address destination, uint32_t link);
  void ReverseInToOut (Ipv4Address destination, uint32_t link);
  void SetAllLinksGoodToReverse (Ipv4Address destination);
  void PopulateGoodToReverse (Ipv4Address destination);
  Ptr<Ipv4Route> TryRouteThroughOutputInterfaces (Ipv4Header &header);
  
  Ipv4Address VerifyAndUpdateAddress (Ipv4Address address);

  /// Send a message along
  void SendMessage (Ptr<Socket>& socket, MessageHeader& message);
  /// Receive a request for metric
  void ReceiveHealingRequest (uint32_t iface, MessageHeader& message);
  /// Receive a response to a previous request
  void ReceiveHealingResponse (uint32_t iface, MessageHeader& message);
  /// Given a message header, initialize the metric correctly
  void FillInMetric (MessageHeader& message);
  /// Common handling for packets
  void CommonBuildPacket (uint32_t iface, MessageHeader& message);
  /// Original routing methods for the Berkeley algorithm
  void RecvDdcHealing (Ptr<Socket> socket);

  void CheckIfLinksReanimated();
  void AdvanceStateMachine(Ipv4Address dest, uint32_t iface, DdcAction action);
  Ptr<Ipv4Route> RouteThroughDdc(const Ipv4Header &header, Ptr<NetDevice> oif, Ptr<const NetDevice> idev);
  Ptr<Ipv4Route> LookupGlobal (const Ipv4Header &header, Ptr<NetDevice> oif = 0, Ptr<const NetDevice> idev = 0);
  Ptr<Ipv4Route> TryRouteThroughInterfaces (Interfaces interfaces, Ipv4Address address);

  /// Something happened to trigger the need to heal, request metrics from our neighbor, while simultaneously
  /// sending them ours. I am mostly planning on using this sending of the metrics as a way to get both sides on
  /// the same idea about I and O. This isn't essential, but why not do it.
  void SendMetricRequest (uint32_t iface);
  // Send a response in response to a request for our metrics
  void SendMetricResponse (uint32_t iface);
};

} // Namespace ns3

#endif /* IPV4_GLOBAL_ROUTING_H */
