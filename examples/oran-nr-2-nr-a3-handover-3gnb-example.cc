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
#include "ns3/nr-gnb-phy.h"
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

NS_LOG_COMPONENT_DEFINE("OranNr2NrA3Handover3GnbExample");

/**
 * Three gNB variant of oran-nr-2-nr-a3-handover-example, in which one cell is
 * given a lower transmit power than the other two.
 *
 * The two gNB example cannot exercise the one decision that
 * OranLmNr2NrA3RsrpHandover actually makes. With two cells there is only ever
 * one neighbour in an Event A3 Measurement Report, so "hand over to the
 * strongest reported neighbour" and "hand over to the only reported neighbour"
 * are the same statement, and a Logic Module that picked the first neighbour, or
 * the nearest one, would score identically.
 *
 * Three collinear gNBs at x = 0, d, and 2d fix the first half of that: around
 * the middle of the row the UE measures two neighbours at once. On their own
 * though, three equal cells still make the strongest neighbour the nearest one,
 * so the choice remains degenerate.
 *
 * Lowering the transmit power of the middle cell breaks the tie. With the outer
 * gNBs at `--gnbTxPower` and the middle one at `--weakGnbTxPower`, there is a
 * stretch either side of x = d where the middle cell is the closest cell but not
 * the strongest one, so an Event A3 report lists a near weak neighbour and a far
 * strong neighbour with a wide RSRP gap between them. Picking the strongest is
 * then a decision with a right and a wrong answer, and the `oran` and `gnb`
 * modes agreeing on it means more than it did with two cells.
 *
 * Under Friis, cell i beats cell j where Pi - 20*log10(di) > Pj - 20*log10(dj).
 * For the defaults (30 dBm outer, 18 dBm middle, d = 200 m) the middle cell is
 * the strongest only over roughly x = 160 m to x = 240 m, while it is the
 * nearest cell over x = 100 m to x = 300 m. The two stretches either side, about
 * 60 m wide each, are where nearest and strongest disagree. Raising
 * `--weakGnbTxPower` towards `--gnbTxPower` shrinks them to nothing and recovers
 * the degenerate case; lowering it far enough removes the middle cell from
 * service altogether, and the UE hands over from the first cell to the third
 * directly, past a neighbour it can hear clearly and must not choose.
 *
 * Everything else follows the two gNB example: the gNBs report the 3GPP RRC
 * Measurement Reports they receive to the RIC, the Event A3 condition and its
 * hysteresis and time-to-trigger are evaluated entirely in the UE RRC, and
 * `--handoverMode` selects between the RIC Logic Module (`oran`), the stock
 * ns3::NrA3RsrpHandoverAlgorithm at the gNB (`gnb`), and no handovers at all
 * (`none`) over an identical radio scenario.
 *
 * Because the no-op handover algorithm registers no reporting configuration, the
 * Event A3 configuration is installed here explicitly, mirroring what
 * NrA3RsrpHandoverAlgorithm::DoInitialize installs. This must happen before the
 * simulation starts, since NrGnbRrc::AddUeMeasReportConfig aborts if it is
 * called after time 0.
 *
 * With `--printMeasReports`, every Measurement Report received is printed with
 * the quantized RSRP of the serving and every neighbour cell and its value in
 * dBm, which is the direct way to read off the neighbour ordering that the
 * Logic Module sees.
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
 * the handover margin.
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
    uint16_t numberOfGnbs = 3;
    Time simTime = Seconds(60);
    double distance = 200;
    double speed = 15;
    std::string scenario = "UMa";
    std::string channelCondition = "Default";
    bool verbose = false;
    std::string dbFileName = "oran-repository.db";

    // The middle cell of the row is the weak one by default, so that the UE
    // meets it head on halfway through its travel with a strong cell on either
    // side. This is 1-based to match the NR cell IDs.
    uint16_t weakGnb = 2;
    double gnbTxPower = 30.0;
    double weakGnbTxPower = 18.0;

    // The defaults of ns3::NrA3RsrpHandoverAlgorithm, so that the "oran" and
    // "gnb" modes are configured identically out of the box.
    double hysteresis = 3.0;
    Time timeToTrigger = MilliSeconds(256);
    // Event A3 reports every MS1024 for as long as the condition holds and stops
    // when it clears, so the Logic Module uses the age of the last report as an
    // implicit check that the condition still holds. This has to sit a little
    // above the reporting interval.
    Time maxReportAge = Seconds(1.5);
    Time lmQueryInterval = Seconds(0.5);
    Time reportInterval = Seconds(0.25);
    std::string handoverMode = "oran";
    bool printMeasReports = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("verbose", "Enable printing SQL queries results", verbose);
    cmd.AddValue("handoverMode",
                 "Where the handover decision is taken: \"oran\" for the Near-RT RIC Logic "
                 "Module, \"gnb\" for the stock ns3::NrA3RsrpHandoverAlgorithm at the gNB, "
                 "or \"none\" for no handovers at all",
                 handoverMode);
    cmd.AddValue("numberOfGnbs", "Number of gNBs, placed in a row d apart", numberOfGnbs);
    cmd.AddValue("weakGnb",
                 "Which gNB gets the reduced transmit power, 1-based, matching the NR cell "
                 "IDs. Set to 0 to give every gNB the same power and recover the degenerate "
                 "case in which the strongest neighbour is always the nearest one",
                 weakGnb);
    cmd.AddValue("gnbTxPower", "Transmit power of the ordinary gNBs in dBm", gnbTxPower);
    cmd.AddValue("weakGnbTxPower",
                 "Transmit power of the reduced range gNB in dBm. The further this is below "
                 "--gnbTxPower, the wider the stretch over which the weak cell is the nearest "
                 "cell but not the strongest one",
                 weakGnbTxPower);
    cmd.AddValue("printMeasReports",
                 "Print every RRC Measurement Report that a gNB receives",
                 printMeasReports);
    cmd.AddValue("hysteresis",
                 "Event A3 handover margin in dB (rounded to the nearest 0.5 dB). A "
                 "non-negative value is applied as the A3 hysteresis IE; a negative value "
                 "is applied as the signed A3 offset IE, which a plain hysteresis cannot "
                 "represent",
                 hysteresis);
    cmd.AddValue("timeToTrigger",
                 "Event A3 time-to-trigger: how long the entry condition must hold "
                 "continuously in the UE RRC before a Measurement Report is sent",
                 timeToTrigger);
    cmd.AddValue("speed",
                 "Speed of the UE in m/s. The UE always travels the same path, so raising "
                 "this shortens the time it spends near the cell edge, which is what makes "
                 "the delay of a handover decision start to matter",
                 speed);
    cmd.AddValue("distance", "Distance between adjacent gNBs in metres", distance);
    cmd.AddValue("scenario",
                 "Propagation scenario for the 3GPP channel model: \"UMa\", \"UMi\", or "
                 "\"RMa\". Unlike free space propagation these have a path loss exponent "
                 "well above 2 and add shadowing, so the cell edge is genuinely marginal "
                 "and arriving late to a handover can cost packets",
                 scenario);
    cmd.AddValue("channelCondition",
                 "Channel condition for the 3GPP channel model: \"Default\" for the "
                 "scenario's own LOS/NLOS probability model, or \"LOS\" or \"NLOS\" to "
                 "force one. \"Default\" makes the run stochastic, so sweep --RngRun",
                 channelCondition);
    cmd.AddValue("maxReportAge", "Measurement Reports older than this are ignored", maxReportAge);
    cmd.AddValue("lmQueryInterval", "Interval between Logic Module queries", lmQueryInterval);
    cmd.AddValue("reportInterval", "Interval between periodic Report generation", reportInterval);
    cmd.AddValue("simTime", "Simulation duration", simTime);
    cmd.AddValue("dbFileName", "The name of the data repository file", dbFileName);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(hysteresis < -15.0 || hysteresis > 15.0,
                    "hysteresis must be in the range [-15.0..15.0] dB");
    NS_ABORT_MSG_IF(speed <= 0.0, "speed must be greater than zero");
    NS_ABORT_MSG_IF(numberOfGnbs < 3,
                    "numberOfGnbs must be at least 3, otherwise no Measurement Report ever "
                    "carries more than one neighbour and there is no choice of target to make");
    NS_ABORT_MSG_IF(weakGnb > numberOfGnbs, "weakGnb must be 0 or the 1-based index of a gNB");

    // The UE sweeps the whole row of gNBs, overshooting the two end cells by an
    // eighth of the gNB separation before it reverses. Deriving the reversal
    // interval from the speed keeps that path identical as the speed changes, so
    // that a speed sweep varies only how long the UE spends near a cell edge and
    // not where it travels.
    double span = (numberOfGnbs - 1) * distance;
    double travel = span + distance / 4.0;
    Time interval = Seconds(travel / speed);

    NS_ABORT_MSG_IF(handoverMode != "oran" && handoverMode != "gnb" && handoverMode != "none",
                    "handoverMode must be one of \"oran\", \"gnb\", or \"none\"");

    // Only the "oran" mode stands up a RIC. The other two exist to compare
    // against it over the identical radio scenario.
    bool useOran = (handoverMode == "oran");

    // The per gNB transmit power is applied to each PHY after installation, so
    // this default only covers the UEs and any gNB the loop below does not reach.
    Config::SetDefault("ns3::NrGnbPhy::TxPower", DoubleValue(gnbTxPower));
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
        // The stock gNB algorithm decides. It registers its own Event A3
        // reporting configuration in DoInitialize, so none is installed by hand
        // in this mode.
        nrHelper->SetHandoverAlgorithmType("ns3::NrA3RsrpHandoverAlgorithm");
        nrHelper->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(hysteresis));
        nrHelper->SetHandoverAlgorithmAttribute("TimeToTrigger", TimeValue(timeToTrigger));
    }
    else
    {
        // All handover decisions come from the RIC, or from nowhere at all in
        // the "none" mode. Note that this also means that no measurement
        // reporting configuration is registered by the gNB, so in the "oran"
        // mode the Event A3 configuration is installed explicitly further down.
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
        mob->SetPosition(Vector((span / 2.0) - (speed * (interval.GetSeconds() / 2)), 0, 1.5));
        mob->SetVelocity(Vector(speed, 0, 0));
    }

    Simulator::Schedule(interval, &ReverseVelocity, ueNodes, interval);

    // The 3GPP spectrum propagation model is a phased array model and asserts
    // that both ends have a PhasedArrayModel installed, so the antennas cannot
    // simply be set to IsotropicAntennaModel with SetUeAntennaTypeId, which
    // replaces the array object itself. A one by one array of isotropic elements
    // keeps the omnidirectional radiation pattern that this scenario relies on
    // while still being a phased array: the handover decisions stay driven by
    // path loss and shadowing rather than by beamforming gain.
    nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(1));
    nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(1));
    nrHelper->SetUeAntennaAttribute("AntennaElement",
                                    PointerValue(CreateObject<IsotropicAntennaModel>()));

    nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(1));
    nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(1));
    nrHelper->SetGnbAntennaAttribute("AntennaElement",
                                     PointerValue(CreateObject<IsotropicAntennaModel>()));

    // A 3GPP channel model rather than free space propagation. Friis decays as
    // the square of the distance and, at the transmit powers used here, leaves
    // the UE with a comfortable signal everywhere along the row of gNBs, so a
    // handover that arrives late never risks a coverage hole and the delay of
    // the decision cannot be measured. The 3GPP scenarios have a path loss
    // exponent well above 2 and add shadowing, which is what produces a cell
    // edge where being late actually costs packets.
    //
    // With a "Default" channel condition the LOS/NLOS state is drawn from the
    // scenario's own probability model, so unlike the free space configuration
    // the results vary with --RngRun and have to be averaged over several runs.
    Ptr<NrChannelHelper> channelHelper = CreateObject<NrChannelHelper>();
    channelHelper->ConfigureFactories(scenario, channelCondition, "ThreeGpp");

    CcBwpCreator ccBwpCreator;
    CcBwpCreator::SimpleOperationBandConf bandConf(2.8e9, 10e6, static_cast<uint8_t>(1));
    OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    channelHelper->AssignChannelsToBands({band});

    BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({band});

    NetDeviceContainer gnbDevs = nrHelper->InstallGnbDevice(gnbNodes, allBwps);
    NetDeviceContainer ueDevs = nrHelper->InstallUeDevice(ueNodes, allBwps);

    // The reduced range cell. This is what stops the strongest neighbour from
    // always being the nearest one, and so is what gives the Logic Module's
    // choice of target a right and a wrong answer.
    for (uint32_t idx = 0; idx < gnbDevs.GetN(); idx++)
    {
        double txPower = (weakGnb > 0 && idx == static_cast<uint32_t>(weakGnb - 1))
                             ? weakGnbTxPower
                             : gnbTxPower;
        NrHelper::GetGnbPhy(gnbDevs.Get(idx), 0)->SetTxPower(txPower);
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

        // There is no Hysteresis, A3Offset, or TimeToTrigger attribute here: all
        // three live in the Event A3 reporting configuration installed on the gNB
        // below, and all three are applied by the UE RRC before the report is
        // sent. MaxReportAge is the only knob the Logic Module has.
        oranHelper->SetDefaultLogicModule("ns3::OranLmNr2NrA3RsrpHandover",
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

        // The Event A3 reporting configuration: a neighbour cell became offset
        // better than the primary cell. This mirrors what
        // NrA3RsrpHandoverAlgorithm::DoInitialize installs, so that the "oran" and
        // "gnb" modes evaluate the identical condition.
        //
        // A non-negative margin is applied as hysteresis. A negative margin (for
        // example the TR 36.839 Set 5 a3-offset of -1 dB) cannot be a hysteresis,
        // since that IE is unsigned, so it is applied as the signed A3 offset.
        int8_t a3OffsetIeValue = 0;
        uint8_t hysteresisIeValue = 0;

        if (hysteresis >= 0.0)
        {
            hysteresisIeValue = nr::EutranMeasurementMapping::ActualHysteresis2IeValue(hysteresis);
        }
        else
        {
            a3OffsetIeValue = nr::EutranMeasurementMapping::ActualA3Offset2IeValue(hysteresis);
        }

        NrRrcSap::ReportConfigEutra reportConfigA3;
        reportConfigA3.eventId = NrRrcSap::ReportConfigEutra::EVENT_A3;
        reportConfigA3.a3Offset = a3OffsetIeValue;
        reportConfigA3.hysteresis = hysteresisIeValue;
        reportConfigA3.timeToTrigger = timeToTrigger.GetMilliSeconds();
        reportConfigA3.reportOnLeave = false;
        reportConfigA3.triggerQuantity = NrRrcSap::ReportConfigEutra::RSRP;
        reportConfigA3.reportInterval = NrRrcSap::ReportConfigEutra::MS1024;

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

            // Install the reporting configuration and record which measurement IDs
            // it produced, so that the Logic Module can tell an Event A3 report
            // from any other. This has to happen before the simulation starts.
            measReporter->AddMeasIds(OranReportNrGnbMeasReport::EVENT_A3,
                                     gnbRrc->AddUeMeasReportConfig(reportConfigA3));

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

    std::cout << "Handover mode " << handoverMode << ", Event A3 hysteresis " << hysteresis
              << " dB, time-to-trigger " << timeToTrigger.GetMilliSeconds() << " ms" << std::endl;
    std::cout << "gNBs at x =";
    for (uint16_t i = 0; i < numberOfGnbs; i++)
    {
        bool weak = (weakGnb > 0 && i == weakGnb - 1);
        std::cout << " " << (distance * i) << " m (CellId " << (i + 1) << ", "
                  << (weak ? weakGnbTxPower : gnbTxPower) << " dBm" << (weak ? ", weak)" : ")");
    }
    std::cout << std::endl;
    std::cout << "UE sweeps x = " << ((span / 2.0) - (speed * (interval.GetSeconds() / 2))) << " m to "
              << ((span / 2.0) + (speed * (interval.GetSeconds() / 2))) << " m at " << speed
              << " m/s, reversing every " << interval.GetSeconds() << " s" << std::endl;

    Simulator::Stop(simTime);
    Simulator::Run();

    PrintFlowStats(monitor, flowmonHelper, handoverMode);

    Simulator::Destroy();
    return 0;
}
