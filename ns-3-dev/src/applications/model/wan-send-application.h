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

#ifndef WAN_SEND_APPLICATION_H
#define WAN_SEND_APPLICATION_H

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/traced-callback.h"
#include <map>
#include <list>

namespace ns3 {

class Address;
class RandomVariable;
class Socket;

/**
 * \ingroup applications
 * \defgroup bulksend WanSendApplication
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
class WanSendApplication : public Application
{
public:
  static TypeId GetTypeId (void);

  WanSendApplication ();

  virtual ~WanSendApplication ();

  void IssueRequest(Address address, uint32_t node, uint64_t toSend);

protected:
  virtual void DoDispose (void);
private:
  // inherited from Application base class.
  virtual void StartApplication (void);    // Called at time specified by Start
  virtual void StopApplication (void);     // Called at time specified by Stop

  void SendData (Ptr<Socket>);

  uint32_t        m_sendSize;     // Size of data to send each time

  std::map <Address, uint32_t> m_nodes;
  std::map<Ptr<Socket>, Address> m_socketToAddress;
  std::map<Ptr<Socket>, uint64_t> m_sentSoFar;
  std::map<Ptr<Socket>, Time> m_startTime;
  std::map<uint32_t, Time> m_requestTime;
  std::map<Ptr<Socket>, uint64_t> m_maxBytes;     // Limit total number of bytes sent
  std::list<Ptr<Socket> > m_socketList; // sending sockets
  std::map<Ptr<Socket>, bool> m_fulfilled;

  TypeId          m_tid;
  TracedCallback<Ptr<const Packet> > m_txTrace;

private:
  void ConnectionSucceeded (Ptr<Socket> socket);
  void ConnectionFailed (Ptr<Socket> socket);
  void DataSend (Ptr<Socket>, uint32_t); // for socket's SetSendCallback
  void Ignore (Ptr<Socket> socket);
};

} // namespace ns3

#endif /* BULK_SEND_APPLICATION_H */
