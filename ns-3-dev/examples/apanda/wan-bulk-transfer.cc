/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/random-variable.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/global-route-manager-impl.h"
#include "ns3/partition-aggregate-client.h"
#include "boost/algorithm/string.hpp"
#include "ns3/data-rate.h"
#include "ns3/inet-socket-address.h"
#include <list>
#include <vector>
#include <stack>
#include <algorithm>
#include <iostream>
#include <map>
#include <fstream>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <functional>
#include "stretch-classes.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("DDC-NSDI-WAN-BULK-TRANSFER");
static const uint32_t PORT = 22;

class Topology : public Object
{
  protected:
    std::map<uint32_t, WanSendApplication*> m_partClients;
    std::vector<PointToPointChannel* > m_channels;
    UniformVariable randVar;
    uint32_t m_numNodes;
    std::vector<std::list<uint32_t>*> m_connectivityGraph;
    Time m_simulationEnd;
    std::vector<uint32_t> m_hosts;
    std::vector<uint32_t> m_nodeTranslate;
    std::map<std::pair<uint32_t, uint32_t>, PointToPointChannel*> m_channelMap;
    std::map<uint32_t, uint32_t> m_nodeForwardTranslationMap;
    std::map<Ipv4Address, uint32_t> m_addressToNodeMap; 
    NodeContainer m_nodes;
    std::vector<NetDeviceContainer> m_nodeDevices;
    std::vector<NetDeviceContainer> m_linkDevices;
    std::vector<NodeCallback> m_callbacks;
    std::vector<std::pair<uint32_t, std::list<uint32_t> > > m_pathsToTest;
    uint32_t m_currentPath;
    uint32_t m_currentTrial;
    double m_delay;
    double m_linkLatency;
    bool m_repair;
    bool m_fail;
  public:
    void SetRepair (bool repair)
    {
      m_repair = repair;
    }

    void SetFail (bool fail)
    {
      m_fail = fail;
    }
    
    void SetPropagationDelay (double latency) 
    {
      m_linkLatency = latency;
    }

    void RetransmitCallback (std::string path, bool fastRetransmit)
    {
      std::cout << path << "," << (fastRetransmit ? "FR" : "T") << std::endl;
    }

    void ReceivePacketCallback (Ptr<const Packet> packet, const Address& addr)
    {
      Ipv4Address ipAddr = InetSocketAddress::ConvertFrom(addr).GetIpv4();
      std::cout << m_addressToNodeMap[ipAddr] << ",RX," << Simulator::Now().ToDouble(Time::US) << "," << packet->GetSize() << std::endl;
    }

    inline uint32_t CannonicalNode (const uint32_t node) 
    {
      return m_nodeTranslate[node];
    }

    inline uint32_t PhysicalNode (const uint32_t node)
    {
      return m_nodeForwardTranslationMap[node];
    }

    inline uint32_t AddressForNode (const Ipv4Address address) 
    {
      return m_addressToNodeMap[address];
    }

    void SetDelay(double delay)
    {
      m_delay = delay;
    }

    void RouteEnded ()
    {
      NS_ASSERT(false);
      NS_LOG_LOGIC("Packet ended");
      m_currentTrial++;
    }
    
    Topology()
    {
      m_numNodes = 0;
      m_currentPath = 0;
      m_currentTrial = 0;
      m_simulationEnd = Seconds(60.0 * 60.0 * 24 * 100);
      m_linkLatency = 10.0;
      m_repair = true;
   }
   
    virtual ~Topology()
    {
        for (uint32_t i = 0; i < m_numNodes; i++) {
            delete m_connectivityGraph[i];
        }
    }
    
