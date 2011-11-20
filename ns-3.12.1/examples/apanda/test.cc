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

Ipv4InterfaceContainer
AssignToSameAddress ( const NetDeviceContainer &c, Ipv4AddressHelper& helper) {
  NS_LOG_FUNCTION_NOARGS ();
  Ipv4InterfaceContainer retval;
  Ipv4Address address = helper.NewAddress ();
  for (uint32_t i = 0; i < c.GetN (); ++i) {
      Ptr<NetDevice> device = c.Get (i);

      Ptr<Node> node = device->GetNode ();
      NS_ASSERT_MSG (node, "Ipv4AddressHelper::Assign(): NetDevice is not not associated "
                     "with any node -> fail");

      Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
      NS_ASSERT_MSG (ipv4, "Ipv4AddressHelper::Assign(): NetDevice is associated"
                     " with a node without IPv4 stack installed -> fail "
                     "(maybe need to use InternetStackHelper?)");

      int32_t interface = ipv4->GetInterfaceForDevice (device);
      if (interface == -1)
        {
          interface = ipv4->AddInterface (device);
        }
      NS_ASSERT_MSG (interface >= 0, "Ipv4AddressHelper::Assign(): "
                     "Interface index not found");

      Ipv4InterfaceAddress ipv4Addr = Ipv4InterfaceAddress (address, ((Ipv4Mask)"255.255.255.0").Get());
      ipv4->AddAddress (interface, ipv4Addr);
      ipv4->SetMetric (interface, 1);
      ipv4->SetUp (interface);
      retval.Add (ipv4, interface);
  }
  helper.NewNetwork();
  return retval;
}

void PrintRoutingTable(Ptr<Node> node) {
  Ipv4StaticRoutingHelper helper;
  Ptr<Ipv4> stack = node -> GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> staticrouting = helper.GetStaticRouting(stack);
  uint32_t numroutes=staticrouting->GetNRoutes();
  Ipv4RoutingTableEntry entry;
  NS_LOG_INFO("Routing table for device: " << Names::FindName(node) <<"\n");
  NS_LOG_INFO("Destination\tMask\t\tGateway\t\tIface\n");
  for (uint32_t i =0 ; i<numroutes;i++) {
      entry =staticrouting->GetRoute(i);
      NS_LOG_INFO(entry.GetDestNetwork()  << "\t" << entry.GetDestNetworkMask() << "\t" << entry.GetGateway() << "\t\t" << entry.GetInterface() << "\n");
   }
  return;
}

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
  //connectivityGraph[0]->push_back(1);
  //connectivityGraph[1]->push_back(2);
  //connectivityGraph[2]->push_back(3);
  //connectivityGraph[3]->push_back(4);
  //connectivityGraph[0]->push_back(4);


  NS_LOG_INFO("Creating nodes");
  NodeContainer nodes;
  nodes.Create (NODES);

  NS_LOG_INFO("Creating point to point connections");
  PointToPointHelper pointToPoint;

  NetDeviceContainer devices;
  std::vector<NetDeviceContainer> nodeDevices(NODES);
  for (int i = 0; i < NODES; i++) {
    for ( std::vector<uint32_t>::iterator iterator = connectivityGraph[i]->begin(); 
    iterator != connectivityGraph[i]->end();
    iterator++) {
      NetDeviceContainer p2pDevices = 
        pointToPoint.Install (nodes.Get(i), nodes.Get(*iterator));
      devices.Add(p2pDevices);
      nodeDevices[i].Add(p2pDevices.Get(0));
      nodeDevices[*iterator].Add(p2pDevices.Get(1));
    }
  }

  InternetStackHelper stack;
  stack.Install (nodes);
  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");

  NS_LOG_INFO("Assigning address");
  std::vector<Ipv4InterfaceContainer> interfacesPerNode(NODES);
  for (int i = 0; i < NODES; i++) {
    //Ipv4InterfaceContainer current = AssignToSameAddress(nodeDevices[i], address);
    Ipv4InterfaceContainer current = address.Assign(nodeDevices[i]);
    // Need to do this to get routing to work correctly, no idea why
    address.NewNetwork();
    interfacesPerNode[i].Add(current);
  }
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // Simulate error
  if (simulateError) {
    interfacesPerNode[2].Get(0).first->SetDown(1);
  }

  UdpEchoServerHelper echoServer (9);

  ApplicationContainer serverApps = echoServer.Install (nodes);
  serverApps.Start (Seconds (1.0));
  serverApps.Stop (Seconds (10.0));

  UdpEchoClientHelper echoClient (interfacesPerNode[4].GetAddress (0), 9);
  echoClient.SetAttribute ("MaxPackets", UintegerValue (packets));
  echoClient.SetAttribute ("Interval", TimeValue (Seconds (1.0)));
  echoClient.SetAttribute ("PacketSize", UintegerValue (1024));

  ApplicationContainer clientApps = echoClient.Install (nodes.Get (0));
  clientApps.Start (Seconds (2.0));
  clientApps.Stop (Seconds (10.0));
  AsciiTraceHelper asciiHelper;
  pointToPoint.EnableAsciiAll(asciiHelper.CreateFileStream("ddc.tr"));
  pointToPoint.EnablePcapAll("ddc1");

  Simulator::Run ();
  Simulator::Destroy ();

  for (int i = 0; i < NODES; i++) {
      delete connectivityGraph[i];
  }
  return 0;
}
