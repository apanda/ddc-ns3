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
#include <list>
#include <vector>
#include <map>
#include <stack>
#include <algorithm>
#include <fstream>
#include <cstdlib>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("Stretch");

struct Simulation : public Object {
  std::vector<PointToPointChannel* > m_channels;
  UniformVariable m_randVar;
  int32_t m_numNodes;
  std::vector<std::list<uint32_t>*> m_connectivityGraph;
  std::vector<uint32_t> m_nodeTranslate;
  std::vector<UdpEchoClient*> m_clients;
  Time m_simulationEnd;
  uint32_t m_nodeSrc;
  uint32_t m_nodeDst;
  NodeContainer m_nodes;
  Simulation()
  {
      m_simulationEnd = Seconds(60.0 * 60.0 * 24 * 7);
  }
  void PopulateGraph(std::string& filename)
  {
    m_numNodes = -1;
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
    }
  
    m_numNodes = tempConnectivityGraph.size();
    std::map<uint32_t, uint32_t> translationMap;
    uint32_t last = 0;
    m_connectivityGraph.resize(m_numNodes);
  
    for (std::map<uint32_t, std::list<uint32_t>*>::iterator it = tempConnectivityGraph.begin();
        it != tempConnectivityGraph.end();
        it++) {
        m_nodeTranslate.push_back(it->first);
        translationMap[it->first] = last;
        last++;
    }
    for (int i = 0; i < m_numNodes; i++) {
      m_connectivityGraph[i] = new std::list<uint32_t>;
      for (std::list<uint32_t>::iterator it = tempConnectivityGraph[m_nodeTranslate[i]]->begin();
           it != tempConnectivityGraph[m_nodeTranslate[i]]->end();
           it++) {
        m_connectivityGraph[i]->push_back(translationMap[*it]);
      }
    }
  }
  
  bool IsGraphConnected(int start) 
  {
    NS_LOG_INFO("IsGraphConnected called, size " << m_numNodes);
  
    std::vector<bool> visited(m_numNodes, false);// = {false};
    std::stack<uint32_t> nodes;
    nodes.push(start);
    while (!nodes.empty()) {
      int node = (uint32_t)nodes.top();
      nodes.pop();
      if (visited[node]) {
        continue;
      }
      visited[node] = true;
      if (!m_connectivityGraph[node]) {
          continue;
      }
      for ( std::list<uint32_t>::iterator iterator = m_connectivityGraph[node]->begin(); 
      iterator != m_connectivityGraph[node]->end();
      iterator++) {
        nodes.push(*iterator);
      }
      for (int i = 0; i < m_numNodes; i++) {
        if (i == node) {
          continue;
        }
        if (!m_connectivityGraph[i]) {
            continue;
        }
        if (std::find(m_connectivityGraph[i]->begin(), m_connectivityGraph[i]->end(), node) !=
                 m_connectivityGraph[i]->end()) {
          nodes.push(i);
        }
      }
    }
    for (int i = 0; i < m_numNodes; i++) {
      bool discon = false;
      if (!visited[i]) {
        NS_LOG_ERROR("Found disconnect " << i << " ("<< m_nodeTranslate[i] << ")");
        discon = true;
      }
      if (discon) {
        return false;
      }
    }
    return true;
  }
  
  void ScheduleLinkRecovery(uint32_t failedLink) 
  {
    uint32_t nodeA = m_channels[failedLink]->GetDevice(0)->GetNode()->GetId();
    uint32_t nodeB = m_channels[failedLink]->GetDevice(1)->GetNode()->GetId();
    uint32_t nodeSrc = (nodeA < nodeB ? nodeA : nodeB);
    uint32_t nodeDest = (nodeA > nodeB ? nodeA : nodeB);
    NS_ASSERT(0 <= nodeSrc && nodeSrc < (uint32_t)m_numNodes);
    NS_ASSERT(0 <= nodeDest && nodeDest < (uint32_t)m_numNodes);
    m_connectivityGraph[nodeSrc]->push_back(nodeDest);
    IsGraphConnected(nodeSrc);
    m_channels[failedLink]->SetLinkUp();
    NS_LOG_INFO("Link " << failedLink << " is now up");
  }
  
  void ScheduleLinkFailure() 
  {
    uint32_t linkOfInterest = (uint32_t)-1;
    linkOfInterest = m_randVar.GetInteger(0, m_channels.size() - 1);
    uint32_t nodeA = m_channels[linkOfInterest]->GetDevice(0)->GetNode()->GetId();
    uint32_t nodeB = m_channels[linkOfInterest]->GetDevice(1)->GetNode()->GetId();
    uint32_t nodeSrc = (nodeA < nodeB ? nodeA : nodeB);
    uint32_t nodeDest = (nodeA > nodeB ? nodeA : nodeB);
    NS_ASSERT(0 <= nodeSrc && nodeSrc < (uint32_t)m_numNodes);
    NS_ASSERT(0 <= nodeDest && nodeDest < (uint32_t)m_numNodes);
    for (std::list<uint32_t>::iterator it = m_connectivityGraph[nodeSrc]->begin();
         it != m_connectivityGraph[nodeSrc]->end();
         it++) {
      NS_ASSERT(0 <= *it && *it < (uint32_t)m_numNodes);
      NS_ASSERT(0 <= *it && *it < (uint32_t)m_numNodes);
      if (*it == nodeDest) {
        m_connectivityGraph[nodeSrc]->erase(it);
        break;
      }
    }
    IsGraphConnected(m_nodeSrc);
    m_channels[linkOfInterest]->SetLinkDown();
    NS_LOG_INFO("Taking " << linkOfInterest << " down");
  }
  
  static void OutputTtlInformation(uint8_t oldTtl, uint8_t newTtl)
  {
      std::cout << "Received packet with TTL = " << newTtl << std::endl;
  }

  void Transmit()
  {
    NS_LOG_INFO("Transmitting from " << m_nodeSrc << " to " << m_nodeDst);
    std::cerr << "Transmit" << std::endl;
  }

  void Step()
  {
    m_nodeSrc = m_randVar.GetInteger(0, m_numNodes);
    m_nodeDst = m_randVar.GetInteger(0, m_numNodes);
    while (m_nodeDst == m_nodeSrc) {
        m_nodeDst = m_randVar.GetInteger(0, m_numNodes);
    }
    m_clients[m_nodeSrc]->ChangeDestination(m_nodes.Get(m_nodeDst)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal(),
                                      9);
    m_clients[m_nodeSrc]->ManualSend();
    
  }

  void Simulate(std::string filename, bool simulateError) {
    NS_LOG_INFO("Loading");
    NS_ASSERT(!filename.empty());
    PopulateGraph(filename);

    NS_LOG_INFO("Connected");
    NS_ASSERT(IsGraphConnected(0));

    NS_LOG_INFO("Creating nodes");
    m_nodes.Create (m_numNodes);

    NS_LOG_INFO("Creating point to point connections");
    PointToPointHelper pointToPoint;

    std::vector<NetDeviceContainer> nodeDevices(m_numNodes);
    std::vector<NetDeviceContainer> linkDevices;
    for (int i = 0; i < m_numNodes; i++) {
      if (m_connectivityGraph[i] == NULL) {
          continue;
      }

      for ( std::list<uint32_t>::iterator iterator = m_connectivityGraph[i]->begin(); 
      iterator != m_connectivityGraph[i]->end();
      iterator++) {
        NetDeviceContainer p2pDevices = 
          pointToPoint.Install (m_nodes.Get(i), m_nodes.Get(*iterator));
        nodeDevices[i].Add(p2pDevices.Get(0));
        nodeDevices[*iterator].Add(p2pDevices.Get(1));
        linkDevices.push_back(p2pDevices);
        m_channels.push_back((PointToPointChannel*)GetPointer(p2pDevices.Get(0)->GetChannel()));
      }
    }

    //pointToPoint.EnablePcapAll("DDCTest");
    InternetStackHelper stack;
    stack.Install (m_nodes);
    Ipv4AddressHelper address;
    address.SetBase ("10.1.1.0", "255.255.255.0");
    NS_LOG_INFO("Assigning address");
    for (int i = 0; i < (int)linkDevices.size(); i++) {
      Ipv4InterfaceContainer current = address.Assign(linkDevices[i]);
      address.NewNetwork();
    }
    NS_LOG_INFO("Done assigning address");
    NS_LOG_INFO("Populating routing tables");
    Ipv4GlobalRoutingHelper::SetSimulationEndTime(m_simulationEnd);
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

    NS_LOG_INFO("Done populating routing tables");
    // Simulate error
    if (simulateError) {
      for (int i = 0 ; i < 5; i++) {
        //ScheduleLinkFailure();
      }
    }
    m_clients.resize(m_numNodes);
    UdpEchoClientHelper echoClientA (m_nodes.Get(0)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal(),
                                      9);
    echoClientA.SetAttribute ("MaxPackets", UintegerValue (1));
    echoClientA.SetAttribute ("Interval", TimeValue (Seconds (m_randVar.GetValue(1.0, 3000.0))));
    echoClientA.SetAttribute ("PacketSize", UintegerValue (1024));
    ApplicationContainer clients = echoClientA.Install (m_nodes);
    clients.Start(Seconds(0.0));
    clients.Stop(m_simulationEnd);
    UdpEchoServerHelper echoServer (9);
    m_clients.resize(m_numNodes);
    for (int i = 0; i < m_numNodes; i++) {
        m_clients[i] = (UdpEchoClient*)(PeekPointer(clients.Get(i)));
    }
    
    ApplicationContainer serverApps = echoServer.Install (m_nodes);
    serverApps.Start (Seconds (0.0));
    serverApps.Stop (m_simulationEnd);
    Simulator::Schedule(Seconds(2.0), &Simulation::Step, this);
    Ptr<Ipv4RoutingProtocol> gr = m_nodes.Get(m_nodeDst)->GetObject<Ipv4>()->GetRoutingProtocol();
    Ipv4GlobalRouting* route = (Ipv4GlobalRouting*)PeekPointer(gr);
    route->TraceConnectWithoutContext("ReceivedTtl", MakeCallback(&Simulation::OutputTtlInformation));
  }
};

int
main (int argc, char *argv[])
{
  AsciiTraceHelper asciiHelper;

  uint32_t packets = 1;
  bool simulateError;
  CommandLine cmd;
  std::string filename;
  cmd.AddValue("packets", "Number of packets to echo", packets);
  cmd.AddValue("error", "Simulate error", simulateError);
  cmd.AddValue("topo", "Topology file", filename);
  cmd.Parse(argc, argv);
  NS_LOG_INFO("Running simulation");
  Ptr<Simulation> sim = ns3::Create<Simulation>();
  sim->Simulate(filename, simulateError);
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}
