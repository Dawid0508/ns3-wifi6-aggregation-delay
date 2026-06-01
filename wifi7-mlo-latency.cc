/* wifi7-mlo-latency.cc
 *
 * Scenariusz 3: Wi-Fi 7 (802.11be) — wpływ MLO (STR) na opóźnienia VoIP
 *
 * Ta sama metodologia co Scenariusz 1 (Wi-Fi 6): nBackground stacji nasyca
 * wspólny kanał 5 GHz, a stacja VoIP (MLD) konkuruje o medium. Przy MLO=true
 * VoIP korzysta dodatkowo z linku 6 GHz, co ma niwelować blokowanie HOL.
 *
 * Uruchomienie z ~/ns-3.47/:
 *   ./ns3 run "ns3-wifi6-aggregation-delay/wifi7-mlo-latency \
 *              --mlo=true --nBackground=5 --run=1 --simTime=30"
 *
 * Pełną kampanię (MLO on/off × 10 powtórzeń) odpala run_scenario3.sh,
 * a wyniki agreguje aggregate_scenario3.py.
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

#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Wifi7MloLatency");

static const uint16_t BULK_DST_PORT = 5001;
static const uint16_t VOIP_DST_PORT = 5002;

int
main(int argc, char* argv[])
{
    bool        mloEnabled  = true;       // --mlo        : MLO STR (2 linki) on/off
    uint32_t    nBackground = 5;          // --nBackground: liczba stacji ruchu masowego (bulk)
    std::string bulkRate    = "150Mbps";  // --bulkRate   : szybkość OnOff jednej stacji tła
    uint32_t    rngRun      = 1;          // --run        : numer powtórzenia (ziarno RNG)
    double      simTime     = 30.0;       // --simTime    : czas symulacji [s]
    std::string outFile;                  // --outFile    : ścieżka XML (pusta => auto)

    CommandLine cmd(__FILE__);
    cmd.AddValue("mlo",         "Włącz MLO STR (dwa linki: 5 GHz + 6 GHz)", mloEnabled);
    cmd.AddValue("nBackground", "Liczba stacji generujących ruch masowy (bulk)", nBackground);
    cmd.AddValue("bulkRate",    "Szybkość OnOff jednej stacji tła (np. 150Mbps)", bulkRate);
    cmd.AddValue("run",         "Numer powtórzenia / ziarno RNG (RngSeedManager::SetRun)", rngRun);
    cmd.AddValue("simTime",     "Czas symulacji [s]", simTime);
    cmd.AddValue("outFile",     "Ścieżka wyjściowego pliku XML (pusta => nazwa automatyczna)", outFile);
    cmd.Parse(argc, argv);

    // Aplikacje startują w t=1 s (czas na skojarzenie STA z AP), więc symulacja
    // krótsza niż to nie wygeneruje ruchu i zaburzy obliczenia przepustowości.
    const double APP_START = 1.0;
    if (simTime <= APP_START)
    {
        NS_FATAL_ERROR("simTime (" << simTime << " s) musi być > " << APP_START
                       << " s, inaczej aplikacje nie zdążą wystartować.");
    }
    if (nBackground == 0)
    {
        NS_FATAL_ERROR("nBackground musi być >= 1.");
    }

    // Niezależne powtórzenia: stałe ziarno + zmienny numer biegu (RngRun).
    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(rngRun);

    std::cout << "\n=== Wi-Fi 7 MLO — Scenariusz 3 ===\n"
              << "MLO         : " << (mloEnabled ? "ON (5+6 GHz)" : "OFF (1 link)") << "\n"
              << "Stacje tła  : " << nBackground << " x " << bulkRate << "\n"
              << "Powtórzenie : run=" << rngRun << "\n"
              << "SimTime     : " << simTime << " s\n\n";

    // ── Liczba linków ─────────────────────────────────────────────────────────
    uint8_t nLinks = mloEnabled ? 2 : 1;

    // ── Węzły ────────────────────────────────────────────────────────────────
    NodeContainer apNode;   apNode.Create(1);
    NodeContainer bgNodes;  bgNodes.Create(nBackground);  // bulk, single-link 5 GHz
    NodeContainer sta2Node; sta2Node.Create(1);           // VoIP, MLD gdy mlo=true

    // ── Mobilność: AP w centrum, pozostałe STA na okręgu o promieniu 5 m ──────
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
    posAlloc->Add(Vector(0.0, 0.0, 0.0));  // AP
    const double   R    = 5.0;
    const uint32_t nSta = nBackground + 1; // stacje tła + VoIP
    for (uint32_t i = 0; i < nSta; ++i)
    {
        double ang = 2.0 * M_PI * i / nSta;
        posAlloc->Add(Vector(R * std::cos(ang), R * std::sin(ang), 0.0));
    }
    mobility.SetPositionAllocator(posAlloc);
    mobility.Install(apNode);
    mobility.Install(bgNodes);
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

    // ── WifiHelper dla stacji tła (single-link, osobny helper) ───────────────
    WifiHelper wifiBg;
    wifiBg.SetStandard(WIFI_STANDARD_80211be);
    wifiBg.SetRemoteStationManager("ns3::ConstantRateWifiManager",
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

    // ── PHY helper dla stacji tła — ten sam ch5 co AP/VoIP (rywalizują) ──────
    SpectrumWifiPhyHelper phyBg(1);
    phyBg.Set(0, "ChannelSettings", StringValue("{42, 80, BAND_5GHZ, 0}"));
    phyBg.AddChannel(ch5, WIFI_SPECTRUM_5_GHZ);  // ten sam obiekt!

    // ── MAC helpers ───────────────────────────────────────────────────────────
    WifiMacHelper apMac;
    apMac.SetType("ns3::ApWifiMac",
                  "Ssid", SsidValue(Ssid("wifi7")));

    WifiMacHelper bgMac;
    bgMac.SetType("ns3::StaWifiMac",
                  "Ssid",          SsidValue(Ssid("wifi7")),
                  "ActiveProbing", BooleanValue(false));

    WifiMacHelper sta2Mac;
    sta2Mac.SetType("ns3::StaWifiMac",
                    "Ssid",          SsidValue(Ssid("wifi7")),
                    "ActiveProbing", BooleanValue(false));

    // ── Instalacja urządzeń ────────────────────────────────────────────────────
    NetDeviceContainer apDevices  = wifi.Install(phyMld, apMac,   apNode);
    NetDeviceContainer bgDevices  = wifiBg.Install(phyBg, bgMac,  bgNodes);  // wszystkie stacje tła
    NetDeviceContainer sta2Devices = wifi.Install(phyMld, sta2Mac, sta2Node);

    // ── Stos IP ───────────────────────────────────────────────────────────────
    InternetStackHelper internet;
    internet.Install(apNode);
    internet.Install(bgNodes);
    internet.Install(sta2Node);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer apIface = ipv4.Assign(apDevices);
    ipv4.Assign(bgDevices);
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

    // Stacje tła: bulk nasycający wspólny kanał 5 GHz (każda po bulkRate)
    OnOffHelper bulk("ns3::UdpSocketFactory",
                      InetSocketAddress(apIface.GetAddress(0), BULK_DST_PORT));
    bulk.SetAttribute("DataRate",   DataRateValue(DataRate(bulkRate)));
    bulk.SetAttribute("PacketSize", UintegerValue(1400));
    bulk.SetAttribute("OnTime",     StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    bulk.SetAttribute("OffTime",    StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer bulkApps;
    for (uint32_t i = 0; i < nBackground; ++i)
    {
        bulkApps.Add(bulk.Install(bgNodes.Get(i)));
    }
    bulkApps.Start(Seconds(APP_START));
    bulkApps.Stop(Seconds(simTime));

    // STA2: VoIP G.729 — 150 B co 20 ms
    // RQ3: VoIP pozostaje w AC_BE (bez DSCP), tak samo jak w scenariuszu Wi-Fi 6.
    // Dzięki temu w wariancie bez MLO ruch VoIP cierpi przez blokowanie HOL na
    // wspólnym łączu 5 GHz, a włączenie MLO (drugi link 6 GHz) pokazuje, w jakim
    // stopniu wielołączowość niweluje ten problem.
    UdpClientHelper voip(apIface.GetAddress(0), VOIP_DST_PORT);
    voip.SetAttribute("MaxPackets", UintegerValue(0xFFFFFFFF));
    voip.SetAttribute("Interval",   TimeValue(MilliSeconds(20)));
    voip.SetAttribute("PacketSize", UintegerValue(150));
    ApplicationContainer voipApps = voip.Install(sta2Node.Get(0));
    voipApps.Start(Seconds(APP_START));
    voipApps.Stop(Seconds(simTime));

    // ── FlowMonitor ───────────────────────────────────────────────────────────
    FlowMonitorHelper flowHelper;
    // Drobniejszy histogram (10 µs zamiast domyślnego 1 ms) → gładki ECDF zamiast
    // pionowej linii, gdy opóźnienia są rzędu mikrosekund.
    flowHelper.SetMonitorAttribute("DelayBinWidth",  DoubleValue(1e-5));
    flowHelper.SetMonitorAttribute("JitterBinWidth", DoubleValue(1e-5));
    Ptr<FlowMonitor> flowMon = flowHelper.InstallAll();

    // ── Symulacja ─────────────────────────────────────────────────────────────
    Simulator::Stop(Seconds(simTime + 0.5));
    Simulator::Run();

    // ── Zapis wyników ─────────────────────────────────────────────────────────
    // Jawny --outFile ma priorytet; w przeciwnym razie nazwa zależy od konfiguracji.
    std::string xmlFile = outFile;
    if (xmlFile.empty())
    {
        std::ostringstream name;
        name << "scratch/ns3-wifi6-aggregation-delay/results/flowmon-wifi7-mlo"
             << (mloEnabled ? "on" : "off") << "-bg" << nBackground
             << "-run" << rngRun << ".xml";
        xmlFile = name.str();
    }

    flowMon->SerializeToXmlFile(xmlFile, true, true);
    NS_LOG_UNCOND("Wyniki zapisane do: " << xmlFile);

    Simulator::Destroy();
    return 0;
}