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
#include <sstream>
#include <string>

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
  std::list<uint32_t> m_path;
  std::list<uint32_t> m_fullPath;
  int32_t m_failedLink;
  uint32_t m_iterations;
  Ptr<OutputStreamWrapper> m_output;
  uint32_t m_packets;
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

  enum State {
      ExploreFull,
      ExploreFailed1,
      ExploreFailed2
  };
  State m_state;
  uint32_t m_fullLength;
  uint32_t m_failedLength;
  std::list<uint32_t> m_failedLengths;
  uint32_t m_packetCount;
  uint32_t m_secondFailedLength;
  
  void UnfailLink(int32_t link) {
    NS_LOG_INFO("UnfailLink");
    Simulator::Stop();
    if (link == -1) {
      return;
    }
    UnfailLinkInternal(link);
    ResetState();
  }

  void UnfailLinkInternal(int32_t link) {
    uint32_t nodeA = m_channels[link]->GetDevice(0)->GetNode()->GetId();
    uint32_t nodeB = m_channels[link]->GetDevice(1)->GetNode()->GetId();
    uint32_t nodeSrc = (nodeA < nodeB ? nodeA : nodeB);
    uint32_t nodeDest = (nodeA > nodeB ? nodeA : nodeB);
    NS_ASSERT(0 <= nodeSrc && nodeSrc < (uint32_t)m_numNodes);
    NS_ASSERT(0 <= nodeDest && nodeDest < (uint32_t)m_numNodes);
    m_connectivityGraph[nodeSrc]->push_back(nodeDest);
    m_channels[link]->SetLinkUp();
  }

  int32_t FailLink(uint32_t nodeA, uint32_t nodeB) 
  {
    int32_t link = -1;
    for (uint32_t linkOfInterest = 0; linkOfInterest < m_channels.size(); linkOfInterest++) {
      uint32_t devA = m_channels[linkOfInterest]->GetDevice(0)->GetNode()->GetId();
      uint32_t devB = m_channels[linkOfInterest]->GetDevice(1)->GetNode()->GetId();
      if ((devA == nodeA && devB== nodeB) ||
           (devA == nodeB && devB == nodeA)) {
        link = linkOfInterest;
        break;
      }
    }
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
    if (link != -1) {
      m_channels[link]->SetLinkDown();
    }
    return link;
  }
  
  static void OutputTtlInformation(uint8_t oldTtl, uint8_t newTtl)
  {
      std::cout << "Received packet with TTL = " << newTtl << std::endl;
  }

  void DroppedPacket()
  {
    NS_LOG_INFO("Packet dropped");
    Simulator::Cancel(m_stepEvent);
    NS_ASSERT(m_state == ExploreFailed1 || m_state == ExploreFull);
    if (m_state == ExploreFailed1) {
      UnfailLink(m_failedLink);
      if (FindAndFailLink()) {
        NS_ASSERT(m_failedLink != -1);
        if (!m_clients[m_nodeSrc]->ManualSend()) {
          return StepActual();
        }
        m_stepEvent = Simulator::Schedule(Seconds(240), &Simulation::Step, this);
        m_state = ExploreFailed1;
        m_packetCount = m_packets;
        m_failedLengths.clear();
      }
      else {
        StepActual();
      }
    }
    else {
      StepActual();
    }
  }
  
  void ResetState()
  {
    NS_LOG_INFO("ResetState");
    for (int i = 0; i < m_numNodes; i++) {
      Ptr<Node> node = m_nodes.Get(i);
      Ptr<GlobalRouter> router = node->GetObject<GlobalRouter>();
      if (router == 0) {
        continue;
      }
      Ptr<Ipv4GlobalRouting> gr = router->GetRoutingProtocol();
      gr->Reset();
    }
  }
  
  bool FindAndFailLink() {
      if (m_fullPath.size() <= 1) {
        return false;
      }
      uint32_t nodeA = m_fullPath.front();
      m_fullPath.pop_front();
      uint32_t nodeB = m_fullPath.front();
      m_failedLink = FailLink(nodeA, nodeB);
      while (!IsGraphConnected(m_nodeSrc) && (m_fullPath.size() > 1)) {
        UnfailLink(m_failedLink);
        nodeA = m_fullPath.front();
        m_fullPath.pop_front();
        nodeB = m_fullPath.front();
        m_failedLink = FailLink(nodeA, nodeB);
      }
      bool ret = IsGraphConnected(m_nodeSrc) && (m_failedLink != -1);
      if (!ret && m_failedLink != -1) {
        UnfailLink(m_failedLink);
      }
      return ret;
  }
  bool m_newIter; 
  void ReceivedPacket(uint32_t node)
  {
      NS_LOG_INFO("Received packet");
      Simulator::Cancel(m_stepEvent);
      if (node != m_nodeDst) {
        return;
      }
      m_path.push_back(node);
      NS_LOG_INFO("Path length = " << m_path.size());
      if (m_state == ExploreFull) {
        m_fullLength = m_path.size();
        m_fullPath.clear();
        m_fullPath.insert(m_fullPath.begin(), m_path.begin(), m_path.end());
        m_path.clear();
        if (FindAndFailLink()) {
          NS_ASSERT(m_failedLink != -1);
          m_state = ExploreFailed1;
          if (!m_clients[m_nodeSrc]->ManualSend()) {
            return DroppedPacket();
          }
          m_stepEvent = Simulator::Schedule(Seconds(240), &Simulation::Step, this);
          m_packetCount = m_packets;
          m_failedLengths.clear();
        }
        else {
          StepActual();
        }
      }
      else if (m_state==ExploreFailed1) {
        m_failedLength = m_path.size();
        m_path.clear();
        if (!m_clients[m_nodeSrc]->ManualSend()) {
          return DroppedPacket();
        }
        m_stepEvent = Simulator::Schedule(Seconds(240), &Simulation::Step, this);
        m_packetCount--;
        m_failedLengths.push_back(m_failedLength);
        if (m_packetCount == 0) {
          m_state = ExploreFailed2;
        }
        if (m_newIter) {
          m_iterations--;
          m_newIter = false;
        }
      }
      else if (m_state == ExploreFailed2) {
        m_secondFailedLength = m_path.size();
        m_path.clear();
        std::stringstream lengths;
        for (std::list<uint32_t>::iterator it = m_failedLengths.begin(); 
             it != m_failedLengths.end();
             it++) {
          lengths << *it << ",";
        }
        (*m_output->GetStream()) << m_nodeSrc << ","<<m_nodeDst<<","<<m_fullLength<<","<<lengths.str()<<m_secondFailedLength<<std::endl;
        UnfailLink(m_failedLink);
        if (FindAndFailLink()) {
          NS_ASSERT(m_failedLink != -1);
          m_state = ExploreFailed1;
          m_packetCount = m_packets;
          m_failedLengths.clear();
          if (!m_clients[m_nodeSrc]->ManualSend()) {
            return DroppedPacket();
          }
          m_stepEvent = Simulator::Schedule(Seconds(240), &Simulation::Step, this);
        }
        else {
          StepActual();
        }
      }

  }

  void Visited(uint32_t node)
  {
      Simulator::Cancel(m_stepEvent);
      m_stepEvent = Simulator::Schedule(Seconds(240), &Simulation::Step, this);
      NS_LOG_INFO("Visited");
      m_path.push_back(node);
  }
  void Step() 
  {
     NS_LOG_INFO("Stepping because of timer");
      StepActual();
  }

  void StepActual()
  {
    std::cerr << "Stopping" << std::endl;
    Simulator::Stop();
    Simulator::Schedule(Seconds(2.0), &Simulation::StepInternal, this);
    if (m_failedLink != -1) {
      UnfailLinkInternal(m_failedLink);
      m_failedLink = -1;
    }
  }
  EventId m_stepEvent;
  void StepInternal()
  {
    m_stepEvent = Simulator::Schedule(Seconds(240), &Simulation::Step, this);
    if (m_failedLink != -1) {
      UnfailLink(m_failedLink);
    }
    std::cerr << m_iterations << std::endl;
    if (m_iterations == 0) {
      std::cerr << "Stopping" << std::endl;
      Simulator::Stop();
    }
    m_newIter = true;
    m_path.clear();
    m_state = ExploreFull;
    m_nodeSrc = 61;//m_randVar.GetInteger(0, m_numNodes - 1);
    m_nodeDst = 11;//m_randVar.GetInteger(0, m_numNodes - 1);
    while (m_nodeDst == m_nodeSrc) {
        m_nodeDst = m_randVar.GetInteger(0, m_numNodes - 1);
    }
    NS_LOG_INFO("Step picking src = " << m_nodeSrc << " dst = " << m_nodeDst);
    m_clients[m_nodeSrc]->ChangeDestination(m_nodes.Get(m_nodeDst)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal(),
                                      9);
    if (!m_clients[m_nodeSrc]->ManualSend()) {
      return DroppedPacket();
    }
    
  }

  void Simulate(std::string filename, Ptr<OutputStreamWrapper> output, bool simulateError, uint32_t iterations, uint32_t packets) {
    m_failedLink = -1;
    m_packets = packets;
    m_output = output;
    m_iterations = 1;
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
    Ipv4GlobalRoutingHelper::SetPacketDropped(MakeCallback(&Simulation::DroppedPacket, this));
    Ipv4GlobalRoutingHelper::SetVisited(MakeCallback(&Simulation::Visited, this));
    Ipv4GlobalRoutingHelper::SetReceived(MakeCallback(&Simulation::ReceivedPacket, this));
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

    NS_LOG_INFO("Done populating routing tables");
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
        m_clients[i]->SetReceivedCallback(MakeCallback(&Simulation::ReceivedPacket, this));
    }
    
    ApplicationContainer serverApps = echoServer.Install (m_nodes);
    serverApps.Start (Seconds (0.0));
    serverApps.Stop (m_simulationEnd);
    for (int i = 0; i <m_numNodes;i++) {
      ((UdpEchoServer*)PeekPointer(serverApps.Get(i)))->SetReceivedCallback(MakeCallback(&Simulation::ReceivedPacket, this));
    }

    Ptr<Ipv4RoutingProtocol> gr = m_nodes.Get(m_nodeDst)->GetObject<Ipv4>()->GetRoutingProtocol();
    Ipv4GlobalRouting* route = (Ipv4GlobalRouting*)PeekPointer(gr);
    route->TraceConnectWithoutContext("ReceivedTtl", MakeCallback(&Simulation::OutputTtlInformation));
    Simulator::Schedule(Seconds(2.0), &Simulation::StepInternal, this);
    while (m_iterations > 0) {
      ResetState();
      Simulator::Run ();
    }
    Simulator::Destroy ();
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
  std::string output;
  uint32_t iterations = 10000;
  cmd.AddValue("packets", "Number of packets to echo", packets);
  cmd.AddValue("error", "Simulate error", simulateError);
  cmd.AddValue("topo", "Topology file", filename);
  cmd.AddValue("output", "Output file", output);
  cmd.AddValue("iter", "Iterations", iterations);
  cmd.Parse(argc, argv);
  NS_LOG_INFO("Running simulation");
  Ptr<Simulation> sim = ns3::Create<Simulation>();
  NS_ASSERT(!output.empty());
  sim->Simulate(filename, asciiHelper.CreateFileStream(output), simulateError, iterations, packets);
  return 0;
}
