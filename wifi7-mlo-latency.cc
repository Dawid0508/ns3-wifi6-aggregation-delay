/* wifi7-mlo-latency.cc
 *
 * Scenariusz 3: Wi-Fi 7 (802.11be) — wpływ MLO (STR) na opóźnienia VoIP
 *
 * Uruchomienie z ~/ns-allinone-3.47/ns-3.47/:
 *   ./ns3 run "wifi7-mlo/wifi7-mlo-latency --mlo=true  --simTime=15"
 *   ./ns3 run "wifi7-mlo/wifi7-mlo-latency --mlo=false --simTime=15"
 *
 * Wyniki: scratch/wifi7-mlo/results/flowmon-results-wifi7-mlo-{on|off}.xml
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/multi-model-spectrum-channel.h"
#include "ns3/neighbor-cache-helper.h"
#include "ns3/network-module.h"
#include "ns3/spectrum-wifi-helper.h"
#include "ns3/wifi-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Wifi7MloLatency");

static const uint16_t BULK_DST_PORT = 5001;
static const uint16_t VOIP_DST_PORT = 5002;

int
main(int argc, char* argv[])
{
    bool mloEnabled = true;
    double simTime  = 15.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("mlo",     "Włącz MLO STR (dwa linki: 5 GHz + 6 GHz)", mloEnabled);
    cmd.AddValue("simTime", "Czas symulacji [s]",                         simTime);
    cmd.Parse(argc, argv);

    // ── Liczba linków ─────────────────────────────────────────────────────────
    uint8_t nLinks = mloEnabled ? 2 : 1;

    // ── Węzły ────────────────────────────────────────────────────────────────
    NodeContainer apNode;   apNode.Create(1);
    NodeContainer sta1Node; sta1Node.Create(1);  // bulk, zawsze single-link
    NodeContainer sta2Node; sta2Node.Create(1);  // VoIP, MLD gdy mlo=true

    // ── Mobilność ─────────────────────────────────────────────────────────────
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
    posAlloc->Add(Vector(0.0,  0.0, 0.0));  // AP
    posAlloc->Add(Vector(-5.0, 0.0, 0.0));  // STA1
    posAlloc->Add(Vector( 5.0, 0.0, 0.0));  // STA2
    mobility.SetPositionAllocator(posAlloc);
    mobility.Install(apNode);
    mobility.Install(sta1Node);
    mobility.Install(sta2Node);

    // ── WifiHelper dla MLD (AP + STA2) ───────────────────────────────────────
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211be);
    wifi.SetRemoteStationManager((uint8_t)0,
                                 "ns3::ConstantRateWifiManager",
                                 "DataMode",    StringValue("EhtMcs9"),
                                 "ControlMode", StringValue("OfdmRate54Mbps"));
    if (mloEnabled)
    {
        wifi.SetRemoteStationManager((uint8_t)1,
                                     "ns3::ConstantRateWifiManager",
                                     "DataMode",    StringValue("EhtMcs9"),
                                     "ControlMode", StringValue("EhtMcs0"));
    }

    // ── WifiHelper dla STA1 (single-link, osobny helper) ─────────────────────
    WifiHelper wifiSta1;
    wifiSta1.SetStandard(WIFI_STANDARD_80211be);
    wifiSta1.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                     "DataMode",    StringValue("EhtMcs9"),
                                     "ControlMode", StringValue("OfdmRate54Mbps"));

    // ── Wspólny kanał 5 GHz — wszyscy go dzielą (STA1, AP, STA2) ────────────
    auto ch5 = CreateObject<MultiModelSpectrumChannel>();
    ch5->AddPropagationLossModel(CreateObject<LogDistancePropagationLossModel>());
    ch5->SetPropagationDelayModel(CreateObject<ConstantSpeedPropagationDelayModel>());

    // ── PHY helper dla MLD (AP + STA2) ───────────────────────────────────────
    SpectrumWifiPhyHelper phyMld(nLinks);
    phyMld.Set(0, "ChannelSettings", StringValue("{42, 80, BAND_5GHZ, 0}"));
    phyMld.AddChannel(ch5, WIFI_SPECTRUM_5_GHZ);  // link 0: wspólny kanał 5 GHz

    if (mloEnabled)
    {
        // Link 1: 6 GHz osobny kanał — VoIP może korzystać z obu linków
        auto ch6 = CreateObject<MultiModelSpectrumChannel>();
        ch6->AddPropagationLossModel(CreateObject<LogDistancePropagationLossModel>());
        ch6->SetPropagationDelayModel(CreateObject<ConstantSpeedPropagationDelayModel>());
        phyMld.Set(1, "ChannelSettings", StringValue("{7, 80, BAND_6GHZ, 0}"));
        phyMld.AddChannel(ch6, WIFI_SPECTRUM_6_GHZ);
    }

    // ── PHY helper dla STA1 — ten sam ch5 co AP/STA2 (rywalizują o kanał) ───
    SpectrumWifiPhyHelper phySta1(1);
    phySta1.Set(0, "ChannelSettings", StringValue("{42, 80, BAND_5GHZ, 0}"));
    phySta1.AddChannel(ch5, WIFI_SPECTRUM_5_GHZ);  // ten sam obiekt!

    // ── MAC helpers ───────────────────────────────────────────────────────────
    WifiMacHelper apMac;
    apMac.SetType("ns3::ApWifiMac",
                  "Ssid", SsidValue(Ssid("wifi7")));

    WifiMacHelper sta1Mac;
    sta1Mac.SetType("ns3::StaWifiMac",
                    "Ssid",          SsidValue(Ssid("wifi7")),
                    "ActiveProbing", BooleanValue(false));

    WifiMacHelper sta2Mac;
    sta2Mac.SetType("ns3::StaWifiMac",
                    "Ssid",          SsidValue(Ssid("wifi7")),
                    "ActiveProbing", BooleanValue(false));

    // ── Instalacja urządzeń ────────────────────────────────────────────────────
    NetDeviceContainer apDevices   = wifi.Install(phyMld,  apMac,   apNode);
    NetDeviceContainer sta1Devices = wifiSta1.Install(phySta1, sta1Mac, sta1Node);
    NetDeviceContainer sta2Devices = wifi.Install(phyMld,  sta2Mac, sta2Node);

    // ── Stos IP ───────────────────────────────────────────────────────────────
    InternetStackHelper internet;
    internet.Install(apNode);
    internet.Install(sta1Node);
    internet.Install(sta2Node);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer apIface = ipv4.Assign(apDevices);
    ipv4.Assign(sta1Devices);
    ipv4.Assign(sta2Devices);

    NeighborCacheHelper neighborCache;
    neighborCache.PopulateNeighborCache();

    // ── Aplikacje ─────────────────────────────────────────────────────────────
    PacketSinkHelper sinkBulk("ns3::UdpSocketFactory",
                               InetSocketAddress(Ipv4Address::GetAny(), BULK_DST_PORT));
    PacketSinkHelper sinkVoip("ns3::UdpSocketFactory",
                               InetSocketAddress(Ipv4Address::GetAny(), VOIP_DST_PORT));
    ApplicationContainer sinks;
    sinks.Add(sinkBulk.Install(apNode.Get(0)));
    sinks.Add(sinkVoip.Install(apNode.Get(0)));
    sinks.Start(Seconds(0.0));
    sinks.Stop(Seconds(simTime));

    // STA1: bulk ~150 Mbps
    OnOffHelper bulk("ns3::UdpSocketFactory",
                      InetSocketAddress(apIface.GetAddress(0), BULK_DST_PORT));
    bulk.SetAttribute("DataRate",   DataRateValue(DataRate("150Mbps")));
    bulk.SetAttribute("PacketSize", UintegerValue(1400));
    bulk.SetAttribute("OnTime",     StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    bulk.SetAttribute("OffTime",    StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer bulkApps = bulk.Install(sta1Node.Get(0));
    bulkApps.Start(Seconds(1.0));
    bulkApps.Stop(Seconds(simTime));

    // STA2: VoIP G.729 — 150 B co 20 ms
    UdpClientHelper voip(apIface.GetAddress(0), VOIP_DST_PORT);
    voip.SetAttribute("MaxPackets", UintegerValue(0xFFFFFFFF));
    voip.SetAttribute("Interval",   TimeValue(MilliSeconds(20)));
    voip.SetAttribute("PacketSize", UintegerValue(150));
    ApplicationContainer voipApps = voip.Install(sta2Node.Get(0));
    voipApps.Start(Seconds(1.0));
    voipApps.Stop(Seconds(simTime));

    // ── FlowMonitor ───────────────────────────────────────────────────────────
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMon = flowHelper.InstallAll();

    // ── Symulacja ─────────────────────────────────────────────────────────────
    Simulator::Stop(Seconds(simTime + 0.5));
    Simulator::Run();

    // ── Zapis wyników ─────────────────────────────────────────────────────────
    std::string xmlFile = mloEnabled
        ? "scratch/wifi7-mlo/results/flowmon-results-wifi7-mlo-on.xml"
        : "scratch/wifi7-mlo/results/flowmon-results-wifi7-mlo-off.xml";

    flowMon->SerializeToXmlFile(xmlFile, true, true);
    NS_LOG_UNCOND("Wyniki zapisane do: " << xmlFile);

    Simulator::Destroy();
    return 0;
}