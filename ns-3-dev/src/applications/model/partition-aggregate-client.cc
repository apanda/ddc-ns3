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

#include "ns3/core-module.h"
#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/traced-callback.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/tcp-socket.h"
#include "ns3/uinteger.h"
#include "ns3/log.h"
#include "ns3/inet-socket-address.h"
#include <list>
#include <iostream>
#include "partition-aggregate-client.h"
#include "partition-aggregate-server.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE ("PartitionAggregateClient");
NS_OBJECT_ENSURE_REGISTERED (PartitionAggregateClient);

TypeId PartitionAggregateClient::GetTypeId (void) 
{
  static TypeId tid = TypeId ("ns3::PartitionAggregateClient")
    .SetParent<Application> ()
    .AddConstructor<PartitionAggregateClient> ();
  return tid;
}

PartitionAggregateClient::PartitionAggregateClient ()
{
  NS_LOG_FUNCTION(this);
}

PartitionAggregateClient::~PartitionAggregateClient ()
{
  NS_LOG_FUNCTION (this);
}

void PartitionAggregateClient::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  // chain up
  Application::DoDispose ();
}

void PartitionAggregateClient::StartApplication (void)
{
  NS_LOG_FUNCTION_NOARGS ();

}

void PartitionAggregateClient::IssueRequest (Ptr<Request> request)
{
  NS_LOG_FUNCTION_NOARGS ();
  // At this point we expect a list of InetAddresses, and a translation from addresses
  // to node IDs
  for (std::list<Address>::iterator it = request->addresses.begin();
       it != request->addresses.end(); it++) {
    std::cout << "Request issued " << request->requestNumber << " " << request->client << " " << request->nodes[*it] << " " << Simulator::Now() << std::endl;
    Ptr<Socket> socket;
    socket = Socket::CreateSocket (GetNode (), TcpSocketFactory::GetTypeId());
    socket->Bind ();
    socket->Connect (*it);
    socket->SetConnectCallback (
      MakeCallback (&PartitionAggregateClient::ConnectionSucceeded, this),
      MakeCallback (&PartitionAggregateClient::ConnectionFailed, this));
    socket->SetRecvCallback (
      MakeCallback (&PartitionAggregateClient::HandleRead, this));
    request->responses[*it] = false;
    request->socketToRequest[socket] = *it;
    request->receivedSoFar[socket] = 0;
    request->startTime[socket] = Simulator::Now();
    m_requests[socket] = request;
  }
}

void PartitionAggregateClient::ConnectionClosed (Ptr<Socket> s)
{
  NS_LOG_FUNCTION_NOARGS();
  Ptr<Request> request = m_requests[s];
  uint32_t node = request->nodes[request->socketToRequest[s]];
  std::cout << "Socket closed " << GetNode()->GetId() << " " << node << " completed = " << request->responses[request->socketToRequest[s]] << std::endl;
}

void PartitionAggregateClient::ConnectionSucceeded (Ptr<Socket> s) 
{
  NS_LOG_FUNCTION_NOARGS ();
  // std::cout << "Connection succeeded " << std::endl;
  const uint32_t REQUEST_SIZE = 1; // In bytes
  Ptr<Packet> packet = Create<Packet> (REQUEST_SIZE);
  uint32_t sent = s->Send (packet);
  NS_ASSERT(sent == REQUEST_SIZE);
}

void PartitionAggregateClient::ConnectionFailed (Ptr<Socket> s) 
{
  NS_LOG_FUNCTION_NOARGS ();
  NS_LOG_LOGIC ("Connection failed");
  std::cout << "Failed to connect" << std::endl;
}

void PartitionAggregateClient::HandleRead (Ptr<Socket> s)
{
  NS_LOG_FUNCTION_NOARGS ();
  Ptr<Packet> packet;
  Ptr<Request> request = m_requests[s];
  while (packet = s->Recv()) {
    request->receivedSoFar[s] += packet->GetSize ();
    if (request->receivedSoFar[s] == PartitionAggregateServer::RESPONSE_SIZE) {
      request->responses[request->socketToRequest[s]] = true;
      uint32_t node = request->nodes[request->socketToRequest[s]];
      request->requestTime[node] = Simulator::Now() - request->startTime[s];
      std::cout << "Request fulfilled, "<< request->requestNumber << " " << request->client << " " << node << " " << request->issueTime << " " << request->requestTime[node] << std::endl;
      s->Close();
    }
  }
}

void PartitionAggregateClient::StopApplication (void)
{
}
}
