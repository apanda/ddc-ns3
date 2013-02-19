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
#include "boost/algorithm/string.hpp"
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

NS_LOG_COMPONENT_DEFINE ("DDC-NSDI-STRETCH");
class Topology : public Object
{
  protected:
    std::vector<UdpEchoClient*> m_clients;
    std::vector<UdpEchoServer*> m_servers;
    std::vector<PointToPointChannel* > m_channels;
    UniformVariable randVar;
    uint32_t m_numNodes;
    std::vector<std::list<uint32_t>*> m_connectivityGraph;
    Time m_simulationEnd;
    std::vector<uint32_t> m_nodeTranslate;
    std::map<std::pair<uint32_t, uint32_t>, PointToPointChannel*> m_channelMap;
    std::map<uint32_t, uint32_t> m_nodeForwardTranslationMap;
    NodeContainer m_nodes;
    std::vector<NetDeviceContainer> m_nodeDevices;
    std::vector<NetDeviceContainer> m_linkDevices;
    std::vector<NodeCallback> m_callbacks;
    std::vector<std::pair<uint32_t, uint32_t> > m_pathsToTest;
    uint32_t m_currentPath;
    uint32_t m_currentTrial;
    uint32_t m_packets;
    double m_delay;
    double m_linkLatency;
  public:
    
    void SetPropagationDelay (double latency) 
    {
      m_linkLatency = latency;
    }

    void AddPathsToTest(std::vector<std::pair<uint32_t, uint32_t> > paths)
    {
      m_pathsToTest.insert(m_pathsToTest.end(), paths.begin(), paths.end());
      std::cout << m_pathsToTest[m_currentPath].first << "," << m_pathsToTest[m_currentPath].second << ",";
      Simulator::Schedule(Seconds(2.0), &Topology::PingMachines, this, m_pathsToTest[m_currentPath].first, m_pathsToTest[m_currentPath].second);
    }

    void RouteEnded ()
    {
      NS_LOG_LOGIC("Packet ended");
      m_currentTrial++;
      if (m_currentTrial < m_packets) {
        std::cout << ",";
        Simulator::Schedule(Seconds(2.0), &Topology::PingMachines, this, m_pathsToTest[m_currentPath].first, m_pathsToTest[m_currentPath].second);
      }
      else {
        m_currentPath++;
        m_currentTrial = 0;
        std::cout << std::endl;
        if (m_currentPath < m_pathsToTest.size()) {
          std::cout << m_pathsToTest[m_currentPath].first << "," << m_pathsToTest[m_currentPath].second << ",";
          Simulator::ScheduleNow(&Topology::Reset, this);
          Simulator::Schedule(Seconds(2.0), &Topology::PingMachines, this, m_pathsToTest[m_currentPath].first, m_pathsToTest[m_currentPath].second);
        }
      }
    }
    
    void SetPackets(uint32_t packets)
    {
      m_packets = packets;
    }

    Topology()
    {
      m_numNodes = 0;
      m_currentPath = 0;
      m_currentTrial = 0;
      m_simulationEnd = Seconds(60.0 * 60.0 * 24 * 7);
      m_packets = 0;
      m_delay = 0.0;
      m_linkLatency = 1.0;
   }
   
