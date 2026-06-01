/**
 * Vienna single-HO replay: PCI 331 (gNB 13) -> PCI 286 (gNB 11)
 *
 * Reproduces the inter-gNB handover observed at 2025-02-08 03:35:14.766 UTC
 * in the Vienna 5G drive-test dataset.  Both cells are operator A, band n78
 * @ 3540 MHz TDD.  The O-RAN Near-RT RIC runs OranLmNr2NrRsrpSinrHandover
 * as the default Logic Module.
 *
 * === PARAMETER PROVENANCE ===
 *
 * FROM DATASET (Vienna drive-test 2025-02-08):
 *   - gNB 13 position          (0, 0, 36) m  (ENU origin)
 *   - gNB 11 position          (592.62, -431.55, 58) m
 *   - gNB 13 mech. azimuth     145 deg
 *   - gNB 11 mech. azimuth     155 deg
 *   - Center frequency          3540 MHz (n78, channel 636000)
 *   - UE waypoints (10 pts)    from GPS, height 1.5 m
 *   - UE mean speed             ~6 m/s
 *   - Real HO time              t=10.766 s (relative to 03:35:04.000)
 *
 * 3GPP DEFAULTS / ASSUMED (calibration knobs):
 *   - gNB Tx power              49 dBm  (3GPP 38.104 Table 6.2.1-1, macro)
 *   - UE Tx power               23 dBm  (3GPP 38.101 default)
 *   - Bandwidth                  100 MHz (n78 typical, not from dataset)
 *   - TDD pattern                DDDSU   (common n78 config, not measured)
 *   - Numerology                 mu=1, 30 kHz SCS
 *   - gNB antenna                8x8 cross-pol (assumed macro panel)
 *   - UE antenna                 2x2
 *   - Downtilt                   6 deg   (typical urban macro)
 *   - Pol slant angle            45 deg  (cross-pol convention)
 *   - Propagation model          3GPP TR 38.901 UMa, NLOS, shadowing ON
 *   - LM SinrThresholdDb         32.0 dB (raised from 5.0 default; see note below)
 *   - LM HysteresisDb            0.0 dB  (reduced from 3.0 default; see note below)
 *   - LM ProcessingDelay         10 ms
 *   - RIC query interval         0.1 s
 *   - E2 report interval         0.1 s (100 ms)
 *   - Building model             NONE (follow-up task)
 *
 * DATASET LIMITATION — SINR THRESHOLD:
 *   The dataset only contains gNB info for the two cells involved in this HO.
 *   The real network has 20+ co-channel n78 cells whose interference drives
 *   serving SINR down to ~5 dB.  With only 2 cells the simulated SINR is
 *   ~25 dB higher than reality (~30 dB vs ~5 dB).  The SINR threshold is
 *   therefore raised from the LM default (5 dB) to 30 dB to compensate for
 *   the missing inter-cell interference.  This is a calibration knob:
 *   --sinr-threshold=<value> on the command line.
 *
 * DATASET LIMITATION — RSRP HYSTERESIS:
 *   The real network uses beam-level measurements (L1-RSRP per SSB beam)
 *   for HO decisions, while this LM only sees cell-wide RSRP aggregated
 *   across beams.  Setting hysteresis to 0 dB at cell level approximates
 *   what a beam-level decision with 3 dB hysteresis would produce.
 *   Calibration knob: --hysteresis=<value> on the command line.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/nr-module.h"
#include "ns3/oran-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/uniform-planar-array.h"

#include <cfloat>
#include <cmath>
#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ViennaHo331286RsrpSinrLm");

// ─── Real PCI assignments ────────────────────────────────────────────────────
static const uint16_t PCI_SOURCE = 331; // gNB 13 (source)
static const uint16_t PCI_TARGET = 286; // gNB 11 (target)

// ─── cellId <-> PCI mapping (populated after InstallGnbDevice) ───────────────
static std::map<uint16_t, uint16_t> g_cellIdToPci;
static std::map<uint16_t, uint16_t> g_pciToCellId;

// ─── CSV logging state ───────────────────────────────────────────────────────
static std::ofstream g_metricsLog;
static double g_rsrpServing = -DBL_MAX;
static double g_rsrpNeighbor = -DBL_MAX;
static double g_sinrServing = -DBL_MAX;
static uint16_t g_servingCellId = 0;
static bool g_hoCommandIssued = false;
static double g_tOffset = 3.0;

static uint16_t
CellIdToPci(uint16_t cellId)
{
    auto it = g_cellIdToPci.find(cellId);
    return (it != g_cellIdToPci.end()) ? it->second : 0;
}

// ─── PHY trace callbacks for CSV logging ─────────────────────────────────────
void
TraceRsrpRsrq(uint16_t rnti,
               uint16_t cellId,
               double rsrp,
               double rsrq,
               bool isServingCell,
               uint8_t componentCarrierId)
{
    if (isServingCell || cellId == g_servingCellId)
    {
        g_rsrpServing = rsrp;
    }
    else
    {
        uint16_t pci = CellIdToPci(cellId);
        if (pci == PCI_TARGET || pci == PCI_SOURCE)
        {
            g_rsrpNeighbor = rsrp;
        }
    }
}

void
TraceSinr(uint16_t cellId, uint16_t rnti, double sinr, uint16_t bwpId)
{
    if (cellId == g_servingCellId)
    {
        g_sinrServing = 10.0 * std::log10(sinr);
    }
}

// ─── Handover trace callbacks ────────────────────────────────────────────────
void
NotifyHandoverStart(std::string context,
                    uint64_t imsi,
                    uint16_t cellId,
                    uint16_t rnti,
                    uint16_t targetCellId)
{
    double t = Simulator::Now().GetSeconds();
    std::cout << t << "s HO START: PCI " << CellIdToPci(cellId) << " -> "
              << CellIdToPci(targetCellId) << " (cellId " << cellId << " -> "
              << targetCellId << ")" << std::endl;
    g_hoCommandIssued = true;
}

void
NotifyHandoverEndOk(std::string context,
                    uint64_t imsi,
                    uint16_t cellId,
                    uint16_t rnti)
{
    double t = Simulator::Now().GetSeconds();
    std::cout << t << "s HO COMPLETE: -> PCI " << CellIdToPci(cellId) << " (cellId "
              << cellId << ") RNTI " << rnti << std::endl;
    g_servingCellId = cellId;
}

// ─── Periodic CSV logger ─────────────────────────────────────────────────────
void
LogMetrics(Ptr<NrUeNetDevice> ueDev, Ptr<Node> ueNode, Time interval)
{
    double simTime = Simulator::Now().GetSeconds();
    double relTime = simTime - g_tOffset;

    g_servingCellId = ueDev->GetCellId();
    uint16_t servingPci = CellIdToPci(g_servingCellId);

    if (g_metricsLog.is_open())
    {
        g_metricsLog << relTime << "," << servingPci << "," << g_rsrpServing << ","
                     << g_rsrpNeighbor << "," << g_sinrServing << ","
                     << (g_hoCommandIssued ? 1 : 0) << std::endl;
    }

    g_hoCommandIssued = false;

    Simulator::Schedule(interval, &LogMetrics, ueDev, ueNode, interval);
}

// ─── SQL debug callback ──────────────────────────────────────────────────────
void
QueryRcSink(std::string query, std::string args, int rc)
{
    std::cout << Simulator::Now().GetSeconds() << " Query "
              << ((rc == SQLITE_OK || rc == SQLITE_DONE) ? "OK" : "ERROR") << "(" << rc
              << "): \"" << query << "\"";
    if (!args.empty())
    {
        std::cout << " (" << args << ")";
    }
    std::cout << std::endl;
}

// ─── main ────────────────────────────────────────────────────────────────────
int
main(int argc, char* argv[])
{
    // --- Configurable parameters (all with 3GPP defaults unless noted) ---
    std::string dbFileName = "vienna-ho-331-286-rsrp-sinr-lm.db";
    std::string metricsPath = "vienna-ho-331-286-metrics.csv";
    double gnbTxPower = 49.0;
    double ueTxPower = 23.0;
    double bandwidthHz = 100e6;
    uint8_t numerology = 1;
    double sinrThreshold = 32.0;
    double hysteresisDb = 0.0;
    double lmProcessingDelayMs = 10.0;
    double lmQueryIntervalSec = 0.1;
    double e2ReportIntervalSec = 0.1;
    double metricsIntervalSec = 0.1;
    double simTimeSec = 20.0;
    bool verbose = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("verbose", "Print SQL query results", verbose);
    cmd.AddValue("db-file", "SQLite database file", dbFileName);
    cmd.AddValue("metrics-log", "CSV metrics output path", metricsPath);
    cmd.AddValue("gnb-tx-power", "gNB Tx power (dBm) [3GPP default]", gnbTxPower);
    cmd.AddValue("ue-tx-power", "UE Tx power (dBm) [3GPP default]", ueTxPower);
    cmd.AddValue("bandwidth", "Channel bandwidth (Hz) [assumed]", bandwidthHz);
    cmd.AddValue("numerology", "NR numerology 0-4 [assumed mu=1]", numerology);
    cmd.AddValue("sinr-threshold", "LM SINR gate (dB) [LM default]", sinrThreshold);
    cmd.AddValue("hysteresis", "LM RSRP hysteresis (dB) [LM default]", hysteresisDb);
    cmd.AddValue("lm-processing-delay", "LM processing delay (ms)", lmProcessingDelayMs);
    cmd.AddValue("lm-query-interval", "RIC LM query interval (s)", lmQueryIntervalSec);
    cmd.AddValue("e2-report-interval", "E2 report interval (s)", e2ReportIntervalSec);
    cmd.AddValue("metrics-interval", "CSV logging interval (s)", metricsIntervalSec);
    cmd.AddValue("sim-time", "Total simulation time (s)", simTimeSec);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(1);                                              
    RngSeedManager::SetRun(1); 

    Time simTime = Seconds(simTimeSec);
    Time lmQueryInterval = Seconds(lmQueryIntervalSec);
    Time metricsInterval = Seconds(metricsIntervalSec);
    std::string lmDelayRv = "ns3::ConstantRandomVariable[Constant=" +
                            std::to_string(lmProcessingDelayMs / 1000.0) + "]";
    std::string e2SendRv = "ns3::ConstantRandomVariable[Constant=" +
                           std::to_string(e2ReportIntervalSec) + "]";

    // --- PHY defaults ---
    Config::SetDefault("ns3::NrGnbPhy::TxPower", DoubleValue(gnbTxPower));
    Config::SetDefault("ns3::NrUePhy::TxPower", DoubleValue(ueTxPower));
    Config::SetDefault("ns3::NrUePhy::EnableUplinkPowerControl", BooleanValue(false));
    Config::SetDefault("ns3::NrUePhy::UeMeasurementsFilterPeriod",
                       TimeValue(MilliSeconds(50)));

    // --- NR helpers ---
    Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();
    Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
    nrHelper->SetEpcHelper(epcHelper);
    nrHelper->SetAttribute("UseIdealRrc", BooleanValue(true));
    nrHelper->SetHandoverAlgorithmType("ns3::NrNoOpHandoverAlgorithm");

    // --- Create nodes ---
    NodeContainer gnbNodes;
    gnbNodes.Create(2);
    NodeContainer ueNodes;
    ueNodes.Create(1);

    // --- gNB placement (ENU, origin = gNB 13) ---
    Ptr<ListPositionAllocator> gnbPosAlloc = CreateObject<ListPositionAllocator>();
    gnbPosAlloc->Add(Vector(0.0, 0.0, 36.0));          // gNB 13 (PCI 331)
    gnbPosAlloc->Add(Vector(592.62, -431.55, 58.0));    // gNB 11 (PCI 286)

    MobilityHelper gnbMobility;
    gnbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    gnbMobility.SetPositionAllocator(gnbPosAlloc);
    gnbMobility.Install(gnbNodes);

    // --- UE trajectory via WaypointMobilityModel ---
    MobilityHelper ueMobility;
    ueMobility.SetMobilityModel("ns3::WaypointMobilityModel",
                                "InitialPositionIsWaypoint",
                                BooleanValue(true));
    ueMobility.Install(ueNodes);

    Ptr<WaypointMobilityModel> waypointMob =
        ueNodes.Get(0)->GetObject<WaypointMobilityModel>();

    struct WP { double t; double x; double y; };
    const WP waypoints[] = {
        // All values measured from phone_data_5g.parquet,
          // PCI ∈ {331, 286, 285}, window 03:35:04.000 → 03:35:20.800 UTC.
          // Real HO from 331 → 286 occurs at t = 10.766 s.
          { 0.14,  312.65, -178.69},
          { 0.66,  308.58, -174.46},
          { 1.67,  304.72, -169.79},
          { 2.67,  300.79, -164.79},
          { 3.72,  296.79, -160.12},
          { 4.72,  293.01, -155.01},
          { 5.73,  289.01, -150.34},
          { 6.73,  284.93, -145.67},
          { 7.75,  281.08, -140.55},
          { 8.74,  277.30, -135.88},
          { 9.74,  273.22, -132.10},
          {10.77,  269.22, -126.98},   // real HO ~here (03:35:14.766 UTC)
          {11.78,  265.14, -122.31},
          {12.79,  261.43, -117.64},
          {13.78,  257.65, -113.86},
          {14.76,  253.95, -109.19},
          {15.79,  250.17, -104.52},
          {16.80,  246.17, -100.30},
    };

    for (const auto& wp : waypoints)
    {
        waypointMob->AddWaypoint(
            Waypoint(Seconds(wp.t + g_tOffset), Vector(wp.x, wp.y, 1.5)));
    }

    // --- Antenna configuration ---
    // gNB: 8x8 cross-pol panel, 6 deg downtilt, 45 deg pol slant
    nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(8));
    nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(8));
    nrHelper->SetGnbAntennaAttribute("IsDualPolarized", BooleanValue(true));
    nrHelper->SetGnbAntennaAttribute("NumHorizontalPorts", UintegerValue(4));
    nrHelper->SetGnbAntennaAttribute("NumVerticalPorts", UintegerValue(4));
    nrHelper->SetGnbAntennaAttribute("DowntiltAngle",
                                     DoubleValue(6.0 * M_PI / 180.0));
    nrHelper->SetGnbAntennaAttribute("PolSlantAngle",
                                     DoubleValue(45.0 * M_PI / 180.0));
    // UE: 2x2
    nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(2));
    nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(2));

    // --- Channel model: 3GPP UMa with shadowing ---
    Ptr<NrChannelHelper> channelHelper = CreateObject<NrChannelHelper>();
    channelHelper->ConfigureFactories("UMa", "Default", "ThreeGpp");

    // --- Band n78: 3540 MHz center, 100 MHz BW, mu=1 ---
    CcBwpCreator ccBwpCreator;
    CcBwpCreator::SimpleOperationBandConf bandConf(3.54e9, bandwidthHz,
                                                   static_cast<uint8_t>(numerology));
    OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    channelHelper->AssignChannelsToBands({band});

    BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({band});

    // --- Install devices ---
    NetDeviceContainer gnbDevs = nrHelper->InstallGnbDevice(gnbNodes, allBwps);
    NetDeviceContainer ueDevs = nrHelper->InstallUeDevice(ueNodes, allBwps);

    // --- Per-gNB antenna bearing (set after install) ---
    // Azimuth convention: dataset = clockwise from North
    // NS-3 BearingAngle = counter-clockwise from East = (90 - azimuth) deg
    double azimuths[] = {145.0, 155.0}; // gNB 13, gNB 11
    uint16_t realPcis[] = {PCI_SOURCE, PCI_TARGET};

    for (uint32_t i = 0; i < gnbDevs.GetN(); i++)
    {
        Ptr<NrGnbNetDevice> gnbDev = gnbDevs.Get(i)->GetObject<NrGnbNetDevice>();
        uint16_t cellId = gnbDev->GetCellId();

        g_cellIdToPci[cellId] = realPcis[i];
        g_pciToCellId[realPcis[i]] = cellId;

        double bearingRad = (90.0 - azimuths[i]) * M_PI / 180.0;
        Ptr<NrGnbPhy> phy = NrHelper::GetGnbPhy(gnbDevs.Get(i), 0);
        Ptr<UniformPlanarArray> antenna =
            DynamicCast<UniformPlanarArray>(phy->GetSpectrumPhy()->GetAntenna());
        antenna->SetAttribute("BearingAngle", DoubleValue(bearingRad));

        // TDD pattern: DDDSU
        phy->SetAttribute("Pattern", StringValue("DL|DL|DL|S|UL|"));

        std::cout << "gNB[" << i << "] cellId=" << cellId << " PCI=" << realPcis[i]
                  << " azimuth=" << azimuths[i] << " bearing=" << bearingRad * 180.0 / M_PI
                  << " deg" << std::endl;
    }

    // --- EPC / Internet ---
    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);
    Ptr<Node> remoteHost = remoteHostContainer.Get(0);
    InternetStackHelper internet;
    internet.Install(remoteHostContainer);

    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s")));
    p2ph.SetDeviceAttribute("Mtu", UintegerValue(1500));
    p2ph.SetChannelAttribute("Delay", TimeValue(Seconds(0.010)));
    Ptr<Node> pgw = epcHelper->GetPgwNode();
    NetDeviceContainer internetDevices = p2ph.Install(pgw, remoteHost);
    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.0.0.0");
    ipv4h.Assign(internetDevices);

    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    remoteHostStaticRouting->AddNetworkRouteTo(
        Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    internet.Install(ueNodes);
    Ipv4InterfaceContainer ueIpIfaces =
        epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueDevs));

    // --- X2 + initial attachment to gNB 13 (PCI 331) ---
    nrHelper->AddX2Interface(gnbNodes);
    nrHelper->AttachToGnb(ueDevs.Get(0), gnbDevs.Get(0));

    Ptr<NrUeNetDevice> ueNetDev = ueDevs.Get(0)->GetObject<NrUeNetDevice>();
    g_servingCellId = ueNetDev->GetCellId();
    std::cout << "Initial attachment: cellId=" << g_servingCellId
              << " PCI=" << CellIdToPci(g_servingCellId) << std::endl;

    // --- Full-buffer DL traffic ---
    uint16_t dlPort = 10000;
    UdpClientHelper dlClient(ueIpIfaces.GetAddress(0), dlPort);
    dlClient.SetAttribute("Interval", TimeValue(MilliSeconds(1)));
    dlClient.SetAttribute("MaxPackets", UintegerValue(1000000));
    dlClient.SetAttribute("PacketSize", UintegerValue(1400));
    ApplicationContainer clientApps = dlClient.Install(remoteHost);
    clientApps.Start(Seconds(1.0));

    PacketSinkHelper dlSink("ns3::UdpSocketFactory",
                            InetSocketAddress(Ipv4Address::GetAny(), dlPort));
    ApplicationContainer serverApps = dlSink.Install(ueNodes.Get(0));
    serverApps.Start(Seconds(0.5));

    // ═══════════════════════════════════════════════════════════════════════
    // O-RAN setup
    // ═══════════════════════════════════════════════════════════════════════
    if (!dbFileName.empty())
    {
        std::remove(dbFileName.c_str());
    }

    Ptr<OranNearRtRic> nearRtRic = nullptr;
    OranE2NodeTerminatorContainer e2TermsGnbs;
    OranE2NodeTerminatorContainer e2TermsUes;
    Ptr<OranHelper> oranHelper = CreateObject<OranHelper>();

    oranHelper->SetAttribute("Verbose", BooleanValue(true));
    oranHelper->SetAttribute("LmQueryInterval", TimeValue(lmQueryInterval));
    oranHelper->SetAttribute("E2NodeInactivityThreshold", TimeValue(Seconds(2)));
    oranHelper->SetAttribute("E2NodeInactivityIntervalRv",
                             StringValue("ns3::ConstantRandomVariable[Constant=2]"));
    oranHelper->SetAttribute("LmQueryMaxWaitTime", TimeValue(Seconds(0)));
    oranHelper->SetAttribute("LmQueryLateCommandPolicy",
                             EnumValue(OranNearRtRic::DROP));
    oranHelper->SetAttribute("RicTransmissionDelayRv",
                             StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));

    oranHelper->SetDataRepository("ns3::OranDataRepositorySqlite",
                                  "DatabaseFile",
                                  StringValue(dbFileName));

    oranHelper->SetDefaultLogicModule("ns3::OranLmNr2NrRsrpSinrHandover",
                                      "SinrThresholdDb",
                                      DoubleValue(sinrThreshold),
                                      "HysteresisDb",
                                      DoubleValue(hysteresisDb),
                                      "ProcessingDelayRv",
                                      StringValue(lmDelayRv));

    oranHelper->SetConflictMitigationModule("ns3::OranCmmHandover");

    nearRtRic = oranHelper->CreateNearRtRic();

    // --- UE E2 terminators with reporters ---
    for (uint32_t idx = 0; idx < ueNodes.GetN(); idx++)
    {
        Ptr<OranReporterLocation> locReporter = CreateObject<OranReporterLocation>();
        Ptr<OranReporterNrUeCellInfo> cellReporter =
            CreateObject<OranReporterNrUeCellInfo>();
        Ptr<OranReporterNrUeRsrpRsrq> rsrpReporter =
            CreateObject<OranReporterNrUeRsrpRsrq>();
        Ptr<OranReporterNrUeSinr> sinrReporter = CreateObject<OranReporterNrUeSinr>();
        Ptr<OranE2NodeTerminatorNrUe> ueTerm =
            CreateObject<OranE2NodeTerminatorNrUe>();

        locReporter->SetAttribute("Terminator", PointerValue(ueTerm));
        locReporter->SetAttribute("Trigger",
                                  StringValue("ns3::OranReportTriggerPeriodic"));

        cellReporter->SetAttribute("Terminator", PointerValue(ueTerm));
        cellReporter->SetAttribute(
            "Trigger",
            StringValue("ns3::OranReportTriggerNrUeHandover[InitialReport=true]"));

        rsrpReporter->SetAttribute("Terminator", PointerValue(ueTerm));
        rsrpReporter->SetAttribute("Trigger",
                                   StringValue("ns3::OranReportTriggerPeriodic"));

        sinrReporter->SetAttribute("Terminator", PointerValue(ueTerm));
        sinrReporter->SetAttribute("Trigger",
                                   StringValue("ns3::OranReportTriggerPeriodic"));

        // Connect NrUePhy traces -> O-RAN reporters AND local CSV callbacks
        for (uint32_t d = 0; d < ueNodes.Get(idx)->GetNDevices(); d++)
        {
            Ptr<NrUeNetDevice> nrUeDev =
                ueNodes.Get(idx)->GetDevice(d)->GetObject<NrUeNetDevice>();
            if (nrUeDev)
            {
                Ptr<NrUePhy> uePhy = nrUeDev->GetPhy(0);
                uePhy->TraceConnectWithoutContext(
                    "ReportUeMeasurements",
                    MakeCallback(&OranReporterNrUeRsrpRsrq::ReportRsrpRsrq,
                                 rsrpReporter));
                uePhy->TraceConnectWithoutContext(
                    "DlCtrlSinr",
                    MakeCallback(&OranReporterNrUeSinr::ReportSinr, sinrReporter));
                uePhy->TraceConnectWithoutContext("ReportUeMeasurements",
                                                  MakeCallback(&TraceRsrpRsrq));
                uePhy->TraceConnectWithoutContext("DlCtrlSinr",
                                                  MakeCallback(&TraceSinr));
                break;
            }
        }

        ueTerm->SetAttribute("NearRtRic", PointerValue(nearRtRic));
        ueTerm->SetAttribute("RegistrationIntervalRv",
                             StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        ueTerm->SetAttribute("SendIntervalRv", StringValue(e2SendRv));
        ueTerm->SetAttribute("TransmissionDelayRv",
                             StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));

        ueTerm->AddReporter(locReporter);
        ueTerm->AddReporter(cellReporter);
        ueTerm->AddReporter(rsrpReporter);
        ueTerm->AddReporter(sinrReporter);

        ueTerm->Attach(ueNodes.Get(idx));
        e2TermsUes.Add(ueTerm);
    }

    // --- gNB E2 terminators ---
    oranHelper->SetE2NodeTerminator(
        "ns3::OranE2NodeTerminatorNrGnb",
        "RegistrationIntervalRv",
        StringValue("ns3::ConstantRandomVariable[Constant=1]"),
        "SendIntervalRv",
        StringValue(e2SendRv),
        "TransmissionDelayRv",
        StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));

    oranHelper->AddReporter("ns3::OranReporterLocation",
                            "Trigger",
                            StringValue("ns3::OranReportTriggerPeriodic"));

    e2TermsGnbs.Add(oranHelper->DeployTerminators(nearRtRic, gnbNodes));

    if (verbose)
    {
        nearRtRic->Data()->TraceConnectWithoutContext("QueryRc",
                                                      MakeCallback(&QueryRcSink));
    }

    // --- Activation sequence ---
    Simulator::Schedule(Seconds(1),
                        &OranHelper::ActivateAndStartNearRtRic,
                        oranHelper,
                        nearRtRic);
    Simulator::Schedule(Seconds(1.5),
                        &OranHelper::ActivateE2NodeTerminators,
                        oranHelper,
                        e2TermsGnbs);
    Simulator::Schedule(Seconds(2),
                        &OranHelper::ActivateE2NodeTerminators,
                        oranHelper,
                        e2TermsUes);

    // --- Handover traces ---
    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverEndOk",
                    MakeCallback(&NotifyHandoverEndOk));
    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverStart",
                    MakeCallback(&NotifyHandoverStart));

    // --- Open metrics CSV ---
    g_metricsLog.open(metricsPath);
    g_metricsLog
        << "sim_time_s,serving_pci,rsrp_serving_dbm,rsrp_neighbor_286_dbm,sinr_serving_db,"
           "ho_command_issued"
        << std::endl;

    Simulator::Schedule(Seconds(g_tOffset),
                        &LogMetrics,
                        ueNetDev,
                        ueNodes.Get(0),
                        metricsInterval);

    // --- Run ---
    Simulator::Stop(simTime);
    Simulator::Run();

    std::cout << "\n=== Simulation complete ===" << std::endl;
    std::cout << "Metrics CSV: " << metricsPath << std::endl;
    std::cout << "SQLite DB:   " << dbFileName << std::endl;

    g_metricsLog.close();
    Simulator::Destroy();

    return 0;
}
