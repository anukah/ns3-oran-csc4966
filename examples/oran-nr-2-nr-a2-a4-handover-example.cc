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

#include "ns3/core-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/isotropic-antenna-model.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/nr-channel-helper.h"
#include "ns3/nr-common.h"
#include "ns3/nr-gnb-net-device.h"
#include "ns3/nr-gnb-rrc.h"
#include "ns3/nr-helper.h"
#include "ns3/nr-point-to-point-epc-helper.h"
#include "ns3/nr-rrc-sap.h"
#include "ns3/oran-module.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/point-to-point-module.h"
#include "ns3/udp-client-server-helper.h"

#include <iomanip>
#include <stdio.h>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("OranNr2NrA2A4HandoverExample");

/**
 * Usage example of the O-RAN NR bridge that reproduces, at the Near-RT RIC, the
 * decision of the ns3::NrA2A4RsrpHandoverAlgorithm gNB handover algorithm.
 *
 * The scenario is the same as oran-nr-2-nr-distance-handover-example: a single
 * NR UE moving back and forth between two NR gNBs, with the gNB-side automatic
 * handover algorithm disabled (NrNoOpHandoverAlgorithm) so that all handovers
 * come from the RIC.
 *
 * What differs is where the handover decision comes from. Instead of comparing
 * positions, the gNBs report to the RIC the 3GPP RRC Measurement Reports that
 * they receive from the UE, and OranLmNr2NrA2A4RsrpHandover applies the
 * two-condition A2/A4 decision to them. Since the Event A2 and Event A4
 * conditions, their thresholds, the layer 3 filtering, and the time-to-trigger
 * are all evaluated in the UE RRC, the Logic Module does not re-evaluate any
 * measurement condition: the arrival of an Event A2 report is the first
 * condition of the algorithm, and the Event A4 reports supply the neighbour cell
 * measurements for the second.
 *
 * Because the no-op handover algorithm registers no reporting configuration, the
 * Event A2 and Event A4 configurations are installed here explicitly. This must
 * happen before the simulation starts, since NrGnbRrc::AddUeMeasReportConfig
 * aborts if it is called after time 0.
 *
 * The `--handoverMode` argument selects where the decision is taken, so that the
 * two can be compared over the identical scenario:
 *
 * - `oran`: the gNBs run NrNoOpHandoverAlgorithm and the RIC decides, as
 *   described above. This is the default.
 * - `gnb`: the gNBs run the stock ns3::NrA2A4RsrpHandoverAlgorithm with
 *   `ServingCellThreshold` set to `--a2Threshold` and `NeighbourCellOffset` to
 *   `--neighbourCellOffset`, and no RIC is instantiated. The algorithm installs
 *   the same Event A2 (MS240) and Event A4 (threshold 0, MS480) reporting
 *   configurations that the `oran` mode installs by hand, so both modes see the
 *   same Measurement Reports and differ only in where and when they are acted
 *   upon.
 * - `none`: no handover algorithm at all, as a baseline that shows what the
 *   flow looks like when the UE stays on its initial cell for the whole run.
 *
 * A FlowMonitor is installed in every mode and a per-flow summary is printed at
 * the end, along with the number of completed handovers, which is what makes the
 * modes comparable.
 *
 * With `--printMeasReports`, every Measurement Report received is printed,
 * including the quantized RSRP of the serving and neighbour cells and its value
 * in dBm. If the run produces no Event A2 reports at all, the serving cell never
 * got worse than the configured threshold: read the printed serving RSRP around
 * the middle of the UE's travel and pass a --a2Threshold below it.
 */

/// Number of handovers that completed during the run, for the final summary.
uint32_t g_handoverEndOkCount = 0;
/// Number of handovers that were started during the run, for the final summary.
uint32_t g_handoverStartCount = 0;
/// Number of handovers that failed during the run, for the final summary.
uint32_t g_handoverEndErrorCount = 0;

/**
 * Print a handover that the UE completed.
 *
 * @param context The trace context.
 * @param imsi The IMSI of the UE.
 * @param cellid The ID of the cell that the UE handed over to.
 * @param rnti The RNTI of the UE.
 */
void
NotifyHandoverEndOkUe(std::string context, uint64_t imsi, uint16_t cellid, uint16_t rnti)
{
    g_handoverEndOkCount++;

    std::cout << Simulator::Now().GetSeconds() << " " << context << " UE IMSI " << imsi
              << ": completed handover to CellId " << cellid << " RNTI " << rnti << std::endl;
}

