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

#include <list>
#include <map>
#include <vector>
#include <utility>
#include <functional>
#include <queue>
#include <stdint.h>
#include "ns3/ipv4-address.h"
#include "ns3/ipv4-header.h"
#include "ns3/ptr.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/random-variable.h"
#include "ns3/channel.h"
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/global-router-interface.h"
#include "ns3/traced-callback.h"

namespace ns3 {

class Packet;
class NetDevice;
class Ipv4Interface;
class Ipv4Address;
class Ipv4Header;
class Ipv4RoutingTableEntry;
class Ipv4MulticastRoutingTableEntry;


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

/** 
 * @apanda
 * Primitive AEO operation, for control plane and such
 */
  bool PrimitiveAEO(Ipv4Address dest);

/**
 * @apanda
 * Set link priority, higher is better
 */
 void SetInterfacePriority(Ipv4Address dest, uint32_t interface, uint32_t priority);

/**
 * @apanda
 * Set reversal order
 */
  void SetReversalOrder (Ipv4Address, const std::list<uint32_t>&);

/**
 * @apanda
 * Send a heartbeat
 */
  void SendInitialHeartbeat (Ipv4Address);

/**
 * @apanda
 * Set reversal callback
 */
 void AddReversalCallback (Callback<void, uint32_t, Ipv4Address>);
protected:
  void DoDispose (void);

/**
 * @apanda
 * Find highest priority output port to send messages out of
 */
 bool FindOutputPort (uint8_t, Ipv4Address addr, uint32_t &link, uint32_t iif);  

/**
 * @apanda
 * Find highest priority live link
 */
 bool FindHighPriorityLink (uint8_t, Ipv4Address address, uint32_t &link);

/**
 * @apanda
 * Inititalize a bunch of data structures for a specific address
 */
 void InitializeDestination (Ipv4Address addr);

/**
 * @apanda
 * Reverse input to output
 */
  void ReverseInputToOutput (uint8_t, Ipv4Address addr, uint32_t link); 

/**
 * @apanda
 * Reverse output to input
 */
  void ReverseOutputToInput (uint8_t, Ipv4Address addr, uint32_t link); 

/**
 * @apanda
 * Send on outlink
 */
  void SendOnOutlink (uint8_t, Ipv4Address addr, Ipv4Header& header, uint32_t link);

/**
 * @apanda
 * Standard receive
 */
  void StandardReceive (Ipv4Address addr, Ipv4Header& header,
                       Ptr<Ipv4Route>& route, Socket::SocketErrno& error, uint32_t iif);

/**
 * @apanda
 * Create a generic routing entry, and prepare the header
 */
  void CreateRoutingEntry (uint8_t, uint32_t link, Ipv4Address addr, Ipv4Header& header, Ptr<Ipv4Route> &route);

/**
 * @apanda
 * Schedule reversals
 */
  void ScheduleReversals(uint8_t, Ipv4Address addr);

/**
 * @apanda
 * Acquire lock
 */
  bool SimpleLock (Ipv4Address, uint32_t);

/**
 * @apanda
 * Release lock
 */
  void SimpleUnlock (Ipv4Address, uint32_t);

/**
 * @apanda
 * Acquire lock
 */
  bool Lock (Ipv4Address, Ptr<NetDevice>);

/**
 * @apanda
 * Release lock
 */
  void Unlock (Ipv4Address, Ptr<NetDevice>);

/**
 * @apanda
 * Receive heartbeat
 */
  void ReceiveHeartbeat (uint32_t, Ipv4Address, Ptr<NetDevice>);

/**
 * @apanda
 * Update remote vnode
 */
  void SetRemoteVnode (Ipv4Address, uint32_t, uint8_t);

/**
 * @apanda
 * Update remote vnode
 */
  void SetRemoteVnode (Ipv4Address, Ptr<NetDevice>, uint8_t);

private:

/**
 * @apanda
 * Check if I should execute a PrimitiveAEO operation
 */
 void CheckAndAEO (Ipv4Address, uint32_t);

/**
 * @apanda
 *Lock locally
 */
  bool LocalLock (Ipv4Address);

/**
 *
 * @apanda
 * Unlock locally
 */
 void LocalUnlock (Ipv4Address);

/**
 *
 * @apanda
 * Local set remote vnode
 */
  void LocalSetRemoteVnode (Ipv4Address, uint32_t, uint8_t);

/**
 * @apanda
 */
  void ClearVnode(uint8_t vnode, Ipv4Address addr);

/**
 * @apanda
 * Update the sequence number for heartbeat
 */
  void UpdateHeartbeat(uint32_t, Ipv4Address);

