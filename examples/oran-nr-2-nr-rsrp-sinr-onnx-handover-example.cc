/**
 * NIST-developed software is provided by NIST as a public service. You may
 * use, copy and distribute copies of the software in any medium, provided that
 * you keep intact this entire notice. You may improve, modify and create
 * derivative works of the software or any portion of the software, and you may
 * copy and distribute such modifications or works. Modified works should carry
 * a notice stating that you changed the software and should note the date and
 * nature of any such change. Please explicitly acknowledge the National
 * Institute of Standards and Technology as the source of the software.
 *
 * NIST-developed software is expressly provided "AS IS." NIST MAKES NO
 * WARRANTY OF ANY KIND, EXPRESS, IMPLIED, IN FACT OR ARISING BY OPERATION OF
 * LAW, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTY OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, NON-INFRINGEMENT AND DATA ACCURACY. NIST
 * NEITHER REPRESENTS NOR WARRANTS THAT THE OPERATION OF THE SOFTWARE WILL BE
 * UNINTERRUPTED OR ERROR-FREE, OR THAT ANY DEFECTS WILL BE CORRECTED. NIST
 * DOES NOT WARRANT OR MAKE ANY REPRESENTATIONS REGARDING THE USE OF THE
 * SOFTWARE OR THE RESULTS THEREOF, INCLUDING BUT NOT LIMITED TO THE
 * CORRECTNESS, ACCURACY, RELIABILITY, OR USEFULNESS OF THE SOFTWARE.
 *
 * You are solely responsible for determining the appropriateness of using and
 * distributing the software and you assume all risks associated with its use,
 * including but not limited to the risks and costs of program errors,
 * compliance with applicable laws, damage to or loss of data, programs or
 * equipment, and the unavailability or interruption of operation. This
 * software is not intended to be used in any situation where a failure could
 * cause risk of injury or damage to property. The software developed by NIST
 * employees is not subject to copyright protection within the United States.
 */