    void PopulateGraph (const std::string& filename)
    {
      std::map<uint32_t, std::list<uint32_t>*> tempConnectivityGraph;
      NS_LOG_INFO("Entering PopulateGraph with file " << filename);
      std::ifstream topology(filename.c_str());
      NS_ASSERT(topology.is_open());
      //boost::split(linkParts, links, boost::is_any_of(","));
      while (topology.good()) {
        std::string input;
        getline(topology, input);
        std::vector<std::string> topoParts;
        boost::split(topoParts, input, boost::is_any_of(" "));
        if (topoParts.size() < 2) {
          continue;
        }
        uint32_t node1 = std::atoi(topoParts[0].c_str());
        uint32_t node2 = std::atoi(topoParts[1].c_str());
        bool isHost = topoParts.size() > 2 && (topoParts[2].compare("h") == 0);
        if (!tempConnectivityGraph[node1]) {
          tempConnectivityGraph[node1] = new std::list<uint32_t>;
        }
        if (!tempConnectivityGraph[node2]) {
          tempConnectivityGraph[node2] = new std::list<uint32_t>;
        }
        tempConnectivityGraph[node1]->push_back(node2);
        tempConnectivityGraph[node2]->push_back(node1);
        if (isHost) {
          m_hosts.push_back(node2);
        }
      }

      m_numNodes = tempConnectivityGraph.size();
      uint32_t last = 0;
      m_connectivityGraph.resize(m_numNodes);
    
      for (std::map<uint32_t, std::list<uint32_t>*>::iterator it = tempConnectivityGraph.begin();
          it != tempConnectivityGraph.end();
          it++) {
          m_nodeTranslate.push_back(it->first);
          m_nodeForwardTranslationMap[it->first] = last;
          NS_LOG_LOGIC("TRANSLATION: " << it->first << " = " << last);
          last++;
      }
      for (uint32_t i = 0; i < m_numNodes; i++) {
        m_connectivityGraph[i] = new std::list<uint32_t>;
        for (std::list<uint32_t>::iterator it = tempConnectivityGraph[m_nodeTranslate[i]]->begin();
             it != tempConnectivityGraph[m_nodeTranslate[i]]->end();
             it++) {
          NS_LOG_LOGIC("Link " << m_nodeTranslate[i] << " " << *it << " translated " << i << " " << m_nodeForwardTranslationMap[*it]);
          m_connectivityGraph[i]->push_back(m_nodeForwardTranslationMap[*it]);
        }
        NS_ASSERT_MSG(!m_connectivityGraph[i]->empty(), "Empty for " << i);
      }
    }

