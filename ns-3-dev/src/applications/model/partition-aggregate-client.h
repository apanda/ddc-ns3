/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2013 Regents of the University of California Berkeley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Aurojit Panda <apanda@cs.berkeley.edu>
 */

#ifndef PARTITION_AGGREGATE_CLIENT_H
#define PARTITION_AGGREGATE_CLIENT_H

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/traced-callback.h"
#include "ns3/inet-socket-address.h"
#include "ns3/nstime.h"
#include <list>
#include <map>

namespace ns3 {
class Address;
class Socket;

struct Request : public Object
{
  uint32_t client;
  uint32_t requestNumber;
  Time issueTime;
  std::list<Address> addresses;
  std::map <Address, bool> responses;
  std::map <Address, uint32_t> nodes;
  std::map<Ptr<Socket>, Address> socketToRequest;
  std::map<Ptr<Socket>, uint32_t> receivedSoFar;
  std::map<Ptr<Socket>, Time> startTime;
  std::map<uint32_t, Time> requestTime;
};

/**
 * \ingroup applications
 * \defgroup partitionaggregate PartitionAggregateClient
 *
 * This traffic generator simply sends data
 * as fast as possible up to MaxBytes or until
 * the appplication is stopped if MaxBytes is
 * zero. Once the lower layer send buffer is
 * filled, it waits until space is free to
 * send more data, essentially keeping a
 * constant flow of data. Only SOCK_STREAM 
 * and SOCK_SEQPACKET sockets are supported. 
 * For example, TCP sockets can be used, but 
 * UDP sockets can not be used.
 */
class PartitionAggregateClient : public Application
{
public:
  static TypeId GetTypeId (void);

  PartitionAggregateClient ();

  virtual ~PartitionAggregateClient ();

  void IssueRequest (Ptr<Request> request);

protected:
  virtual void DoDispose (void);
private:
  // inherited from Application base class.
  virtual void StartApplication (void);    // Called at time specified by Start
  virtual void StopApplication (void);     // Called at time specified by Stop
  // Ptr<Socket> m_socket;
  std::list<Ptr<Socket> > m_socketList; //the accepted sockets
  std::map<Ptr<Socket>, Ptr<Request> > m_requests; //Map socket to request

private:
  void ConnectionSucceeded (Ptr<Socket>);
  void ConnectionFailed (Ptr<Socket>);
  void HandleRead (Ptr<Socket>);
  void ConnectionClosed (Ptr<Socket>);
};
} // namespace ns3
#endif // ifndef PARTITION_AGGREGATE_SERVER_H