/**
 * Print a handover that the UE started, and count it. A handover that starts but
 * never ends is a failure, so the two counts are printed side by side.
 *
 * @param context The trace context.
 * @param imsi The IMSI of the UE.
 * @param cellid The ID of the cell that the UE is handing over from.
 * @param rnti The RNTI of the UE.
 * @param targetCellId The ID of the cell that the UE is handing over to.
 */
void
NotifyHandoverStartUe(std::string context,
                      uint64_t imsi,
                      uint16_t cellid,
                      uint16_t rnti,
                      uint16_t targetCellId)
{
    g_handoverStartCount++;

    std::cout << Simulator::Now().GetSeconds() << " " << context << " UE IMSI " << imsi
              << ": started handover from CellId " << cellid << " to CellId " << targetCellId
              << " RNTI " << rnti << std::endl;
}

/**
 * Print a handover that failed, and count it.
 *
 * @param context The trace context.
 * @param imsi The IMSI of the UE.
 * @param cellid The ID of the cell that the UE was handing over to.
 * @param rnti The RNTI of the UE.
 */
void
NotifyHandoverEndErrorUe(std::string context, uint64_t imsi, uint16_t cellid, uint16_t rnti)
{
    g_handoverEndErrorCount++;

    std::cout << Simulator::Now().GetSeconds() << " " << context << " UE IMSI " << imsi
              << ": FAILED handover to CellId " << cellid << " RNTI " << rnti << std::endl;
}

/**
 * Print a Measurement Report received by a gNB, with the quantized RSRP of each
 * measured cell and its equivalent in dBm. This is what to read when choosing
 * the Event A2 threshold.
 *
 * @param imsi The IMSI of the UE that sent the report.
 * @param cellId The ID of the cell that received the report.
 * @param rnti The RNTI of the UE that sent the report.
 * @param measReport The Measurement Report.
 */
void
NotifyMeasurementReport(uint64_t imsi,
                        uint16_t cellId,
                        uint16_t rnti,
                        NrRrcSap::MeasurementReport measReport)
{
    const NrRrcSap::MeasResults& results = measReport.measResults;

    std::cout << Simulator::Now().GetSeconds() << " MeasReport at CellId " << cellId << " from IMSI "
              << imsi << " measId " << +results.measId << ": serving RSRP "
              << +results.measResultPCell.rsrpResult << " ("
              << nr::EutranMeasurementMapping::RsrpRange2Dbm(results.measResultPCell.rsrpResult)
              << " dBm)";

    if (results.haveMeasResultNeighCells)
    {
        for (const auto& neighbour : results.measResultListEutra)
        {
            std::cout << "; neighbour CellId " << neighbour.physCellId << " RSRP ";
            if (neighbour.haveRsrpResult)
            {
                std::cout << +neighbour.rsrpResult << " ("
                          << nr::EutranMeasurementMapping::RsrpRange2Dbm(neighbour.rsrpResult)
                          << " dBm)";
            }
            else
            {
                std::cout << "n/a";
            }
        }
    }

    std::cout << std::endl;
}

/**
 * Reverse the direction of travel of the UEs.
 *
 * @param nodes The UE nodes.
 * @param interval The interval between reversals.
 */
void
ReverseVelocity(NodeContainer nodes, Time interval)
{
    for (uint32_t idx = 0; idx < nodes.GetN(); idx++)
    {
        Ptr<ConstantVelocityMobilityModel> mobility =
            nodes.Get(idx)->GetObject<ConstantVelocityMobilityModel>();
        mobility->SetVelocity(Vector(mobility->GetVelocity().x * -1, 0, 0));
    }

    Simulator::Schedule(interval, &ReverseVelocity, nodes, interval);
}

/**
 * Print the result of a data repository query.
 *
 * @param query The query.
 * @param args The bound arguments.
 * @param rc The return code.
 */
void
QueryRcSink(std::string query, std::string args, int rc)
{
    std::cout << Simulator::Now().GetSeconds() << " Query "
              << ((rc == SQLITE_OK || rc == SQLITE_DONE) ? "OK" : "ERROR") << "(" << rc << "): \""
              << query << "\"";

    if (!args.empty())
    {
        std::cout << " (" << args << ")";
    }
    std::cout << std::endl;
}