    void HookupSimulation()
    {
      NS_LOG_INFO("Creating nodes");
      m_nodes.Create (m_numNodes);
      m_nodeDevices.resize(m_numNodes);
      NS_LOG_INFO("Creating point to point connections");
      PointToPointHelper pointToPoint;
      pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("1Gbps"));
      std::cout << "Latency = " << Time::FromDouble(m_linkLatency, Time::MS).GetMicroSeconds() << " us" << std::endl;
      pointToPoint.SetChannelAttribute("Delay", TimeValue(Time::FromDouble(m_linkLatency, Time::MS))); 
      Config::SetDefault ("ns3::RttEstimator::MinRTO", TimeValue(MilliSeconds(11)));
      Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
      Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue(1200));
      for (uint32_t i = 0; i < m_numNodes; i++) {
        m_callbacks.push_back(NodeCallback(m_nodeTranslate[i], this));
        NS_ASSERT(!m_connectivityGraph[i]->empty());
        for ( std::list<uint32_t>::iterator iterator = m_connectivityGraph[i]->begin(); 
        iterator != m_connectivityGraph[i]->end();
        iterator++) {
          NS_LOG_LOGIC("ITER " << i << " " << *iterator);
          std::pair<uint32_t, uint32_t> normalizedLink;
          if (m_nodeTranslate[i] < m_nodeTranslate[*iterator]) {
            normalizedLink = std::pair<uint32_t, uint32_t>(m_nodeTranslate[i], m_nodeTranslate[*iterator]);
          }
          else {
            normalizedLink = std::pair<uint32_t, uint32_t>(m_nodeTranslate[*iterator], m_nodeTranslate[i]);
          }
          if (m_channelMap[normalizedLink] != 0) {
            NS_LOG_LOGIC ("NOT adding link " << normalizedLink.first << " " << normalizedLink.second << " already exists");
            continue;
          }
          pointToPoint.SetDeviceAttribute ("DataRate", DataRateValue(DataRate("1Gbps")));
          pointToPoint.SetChannelAttribute("Delay", TimeValue(Time::FromDouble(m_linkLatency, Time::MS))); 
          NetDeviceContainer p2pDevices = 
            pointToPoint.Install (m_nodes.Get(i), m_nodes.Get(*iterator));
          m_nodeDevices[i].Add(p2pDevices.Get(0));
          //p2pDevices.Get(0)->TraceConnectWithoutContext("MacTxDrop", MakeCallback(&NodeCallback::PhyDropTrace, &m_callbacks[i]));
          m_nodeDevices[*iterator].Add(p2pDevices.Get(1));
          //p2pDevices.Get(1)->TraceConnectWithoutContext("MacTxDrop", MakeCallback(&NodeCallback::PhyDropTrace, &m_callbacks[*iterator]));
          m_linkDevices.push_back(p2pDevices);
          PointToPointChannel* channel = (PointToPointChannel*)GetPointer(p2pDevices.Get(0)->GetChannel());
          m_channels.push_back(channel);
          NS_LOG_LOGIC ("Adding link " << normalizedLink.first << " " << normalizedLink.second);
          m_channelMap[normalizedLink] = channel;
        }
      }

      InternetStackHelper stack;
      stack.Install (m_nodes);
      Ipv4AddressHelper address;
      address.SetBase ("10.1.1.0", "255.255.255.0");
      NS_LOG_INFO("Assigning address");
      for (int i = 0; i < (int)m_linkDevices.size(); i++) {
        Ipv4InterfaceContainer current = address.Assign(m_linkDevices[i]);
        address.NewNetwork();
      }
      NS_LOG_INFO("Assigned addresses");

      for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); it++) {
        Ptr<Node> node = *it;
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        for (uint32_t i = 1; i < ipv4->GetNInterfaces (); i++) {
          for (uint32_t j = 0; j < ipv4->GetNAddresses (i); j++) {
            m_addressToNodeMap[ipv4->GetAddress(i, j).GetLocal ()] = node->GetId(); 
          }
        }
      }
      NS_LOG_INFO("Added addresses");

      Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

      NS_LOG_INFO("Done populating routing table");

      for (uint32_t i = 0; i < m_numNodes; i++) {
        Ptr<GlobalRouter> router = m_nodes.Get(i)->GetObject<GlobalRouter>();
        Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol();
        gr->SetAttribute("ReverseOutputToInputDelay", TimeValue(MilliSeconds(m_delay * 1e-6)));
        gr->SetAttribute("ReverseInputToOutputDelay", TimeValue(MilliSeconds(m_delay * 1e-6)));
        gr->SetAttribute("AllowReversal", BooleanValue(m_repair));
        Ptr<Ipv4L3Protocol> l3 = m_nodes.Get(i)->GetObject<Ipv4L3Protocol>();
        //l3->TraceConnectWithoutContext("Drop", MakeCallback(&NodeCallback::DropTrace, &m_callbacks[i]));
        l3->SetAttribute("DefaultTtl", UintegerValue(255));
        gr->AddReversalCallback(MakeCallback(&NodeCallback::NodeReversal, &m_callbacks[i]));
      }


      PacketSinkHelper sinkHelp ("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny (), 5000));
      WanSendHelper bulkHelp ("ns3::TcpSocketFactory");
      for (std::vector<uint32_t>::iterator it = m_hosts.begin(); it != m_hosts.end(); it++) {
        ApplicationContainer paclient = bulkHelp.Install(m_nodes.Get(PhysicalNode(*it)));
        ApplicationContainer sink = sinkHelp.Install(m_nodes.Get(PhysicalNode(*it)));
        m_partClients[*it] = (WanSendApplication*)PeekPointer(paclient.Get(0));
      }

      NS_LOG_INFO("Done setting up simulation");
    }

    void Reset ()
    {
      SimulationSingleton<GlobalRouteManagerImpl>::Get ()->SendHeartbeats();
    }
    
    void FailLink (uint32_t from, uint32_t to)
    {
      NS_ASSERT(from < to);
      NS_LOG_LOGIC("Failing link between " << from << " and " << to);
      FailLinkPair(std::pair<uint32_t, uint32_t>(from, to));
    }

    void FailLinkPair(std::pair<uint32_t, uint32_t> key)
    {
      NS_LOG_LOGIC("Failing link between " << key.first << " and " << key.second);
      std::cout << Simulator::Now() << " " <<  key.first << "," << key.second << ",F" << std::endl;
      m_channelMap[key]->SetLinkDown();
    }

    void ScheduleEvents (std::string schedule)
    {
      std::ifstream scheduleFile(schedule.c_str());
      NS_ASSERT(scheduleFile.is_open());
      while (scheduleFile.good()) {
        std::string input;
        getline(scheduleFile, input);
        if (input.empty()) {
          continue;
        }
        std::vector<std::string> parts;
        boost::split(parts, input, boost::is_any_of(" "));
        double time = (double)std::atof(parts[0].c_str());
        bool query = (parts[1].compare("q") == 0);
        bool linkfail = (parts[1].compare("f") == 0);
        if (query) {
          std::cout << input << std::endl;
          uint32_t client = std::atoi(parts[2].c_str());
          uint32_t serverIndex = std::atoi(parts[3].c_str());
          Ipv4Address server  = m_nodes.Get(PhysicalNode(serverIndex))->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
          std::cerr << m_partClients[client];
          Simulator::Schedule(Seconds(time), &WanSendApplication::IssueRequest, m_partClients[client], InetSocketAddress(server, 5000), serverIndex, (uint64_t)std::atof(parts[4].c_str()));
        }
        else if (linkfail) {
          for (uint32_t it3 = 2; it3 < parts.size(); it3++) {
            std::vector<std::string> nodeParts;
            boost::split(nodeParts, parts[it3], boost::is_any_of("="));
            NS_ASSERT(nodeParts.size() == 2);
            uint32_t node0 = std::atoi(nodeParts[0].c_str());
            uint32_t node1 = std::atoi(nodeParts[1].c_str());
            std::pair<uint32_t, uint32_t> key = std::pair<uint32_t, uint32_t>(std::min(node0, node1), std::max(node0, node1));
            NS_LOG_LOGIC (node0 << " " << node1 << " " << nodeParts[0] << " " << nodeParts[1]);
            NS_ASSERT(m_channelMap[key] != 0);
            //NS_ASSERT(m_channelMap[std::pair<uint32_t, uint32_t>(std::min(node0, node1), std::max(node0, node1))] != 0);
            if (m_fail) {
              Simulator::Schedule(Seconds(time), &Topology::FailLink, this, std::min(node0, node1), std::max(node0, node1));
            }
          }
        }
      }
      std::cout << "Done scheduling" << std::endl;
    }
};


