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
#define METRIC_WORD_SIZE 4
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
  uint32_t size = DDC_MSG_HEADER_SIZE + METRIC_WORD_SIZE;
  if (m_metrics.size() != 0) {
    uint32_t clusters = ((m_metrics.size() - 1) / 3) + 1;
    size += (clusters * ((3 * IPV4_ADDRESS_SIZE) + METRIC_WORD_SIZE));
  }
  return size;
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
  i.WriteU8 (m_type);
  i.WriteU8 (m_vtime);
  i.WriteHtonU16 (GetSerializedSize());
  i.WriteHtonU32 (m_originator.Get());
  i.WriteU8 (m_ttl);
  i.WriteU8 (m_hops);
  i.WriteHtonU16 (m_seq);
  uint8_t metric[3];
  Ipv4Address address[3];
  int node = 0;
  for (int j = 0; j < 3; j++) {
    metric[j] = 0;
    address[j] = Ipv4Address ();
  }
  for (std::list<MetricListEntry>::const_iterator it = m_metrics.begin();
          it != m_metrics.end();
          it++) {
    const MetricListEntry &entry = *it;
    metric[node] = entry.metric;
    address[node] = entry.address;
    node++;
    NS_ASSERT (node <= 3);
    if (node == 3) {
      i.WriteU8(3); // Count
      i.WriteU8(metric[0]); // metric 0
      i.WriteU8(metric[1]); // metric 1
      i.WriteU8(metric[2]); // metric 2
      i.WriteHtonU32(address[0].Get());
      i.WriteHtonU32(address[1].Get());
      i.WriteHtonU32(address[2].Get());
      node = 0;
      for (int j = 0; j < 3; j++) {
        metric[j] = 0;
        address[j] = Ipv4Address ();
      }
    }
  }
  if (node != 0) {
    i.WriteU8(node);
    for (int j = 0; j < 3; j++) {
      i.WriteU8(metric[j]);
    }
    for (int j = 0; j < 3; j++) {
      i.WriteHtonU32(address[j].Get());
    }
  }
  i.WriteHtonU32(0);
}

uint32_t
MessageHeader::Deserialize (Buffer::Iterator start)
{
  Buffer::Iterator i = start;
  m_type = i.ReadU8();
  m_vtime = i.ReadU8();
  uint16_t size = i.ReadNtohU16();
  m_originator = Ipv4Address (i.ReadNtohU32());
  m_ttl = i.ReadU8();
  m_hops = i.ReadU8();
  m_seq = i.ReadNtohU16();
  size -= (DDC_MSG_HEADER_SIZE + METRIC_WORD_SIZE);
  NS_ASSERT ((size % ((3 * IPV4_ADDRESS_SIZE) + METRIC_WORD_SIZE)) == 0);
  int cluster = size / ((3 * IPV4_ADDRESS_SIZE) + METRIC_WORD_SIZE);
  for (int j = 0; j < cluster; j++) {
    uint8_t count = i.ReadU8();
    uint8_t metric[3];
    for (int ii = 0; ii < 3; ii++) {
      metric[ii] = i.ReadU8();
    }
    Ipv4Address addresses[3];
    for (int ii = 0; ii < 3; ii++) {
      addresses[ii] = Ipv4Address(i.ReadNtohU32());
    }
    for (int ii = 0; ii < count; ii++) {
      m_metrics.push_back(MetricListEntry(addresses[ii], metric[ii]));
    }
  }
  int endMarker = i.ReadNtohU32();
  NS_ASSERT(endMarker == 0);
  return GetSerializedSize ();
}
}
