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

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("DDC-NSDI-STRETCH");

class Topology : public Object
{
  protected:
    std::vector<UdpEchoClient*> m_clients;
    std::vector<UdpEchoServer*> m_servers;
    std::vector<PointToPointChannel* > m_channels;
    UniformVariable randVar;
    int32_t m_numNodes;
    std::vector<std::list<uint32_t>*> m_connectivityGraph;
    Time m_simulationEnd;
    std::vector<uint32_t> m_nodeTranslate;
    std::map<std::pair<uint32_t, uint32_t>, PointToPointChannel*> m_channelMap;
    std::map<uint32_t, uint32_t> m_nodeForwardTranslationMap;
    NodeContainer m_nodes;
    std::vector<NetDeviceContainer> m_nodeDevices;
    std::vector<NetDeviceContainer> m_linkDevices;
  public:

    void RxPacket (Ptr<const Packet> packet, Ipv4Header& header) {
      NS_LOG_LOGIC("Received packet " << (uint32_t)header.GetTtl());
    }
    
    void ServerRxPacket (Ptr<const Packet> packet, Ipv4Header& header) {
      NS_LOG_LOGIC("Server Received packet " << (uint32_t)header.GetTtl());
    }
    Topology()
    {
      m_numNodes = 0;
      m_simulationEnd = Seconds(60.0 * 60.0 * 24 * 7);
   }
   
    virtual ~Topology()
    {
        for (int i = 0; i < m_numNodes; i++) {
            delete m_connectivityGraph[i];
        }
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
      uint32_t last = 0;
      m_connectivityGraph.resize(m_numNodes);
    
      for (std::map<uint32_t, std::list<uint32_t>*>::iterator it = tempConnectivityGraph.begin();
          it != tempConnectivityGraph.end();
          it++) {
          m_nodeTranslate.push_back(it->first);
          m_nodeForwardTranslationMap[it->first] = last;
          last++;
      }
      for (int i = 0; i < m_numNodes; i++) {
        m_connectivityGraph[i] = new std::list<uint32_t>;
        for (std::list<uint32_t>::iterator it = tempConnectivityGraph[m_nodeTranslate[i]]->begin();
             it != tempConnectivityGraph[m_nodeTranslate[i]]->end();
             it++) {
          m_connectivityGraph[i]->push_back(m_nodeForwardTranslationMap[*it]);
        }
      }
    }

    void HookupSimulation()
    {
      NS_LOG_INFO("Creating nodes");
      m_nodes.Create (m_numNodes);
      m_nodeDevices.resize(m_numNodes);
      NS_LOG_INFO("Creating point to point connections");
      PointToPointHelper pointToPoint;
      for (int i = 0; i < m_numNodes; i++) {
        for ( std::list<uint32_t>::iterator iterator = m_connectivityGraph[i]->begin(); 
        iterator != m_connectivityGraph[i]->end();
        iterator++) {
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
      for (int i = 0; i < m_numNodes; i++) {
        m_servers[i] =  (UdpEchoServer*)PeekPointer(serverApps.Get(i));
        m_servers[i]->AddReceivePacketEvent(MakeCallback(&Topology::ServerRxPacket, this));
        int j = 0;
        UdpEchoClientHelper echoClient (m_nodes.Get(j)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal(),
                                          9);
        echoClient.SetAttribute ("MaxPackets", UintegerValue (0));
        echoClient.SetAttribute ("Interval", TimeValue (Seconds (randVar.GetValue(1.0, 3000.0))));
        echoClient.SetAttribute ("PacketSize", UintegerValue (1024));
        ApplicationContainer clientApps = echoClient.Install (m_nodes.Get (i));
        m_clients[i] = (UdpEchoClient*)(PeekPointer(clientApps.Get(0)));
        Simulator::ScheduleNow(&UdpEchoClient::StartApplication, m_clients[i]);
      }
      //clients[11]->SetAttribute("RemoteAddress",
      //  AddressValue(nodes.Get(0)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal()));
      //Simulator::ScheduleNow(&UdpEchoClient::StartApplication, clients[11]);
      //clients[11]->AddReceivePacketEvent(MakeCallback(&RxPacket));
      //Simulator::Schedule(Seconds(1.0), &UdpEchoClient::Send, clients[11]);

      /*Ptr<OutputStreamWrapper> out = asciiHelper.CreateFileStream("route.table");
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
      }*/
    }
};

int
main (int argc, char *argv[])
{
  AsciiTraceHelper asciiHelper;

  Ptr<OutputStreamWrapper> stream = asciiHelper.CreateFileStream ("tcp-trace.tr");
  uint32_t packets = 1;
  bool simulateError;
  CommandLine cmd;
  cmd.AddValue("packets", "Number of packets to echo", packets);
  cmd.AddValue("error", "Simulate error", simulateError);
  cmd.Parse(argc, argv);
  //LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
  //LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);

  //Simulator::Run ();
  //Simulator::Destroy ();

  return 0;
}