void NodeCallback::RxPacket (Ptr<const Packet> packet, Ipv4Header& header) {
  NS_LOG_LOGIC(m_id << " Received packet " << (uint32_t)header.GetTtl());
  uint32_t seq = 0;
  packet->CopyData ((uint8_t*)&seq, sizeof(seq));
  std::cout << m_topology->CannonicalNode(m_topology->AddressForNode(header.GetSource()))
            <<"," << m_id << ","<< seq << std::endl;
}

void NodeCallback::ServerRxPacket (Ptr<const Packet> packet, Ipv4Header& header) {
  NS_LOG_LOGIC(m_id << " Server Received packet " << (uint32_t)header.GetTtl());
  uint32_t seq = 0;
  packet->CopyData ((uint8_t*)&seq, sizeof(seq));
  std::cout << m_topology->CannonicalNode(m_topology->AddressForNode(header.GetSource()))
            <<"," << m_id << ","<< seq << std::endl;
  //std::cout << seq << ",";
}

void NodeCallback::NodeReversal (uint32_t iface, Ipv4Address addr) {
  NS_LOG_LOGIC(m_id << " reversed iface " << iface << " for " << addr);
  std::cout << Simulator::Now() << " " << m_id << ",R (" << addr << ")" << std::endl;
}

void NodeCallback::DropTrace (const Ipv4Header& hdr, Ptr<const Packet> packet, Ipv4L3Protocol::DropReason drop, Ptr<Ipv4> ipv4, uint32_t iface) {
  NS_LOG_LOGIC(m_id << " dropped packet " << iface);
  //std::cout << m_topology->CannonicalNode(m_topology->AddressForNode(hdr.GetSource()))
  //          << "," << m_topology->CannonicalNode(m_topology->AddressForNode(hdr.GetDestination()))
  //          << ",D (" << (uint32_t)hdr.GetTtl() << ")" << std::endl;
}