/**
 * Print the FlowMonitor statistics of the run, one block per flow, followed by
 * the handover counts. This is the output to compare between `--handoverMode`
 * settings.
 *
 * The throughput is computed over the lifetime of the flow (first transmitted
 * packet to last received packet) rather than over the simulation duration, so
 * that a flow that starts late is not penalized.
 *
 * @param monitor The FlowMonitor.
 * @param helper The FlowMonitorHelper that installed it, for the classifier.
 * @param handoverMode The mode that produced these statistics.
 */
void
PrintFlowStats(Ptr<FlowMonitor> monitor, FlowMonitorHelper& helper, std::string handoverMode)
{
    monitor->CheckForLostPackets();

    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(helper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    uint64_t totalTxPackets = 0;
    uint64_t totalRxPackets = 0;
    uint64_t totalLostPackets = 0;
    double totalDelaySum = 0;

    std::cout << std::endl;
    std::cout << "=== FlowMonitor results (handoverMode=" << handoverMode << ") ===" << std::endl;

    for (const auto& [flowId, flowStats] : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flowId);

        // The packets that were transmitted but neither received nor accounted
        // for as lost are still in flight at the end of the run.
        uint64_t lost = flowStats.txPackets - flowStats.rxPackets;
        double duration =
            (flowStats.timeLastRxPacket - flowStats.timeFirstTxPacket).GetSeconds();

        std::cout << "Flow " << flowId << " (" << t.sourceAddress << ":" << t.sourcePort << " -> "
                  << t.destinationAddress << ":" << t.destinationPort << ")" << std::endl;
        std::cout << "  Tx packets:      " << flowStats.txPackets << std::endl;
        std::cout << "  Rx packets:      " << flowStats.rxPackets << std::endl;
        std::cout << "  Lost packets:    " << lost << " ("
                  << (flowStats.txPackets > 0 ? 100.0 * lost / flowStats.txPackets : 0.0) << " %)"
                  << std::endl;
        std::cout << "  Throughput:      "
                  << (duration > 0 ? flowStats.rxBytes * 8.0 / duration / 1e3 : 0.0) << " kb/s"
                  << std::endl;
        std::cout << "  Mean delay:      "
                  << (flowStats.rxPackets > 0
                          ? flowStats.delaySum.GetSeconds() * 1e3 / flowStats.rxPackets
                          : 0.0)
                  << " ms" << std::endl;
        std::cout << "  Mean jitter:     "
                  << (flowStats.rxPackets > 1
                          ? flowStats.jitterSum.GetSeconds() * 1e3 / (flowStats.rxPackets - 1)
                          : 0.0)
                  << " ms" << std::endl;

        totalTxPackets += flowStats.txPackets;
        totalRxPackets += flowStats.rxPackets;
        totalLostPackets += lost;
        totalDelaySum += flowStats.delaySum.GetSeconds();
    }

    std::cout << "--- Aggregate ---" << std::endl;
    std::cout << "  Tx packets:      " << totalTxPackets << std::endl;
    std::cout << "  Rx packets:      " << totalRxPackets << std::endl;
    std::cout << "  Lost packets:    " << totalLostPackets << " ("
              << (totalTxPackets > 0 ? 100.0 * totalLostPackets / totalTxPackets : 0.0) << " %)"
              << std::endl;
    std::cout << "  Mean delay:      "
              << (totalRxPackets > 0 ? totalDelaySum * 1e3 / totalRxPackets : 0.0) << " ms"
              << std::endl;
    std::cout << "--- Handovers ---" << std::endl;
    std::cout << "  Started:         " << g_handoverStartCount << std::endl;
    std::cout << "  Completed:       " << g_handoverEndOkCount << std::endl;
    std::cout << "  Failed:          " << g_handoverEndErrorCount << std::endl;
}

