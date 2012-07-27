#pragma once
using namespace ns3;
class Topology;
class NodeCallback : public Object {
  uint32_t m_id;
  Topology* m_topology;
public:
  NodeCallback (uint32_t id, Topology* topology) : m_id(id), m_topology(topology) {};

  void RxPacket (Ptr<const Packet> packet, Ipv4Header& header);  
  void ServerRxPacket (Ptr<const Packet> packet, Ipv4Header& header);
  void NodeReversal (uint32_t iface, Ipv4Address addr);  
  void DropTrace (const Ipv4Header& hdr, Ptr<const Packet> packet, Ipv4L3Protocol::DropReason drop, Ptr<Ipv4> ipv4, uint32_t iface);
};
