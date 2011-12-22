// -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*-
//
// Copyright (c) 2011 Regents of the University of California
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

#ifndef DDC_HEADERS_H
#define DDC_HEADERS_H

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

namespace ns3 {
// _________________________________________________________________
// |0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7|
// -----------------------------------------------------------------
// |  Packet Length                |        Seq No.                |
// -----------------------------------------------------------------
// |  Type         |  VTime        |      Message Size             |
// -----------------------------------------------------------------
// |                 Originator IP                                 |
// -----------------------------------------------------------------
// | TTL           |  Hops         |      Message Seq              |
// -----------------------------------------------------------------
class PacketHeader : public Header
{
public:
  PacketHeader ();
  virtual ~PacketHeader ();

  void SetPacketLength (uint16_t length)
  {
    m_packetLength = length;
  }
  uint16_t GetPacketLength () const
  {
    return m_packetLength;
  }

  void SetPacketSequenceNumber (uint16_t seqnum)
  {
    m_packetSequenceNumber = seqnum;
  }
  uint16_t GetPacketSequenceNumber () const
  {
    return m_packetSequenceNumber;
  }

private:
  uint16_t m_packetLength;
  uint16_t m_packetSequenceNumber;

public:
  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual void Print (std::ostream &os) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);
};

// _________________________________________________________________
// |0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7|
// -----------------------------------------------------------------
// |  Packet Length                |        Seq No.                |
// -----------------------------------------------------------------
// |  Type         |  VTime        |      Message Size             |
// -----------------------------------------------------------------
// |                 Originator IP                                 |
// -----------------------------------------------------------------
// | TTL           |  Hops         |      Message Seq              |
// -----------------------------------------------------------------
//
//
// -----------------------------------------------------------------
// | Count         |  Metric 0     |  Metric 1     | Metric 2      |
// -----------------------------------------------------------------
// |                       IP 0                                    |
// -----------------------------------------------------------------
// |                       IP 1                                    |
// -----------------------------------------------------------------
// |                       IP 2                                    |
// -----------------------------------------------------------------
// | Count         | ...                                           |
// |                          .                                    |
// |                          .                                    |
// |                          .                                    |
// -----------------------------------------------------------------
// |0 0 0 0 0 0 0 0|0 0 0 0 0 0 0 0|0 0 0 0 0 0 0 0|0 0 0 0 0 0 0 0|
// -----------------------------------------------------------------
class MessageHeader : public Header
{
public:
  enum MessageType {
    REQUEST_METRIC,
    RESPONSE_METRIC
  };

  struct MetricListEntry {
    Ipv4Address address;
    uint8_t metric;
    MetricListEntry (Ipv4Address _address, uint8_t _metric)
    {
      address = _address;
      metric = _metric;
    }
  };

  MessageHeader ();
  virtual ~MessageHeader ();

  void SetMessageType (MessageType type)
  {
    m_type = (uint8_t)type;
  }
  MessageType GetMessageType () const
  {
    return (MessageType)m_type;
  }

  void SetValidTime (uint8_t vtime)
  {
    m_vtime = vtime;
  }
  uint8_t GetValidTime () const
  {
    return m_vtime;
  }
  
  void SetOriginator (Ipv4Address originator)
  {
    m_originator = originator;
  }

  Ipv4Address GetOriginator() const
  {
    return m_originator;
  }

  void SetTTL (uint8_t ttl)
  {
    m_ttl = ttl;
  }

  void DecTTL ()
  {
    m_ttl--;
  }

  uint8_t GetTTL () const
  {
    return m_ttl;
  }

  void SetHops (uint8_t hops)
  {
    m_hops = hops;
  }

  void IncHops ()
  {
    m_hops++;
  }

  uint8_t GetHops () const
  {
    return m_hops;
  }

  void SetMessageSequence (uint16_t seq)
  {
    m_seq = seq;
  }

  uint16_t GetMessageSequence ()
  {
    return m_seq;
  }

  std::list<MetricListEntry>& GetMetricList()
  {
    return m_metrics;
  }

private:
  uint8_t m_type;
  uint8_t m_vtime;
  Ipv4Address m_originator;
  uint8_t m_ttl;
  uint8_t m_hops;
  uint16_t m_seq;
  std::list<MetricListEntry> m_metrics;
public:
  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual void Print (std::ostream &os) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);
};
}
#endif