  /// @apanda Link direction for DDC
  enum LinkDirection {
    In = 1,
    Out = 2,
    Dead = 255,
    Unknown = 0
  };
  /// Set to true if packets are randomly routed among ECMP; set to false for using only one route consistently
  bool m_randomEcmpRouting;
  /// Set to true if this interface should respond to interface events by globallly recomputing routes 
  bool m_respondToInterfaceEvents;
  /// A uniform random number generator for randomly routing packets among ECMP 
  UniformVariable m_rand;
  typedef std::list<Ipv4RoutingTableEntry *> HostRoutes;
  typedef std::list<Ipv4RoutingTableEntry *>::const_iterator HostRoutesCI;
  typedef std::list<Ipv4RoutingTableEntry *>::iterator HostRoutesI;
  typedef std::list<Ipv4RoutingTableEntry *> NetworkRoutes;
  typedef std::list<Ipv4RoutingTableEntry *>::const_iterator NetworkRoutesCI;
  typedef std::list<Ipv4RoutingTableEntry *>::iterator NetworkRoutesI;
  typedef std::list<Ipv4RoutingTableEntry *> ASExternalRoutes;
  typedef std::list<Ipv4RoutingTableEntry *>::const_iterator ASExternalRoutesCI;
  typedef std::list<Ipv4RoutingTableEntry *>::iterator ASExternalRoutesI;

  // @apanda Types
  typedef std::map<Ipv4Address, std::vector<LinkDirection> > InterfaceDirection;
  typedef std::map<Ipv4Address, std::vector<uint32_t> > InterfacePriorities;
  typedef std::pair<uint32_t, uint32_t> PriorityInterface;
  typedef std::priority_queue<PriorityInterface, std::vector<PriorityInterface>, 
            std::greater< std::vector<PriorityInterface>::value_type> > InterfaceQueue;
  typedef std::map<Ipv4Address, InterfaceQueue> DestinationPriorityInterfaceQueues;
  typedef std::map<Ipv4Address, std::list<uint32_t> > DestinationListInterfaceQueues;
  typedef std::map<Ipv4Address, std::vector<uint32_t> > InterfaceReversalTTL;
  typedef std::map<Ipv4Address, std::vector<uint8_t> > SequenceNumbers;
  typedef std::map<Ipv4Address, uint8_t> VnodeNumbers;
  typedef std::map<Ipv4Address, std::vector<uint8_t> > RemoteVnodeNumbers;
  typedef std::map<Ipv4Address, std::vector<bool> > LockStatus;
  typedef std::map<Ipv4Address, bool> LockHeld;
  typedef std::map<Ipv4Address, uint32_t> LockCounts; 
  typedef std::map<Ipv4Address, bool> AeoRequests;
  typedef std::map<Ipv4Address, std::vector<uint32_t> > LinkOrder;
  typedef std::map<Ipv4Address, std::vector<bool> > HeartbeatState;
  typedef std::map<Ipv4Address, uint32_t> CurrentSequence;

  LockStatus m_locks;
  LockCounts m_lockCounts;
  LockHeld m_held;
  VnodeNumbers m_localVnode;
  RemoteVnodeNumbers m_remoteVnode;
  InterfaceReversalTTL m_ttl;
  InterfacePriorities m_priorities;
  AeoRequests m_aeoRequested;
  LinkOrder m_reverseBefore;
  LinkOrder m_reverseAfter;
  CurrentSequence m_heartbeatSequence;
  HeartbeatState m_heartbeatState;
  bool m_allowReversal;
  struct ForwardingState {
    /// @apanda Direction vectors
    InterfaceDirection m_directions;
    /// @apanda Book keeping for the various interfaces
    DestinationListInterfaceQueues m_inputs;
    DestinationListInterfaceQueues m_to_reverse;
    DestinationPriorityInterfaceQueues m_outputs;
    SequenceNumbers m_localSeq;
    SequenceNumbers m_remoteSeq;
    DestinationPriorityInterfaceQueues m_prioritized_links;
    /// end @apanda
  };

  ForwardingState m_vnodeState[2];

  Time m_reverseInputToOutputDelay;
  Time m_reverseOutputToInputDelay;

  HostRoutes m_hostRoutes;
  NetworkRoutes m_networkRoutes;
  ASExternalRoutes m_ASexternalRoutes; // External routes imported

  Ptr<Ipv4> m_ipv4;

  TracedCallback<uint32_t, Ipv4Address> m_reversalCallback;
};

} // Namespace ns3

#endif /* IPV4_GLOBAL_ROUTING_H */
