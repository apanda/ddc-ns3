/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007 INESC Porto
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
 * Author: Gustavo J. A. M. Carneiro  <gjc@inescporto.pt>
 */

#include "ns3/assert.h"

#include "ddc-headers.h"
#include "ns3/log.h"

#define IPV4_ADDRESS_SIZE 4
#define DDC_MSG_HEADER_SIZE 12
#define DDC_PKT_HEADER_SIZE 4

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("DdcHeader");

// ---------------- DDC Packet -------------------------------

NS_OBJECT_ENSURE_REGISTERED (PacketHeader);

PacketHeader::PacketHeader ()
{
}

PacketHeader::~PacketHeader ()
{
}

TypeId
PacketHeader::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PacketHeader")
    .SetParent<Header> ()
    .AddConstructor<PacketHeader> ()
  ;
  return tid;
}
TypeId
PacketHeader::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

uint32_t 
PacketHeader::GetSerializedSize (void) const
{
  return DDC_PKT_HEADER_SIZE;
}

void 
PacketHeader::Print (std::ostream &os) const
{
  // TODO
}

void
PacketHeader::Serialize (Buffer::Iterator start) const
{
  Buffer::Iterator i = start;
  i.WriteHtonU16 (m_packetLength);
  i.WriteHtonU16 (m_packetSequenceNumber);
}

uint32_t
PacketHeader::Deserialize (Buffer::Iterator start)
{
  Buffer::Iterator i = start;
  m_packetLength  = i.ReadNtohU16 ();
  m_packetSequenceNumber = i.ReadNtohU16 ();
  return GetSerializedSize ();
}

MessageHeader::MessageHeader ()
{
}

MessageHeader::~MessageHeader ()
{
}

TypeId
MessageHeader::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MessageHeader")
    .SetParent<Header> ()
    .AddConstructor<PacketHeader> ()
  ;
  return tid;
}
TypeId
MessageHeader::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

uint32_t 
MessageHeader::GetSerializedSize (void) const
{
  return DDC_MSG_HEADER_SIZE;
}

void 
MessageHeader::Print (std::ostream &os) const
{
  // TODO
}

void
MessageHeader::Serialize (Buffer::Iterator start) const
{
  Buffer::Iterator i = start;
  //i.WriteHtonU16 (m_packetLength);
  //i.WriteHtonU16 (m_packetSequenceNumber);
}

uint32_t
MessageHeader::Deserialize (Buffer::Iterator start)
{
  Buffer::Iterator i = start;
  //m_packetLength  = i.ReadNtohU16 ();
  //m_packetSequenceNumber = i.ReadNtohU16 ();
  return GetSerializedSize ();
}
}
