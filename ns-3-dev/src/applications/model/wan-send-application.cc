/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2010 Georgia Institute of Technology
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
 * Author: George F. Riley <riley@ece.gatech.edu>
 */

#include "ns3/log.h"
#include "ns3/address.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/tcp-socket-factory.h"
#include "wan-send-application.h"

NS_LOG_COMPONENT_DEFINE ("WanSendApplication");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (WanSendApplication);

TypeId
WanSendApplication::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::WanSendApplication")
    .SetParent<Application> ()
    .AddConstructor<WanSendApplication> ()
    .AddAttribute ("SendSize", "The amount of data to send each time.",
                   UintegerValue (512),
                   MakeUintegerAccessor (&WanSendApplication::m_sendSize),
                   MakeUintegerChecker<uint32_t> (1))
    .AddAttribute ("Protocol", "The type of protocol to use.",
                   TypeIdValue (TcpSocketFactory::GetTypeId ()),
                   MakeTypeIdAccessor (&WanSendApplication::m_tid),
                   MakeTypeIdChecker ())
    .AddTraceSource ("Tx", "A new packet is created and is sent",
                     MakeTraceSourceAccessor (&WanSendApplication::m_txTrace))
  ;
  return tid;
}


WanSendApplication::WanSendApplication ()
{
  NS_LOG_FUNCTION (this);
}

WanSendApplication::~WanSendApplication ()
{
  NS_LOG_FUNCTION (this);
}

void
WanSendApplication::DoDispose (void)
{
  NS_LOG_FUNCTION (this);

  // chain up
  Application::DoDispose ();
}

// Application Methods
void WanSendApplication::StartApplication (void) // Called at time specified by Start
{
  NS_LOG_FUNCTION (this);
}

void WanSendApplication::StopApplication (void) // Called at time specified by Stop
{
  NS_LOG_FUNCTION (this);
}


// Private helpers

void WanSendApplication::SendData (Ptr<Socket> socket)
{
  uint64_t sent = m_sentSoFar[socket];
  uint64_t flowSize = m_maxBytes[socket];
  while (sent < flowSize) {
    uint32_t toSend = m_sendSize;
    Ptr<Packet> packet = Create<Packet> (toSend);
    int actual = socket->Send (packet);
    if (actual > 0)
    {
      sent += (uint64_t)actual;
    }

    if ((unsigned)actual != toSend)
    {
      break;
    }
  }

  m_sentSoFar[socket] = sent;
  if (sent >= flowSize && !m_fulfilled[socket])
  {
    socket->Close ();
    m_fulfilled[socket] = true;
    Address addr = m_socketToAddress[socket];
    std::cout << "Flow Completed " << GetNode() << " " << m_nodes[addr] << " " << (Simulator::Now() - m_startTime[socket]) << " " << flowSize << " " << sent << std::endl;
  }
}
  
void WanSendApplication::IssueRequest(Address address, uint32_t node, uint64_t toSend)
{
  NS_LOG_FUNCTION (this << address << node << toSend);
  Ptr<Socket> socket;
  socket = Socket::CreateSocket (GetNode (), TcpSocketFactory::GetTypeId());
  socket->Bind ();
  socket->Connect (address);
  socket->SetConnectCallback (
    MakeCallback (&WanSendApplication::ConnectionSucceeded, this),
    MakeCallback (&WanSendApplication::ConnectionFailed, this));
  socket->SetSendCallback (
    MakeCallback (&WanSendApplication::DataSend, this));
  m_nodes[address] = node;
  m_socketToAddress[socket] = address;
  m_sentSoFar[socket] = 0;
  m_startTime[socket] = Simulator::Now();
  m_maxBytes[socket] = toSend;
  m_fulfilled[socket] = false;
  m_socketList.push_back(socket);
  std::cout << "Flow started " << GetNode() << " " << node << " " << Simulator::Now() << " " << toSend << std::endl;
  
}

void WanSendApplication::ConnectionSucceeded (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  NS_LOG_LOGIC ("WanSendApplication Connection succeeded");
  std::cout << "Connection succeeded " << std::endl;
  SendData (socket);
}

void WanSendApplication::ConnectionFailed (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  NS_LOG_LOGIC ("WanSendApplication, Connection Failed");
}

void WanSendApplication::DataSend (Ptr<Socket> s, uint32_t)
{
  NS_LOG_FUNCTION (this);
  SendData(s);
}



} // Namespace ns3