    virtual ~Topology()
    {
        for (uint32_t i = 0; i < m_numNodes; i++) {
            delete m_connectivityGraph[i];
        }
    }
    void PopulateGraph(std::string& filename)
    {
      std::map<uint32_t, std::list<uint32_t>*> tempConnectivityGraph;
      NS_LOG_INFO("Entering PopulateGraph with file " << filename);
      std::ifstream topology(filename.c_str());
      NS_ASSERT(topology.is_open());
    
      while (topology.good()) {
        std::string input;
        getline(topology, input);
        size_t found = input.find(" ");
        if (found == std::string::npos) {
          continue;
        }
        std::string node1s = input.substr(0, int(found));
        std::string node2s = input.substr(int(found) + 1, input.length() - (int(found) + 1));
        uint32_t node1 = std::atoi(node1s.c_str()) ;
        uint32_t node2 = std::atoi(node2s.c_str());
        if (!tempConnectivityGraph[node1]) {
          tempConnectivityGraph[node1] = new std::list<uint32_t>;
        }
        if (!tempConnectivityGraph[node2]) {
          tempConnectivityGraph[node2] = new std::list<uint32_t>;
        }
        tempConnectivityGraph[node1]->push_back(node2);
        tempConnectivityGraph[node2]->push_back(node1);
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
      pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("10Gbps"));
      pointToPoint.SetChannelAttribute("Delay", TimeValue(Time::FromDouble(m_linkLatency, Time::MS))); 
      for (uint32_t i = 0; i < m_numNodes; i++) {
        m_callbacks.push_back(NodeCallback(m_nodeTranslate[i], this));
        NS_ASSERT(!m_connectivityGraph[i]->empty());
        for ( std::list<uint32_t>::iterator iterator = m_connectivityGraph[i]->begin(); 
        iterator != m_connectivityGraph[i]->end();
        iterator++) {
          if (*iterator < i) {
            continue;
          }
          NetDeviceContainer p2pDevices = 
            pointToPoint.Install (m_nodes.Get(i), m_nodes.Get(*iterator));
          m_nodeDevices[i].Add(p2pDevices.Get(0));
          m_nodeDevices[*iterator].Add(p2pDevices.Get(1));
          m_linkDevices.push_back(p2pDevices);
          PointToPointChannel* channel = (PointToPointChannel*)GetPointer(p2pDevices.Get(0)->GetChannel());
          m_channels.push_back(channel);
          std::pair<uint32_t, uint32_t> normalizedLink;
          if (m_nodeTranslate[i] < m_nodeTranslate[*iterator]) {
            normalizedLink = std::pair<uint32_t, uint32_t>(m_nodeTranslate[i], m_nodeTranslate[*iterator]);
          }
          else {
            normalizedLink = std::pair<uint32_t, uint32_t>(m_nodeTranslate[i], m_nodeTranslate[*iterator]);
          }
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
      Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

      UdpEchoServerHelper echoServer (9);

      ApplicationContainer serverApps = echoServer.Install (m_nodes);
      serverApps.Start (Seconds (1.0));
      serverApps.Stop (m_simulationEnd);
      m_clients.resize(m_numNodes);
      m_servers.resize(m_numNodes);
      for (uint32_t i = 0; i < m_numNodes; i++) {
        Ptr<GlobalRouter> router = m_nodes.Get(i)->GetObject<GlobalRouter>();
        Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol();
        Ptr<Ipv4L3Protocol> l3 = m_nodes.Get(i)->GetObject<Ipv4L3Protocol>();
        l3->TraceConnectWithoutContext("Drop", MakeCallback(&NodeCallback::DropTrace, &m_callbacks[i]));
        l3->SetAttribute("DefaultTtl", UintegerValue(255));
        gr->SetAttribute("ReverseOutputToInputDelay", TimeValue(Time::FromDouble(m_delay, Time::MS)));
        gr->SetAttribute("ReverseInputToOutputDelay", TimeValue(Time::FromDouble(m_delay, Time::MS)));
        gr->AddReversalCallback(MakeCallback(&NodeCallback::NodeReversal, &m_callbacks[i]));
        m_servers[i] =  (UdpEchoServer*)PeekPointer(serverApps.Get(i));
        m_servers[i]->AddReceivePacketEvent(MakeCallback(&NodeCallback::ServerRxPacket, &m_callbacks[i]));
        int j = 0;
        UdpEchoClientHelper echoClient (m_nodes.Get(j)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal(),
                                          9);
        echoClient.SetAttribute ("MaxPackets", UintegerValue (0));
        echoClient.SetAttribute ("Interval", TimeValue (Seconds (randVar.GetValue(1.0, 3000.0))));
        echoClient.SetAttribute ("PacketSize", UintegerValue (1024));
        ApplicationContainer clientApps = echoClient.Install (m_nodes.Get (i));
        m_clients[i] = (UdpEchoClient*)(PeekPointer(clientApps.Get(0)));
        m_clients[i]->AddReceivePacketEvent(MakeCallback(&NodeCallback::RxPacket, &m_callbacks[i]));
        Simulator::ScheduleNow(&UdpEchoClient::StartApplication, m_clients[i]);
      }
    }
    void Reset ()
    {
      SimulationSingleton<GlobalRouteManagerImpl>::Get ()->SendHeartbeats();
    }

    void PingMachines (uint32_t client, uint32_t server)
    {
      NS_LOG_LOGIC("Untranslated sending between " << client << " and " << server);
      client = m_nodeForwardTranslationMap[client];
      server = m_nodeForwardTranslationMap[server];
      NS_LOG_LOGIC("Sending between " << client << " and " << server);
      m_clients[client]->SetRemote(m_nodes.Get(server)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal(), 9);
      Simulator::ScheduleNow(&UdpEchoClient::StopApplication, m_clients[client]);
      Simulator::ScheduleNow(&UdpEchoClient::StartApplication, m_clients[client]);
      Simulator::ScheduleNow(&UdpEchoClient::Send, m_clients[client]);
      std::cout << "(" << Simulator::Now() << ") ";
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
      m_channelMap[key]->SetLinkDown();
    }

    void SetDelay(double delay)
    {
      m_delay = delay;
    }
};


void NodeCallback::RxPacket (Ptr<const Packet> packet, Ipv4Header& header) {
  NS_LOG_LOGIC(m_id << " Received packet " << (uint32_t)header.GetTtl());
  std::cout << (uint32_t)header.GetTtl() << " (" << Simulator::Now() << ") ";
  m_topology->RouteEnded();
}

void NodeCallback::ServerRxPacket (Ptr<const Packet> packet, Ipv4Header& header) {
  NS_LOG_LOGIC(m_id << " Server Received packet " << (uint32_t)header.GetTtl());
  std::cout << (uint32_t)header.GetTtl() << " (" << Simulator::Now() << "),";
}

void NodeCallback::NodeReversal (uint32_t iface, Ipv4Address addr) {
  NS_LOG_LOGIC(m_id << " reversed iface " << iface << " for " << addr);
}

void NodeCallback::DropTrace (const Ipv4Header& hdr, Ptr<const Packet> packet, Ipv4L3Protocol::DropReason drop, Ptr<Ipv4> ipv4, uint32_t iface) {
  NS_LOG_LOGIC(m_id << " dropped packet " << iface);
  std::cout << "D (" << Simulator::Now() << ") ";
  m_topology->RouteEnded();
}

void NodeCallback::PhyDropTrace (Ptr<const Packet>) {
  std::cout << m_id << "P (" << Simulator::Now() << ") ";
  m_topology->RouteEnded();
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

int
main (int argc, char *argv[])
{
  AsciiTraceHelper asciiHelper;

  Ptr<OutputStreamWrapper> stream = asciiHelper.CreateFileStream ("tcp-trace.tr");
  bool simulateError;
  std::string topology;
  std::string links;
  std::string paths;
  uint32_t packets = 1;
  double delay;
  double linkLatency = 1.0;
  std::vector<std::pair<uint32_t, uint32_t> > linksToFail;
  std::vector<std::pair<uint32_t, uint32_t> > pathsToTest;
  CommandLine cmd;
  cmd.AddValue("packets", "Number of packets to echo", packets);
  cmd.AddValue("error", "Simulate error", simulateError);
  cmd.AddValue("topology", "Topology file", topology);
  cmd.AddValue("links", "Links to fail", links);
  cmd.AddValue("paths", "Source destination pairs to test", paths);
  cmd.AddValue("packets", "Packets to send per trial", packets);
  cmd.AddValue("delay", "Delay for repairs", delay);
  cmd.AddValue("latency", "Propagation delay (ms)", linkLatency);
  cmd.Parse(argc, argv);
  if (!links.empty()) {
    ParseLinks(links, linksToFail);
  }
  if (!paths.empty()) {
    ParseLinks(paths, pathsToTest);
  }
  Topology simulationTopology;
  simulationTopology.SetDelay(delay);
  simulationTopology.SetPackets(packets);
  simulationTopology.SetPropagationDelay(linkLatency);
  simulationTopology.PopulateGraph(topology);
  simulationTopology.HookupSimulation();
  //LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
  //LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);
  //Simulator::ScheduleNow(&Topology::FailLink, &simulationTopology, 5, 6);
  //Simulator::ScheduleNow(&Topology::FailLink, &simulationTopology, 6, 7);
  for (std::vector<std::pair<uint32_t, uint32_t> >::iterator it = linksToFail.begin();
       it != linksToFail.end();
       it++) {
    Simulator::ScheduleNow(&Topology::FailLinkPair, &simulationTopology, *it);
  }
  simulationTopology.AddPathsToTest(pathsToTest);
  //Simulator::Schedule(Seconds(1.0), &Topology::PingMachines, &simulationTopology, 1, 6);
  //simulationTopology.PingMachines(1, 6);
  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}