/**
 * Vienna single-HO replay using ONNX ML-based handover decision.
 *
 * Same scenario as vienna-ho-replay.cc (PCI 331 -> PCI 286, Vienna drive-test)
 * but replaces the rule-based OranLmNr2NrRsrpSinrHandover with the ONNX-based
 * OranLmNr2NrRsrpSinrOnnxHandover that uses a trained logistic regression
 * model (nr_rsrp_sinr_logistic.onnx) for handover decisions.
 *
 * The ONNX model takes [sinr_serving_db, rsrp_diff] as input features and
 * outputs a binary label: 1 = handover recommended.
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

NS_LOG_COMPONENT_DEFINE("OranNr2NrRsrpSinrOnnxHandoverExample");

static const uint16_t PCI_SOURCE = 331;
static const uint16_t PCI_TARGET = 286;

static std::map<uint16_t, uint16_t> g_cellIdToPci;
static std::map<uint16_t, uint16_t> g_pciToCellId;

static std::ofstream g_metricsLog;
static double g_rsrpServing = -DBL_MAX;
static double g_rsrpNeighbor = -DBL_MAX;
static double g_sinrServing = -DBL_MAX;
static uint16_t g_servingCellId = 0;
static bool g_hoCommandIssued = false;
static double g_tOffset = 3.0;

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

static uint16_t
CellIdToPci(uint16_t cellId)
{
    auto it = g_cellIdToPci.find(cellId);
    return (it != g_cellIdToPci.end()) ? it->second : 0;
}

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

int
main(int argc, char* argv[])
{
    std::string dbFileName = "vienna-ho-331-286-onnx-lm.db";
    std::string metricsPath = "vienna-ho-331-286-onnx-metrics.csv";
    std::string onnxModelPath = "nr_rsrp_sinr_logistic.onnx";
    double gnbTxPower = 49.0;
    double ueTxPower = 23.0;
    double bandwidthHz = 100e6;
    uint8_t numerology = 1;
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
    cmd.AddValue("onnx-model", "Path to the ONNX model file", onnxModelPath);
    cmd.AddValue("gnb-tx-power", "gNB Tx power (dBm)", gnbTxPower);
    cmd.AddValue("ue-tx-power", "UE Tx power (dBm)", ueTxPower);
    cmd.AddValue("bandwidth", "Channel bandwidth (Hz)", bandwidthHz);
    cmd.AddValue("numerology", "NR numerology 0-4", numerology);
    cmd.AddValue("lm-processing-delay", "LM processing delay (ms)", lmProcessingDelayMs);
    cmd.AddValue("lm-query-interval", "RIC LM query interval (s)", lmQueryIntervalSec);
    cmd.AddValue("e2-report-interval", "E2 report interval (s)", e2ReportIntervalSec);
    cmd.AddValue("metrics-interval", "CSV logging interval (s)", metricsIntervalSec);
    cmd.AddValue("sim-time", "Total simulation time (s)", simTimeSec);
    cmd.Parse(argc, argv);

    Time simTime = Seconds(simTimeSec);
    Time lmQueryInterval = Seconds(lmQueryIntervalSec);
    Time metricsInterval = Seconds(metricsIntervalSec);
    std::string lmDelayRv = "ns3::ConstantRandomVariable[Constant=" +
                            std::to_string(lmProcessingDelayMs / 1000.0) + "]";
    std::string e2SendRv = "ns3::ConstantRandomVariable[Constant=" +
                           std::to_string(e2ReportIntervalSec) + "]";

    Config::SetDefault("ns3::NrGnbPhy::TxPower", DoubleValue(gnbTxPower));
    Config::SetDefault("ns3::NrUePhy::TxPower", DoubleValue(ueTxPower));
    Config::SetDefault("ns3::NrUePhy::EnableUplinkPowerControl", BooleanValue(false));
    Config::SetDefault("ns3::NrUePhy::UeMeasurementsFilterPeriod",
                       TimeValue(MilliSeconds(50)));

    Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();
    Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
    nrHelper->SetEpcHelper(epcHelper);
    nrHelper->SetAttribute("UseIdealRrc", BooleanValue(true));
    nrHelper->SetHandoverAlgorithmType("ns3::NrNoOpHandoverAlgorithm");

    NodeContainer gnbNodes;
    gnbNodes.Create(2);
    NodeContainer ueNodes;
    ueNodes.Create(1);

    Ptr<ListPositionAllocator> gnbPosAlloc = CreateObject<ListPositionAllocator>();
    gnbPosAlloc->Add(Vector(0.0, 0.0, 36.0));
    gnbPosAlloc->Add(Vector(592.62, -431.55, 58.0));

    MobilityHelper gnbMobility;
    gnbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    gnbMobility.SetPositionAllocator(gnbPosAlloc);
    gnbMobility.Install(gnbNodes);

    MobilityHelper ueMobility;
    ueMobility.SetMobilityModel("ns3::WaypointMobilityModel",
                                "InitialPositionIsWaypoint",
                                BooleanValue(true));
    ueMobility.Install(ueNodes);

    Ptr<WaypointMobilityModel> waypointMob =
        ueNodes.Get(0)->GetObject<WaypointMobilityModel>();

    struct WP { double t; double x; double y; };
    const WP waypoints[] = {
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
        {10.77,  269.22, -126.98},
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

    nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(8));
    nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(8));
    nrHelper->SetGnbAntennaAttribute("IsDualPolarized", BooleanValue(true));
    nrHelper->SetGnbAntennaAttribute("NumHorizontalPorts", UintegerValue(4));
    nrHelper->SetGnbAntennaAttribute("NumVerticalPorts", UintegerValue(4));
    nrHelper->SetGnbAntennaAttribute("DowntiltAngle",
                                     DoubleValue(6.0 * M_PI / 180.0));
    nrHelper->SetGnbAntennaAttribute("PolSlantAngle",
                                     DoubleValue(45.0 * M_PI / 180.0));
    nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(2));
    nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(2));

    Ptr<NrChannelHelper> channelHelper = CreateObject<NrChannelHelper>();
    channelHelper->ConfigureFactories("UMa", "Default", "ThreeGpp");

    CcBwpCreator ccBwpCreator;
    CcBwpCreator::SimpleOperationBandConf bandConf(3.54e9, bandwidthHz,
                                                   static_cast<uint8_t>(numerology));
    OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    channelHelper->AssignChannelsToBands({band});

    BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({band});

    NetDeviceContainer gnbDevs = nrHelper->InstallGnbDevice(gnbNodes, allBwps);
    NetDeviceContainer ueDevs = nrHelper->InstallUeDevice(ueNodes, allBwps);

    double azimuths[] = {145.0, 155.0};
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

        phy->SetAttribute("Pattern", StringValue("DL|DL|DL|S|UL|"));

        std::cout << "gNB[" << i << "] cellId=" << cellId << " PCI=" << realPcis[i]
                  << " azimuth=" << azimuths[i] << " bearing=" << bearingRad * 180.0 / M_PI
                  << " deg" << std::endl;
    }

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

    nrHelper->AddX2Interface(gnbNodes);
    nrHelper->AttachToGnb(ueDevs.Get(0), gnbDevs.Get(0));

    Ptr<NrUeNetDevice> ueNetDev = ueDevs.Get(0)->GetObject<NrUeNetDevice>();
    g_servingCellId = ueNetDev->GetCellId();
    std::cout << "Initial attachment: cellId=" << g_servingCellId
              << " PCI=" << CellIdToPci(g_servingCellId) << std::endl;

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
    // O-RAN setup with ONNX-based Logic Module
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

    oranHelper->SetDefaultLogicModule("ns3::OranLmNr2NrRsrpSinrOnnxHandover",
                                      "OnnxModelPath",
                                      StringValue(onnxModelPath),
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

    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverEndOk",
                    MakeCallback(&NotifyHandoverEndOk));
    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverStart",
                    MakeCallback(&NotifyHandoverStart));

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

    Simulator::Stop(simTime);
    Simulator::Run();

    std::cout << "\n=== Simulation complete (ONNX LM) ===" << std::endl;
    std::cout << "Metrics CSV: " << metricsPath << std::endl;
    std::cout << "SQLite DB:   " << dbFileName << std::endl;

    g_metricsLog.close();
    Simulator::Destroy();

    return 0;
}
