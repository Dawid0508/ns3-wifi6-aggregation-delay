/*
 * wifi6-ampdu-latency.cc
 *
 * Proof-of-Concept: Wpływ agregacji ramek A-MPDU na opóźnienia w Wi-Fi 6 (802.11ax)
 *
 * Topologia:
 *
 *   STA1 (bulk UDP) ---[5m]--- AP ---[5m]--- STA2 (VoIP UDP)
 *
 * Scenariusze:
 *   ./ns3 run "wifi6-ampdu-latency --ampdu=true"   → agregacja włączona (domyślnie)
 *   ./ns3 run "wifi6-ampdu-latency --ampdu=false"  → agregacja wyłączona
 *
 * Wyniki: flowmon-results-ampdu-{on|off}.xml + tabela w konsoli
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <iomanip>
#include <sstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Wifi6AmpduLatency");

int
main(int argc, char* argv[])
{
    // =========================================================================
    // Parametry symulacji (dostępne przez wiersz poleceń)
    // =========================================================================
    bool   enableAmpdu = true;  // --ampdu   : włącz/wyłącz agregację A-MPDU
    double simTime     = 15.0;  // --simTime : czas symulacji [s]

    CommandLine cmd(__FILE__);
    cmd.AddValue("ampdu",   "Włącz maksymalną agregację A-MPDU (true/false)", enableAmpdu);
    cmd.AddValue("simTime", "Czas trwania symulacji w sekundach",             simTime);
    cmd.Parse(argc, argv);

    std::cout << "\n=== Wi-Fi 6 A-MPDU Latency PoC ===\n"
              << "A-MPDU   : " << (enableAmpdu ? "ENABLED"  : "DISABLED") << "\n"
              << "SimTime  : " << simTime << " s\n\n";

    // =========================================================================
    // Węzły sieci
    // =========================================================================
    NodeContainer apNode;
    apNode.Create(1);

    // staNodes.Get(0) = STA1 — ciężki ruch UDP (bulk)
    // staNodes.Get(1) = STA2 — ruch VoIP (150 B co 20 ms)
    NodeContainer staNodes;
    staNodes.Create(2);

    // =========================================================================
    // Warstwa fizyczna (PHY) – 802.11ax, 5 GHz, 80 MHz
    // =========================================================================
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper     phy;
    phy.SetChannel(channel.Create());

    // Kanał 0 = ns-3 dobiera domyślny; 80 MHz w paśmie 5 GHz
    // (odpowiada np. kanałom 36-48 ze środkiem na 42 w standardzie Wi-Fi)
    phy.Set("ChannelSettings", StringValue("{0, 80, BAND_5GHZ, 0}"));

    // =========================================================================
    // Warstwa MAC + standard 802.11ax
    // =========================================================================
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);

    // Idealny menedżer szybkości transmisji — w PoC eliminuje szum losowy
    // z doboru MCS i pozwala skupić się na efekcie A-MPDU
    wifi.SetRemoteStationManager("ns3::IdealWifiManager");

    // -------------------------------------------------------------------------
    // Konfiguracja A-MPDU per Access Category
    //
    // 802.11ax dopuszcza do 6 500 631 B (≈6,2 MB) na jeden A-MPDU.
    // Wartość 0 wyłącza agregację dla danej kolejki AC.
    //
    // BE (Best Effort) – używane przez STA1 (bulk) i STA2 (VoIP domyślnie)
    // VO (Voice)       – używane przez STA2 jeśli gniazdo zostanie oznaczone QoS
    // -------------------------------------------------------------------------
    uint32_t maxBe = enableAmpdu ? 6500631u : 0u;
    uint32_t maxVo = enableAmpdu ? 65535u   : 0u; // mniejszy limit dla klasy Voice

    WifiMacHelper mac;
    Ssid ssid("wifi6-lab");

    // AP
    mac.SetType("ns3::ApWifiMac",
                "Ssid",            SsidValue(ssid),
                "BE_MaxAmpduSize", UintegerValue(maxBe),
                "VO_MaxAmpduSize", UintegerValue(maxVo));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, apNode);

    // Stacje klienckie
    mac.SetType("ns3::StaWifiMac",
                "Ssid",            SsidValue(ssid),
                "BE_MaxAmpduSize", UintegerValue(maxBe),
                "VO_MaxAmpduSize", UintegerValue(maxVo));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, staNodes);

    // =========================================================================
    // Mobilność – pozycje stałe (ConstantPositionMobilityModel)
    // =========================================================================
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
    posAlloc->Add(Vector( 0.0, 0.0, 0.0)); // AP   – centrum
    posAlloc->Add(Vector( 5.0, 0.0, 0.0)); // STA1 – 5 m od AP
    posAlloc->Add(Vector(-5.0, 0.0, 0.0)); // STA2 – 5 m od AP

    mobility.SetPositionAllocator(posAlloc);
    mobility.Install(apNode);
    mobility.Install(staNodes);

    // =========================================================================
    // Stos sieciowy TCP/IP + adresacja IPv4
    // =========================================================================
    InternetStackHelper internet;
    internet.Install(apNode);
    internet.Install(staNodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer apIface  = ipv4.Assign(apDevice);
    Ipv4InterfaceContainer staIface = ipv4.Assign(staDevices);

    Ipv4Address apAddr = apIface.GetAddress(0);

    // =========================================================================
    // Aplikacje
    // =========================================================================
    const uint16_t BULK_PORT = 5001;
    const uint16_t VOIP_PORT = 5002;
    const double   APP_START = 1.0; // [s] – czas na skojarzenie STA z AP

    // -------------------------------------------------------------------------
    // STA1 → AP : ciężki ruch UDP (nasycenie kanału)
    //
    // OnOffApplication w trybie „always on" z dużymi pakietami UDP.
    // Symuluje np. transfer pliku lub strumień wideo HD w tle.
    // -------------------------------------------------------------------------
    OnOffHelper bulkClient("ns3::UdpSocketFactory",
                           InetSocketAddress(apAddr, BULK_PORT));
    bulkClient.SetAttribute("DataRate",   DataRateValue(DataRate("150Mbps")));
    bulkClient.SetAttribute("PacketSize", UintegerValue(1400));  // bliskie MTU Ethernet
    // OnTime = bardzo duży stały czas → aplikacja nigdy nie przechodzi w tryb Off
    bulkClient.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=10000]"));
    bulkClient.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    ApplicationContainer sta1App = bulkClient.Install(staNodes.Get(0));
    sta1App.Start(Seconds(APP_START));
    sta1App.Stop(Seconds(simTime));

    // -------------------------------------------------------------------------
    // STA2 → AP : ruch VoIP (imitacja kodeka G.711 / G.729)
    //
    // UdpClientHelper wysyła pakiety w stałych odstępach czasu:
    //   150 B co 20 ms = 60 kbps → profil zbliżony do G.729
    //
    // Uwaga: bez oznaczenia DSCP pakiety trafiają do kolejki AC_BE.
    // Aby użyć kolejki AC_VO, należy oznaczyć gniazdo socketem QoS
    // (SetIpTos z wartością DSCP EF = 0xB8).
    // -------------------------------------------------------------------------
    UdpClientHelper voipClient(apAddr, VOIP_PORT);
    voipClient.SetAttribute("Interval",   TimeValue(MilliSeconds(20)));
    voipClient.SetAttribute("PacketSize", UintegerValue(150));
    voipClient.SetAttribute("MaxPackets", UintegerValue(0xFFFFFFFF)); // bez limitu pakietów

    ApplicationContainer sta2App = voipClient.Install(staNodes.Get(1));
    sta2App.Start(Seconds(APP_START));
    sta2App.Stop(Seconds(simTime));

    // -------------------------------------------------------------------------
    // Odbiorniki UDP na AP (wymagane, by pakiety dotarły do warstwy aplikacji)
    // -------------------------------------------------------------------------
    PacketSinkHelper bulkSink("ns3::UdpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), BULK_PORT));
    PacketSinkHelper voipSink("ns3::UdpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), VOIP_PORT));

    ApplicationContainer sinks;
    sinks.Add(bulkSink.Install(apNode.Get(0)));
    sinks.Add(voipSink.Install(apNode.Get(0)));
    sinks.Start(Seconds(0.0));
    sinks.Stop(Seconds(simTime + 2.0));

    // =========================================================================
    // FlowMonitor – monitorowanie ruchu per przepływ IP
    // =========================================================================
    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor>  monitor = flowmonHelper.InstallAll();

    // =========================================================================
    // Uruchomienie symulacji
    // =========================================================================
    Simulator::Stop(Seconds(simTime + 1.0));
    Simulator::Run();

    // =========================================================================
    // Eksport wyników FlowMonitor do pliku XML
    // =========================================================================
    monitor->CheckForLostPackets();

    // Ścieżka względna do katalogu projektu (ns3 run wykonuje z ns-3.47/)
    std::string xmlFile = std::string("scratch/ns3-wifi6-aggregation-delay/results/flowmon-results-") +
                          (enableAmpdu ? "ampdu-on" : "ampdu-off") + ".xml";
    // Argumenty: nazwa pliku, dołącz HistogramOfDelays, dołącz HistogramOfJitters
    monitor->SerializeToXmlFile(xmlFile, true, true);
    std::cout << ">>> Plik XML zapisany: " << xmlFile << "\n\n";

    // =========================================================================
    // Podsumowanie w konsoli
    // =========================================================================
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    const FlowMonitor::FlowStatsContainer& stats = monitor->GetFlowStats();

    const int W = 10;
    std::cout << std::left
              << std::setw(W)      << "FlowID"
              << std::setw(26)     << "Źródło (IP:port)"
              << std::setw(26)     << "Cel (IP:port)"
              << std::setw(W)      << "TxPkts"
              << std::setw(W)      << "RxPkts"
              << std::setw(W)      << "Lost"
              << std::setw(16)     << "Avg Delay[ms]"
              << std::setw(16)     << "Avg Jitter[ms]"
              << std::setw(14)     << "Throughput"
              << "\n"
              << std::string(118, '-') << "\n";

    for (const auto& [id, s] : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(id);

        std::ostringstream src, dst;
        src << t.sourceAddress      << ":" << t.sourcePort;
        dst << t.destinationAddress << ":" << t.destinationPort;

        double avgDelayMs  = (s.rxPackets > 0)
                             ? s.delaySum.GetSeconds() / s.rxPackets * 1e3
                             : 0.0;
        double avgJitterMs = (s.rxPackets > 1)
                             ? s.jitterSum.GetSeconds() / (s.rxPackets - 1) * 1e3
                             : 0.0;
        // Przepustowość względem całego czasu symulacji (bez czasu rozgrzewki)
        double throughputMbps = s.rxBytes * 8.0 / (simTime - APP_START) / 1e6;

        std::cout << std::left  << std::fixed << std::setprecision(3)
                  << std::setw(W)  << id
                  << std::setw(26) << src.str()
                  << std::setw(26) << dst.str()
                  << std::setw(W)  << s.txPackets
                  << std::setw(W)  << s.rxPackets
                  << std::setw(W)  << s.lostPackets
                  << std::setw(16) << avgDelayMs
                  << std::setw(16) << avgJitterMs
                  << throughputMbps << " Mbps\n";
    }

    std::cout << "\n";
    Simulator::Destroy();
    return 0;
}