int
main(int argc, char* argv[])
{
    uint16_t numberOfUes = 1;
    uint16_t numberOfGnbs = 2;
    Time simTime = Seconds(50);
    double distance = 200;
    Time interval = Seconds(15);
    double speed = 15;
    bool verbose = false;
    std::string dbFileName = "oran-repository.db";

    // The default is chosen for this scenario (Friis propagation at 2.8 GHz over
    // 10 MHz, 30 dBm gNB transmit power, 200 m spacing), where the serving RSRP
    // around the middle of the UE's travel is in the high 70s of the quantized
    // range. It is deliberately not the 46 that NrA2A4RsrpHandoverAlgorithm
    // defaults to: 46 means -111 dBm, which this scenario never reaches, so the
    // Event A2 condition would never hold and no handover would ever be issued.
    uint32_t a2Threshold = 77;
    uint32_t neighbourCellOffset = 1;
    Time maxReportAge = Seconds(1);
    Time lmQueryInterval = Seconds(0.5);
    Time reportInterval = Seconds(0.25);
    std::string handoverMode = "oran";
    bool printMeasReports = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("verbose", "Enable printing SQL queries results", verbose);
    cmd.AddValue("handoverMode",
                 "Where the handover decision is taken: \"oran\" for the Near-RT RIC Logic "
                 "Module, \"gnb\" for the stock ns3::NrA2A4RsrpHandoverAlgorithm at the gNB, "
                 "or \"none\" for no handovers at all",
                 handoverMode);
    cmd.AddValue("printMeasReports",
                 "Print every RRC Measurement Report that a gNB receives",
                 printMeasReports);
    cmd.AddValue("a2Threshold",
                 "Event A2 threshold: the serving cell RSRP below which neighbour cells are "
                 "considered for handover, in the quantized range [0..127] of Table 10.1.6.1-1 "
                 "of 3GPP TS 38.133 (RSRP in dBm is this value minus 157)",
                 a2Threshold);
    cmd.AddValue("neighbourCellOffset",
                 "Minimum offset between the serving and the best neighbour cell needed to "
                 "trigger a handover, in the same quantized range",
                 neighbourCellOffset);
    cmd.AddValue("maxReportAge", "Measurement Reports older than this are ignored", maxReportAge);
    cmd.AddValue("lmQueryInterval", "Interval between Logic Module queries", lmQueryInterval);
    cmd.AddValue("reportInterval", "Interval between periodic Report generation", reportInterval);
    cmd.AddValue("simTime", "Simulation duration", simTime);
    cmd.AddValue("dbFileName", "The name of the data repository file", dbFileName);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(a2Threshold > 127, "a2Threshold must be in the range [0..127]");
    NS_ABORT_MSG_IF(neighbourCellOffset > 127, "neighbourCellOffset must be in the range [0..127]");
    NS_ABORT_MSG_IF(handoverMode != "oran" && handoverMode != "gnb" && handoverMode != "none",
                    "handoverMode must be one of \"oran\", \"gnb\", or \"none\"");

    // Only the "oran" mode stands up a RIC. The other two exist to compare
    // against it over the identical radio scenario.
    bool useOran = (handoverMode == "oran");

    Config::SetDefault("ns3::NrGnbPhy::TxPower", DoubleValue(30));
    Config::SetDefault("ns3::NrUePhy::TxPower", DoubleValue(23));
    Config::SetDefault("ns3::NrUePhy::EnableUplinkPowerControl", BooleanValue(false));

    // The Reporters accumulate Reports and hand them to the E2 Terminator when
    // their trigger fires, so this interval bounds how quickly a Measurement
    // Report can reach the RIC.
    Config::SetDefault("ns3::OranReportTriggerPeriodic::IntervalRv",
                       StringValue("ns3::ConstantRandomVariable[Constant=" +
                                   std::to_string(reportInterval.GetSeconds()) + "]"));

    Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();
    Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
    nrHelper->SetEpcHelper(epcHelper);
    nrHelper->SetAttribute("UseIdealRrc", BooleanValue(true));

    if (handoverMode == "gnb")
    {
        // The stock gNB algorithm decides. It registers its own Event A2 and
        // Event A4 reporting configurations in DoInitialize, so none are
        // installed by hand in this mode.
        nrHelper->SetHandoverAlgorithmType("ns3::NrA2A4RsrpHandoverAlgorithm");
        nrHelper->SetHandoverAlgorithmAttribute("ServingCellThreshold",
                                                UintegerValue(a2Threshold));
        nrHelper->SetHandoverAlgorithmAttribute("NeighbourCellOffset",
                                                UintegerValue(neighbourCellOffset));
    }
    else
    {
        // All handover decisions come from the RIC, or from nowhere at all in
        // the "none" mode. Note that this also means that no measurement
        // reporting configuration is registered by the gNB, so in the "oran"
        // mode the Event A2 and Event A4 configurations are installed
        // explicitly further down.
        nrHelper->SetHandoverAlgorithmType("ns3::NrNoOpHandoverAlgorithm");
    }

    NodeContainer gnbNodes;
    NodeContainer ueNodes;
    gnbNodes.Create(numberOfGnbs);
    ueNodes.Create(numberOfUes);

    Ptr<ListPositionAllocator> gnbPosAlloc = CreateObject<ListPositionAllocator>();
    for (uint16_t i = 0; i < numberOfGnbs; i++)
    {
        gnbPosAlloc->Add(Vector(distance * i, 0, 25));
    }

    MobilityHelper gnbMobility;
    gnbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    gnbMobility.SetPositionAllocator(gnbPosAlloc);
    gnbMobility.Install(gnbNodes);

    MobilityHelper ueMobility;
    ueMobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    ueMobility.Install(ueNodes);

    for (uint16_t i = 0; i < numberOfUes; i++)
    {
        Ptr<ConstantVelocityMobilityModel> mob =
            ueNodes.Get(i)->GetObject<ConstantVelocityMobilityModel>();
        mob->SetPosition(Vector((distance / 2) - (speed * (interval.GetSeconds() / 2)), 0, 1.5));
        mob->SetVelocity(Vector(speed, 0, 0));
    }

    Simulator::Schedule(interval, &ReverseVelocity, ueNodes, interval);

    nrHelper->SetUeAntennaTypeId(IsotropicAntennaModel::GetTypeId().GetName());
    nrHelper->SetGnbAntennaTypeId(IsotropicAntennaModel::GetTypeId().GetName());

    Ptr<NrChannelHelper> channelHelper = CreateObject<NrChannelHelper>();
    channelHelper->ConfigurePropagationFactory(FriisPropagationLossModel::GetTypeId());

    CcBwpCreator ccBwpCreator;
    CcBwpCreator::SimpleOperationBandConf bandConf(2.8e9, 10e6, static_cast<uint8_t>(1));
    OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    channelHelper->AssignChannelsToBands({band});

    BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({band});

    NetDeviceContainer gnbDevs = nrHelper->InstallGnbDevice(gnbNodes, allBwps);
    NetDeviceContainer ueDevs = nrHelper->InstallUeDevice(ueNodes, allBwps);

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
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    internet.Install(ueNodes);
    Ipv4InterfaceContainer ueIpIfaces = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueDevs));

    nrHelper->AddX2Interface(gnbNodes);
    nrHelper->AttachToGnb(ueDevs.Get(0), gnbDevs.Get(0));

    uint16_t dlPort = 10000;
    UdpClientHelper dlClientHelper(ueIpIfaces.GetAddress(0), dlPort);
    dlClientHelper.SetAttribute("Interval", TimeValue(MilliSeconds(10)));
    dlClientHelper.SetAttribute("MaxPackets", UintegerValue(100000));
    dlClientHelper.SetAttribute("PacketSize", UintegerValue(512));
    ApplicationContainer clientApps = dlClientHelper.Install(remoteHost);
    clientApps.Start(Seconds(1.0));

    PacketSinkHelper dlPacketSinkHelper("ns3::UdpSocketFactory",
                                        InetSocketAddress(Ipv4Address::GetAny(), dlPort));
    ApplicationContainer serverApps = dlPacketSinkHelper.Install(ueNodes.Get(0));
    serverApps.Start(Seconds(0.5));

    if (useOran)
    {
        // O-RAN setup
        if (!dbFileName.empty())
        {
            std::remove(dbFileName.c_str());
        }

        Ptr<OranNearRtRic> nearRtRic = nullptr;
        OranE2NodeTerminatorContainer e2NodeTerminatorsGnbs;
        OranE2NodeTerminatorContainer e2NodeTerminatorsUes;
        Ptr<OranHelper> oranHelper = CreateObject<OranHelper>();

        oranHelper->SetAttribute("Verbose", BooleanValue(true));
        oranHelper->SetAttribute("LmQueryInterval", TimeValue(lmQueryInterval));
        oranHelper->SetAttribute("E2NodeInactivityThreshold", TimeValue(Seconds(2)));
        oranHelper->SetAttribute("E2NodeInactivityIntervalRv",
                                 StringValue("ns3::ConstantRandomVariable[Constant=2]"));
        oranHelper->SetAttribute("LmQueryMaxWaitTime", TimeValue(Seconds(0)));
        oranHelper->SetAttribute("LmQueryLateCommandPolicy", EnumValue(OranNearRtRic::DROP));
        oranHelper->SetAttribute("RicTransmissionDelayRv",
                                 StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));

        oranHelper->SetDataRepository("ns3::OranDataRepositorySqlite",
                                      "DatabaseFile",
                                      StringValue(dbFileName));

        // There is no ServingCellThreshold attribute here: that threshold lives in
        // the Event A2 reporting configuration installed on the gNB below, and is
        // evaluated by the UE RRC.
        oranHelper->SetDefaultLogicModule("ns3::OranLmNr2NrA2A4RsrpHandover",
                                          "NeighbourCellOffset",
                                          UintegerValue(neighbourCellOffset),
                                          "MaxReportAge",
                                          TimeValue(maxReportAge),
                                          "ProcessingDelayRv",
                                          StringValue("ns3::ConstantRandomVariable[Constant=0]"));

        oranHelper->SetConflictMitigationModule("ns3::OranCmmNoop");

        nearRtRic = oranHelper->CreateNearRtRic();

        // NR UE terminators. The UE reports its serving cell so that the Logic Module
        // can tell which cell a Measurement Report describes, and its location purely
        // for the record.
        oranHelper->SetE2NodeTerminator("ns3::OranE2NodeTerminatorNrUe",
                                        "RegistrationIntervalRv",
                                        StringValue("ns3::ConstantRandomVariable[Constant=1]"),
                                        "SendIntervalRv",
                                        StringValue("ns3::ConstantRandomVariable[Constant=1]"),
                                        "TransmissionDelayRv",
                                        StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));

        oranHelper->AddReporter("ns3::OranReporterLocation",
                                "Trigger",
                                StringValue("ns3::OranReportTriggerPeriodic"));

        oranHelper->AddReporter("ns3::OranReporterNrUeCellInfo",
                                "Trigger",
                                StringValue("ns3::OranReportTriggerNrUeHandover[InitialReport=true]"));

        e2NodeTerminatorsUes.Add(oranHelper->DeployTerminators(nearRtRic, ueNodes));

        // The Event A2 reporting configuration: the serving cell became worse than
        // the threshold. This is the first condition of the handover algorithm.
        NrRrcSap::ReportConfigEutra reportConfigA2;
        reportConfigA2.eventId = NrRrcSap::ReportConfigEutra::EVENT_A2;
        reportConfigA2.threshold1.choice = NrRrcSap::ThresholdEutra::THRESHOLD_RSRP;
        reportConfigA2.threshold1.range = a2Threshold;
        reportConfigA2.triggerQuantity = NrRrcSap::ReportConfigEutra::RSRP;
        reportConfigA2.reportInterval = NrRrcSap::ReportConfigEutra::MS240;

        // The Event A4 reporting configuration: a neighbour cell became better than
        // the threshold. The threshold is intentionally very low so that every
        // detectable neighbour is reported, which is what supplies the Logic Module
        // with the neighbour cell measurements for the second condition.
        NrRrcSap::ReportConfigEutra reportConfigA4;
        reportConfigA4.eventId = NrRrcSap::ReportConfigEutra::EVENT_A4;
        reportConfigA4.threshold1.choice = NrRrcSap::ThresholdEutra::THRESHOLD_RSRP;
        reportConfigA4.threshold1.range = 0;
        reportConfigA4.triggerQuantity = NrRrcSap::ReportConfigEutra::RSRP;
        reportConfigA4.reportInterval = NrRrcSap::ReportConfigEutra::MS480;

        // NR gNB terminators. These are built by hand rather than through
        // OranHelper::AddReporter because each Measurement Report Reporter has to be
        // given the measurement IDs of the reporting configurations installed on its
        // own gNB, and connected to that gNB's RRC trace source.
        for (uint32_t idx = 0; idx < gnbNodes.GetN(); idx++)
        {
            Ptr<OranE2NodeTerminatorNrGnb> gnbTerminator =
                CreateObject<OranE2NodeTerminatorNrGnb>();
            gnbTerminator->SetAttribute("NearRtRic", PointerValue(nearRtRic));
            gnbTerminator->SetAttribute("RegistrationIntervalRv",
                                        StringValue("ns3::ConstantRandomVariable[Constant=1]"));
            gnbTerminator->SetAttribute("SendIntervalRv",
                                        StringValue("ns3::ConstantRandomVariable[Constant=1]"));
            gnbTerminator->SetAttribute("TransmissionDelayRv",
                                        StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));

            Ptr<OranReporterLocation> locationReporter = CreateObject<OranReporterLocation>();
            locationReporter->SetAttribute("Terminator", PointerValue(gnbTerminator));
            locationReporter->SetAttribute("Trigger",
                                           StringValue("ns3::OranReportTriggerPeriodic"));

            Ptr<OranReporterNrGnbMeasReport> measReporter =
                CreateObject<OranReporterNrGnbMeasReport>();
            measReporter->SetAttribute("Terminator", PointerValue(gnbTerminator));
            measReporter->SetAttribute("Trigger", StringValue("ns3::OranReportTriggerPeriodic"));

            Ptr<NrGnbNetDevice> gnbDev = gnbDevs.Get(idx)->GetObject<NrGnbNetDevice>();
            NS_ABORT_MSG_IF(gnbDev == nullptr, "Unable to find the NR gNB network device");
            Ptr<NrGnbRrc> gnbRrc = gnbDev->GetRrc();

            // Install the reporting configurations and record which measurement IDs
            // belong to which event, so that the Logic Module can tell an Event A2
            // report apart from an Event A4 report. This has to happen before the
            // simulation starts.
            measReporter->AddMeasIds(OranReportNrGnbMeasReport::EVENT_A2,
                                     gnbRrc->AddUeMeasReportConfig(reportConfigA2));
            measReporter->AddMeasIds(OranReportNrGnbMeasReport::EVENT_A4,
                                     gnbRrc->AddUeMeasReportConfig(reportConfigA4));

            gnbRrc->TraceConnectWithoutContext(
                "RecvMeasurementReport",
                MakeCallback(&OranReporterNrGnbMeasReport::ReportMeasurements, measReporter));

            gnbTerminator->AddReporter(locationReporter);
            gnbTerminator->AddReporter(measReporter);

            gnbTerminator->Attach(gnbNodes.Get(idx));
            e2NodeTerminatorsGnbs.Add(gnbTerminator);
        }

        if (verbose)
        {
            nearRtRic->Data()->TraceConnectWithoutContext("QueryRc", MakeCallback(&QueryRcSink));
        }

        Simulator::Schedule(Seconds(1), &OranHelper::ActivateAndStartNearRtRic, oranHelper, nearRtRic);
        Simulator::Schedule(Seconds(1.5),
                            &OranHelper::ActivateE2NodeTerminators,
                            oranHelper,
                            e2NodeTerminatorsGnbs);
        Simulator::Schedule(Seconds(2),
                            &OranHelper::ActivateE2NodeTerminators,
                            oranHelper,
                            e2NodeTerminatorsUes);
    }

    // The Measurement Reports are printed from the gNB RRC directly rather than
    // from the Reporter, so that they are available in every mode and not only
    // when a RIC is present.
    if (printMeasReports)
    {
        for (uint32_t idx = 0; idx < gnbDevs.GetN(); idx++)
        {
            Ptr<NrGnbNetDevice> gnbDev = gnbDevs.Get(idx)->GetObject<NrGnbNetDevice>();
            NS_ABORT_MSG_IF(gnbDev == nullptr, "Unable to find the NR gNB network device");
            gnbDev->GetRrc()->TraceConnectWithoutContext(
                "RecvMeasurementReport",
                MakeCallback(&NotifyMeasurementReport));
        }
    }

    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverStart",
                    MakeCallback(&NotifyHandoverStartUe));
    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverEndOk",
                    MakeCallback(&NotifyHandoverEndOkUe));
    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverEndError",
                    MakeCallback(&NotifyHandoverEndErrorUe));

    // The FlowMonitor is what makes the modes comparable: the scenario, the
    // traffic, and the mobility are identical, so any difference in loss, delay,
    // or throughput comes from where and when the handover decision was taken.
    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

    std::cout << "Handover mode " << handoverMode << ", Event A2 threshold " << a2Threshold << " ("
              << nr::EutranMeasurementMapping::RsrpRange2Dbm(a2Threshold)
              << " dBm), neighbour cell offset " << neighbourCellOffset << std::endl;

    Simulator::Stop(simTime);
    Simulator::Run();

    PrintFlowStats(monitor, flowmonHelper, handoverMode);

    Simulator::Destroy();
    return 0;
}
