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

NS_LOG_COMPONENT_DEFINE ("DataDrivenConnectivityTopologyReader");

std::vector<PointToPointChannel* > channels;
UniformVariable randVar;
int32_t NODES = -1;
std::vector<std::list<uint32_t>*> connectivityGraph;
std::vector<uint32_t> nodeTranslate;
Time simulationEnd = Seconds(60.0 * 60.0 * 24 * 7);
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
  }

  NODES = tempConnectivityGraph.size();
  std::map<uint32_t, uint32_t> translationMap;
  uint32_t last = 0;
  connectivityGraph.resize(NODES);

  for (std::map<uint32_t, std::list<uint32_t>*>::iterator it = tempConnectivityGraph.begin();
      it != tempConnectivityGraph.end();
      it++) {
      nodeTranslate.push_back(it->first);
      translationMap[it->first] = last;
      last++;
  }
  for (int i = 0; i < NODES; i++) {
    connectivityGraph[i] = new std::list<uint32_t>;
    for (std::list<uint32_t>::iterator it = tempConnectivityGraph[nodeTranslate[i]]->begin();
         it != tempConnectivityGraph[nodeTranslate[i]]->end();
         it++) {
      connectivityGraph[i]->push_back(translationMap[*it]);
    }
  }
}

bool IsGraphConnected(int start) 
{
  NS_LOG_INFO("IsGraphConnected called, size " << NODES);

  std::vector<bool> visited(NODES, false);// = {false};
  std::stack<uint32_t> nodes;
  nodes.push(start);
  while (!nodes.empty()) {
    int node = (uint32_t)nodes.top();
    nodes.pop();
    if (visited[node]) {
      continue;
    }
    visited[node] = true;
    if (!connectivityGraph[node]) {
        continue;
    }
    for ( std::list<uint32_t>::iterator iterator = connectivityGraph[node]->begin(); 
    iterator != connectivityGraph[node]->end();
    iterator++) {
      nodes.push(*iterator);
    }
    for (int i = 0; i < NODES; i++) {
      if (i == node) {
        continue;
      }
      if (!connectivityGraph[i]) {
          continue;
      }
      if (std::find(connectivityGraph[i]->begin(), connectivityGraph[i]->end(), node) !=
               connectivityGraph[i]->end()) {
        nodes.push(i);
      }
    }
  }
  for (int i = 0; i < NODES; i++) {
    bool discon = false;
    if (!visited[i]) {
      NS_LOG_ERROR("Found disconnect " << i << " ("<< nodeTranslate[i] << ")");
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
  uint32_t nodeA = channels[failedLink]->GetDevice(0)->GetNode()->GetId();
  uint32_t nodeB = channels[failedLink]->GetDevice(1)->GetNode()->GetId();
  uint32_t nodeSrc = (nodeA < nodeB ? nodeA : nodeB);
  uint32_t nodeDest = (nodeA > nodeB ? nodeA : nodeB);
  NS_ASSERT(0 <= nodeSrc && nodeSrc < (uint32_t)NODES);
  NS_ASSERT(0 <= nodeDest && nodeDest < (uint32_t)NODES);
  connectivityGraph[nodeSrc]->push_back(nodeDest);
  IsGraphConnected(nodeSrc);
  channels[failedLink]->SetLinkUp();
  NS_LOG_INFO("Link " << failedLink << " is now up");
}

void ScheduleLinkFailure() 
{
  uint32_t linkOfInterest = (uint32_t)-1;
  linkOfInterest = randVar.GetInteger(0, channels.size() - 1);
  uint32_t nodeA = channels[linkOfInterest]->GetDevice(0)->GetNode()->GetId();
  uint32_t nodeB = channels[linkOfInterest]->GetDevice(1)->GetNode()->GetId();
  uint32_t nodeSrc = (nodeA < nodeB ? nodeA : nodeB);
  uint32_t nodeDest = (nodeA > nodeB ? nodeA : nodeB);
  NS_ASSERT(0 <= nodeSrc && nodeSrc < (uint32_t)NODES);
  NS_ASSERT(0 <= nodeDest && nodeDest < (uint32_t)NODES);
  for (std::list<uint32_t>::iterator it = connectivityGraph[nodeSrc]->begin();
       it != connectivityGraph[nodeSrc]->end();
       it++) {
    NS_ASSERT(0 <= *it && *it < (uint32_t)NODES);
    NS_ASSERT(0 <= *it && *it < (uint32_t)NODES);
    if (*it == nodeDest) {
      connectivityGraph[nodeSrc]->erase(it);
      break;
    }
  }
  IsGraphConnected(nodeSrc);
  channels[linkOfInterest]->SetLinkDown();
  NS_LOG_INFO("Taking " << linkOfInterest << " down");
  /*Time downStep = Seconds(randVar.GetValue(240.0, 3600.0));
  Time tAbsolute = Simulator::Now() + downStep; 
  if (tAbsolute < simulationEnd) {
    Simulator::Schedule(downStep, &ScheduleLinkFailure);
  }
  Time upStep = Seconds(randVar.GetValue(240.0, 5200.0));
  tAbsolute = Simulator::Now() + upStep;
  if (tAbsolute < simulationEnd) {
    Simulator::Schedule(upStep, &ScheduleLinkRecovery, linkOfInterest);
  }*/
  //Simulator::Schedule(Seconds(randVar.GetValue(180.0, 6000.0)), &ScheduleLinkRecovery, failedLink);
}

int
main (int argc, char *argv[])
{
  AsciiTraceHelper asciiHelper;

  Ptr<OutputStreamWrapper> stream = asciiHelper.CreateFileStream ("tcp-trace.tr");
  uint32_t packets = 1;
  bool simulateError;
  CommandLine cmd;
  std::string filename;
  cmd.AddValue("packets", "Number of packets to echo", packets);
  cmd.AddValue("error", "Simulate error", simulateError);
  cmd.AddValue("topo", "Topology file", filename);
  cmd.Parse(argc, argv);
  NS_ASSERT(!filename.empty());
  PopulateGraph(filename);
  //LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
  //LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);

  //for (int i = 0; i < NODES; i++) {
  //    connectivityGraph[i] = new std::list<uint32_t>;
  //}

  //// Graph
  //// 0 -> 1, 2
  //// 1 -> 0, 3
  //// 2->  0, 3, 4
  //// 3 -> 1, 2, 4
  //// 4 -> 2, 3
  //connectivityGraph[0]->push_back(1);
  //connectivityGraph[0]->push_back(6);
  //connectivityGraph[0]->push_back(7);
  //connectivityGraph[0]->push_back(5);
  //connectivityGraph[1]->push_back(6);
  //connectivityGraph[1]->push_back(7);
  //connectivityGraph[1]->push_back(8);
  //connectivityGraph[2]->push_back(7);
  //connectivityGraph[2]->push_back(8);
  //connectivityGraph[2]->push_back(9);
  //connectivityGraph[2]->push_back(3);
  //connectivityGraph[2]->push_back(5);
  //connectivityGraph[3]->push_back(8);
  //connectivityGraph[3]->push_back(9);
  //connectivityGraph[3]->push_back(10);
  //connectivityGraph[4]->push_back(9);
  //connectivityGraph[4]->push_back(10);
  //connectivityGraph[4]->push_back(11);
  //connectivityGraph[4]->push_back(5);
  //connectivityGraph[4]->push_back(5);
  //connectivityGraph[6]->push_back(11);
  //connectivityGraph[7]->push_back(8);
  //connectivityGraph[8]->push_back(11);
  //connectivityGraph[9]->push_back(10);
  //connectivityGraph[10]->push_back(11);
  NS_ASSERT(IsGraphConnected(0));

  NS_LOG_INFO("Creating nodes");
  NodeContainer nodes;
  nodes.Create (NODES);

  NS_LOG_INFO("Creating point to point connections");
  PointToPointHelper pointToPoint;

  std::vector<NetDeviceContainer> nodeDevices(NODES);
  std::vector<NetDeviceContainer> linkDevices;
  for (int i = 0; i < NODES; i++) {
    if (connectivityGraph[i] == NULL) {
        continue;
    }

    for ( std::list<uint32_t>::iterator iterator = connectivityGraph[i]->begin(); 
    iterator != connectivityGraph[i]->end();
    iterator++) {
      NetDeviceContainer p2pDevices = 
        pointToPoint.Install (nodes.Get(i), nodes.Get(*iterator));
      nodeDevices[i].Add(p2pDevices.Get(0));
      nodeDevices[*iterator].Add(p2pDevices.Get(1));
      linkDevices.push_back(p2pDevices);
      channels.push_back((PointToPointChannel*)GetPointer(p2pDevices.Get(0)->GetChannel()));
    }
  }

  //pointToPoint.EnablePcapAll("DDCTest");
  InternetStackHelper stack;
  stack.Install (nodes);
  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  NS_LOG_INFO("Assigning address");
  for (int i = 0; i < (int)linkDevices.size(); i++) {
    Ipv4InterfaceContainer current = address.Assign(linkDevices[i]);
    stack.EnableAsciiIpv4(stream, current); 
    address.NewNetwork();
  }
  NS_LOG_INFO("Done assigning address");
  NS_LOG_INFO("Populating routing tables");
  //Ipv4GlobalRoutingHelper::SetSimulationEndTime(simulationEnd);
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  NS_LOG_INFO("Done populating routing tables");
  // Simulate error
  if (simulateError) {
    for (int i = 0 ; i < 5; i++) {
      ScheduleLinkFailure();
    }
  }

  UdpEchoServerHelper echoServer (9);

  ApplicationContainer serverApps = echoServer.Install (nodes);
  serverApps.Start (Seconds (1.0));
  serverApps.Stop (simulationEnd);
  for (int i = 0; i < NODES; i++) {
    for (int j = 0; j < NODES; j++) {
      if (i == j) {
        continue;
      }
      UdpEchoClientHelper echoClient (nodes.Get(j)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal(),
                                        9);
      echoClient.SetAttribute ("MaxPackets", UintegerValue (1));
      echoClient.SetAttribute ("Interval", TimeValue (Seconds (randVar.GetValue(1.0, 3000.0))));
      echoClient.SetAttribute ("PacketSize", UintegerValue (1024));
      ApplicationContainer clientApps = echoClient.Install (nodes.Get (i));
      clientApps.Start (Seconds (randVar.GetValue(1.0, 120.0)));
      clientApps.Stop (simulationEnd);
    }
  }

  Ptr<OutputStreamWrapper> out = asciiHelper.CreateFileStream("route.table");
  //NS_LOG_INFO("Node interface list");
  //for (int i = 0; i < NODES; i++) {
  //    Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
  //    NS_ASSERT(ipv4 != NULL);
  //    for (int j = 0; j < (int)ipv4->GetNInterfaces(); j++) {
  //        for (int k = 0; k < (int)ipv4->GetNAddresses(j); k++) {
  //          Ipv4InterfaceAddress iaddr = ipv4->GetAddress (j, k);
  //          Ipv4Address addr = iaddr.GetLocal ();
  //          (*out->GetStream()) << i << "\t" << j << "\t" << addr << "\n";
  //        }
  //    }
  //}
  //for (int i = 0; i < NODES; i++) {
  //  nodes.Get(i)->GetObject<Ipv4>()->GetRoutingProtocol()->PrintRoutingTable(out);
  //}
  NS_LOG_INFO("Running simulation");
  Simulator::Run ();
  Simulator::Destroy ();

  for (int i = 0; i < NODES; i++) {
      delete connectivityGraph[i];
  }
  return 0;
}
