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
#include <list>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("DataDrivenConnectivity1");

int
main (int argc, char *argv[])
{
  uint32_t packets = 1;
  bool simulateError;
  CommandLine cmd;
  cmd.AddValue("packets", "Number of packets to echo", packets);
  cmd.AddValue("error", "Simulate error", simulateError);
  cmd.Parse(argc, argv);
  LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
  LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);

  const int32_t NODES = 5;
  std::vector<std::vector<uint32_t>*> connectivityGraph(NODES);
  for (int i = 0; i < NODES; i++) {
      connectivityGraph[i] = new std::vector<uint32_t>;
  }

  // Graph
  // 0 -> 1, 2
  // 1 -> 0, 3
  // 2->  0, 3, 4
  // 3 -> 1, 2, 4
  // 4 -> 2, 3
  connectivityGraph[0]->push_back(1);
  connectivityGraph[0]->push_back(2);
  connectivityGraph[1]->push_back(3);
  connectivityGraph[2]->push_back(3);
  connectivityGraph[2]->push_back(4);
  connectivityGraph[3]->push_back(4);

  NS_LOG_INFO("Creating nodes");
  NodeContainer nodes;
  nodes.Create (NODES);

  NS_LOG_INFO("Creating point to point connections");
  PointToPointHelper pointToPoint;
  pointToPoint.EnablePcapAll("DDCTest");

  std::vector<NetDeviceContainer> nodeDevices(NODES);
  std::vector<NetDeviceContainer> linkDevices;
  for (int i = 0; i < NODES; i++) {
    for ( std::vector<uint32_t>::iterator iterator = connectivityGraph[i]->begin(); 
    iterator != connectivityGraph[i]->end();
    iterator++) {
      NetDeviceContainer p2pDevices = 
        pointToPoint.Install (nodes.Get(i), nodes.Get(*iterator));
      nodeDevices[i].Add(p2pDevices.Get(0));
      nodeDevices[*iterator].Add(p2pDevices.Get(1));
      linkDevices.push_back(p2pDevices);
    }
  }

  InternetStackHelper stack;
  stack.Install (nodes);
  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");

  NS_LOG_INFO("Assigning address");
  for (int i = 0; i < (int)linkDevices.size(); i++) {
    Ipv4InterfaceContainer current = address.Assign(linkDevices[i]);
    address.NewNetwork();
  }
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // Simulate error
  if (simulateError) {
    ((PointToPointChannel*)(PeekPointer(nodeDevices[2].Get(2)->GetChannel())))->SetLinkDown();
  }

  UdpEchoServerHelper echoServer (9);

  ApplicationContainer serverApps = echoServer.Install (nodes);
  serverApps.Start (Seconds (1.0));
  serverApps.Stop (Seconds (10.0));

  UdpEchoClientHelper echoClient (nodes.Get(4)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal(),
                                    9);
  echoClient.SetAttribute ("MaxPackets", UintegerValue (packets));
  echoClient.SetAttribute ("Interval", TimeValue (Seconds (1.0)));
  echoClient.SetAttribute ("PacketSize", UintegerValue (1024));

  ApplicationContainer clientApps = echoClient.Install (nodes.Get (0));
  clientApps.Start (Seconds (2.0));
  clientApps.Stop (Seconds (10.0));
  AsciiTraceHelper asciiHelper;

  Ptr<OutputStreamWrapper> out = asciiHelper.CreateFileStream("route.table");
  NS_LOG_INFO("Node interface list");
  for (int i = 0; i < NODES; i++) {
      Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
      NS_ASSERT(ipv4 != NULL);
      for (int j = 0; j < (int)ipv4->GetNInterfaces(); j++) {
          for (int k = 0; k < (int)ipv4->GetNAddresses(j); k++) {
            Ipv4InterfaceAddress iaddr = ipv4->GetAddress (j, k);
            Ipv4Address addr = iaddr.GetLocal ();
            (*out->GetStream()) << i << "\t" << j << "\t" << addr << "\n";
          }
      }
  }
  for (int i = 0; i < NODES; i++) {
    nodes.Get(i)->GetObject<Ipv4>()->GetRoutingProtocol()->PrintRoutingTable(out);
  }
  Simulator::Run ();
  Simulator::Destroy ();

  for (int i = 0; i < NODES; i++) {
      delete connectivityGraph[i];
  }
  return 0;
}
