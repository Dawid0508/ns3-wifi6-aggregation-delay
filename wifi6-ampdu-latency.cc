/*
 * wifi6-ampdu-latency.cc
 *
 * Proof-of-Concept: Wpływ agregacji ramek A-MPDU na opóźnienia w Wi-Fi 6 (802.11ax)
 *
 * Topologia:
 *
 *   STA1 (bulk UDP) ---[5m]--- AP ---[5m]--- STA2 (VoIP UDP)
 *
 * Scenariusz 1 (4.1) — sweep limitu A-MPDU przy rywalizacji z ruchem masowym:
 *   ./ns3 run "ns3-wifi6-aggregation-delay/wifi6-ampdu-latency \
 *              --maxAmpdu=6500631 --nBackground=5 --run=1 --simTime=30"
 *
 * Szybki tryb on/off (zgodny ze starszymi komendami):
 *   ./ns3 run "ns3-wifi6-aggregation-delay/wifi6-ampdu-latency --ampdu=true"
 *   ./ns3 run "ns3-wifi6-aggregation-delay/wifi6-ampdu-latency --ampdu=false"
 *
 * Pełną kampanię (5 wartości A-MPDU × 10 powtórzeń) odpala run_scenario1.sh,
 * a wyniki agreguje aggregate_scenario1.py.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <cmath>
#include <cstdint>
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
    bool        enableAmpdu = true;       // --ampdu      : szybki on/off (gdy --maxAmpdu < 0)
    int64_t     maxAmpduArg = -1;         // --maxAmpdu   : BE_MaxAmpduSize [B]; <0 => użyj --ampdu
    uint32_t    nBackground = 5;          // --nBackground: liczba stacji ruchu masowego (bulk)
    std::string bulkRate    = "150Mbps";  // --bulkRate   : szybkość OnOff jednej stacji tła
    uint32_t    rngRun      = 1;          // --run        : numer powtórzenia (ziarno RNG)
    double      simTime     = 30.0;       // --simTime    : czas symulacji [s]
    std::string outFile;                  // --outFile    : ścieżka XML (pusta => auto)

    CommandLine cmd(__FILE__);
    cmd.AddValue("ampdu",       "Szybki przełącznik agregacji on/off (gdy --maxAmpdu < 0)", enableAmpdu);
    cmd.AddValue("maxAmpdu",    "Limit BE_MaxAmpduSize w bajtach (0..6500631); < 0 => użyj --ampdu", maxAmpduArg);
    cmd.AddValue("nBackground", "Liczba stacji generujących ruch masowy (bulk)", nBackground);
    cmd.AddValue("bulkRate",    "Szybkość OnOff jednej stacji tła (np. 150Mbps)", bulkRate);
    cmd.AddValue("run",         "Numer powtórzenia / ziarno RNG (RngSeedManager::SetRun)", rngRun);
    cmd.AddValue("simTime",     "Czas trwania symulacji w sekundach", simTime);
    cmd.AddValue("outFile",     "Ścieżka wyjściowego pliku XML (pusta => nazwa automatyczna)", outFile);
    cmd.Parse(argc, argv);

    // Aplikacje startują w t=APP_START (1 s) – czas na skojarzenie STA z AP.
    // Dla simTime <= APP_START obliczenie przepustowości dzieliłoby przez
    // zero lub liczbę ujemną, a aplikacje nie zdążyłyby nadać ruchu.
    if (simTime <= 1.0)
    {
        NS_FATAL_ERROR("simTime (" << simTime << " s) musi być > 1.0 s "
                       "(aplikacje startują dopiero w t=1 s).");
    }
    if (nBackground == 0)
    {
        NS_FATAL_ERROR("nBackground musi być >= 1.");
    }

    // Niezależne powtórzenia: stałe ziarno + zmienny numer biegu (RngRun).
    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(rngRun);

    // Efektywny limit A-MPDU dla klasy BE (sweep RQ1) lub z przełącznika on/off.
    uint32_t maxBe = (maxAmpduArg >= 0) ? static_cast<uint32_t>(maxAmpduArg)
                                        : (enableAmpdu ? 6500631u : 0u);

    std::cout << "\n=== Wi-Fi 6 A-MPDU — Scenariusz 1 ===\n"
              << "BE_MaxAmpduSize : " << maxBe << " B\n"
              << "Stacje tła      : " << nBackground << " x " << bulkRate << "\n"
              << "Powtórzenie     : run=" << rngRun << "\n"
              << "SimTime         : " << simTime << " s\n\n";

    // =========================================================================
    // Węzły sieci
    // =========================================================================
    NodeContainer apNode;
    apNode.Create(1);

    // Indeksy stacji: [0 .. nBackground-1] = ruch masowy (bulk),
    //                 [nBackground]        = ruch VoIP (150 B co 20 ms).
    NodeContainer staNodes;
    staNodes.Create(nBackground + 1);
    const uint32_t VOIP_IDX = nBackground;

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
    // VoIP jest w AC_BE, więc limit kolejki VO nie wpływa na wyniki; ustawiamy = BE.
    uint32_t maxVo = maxBe;

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
    posAlloc->Add(Vector(0.0, 0.0, 0.0)); // AP – centrum
    // Wszystkie STA na okręgu o promieniu 5 m wokół AP (równomiernie po kącie).
    const double   R    = 5.0;
    const uint32_t nSta = nBackground + 1;
    for (uint32_t i = 0; i < nSta; ++i)
    {
        double ang = 2.0 * M_PI * i / nSta;
        posAlloc->Add(Vector(R * std::cos(ang), R * std::sin(ang), 0.0));
    }

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
    // Stacje tła → AP : ciężki ruch UDP (nasycenie współdzielonego kanału)
    //
    // Każda z nBackground stacji uruchamia OnOffApplication w trybie „always on"
    // z dużymi pakietami UDP. Razem nasycają kanał i zmuszają MAC do budowania
    // maksymalnych ramek A-MPDU, co wywołuje badany efekt HOL na ruchu VoIP.
    // -------------------------------------------------------------------------
    OnOffHelper bulkClient("ns3::UdpSocketFactory",
                           InetSocketAddress(apAddr, BULK_PORT));
    bulkClient.SetAttribute("DataRate",   DataRateValue(DataRate(bulkRate)));
    bulkClient.SetAttribute("PacketSize", UintegerValue(1400));  // bliskie MTU Ethernet
    // OnTime = bardzo duży stały czas → aplikacja nigdy nie przechodzi w tryb Off
    bulkClient.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=10000]"));
    bulkClient.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    ApplicationContainer bulkApps;
    for (uint32_t i = 0; i < nBackground; ++i)
    {
        bulkApps.Add(bulkClient.Install(staNodes.Get(i)));
    }
    bulkApps.Start(Seconds(APP_START));
    bulkApps.Stop(Seconds(simTime));

    // -------------------------------------------------------------------------
    // STA2 → AP : ruch VoIP (imitacja kodeka G.711 / G.729)
    //
    // UdpClientHelper wysyła pakiety w stałych odstępach czasu:
    //   150 B co 20 ms = 60 kbps → profil zbliżony do G.729
    //
    // RQ1: ruch VoIP CELOWO pozostaje w domyślnej kategorii AC_BE (brak DSCP),
    // czyli rywalizuje o kanał w tej samej klasie co strumień bulk. Tylko wtedy
    // można zaobserwować, jak rozmiar A-MPDU wpływa na opóźnienia małych,
    // wrażliwych pakietów (efekt blokowania czoła kolejki / HOL). Oznaczenie
    // gniazda jako AC_VO dałoby mu priorytet i ukryło badane zjawisko.
    // -------------------------------------------------------------------------
    UdpClientHelper voipClient(apAddr, VOIP_PORT);
    voipClient.SetAttribute("Interval",   TimeValue(MilliSeconds(20)));
    voipClient.SetAttribute("PacketSize", UintegerValue(150));
    voipClient.SetAttribute("MaxPackets", UintegerValue(0xFFFFFFFF)); // bez limitu pakietów

    ApplicationContainer sta2App = voipClient.Install(staNodes.Get(VOIP_IDX));
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
    // Domyślna szerokość słupka histogramu (1 ms) jest zbyt zgrubna dla opóźnień
    // rzędu mikrosekund – wszystkie pakiety wpadają do 1–2 słupków i CDF wychodzi
    // jako pionowa linia. 10 µs daje setki słupków → gładka krzywa ECDF.
    flowmonHelper.SetMonitorAttribute("DelayBinWidth",  DoubleValue(1e-5));
    flowmonHelper.SetMonitorAttribute("JitterBinWidth", DoubleValue(1e-5));
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

    // Ścieżka względna do katalogu projektu (ns3 run wykonuje z ns-3.47/).
    // Jawny --outFile ma priorytet; w przeciwnym razie nazwa zależy od konfiguracji.
    std::string xmlFile = outFile;
    if (xmlFile.empty())
    {
        std::ostringstream name;
        name << "scratch/ns3-wifi6-aggregation-delay/results/flowmon-ampdu"
             << maxBe << "-bg" << nBackground << "-run" << rngRun << ".xml";
        xmlFile = name.str();
    }
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