void NodeCallback::PhyDropTrace (Ptr<const Packet> packet) {
  //std::cout << m_id << ",P" << std::endl;
}

void
ParseLinks(std::string links, std::vector<std::pair<uint32_t, uint32_t> >& results)
{
  std::vector<std::string> linkParts;
  
  boost::split(linkParts, links, boost::is_any_of(","));
  for (std::vector<std::string>::iterator it = linkParts.begin(); 
       it != linkParts.end(); it++) {
    std::vector<std::string> nodeParts;
    boost::split(nodeParts, *it, boost::is_any_of("="));
    NS_ASSERT(nodeParts.size() == 2);
    results.push_back(std::pair<uint32_t, uint32_t>(std::atoi(nodeParts[0].c_str()), std::atoi(nodeParts[1].c_str())));
  }
}

void
ParseRequests(std::string links, std::vector<std::pair<uint32_t, std::list<uint32_t> > >&results)
{
  std::vector<std::string> linkParts;
  boost::split(linkParts, links, boost::is_any_of(","));
  for (std::vector<std::string>::iterator it = linkParts.begin();
       it != linkParts.end(); it++) {
    std::vector<std::string> nodeParts;
    boost::split(nodeParts, *it, boost::is_any_of("="));
    std::list<uint32_t> destinations;
    NS_ASSERT(nodeParts.size() >= 2);
    for (uint32_t i = 1; i < nodeParts.size(); i++) {
      destinations.push_back(std::atoi(nodeParts[i].c_str()));
    }
    results.push_back(std::pair<uint32_t, std::list<uint32_t> >(std::atoi(nodeParts[0].c_str()), destinations));
  }
}

int
main (int argc, char *argv[])
{
  AsciiTraceHelper asciiHelper;

  Ptr<OutputStreamWrapper> stream = asciiHelper.CreateFileStream ("tcp-trace.tr");
  std::string topology;
  double delay = 0.0;
  double linkLatency = 0.5;
  std::string schedule;
  bool repair;
  bool fail = true;
  CommandLine cmd;
  cmd.AddValue("topology", "Topology file", topology);
  cmd.AddValue("delay", "Delay for repairs", delay);
  cmd.AddValue("latency", "Propagation delay (ms)", linkLatency);
  cmd.AddValue("schedule", "Simulation schedule", schedule);
  cmd.AddValue("repair", "Allow reversals", repair);
  cmd.AddValue("fail",  "Actually fail links", fail);
  cmd.Parse(argc, argv);
  std::cerr << "Repair  = " << repair <<std::endl;
  std::cerr << "Fail = " << fail << std::endl;
  Topology simulationTopology;
  simulationTopology.SetFail(fail);
  simulationTopology.SetRepair(repair);
  simulationTopology.SetDelay(delay);
  simulationTopology.SetPropagationDelay(linkLatency);
  simulationTopology.PopulateGraph(topology);
  simulationTopology.HookupSimulation();
  simulationTopology.ScheduleEvents(schedule);
  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}
