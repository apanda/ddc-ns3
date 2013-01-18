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
#include "partition-aggregate-server.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE ("PartitionAggregateServer");
NS_OBJECT_ENSURE_REGISTERED (PartitionAggregateServer);

TypeId PartitionAggregateServer::GetTypeId (void) 
{
  static TypeId tid = TypeId ("ns3::PartitionAggregateServer")
    .SetParent<Application> ()
    .AddConstructor<PartitionAggregateServer> ()
    .AddAttribute ("Port", "Port on which we listen for incoming packets.",
                   UintegerValue (5000),
                   MakeUintegerAccessor (&PartitionAggregateServer::m_port),
                   MakeUintegerChecker<uint16_t> ())
  ;
  return tid;
}

PartitionAggregateServer::PartitionAggregateServer ()
{
  NS_LOG_FUNCTION(this);
  m_socket = 0;
}

PartitionAggregateServer::~PartitionAggregateServer ()
{
  NS_LOG_FUNCTION (this);
}

void PartitionAggregateServer::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_socket = 0;

  // chain up
  Application::DoDispose ();
}

void PartitionAggregateServer::StartApplication (void)
{
  NS_LOG_FUNCTION_NOARGS ();

  if (m_socket == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::TcpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), m_port);
      m_socket->Bind (local);
      m_socket->SetAcceptCallback (
            MakeNullCallback<bool, Ptr<Socket>, const Address &> (),
            MakeCallback (&PartitionAggregateServer::HandleAccept, this));

    }
}

void PartitionAggregateServer::HandleAccept (Ptr<Socket> s, const Address& from) 
{
  NS_LOG_FUNCTION (this << s << from);
  s->SetRecvCallback (MakeCallback (&PartitionAggregateServer::HandleRead, this));
}

void PartitionAggregateServer::HandleRead (Ptr<Socket> s)
{
  
  NS_LOG_FUNCTION (this << socket);
  s->SetSendCallback(MakeCallback(&PartitionAggregateServer::SendCallback, this));
  m_remaining[s] = 0;
  SendCallback(s, 1024);
}

void PartitionAggregateServer::SendCallback (Ptr<Socket> s, uint32_t b)
{
  NS_LOG_FUNCTION (this << socket);
  Ptr<Packet> packet;
  Address from;
  while ((packet = s->RecvFrom (from)))
    {
      if (packet->GetSize () == 0)
        { //EOF
          break;
        }
    }
  uint32_t bytes = m_remaining[s];
  while (bytes < RESPONSE_SIZE)
    { // Time to send more
      uint32_t toSend = SEND_SIZE;
      // Make sure we don't send too many
      toSend = std::min (SEND_SIZE, RESPONSE_SIZE - bytes);
      Ptr<Packet> packet = Create<Packet> (toSend);
      int actual = s->Send (packet);
      if (actual > 0)
        {
          bytes += actual;
        }
      // We exit this loop when actual < toSend as the send side
      // buffer is full. The "DataSent" callback will pop when
      // some buffer space has freed ip.
      if ((unsigned)actual != toSend)
        {
          break;
        }
    }
  // Check if time to close (all sent)
  if (bytes == RESPONSE_SIZE)
    {
      s->Close ();
      m_remaining.erase(s);
    }
  else
    {
      m_remaining[s] = bytes; 
    }
}

void PartitionAggregateServer::StopApplication (void)
{
  m_socket->Close();
}
}
