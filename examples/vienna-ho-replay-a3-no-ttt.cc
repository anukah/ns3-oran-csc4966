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
 * @file
 * @ingroup examples
 *
 * Vienna replay, Event A3 with NO time-to-trigger: PCI 331 (gNB 13) and
 * PCI 286 (gNB 11).
 *
 * This is the geometric Event A3 replay with the time-to-trigger defaulting to
 * zero. Re-run it with --timeToTrigger=256ms for the 3GPP-default baseline:
 * every other parameter is identical, so a diff of the two runs isolates
 * exactly one thing: what the dwell requirement is worth.
 *
 * === WHAT REMOVING THE TIME-TO-TRIGGER DOES ===
 *
 * The time-to-trigger is the only explicit DWELL requirement in the Event A3
 * decision, though not the only source of memory -- see the note on layer 3
 * filtering below. With the 3GPP default of 256 ms, the entry condition
 *
 *     Mn + Ofn + Ocn - Hys > Mp + Ofp + Ocp + Off
 *
 * must hold *continuously* in the UE RRC for the whole interval before a
 * Measurement Report is sent; if it breaks even momentarily the timer is
 * cancelled and nothing is reported. At zero the condition is evaluated
 * instantaneously, so a single measurement sample above the margin is enough.
 *
 * That converts Event A3 from a filter into a comparator, and it is the
 * degenerate baseline worth having: every transient crossing becomes a
 * handover. In this scenario the cells are close enough that fast fading alone
 * drives the differential across the margin repeatedly, so removing the dwell
 * should produce substantially more handovers than the 256 ms configuration,
 * most of them reversed within a second.
 *
 * That is the point. A handover policy is interesting only insofar as it
 * distinguishes a sustained change in radio conditions from a transient one,
 * and this configuration deliberately cannot. It gives an upper bound on
 * handover count and ping-pong rate for the same trajectory and the same
 * shadowing realisation, against which both stock A3 and any learned policy can
 * be scored. Run it with the same --rng-run as the 256 ms configuration, or the
 * comparison means nothing.
 *
 * Note that zero is a legal 3GPP time-to-trigger, so this is a permitted
 * configuration rather than a modelling hack -- it is simply one no operator
 * would deploy.
 *
 * === WHAT ZERO TIME-TO-TRIGGER DOES *NOT* REMOVE ===
 *
 * Two other things smooth the decision, and neither is affected by this file.
 *
 * Layer 3 filtering. The quantities Mn and Mp in the entry condition are not
 * raw PHY samples. NrGnbRrc::RsrpFilterCoefficient defaults to 4, which
 * NrUeRrc turns into an exponential moving average with a = 0.5^(4/4) = 0.5,
 * applied as Fn = (1 - a) * Fn-1 + a * Mn on every measurement. So each value
 * the condition sees is already half-composed of the previous one. Underneath
 * that, NrUePhy::UeMeasurementsFilterPeriod (50 ms here) averages the PHY
 * samples feeding it. A single-sample spike therefore still cannot cross the
 * margin on its own; it takes a couple of consecutive samples. Set
 * RsrpFilterCoefficient to 0 to remove this too, if a genuinely memoryless
 * comparator is what is wanted.
 *
 * RIC pipeline latency. The Reporter drain interval, the E2 transmission delay,
 * the Logic Module query interval and its processing delay all remain, totalling
 * of order 200 ms here. Removing the time-to-trigger removes the dwell
 * requirement, not the reaction time, so handovers are prompt but not
 * instantaneous.
 *
 * === WHY EVENT A3 FOR THIS SCENARIO ===
 *
 * Event A3 is the relative condition, Mn + Ofn + Ocn - Hys > Mp + Ofp + Ocp +
 * Off: a neighbour became offset better than the primary cell. That suits this
 * replay for three reasons.
 *
 * 1. The calibration errors are common-mode. gNB Tx power, the antenna panel,
 *    the downtilt and the propagation model are all assumed rather than
 *    measured, and each puts a bias on absolute RSRP. Both cells are the same
 *    operator on the same band with the same assumed panel and the same
 *    propagation model, so that bias is very largely shared and cancels in a
 *    relative comparison. An absolute threshold, as Event A2 needs, would have
 *    to absorb it.
 *
 * 2. Both cells are intra-frequency co-channel, which is the case Event A3 is
 *    configured for in a real network. Event A2 plus Event A4 is the
 *    inter-frequency pattern.
 *
 * 3. The replay is trying to hit one instant, so the time-to-trigger is the
 *    natural calibration knob, and unlike a SINR threshold it is a parameter
 *    the real network genuinely has.
 *
 * The SINR gate of vienna-ho-replay.cc, and the whole dataset limitation that
 * justified raising it to 32 dB, therefore does not exist here. The dataset
 * only carries the two cells involved in this handover, while the real network
 * has 20+ co-channel n78 cells whose interference drives serving SINR down to
 * about 5 dB; a decision rule built on RSRP ratios is not exposed to that gap
 * the way one built on absolute SINR is.
 *
 * === WHERE THE A3 CONDITION IS EVALUATED ===
 *
 * Not in the Logic Module. The entry inequality, the hysteresis, the offset and
 * the time-to-trigger all live in the Event A3 reporting configuration that
 * this file installs on each gNB with NrGnbRrc::AddUeMeasReportConfig, and all
 * of them are applied by the UE RRC in NrUeRrc::MeasurementReportTriggering
 * before a report is ever sent. An Event A3 report exists only because the
 * condition held continuously for the whole time-to-trigger, so all
 * OranLmNr2NrA3RsrpHandover does is pick the strongest reported neighbour.
 * Its only knob is MaxReportAge.
 *
 * Because NrNoOpHandoverAlgorithm registers no reporting configuration, the
 * Event A3 configuration has to be installed by hand, and it has to happen
 * before the simulation starts: AddUeMeasReportConfig aborts if called after
 * time 0.
 *
 * === PARAMETER PROVENANCE ===
 *
 * FROM DATASET (Vienna drive-test 2025-02-08):
 *   - gNB 13 position          (0, 0, 36) m  (ENU origin)
 *   - gNB 11 position          (592.62, -431.55, 58) m
 *   - gNB 13 mech. azimuth     145 deg
 *   - gNB 11 mech. azimuth     155 deg
 *   - Center frequency         3540 MHz (n78, channel 636000)
 *   - UE waypoints (18 pts)    from GPS, height 1.5 m
 *   - UE mean speed            ~6 m/s
 *   - Real HO time             t=10.766 s (relative to 03:35:04.000)
 *
 * 3GPP DEFAULTS / ASSUMED (calibration knobs):
 *   - gNB Tx power             49 dBm  (3GPP 38.104 Table 6.2.1-1, macro)
 *   - UE Tx power              23 dBm  (3GPP 38.101 default)
 *   - Bandwidth                100 MHz (n78 typical, not from dataset)
 *   - TDD pattern              DDDSU   (common n78 config, not measured)
 *   - Numerology               mu=1, 30 kHz SCS
 *   - gNB antenna              8x8 cross-pol (assumed macro panel)
 *   - UE antenna               2x2
 *   - Downtilt                 6 deg   (typical urban macro)
 *   - Pol slant angle          45 deg  (cross-pol convention)
 *   - Propagation model        3GPP TR 38.901 UMa, NLOS, shadowing ON
 *   - A3 hysteresis            3.0 dB  (NrA3RsrpHandoverAlgorithm default, and
 *                                       the margin the real network is
 *                                       believed to use at beam level)
 *   - A3 time-to-trigger       256 ms  (NrA3RsrpHandoverAlgorithm default)
 *   - A3 report interval       1024 ms
 *   - LM MaxReportAge          1.5 s   (a little above the report interval)
 *   - LM ProcessingDelay       10 ms
 *   - RIC query interval       0.1 s
 *   - E2 report interval       0.1 s
 *   - Building model           NONE (follow-up task)
 *
 * === UE STARTING POSITION ===
 *
 * The waypoints are offset by g_tOffset so that the RIC and the E2 terminators
 * are up before the trajectory starts. During that window the UE stands still
 * at the first dataset waypoint, which is what the explicit position allocator
 * below is for: MobilityHelper otherwise places a node at the origin, and
 * InitialPositionIsWaypoint turns that placement into a waypoint at t=0, which
 * would send the UE across the ~360 m to the first waypoint at ~115 m/s. An
 * earlier revision of this example did exactly that and produced a spurious
 * Event A3 at t=2.756 s, and a handover that completed at t=3.117 s rather than
 * at the t=13.766 s the replay is aiming for.
 *
 * NOTE: vienna-ho-replay.cc does NOT yet carry this fix, so its UE still makes
 * that ~115 m/s flight over the first g_tOffset seconds. The two examples are
 * therefore not directly comparable on timing until it does.
 *
 * Note also that the RIC loop adds its own latency on top of the
 * time-to-trigger: the Reporter has to drain, the Report has to reach the RIC,
 * the Logic Module has to be queried, and the Command has to travel back. A
 * RIC-side Event A3 is always slower to act than a gNB-side one on the same
 * measurements.
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

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ViennaHo331286A3NoTtt");

// --- Real PCI assignments ---------------------------------------------------
static const uint16_t PCI_SOURCE = 331; // gNB 13 (source)
static const uint16_t PCI_TARGET = 286; // gNB 11 (target)

// --- cellId to PCI mapping (populated after InstallGnbDevice) ---------------
static std::map<uint16_t, uint16_t> g_cellIdToPci;
static std::map<uint16_t, uint16_t> g_pciToCellId;

/// Offset applied to every dataset waypoint, to let the RIC come up first.
static double g_tOffset = 3.0;

/// Number of handovers that started during the run, for the final summary.
static uint32_t g_handoverStartCount = 0;
/// Number of handovers that completed during the run, for the final summary.
static uint32_t g_handoverEndOkCount = 0;
/// Number of handovers that failed during the run, for the final summary.
static uint32_t g_handoverEndErrorCount = 0;

/**
 * Translate an ns-3 cell ID into the real PCI it stands in for.
 *
 * @param cellId The ns-3 cell ID.
 * @return The real PCI, or 0 if the cell ID is unknown.
 */
static uint16_t
CellIdToPci(uint16_t cellId)
{
    auto it = g_cellIdToPci.find(cellId);
    return (it != g_cellIdToPci.end()) ? it->second : 0;
}

/**
 * Print a handover that the UE started, in terms of real PCIs.
 *
 * @param context The trace context.
 * @param imsi The IMSI of the UE.
 * @param cellId The ID of the cell that the UE is handing over from.
 * @param rnti The RNTI of the UE.
 * @param targetCellId The ID of the cell that the UE is handing over to.
 */
void
NotifyHandoverStart(std::string context,
                    uint64_t imsi,
                    uint16_t cellId,
                    uint16_t rnti,
                    uint16_t targetCellId)
{
    g_handoverStartCount++;

    std::cout << Simulator::Now().GetSeconds() << "s HO START: PCI " << CellIdToPci(cellId)
              << " -> " << CellIdToPci(targetCellId) << " (cellId " << cellId << " -> "
              << targetCellId << ")" << std::endl;
}

/**
 * Print a handover that the UE completed, in terms of real PCIs.
 *
 * @param context The trace context.
 * @param imsi The IMSI of the UE.
 * @param cellId The ID of the cell that the UE handed over to.
 * @param rnti The RNTI of the UE.
 */
void
NotifyHandoverEndOk(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
    g_handoverEndOkCount++;

    std::cout << Simulator::Now().GetSeconds() << "s HO COMPLETE: -> PCI "
              << CellIdToPci(cellId) << " (cellId " << cellId << ") RNTI " << rnti << std::endl;
}

/**
 * Print a handover that failed.
 *
 * @param context The trace context.
 * @param imsi The IMSI of the UE.
 * @param cellId The ID of the cell that the UE was handing over to.
 * @param rnti The RNTI of the UE.
 */
void
NotifyHandoverEndError(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
    g_handoverEndErrorCount++;

    std::cout << Simulator::Now().GetSeconds() << "s HO FAILED: -> PCI " << CellIdToPci(cellId)
              << " (cellId " << cellId << ") RNTI " << rnti << std::endl;
}

/**
 * Print a Measurement Report received by a gNB, with the quantized RSRP of each
 * measured cell and its equivalent in dBm. This is what to read when choosing
 * the Event A3 hysteresis and time-to-trigger.
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

    std::cout << Simulator::Now().GetSeconds() << "s MeasReport at PCI " << CellIdToPci(cellId)
              << " measId " << +results.measId << ": serving RSRP "
              << +results.measResultPCell.rsrpResult << " ("
              << nr::EutranMeasurementMapping::RsrpRange2Dbm(results.measResultPCell.rsrpResult)
              << " dBm)";

    if (results.haveMeasResultNeighCells)
    {
        for (const auto& neighbour : results.measResultListEutra)
        {
            std::cout << "; neighbour PCI " << CellIdToPci(neighbour.physCellId) << " RSRP ";
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

int
main(int argc, char* argv[])
{
    // --- Configurable parameters (all with 3GPP defaults unless noted) ---
    std::string dbFileName = "vienna-ho-331-286-a3-no-ttt.db";
    double gnbTxPower = 49.0;
    double ueTxPower = 23.0;
    double bandwidthHz = 100e6;
    uint8_t numerology = 1;
    // Defaults of ns3::NrA3RsrpHandoverAlgorithm.
    double hysteresis = 3.0;
    Time timeToTrigger = MilliSeconds(0);
    // Event A3 reports every MS1024 for as long as the condition holds, and
    // stops when it clears, so the Logic Module uses the age of the last report
    // as an implicit check that the condition still holds. This has to sit a
    // little above the reporting interval.
    Time maxReportAge = Seconds(1.5);
    double lmProcessingDelayMs = 10.0;
    double lmQueryIntervalSec = 0.1;
    double e2ReportIntervalSec = 0.1;
    double simTimeSec = 20.0;
    bool verbose = false;
    bool printMeasReports = false;
    // The handover this example replays is a marginal, fading-driven event: with
    // isotropic elements the two cells differ by only 0.9 dB at the start of the
    // trajectory, growing to about 10 dB at the end, while UMa NLOS shadow
    // fading has a standard deviation of 7.8 dB per link. Whether Event A3 ever
    // fires is therefore decided by the shadowing realisation, so the run number
    // is a first-class parameter of this scenario rather than a detail. Sweep it.
    uint32_t rngRun = 1;

    CommandLine cmd(__FILE__);
    cmd.AddValue("verbose", "Print SQL query results", verbose);
    cmd.AddValue("printMeasReports",
                 "Print every RRC Measurement Report that a gNB receives",
                 printMeasReports);
    cmd.AddValue("db-file", "SQLite database file", dbFileName);
    cmd.AddValue("gnb-tx-power", "gNB Tx power (dBm) [3GPP default]", gnbTxPower);
    cmd.AddValue("ue-tx-power", "UE Tx power (dBm) [3GPP default]", ueTxPower);
    cmd.AddValue("bandwidth", "Channel bandwidth (Hz) [assumed]", bandwidthHz);
    cmd.AddValue("numerology", "NR numerology 0-4 [assumed mu=1]", numerology);
    cmd.AddValue("hysteresis",
                 "Event A3 handover margin in dB, rounded to the nearest 0.5 dB. A "
                 "non-negative value is applied as the A3 hysteresis IE; a negative value "
                 "is applied as the signed A3 offset IE, which a plain hysteresis cannot "
                 "represent [calibration knob]",
                 hysteresis);
    cmd.AddValue("timeToTrigger",
                 "Event A3 time-to-trigger: how long the entry condition must hold "
                 "continuously in the UE RRC before a Measurement Report is sent "
                 "[calibration knob]",
                 timeToTrigger);
    cmd.AddValue("maxReportAge", "Measurement Reports older than this are ignored", maxReportAge);
    cmd.AddValue("lm-processing-delay", "LM processing delay (ms)", lmProcessingDelayMs);
    cmd.AddValue("lm-query-interval", "RIC LM query interval (s)", lmQueryIntervalSec);
    cmd.AddValue("e2-report-interval", "E2 report interval (s)", e2ReportIntervalSec);
    cmd.AddValue("sim-time", "Total simulation time (s)", simTimeSec);
    cmd.AddValue("rng-run",
                 "RNG run number. Selects the shadow-fading realisation, which is what "
                 "decides whether the handover occurs at all in this scenario. Sweep it "
                 "rather than reading any single run as the result",
                 rngRun);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(hysteresis < -15.0 || hysteresis > 15.0,
                    "hysteresis must be in the range [-15.0..15.0] dB");

    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(rngRun);

    Time simTime = Seconds(simTimeSec);
    Time lmQueryInterval = Seconds(lmQueryIntervalSec);
    std::string lmDelayRv = "ns3::ConstantRandomVariable[Constant=" +
                            std::to_string(lmProcessingDelayMs / 1000.0) + "]";
    std::string e2SendRv =
        "ns3::ConstantRandomVariable[Constant=" + std::to_string(e2ReportIntervalSec) + "]";

    // --- PHY defaults ---
    Config::SetDefault("ns3::NrGnbPhy::TxPower", DoubleValue(gnbTxPower));
    Config::SetDefault("ns3::NrUePhy::TxPower", DoubleValue(ueTxPower));
    Config::SetDefault("ns3::NrUePhy::EnableUplinkPowerControl", BooleanValue(false));
    Config::SetDefault("ns3::NrUePhy::UeMeasurementsFilterPeriod", TimeValue(MilliSeconds(50)));

    // The Reporters accumulate Reports and hand them to the E2 Terminator when
    // their trigger fires, so this interval bounds how quickly a Measurement
    // Report can reach the RIC. The default is 1 s, which would dominate the
    // whole decision latency over a 17 s replay.
    Config::SetDefault("ns3::OranReportTriggerPeriodic::IntervalRv",
                       StringValue(e2SendRv));

    // --- NR helpers ---
    Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();
    Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
    nrHelper->SetEpcHelper(epcHelper);
    nrHelper->SetAttribute("UseIdealRrc", BooleanValue(true));
    // All handover decisions come from the RIC. This also means the gNB
    // registers no measurement reporting configuration, so the Event A3
    // configuration is installed by hand further down.
    nrHelper->SetHandoverAlgorithmType("ns3::NrNoOpHandoverAlgorithm");

    // --- Create nodes ---
    NodeContainer gnbNodes;
    gnbNodes.Create(2);
    NodeContainer ueNodes;
    ueNodes.Create(1);

    // --- gNB placement (ENU, origin = gNB 13) ---
    Ptr<ListPositionAllocator> gnbPosAlloc = CreateObject<ListPositionAllocator>();
    gnbPosAlloc->Add(Vector(0.0, 0.0, 36.0));       // gNB 13 (PCI 331)
    gnbPosAlloc->Add(Vector(592.62, -431.55, 58.0)); // gNB 11 (PCI 286)

    MobilityHelper gnbMobility;
    gnbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    gnbMobility.SetPositionAllocator(gnbPosAlloc);
    gnbMobility.Install(gnbNodes);

    // --- UE trajectory via WaypointMobilityModel ---
    struct WP
    {
        double t; ///< Time of the waypoint, before the offset is applied
        double x; ///< ENU east coordinate, in metres
        double y; ///< ENU north coordinate, in metres
    };

    // All values measured from phone_data_5g.parquet, PCI in {331, 286, 285},
    // window 03:35:04.000 to 03:35:20.800 UTC. The real handover from 331 to
    // 286 occurs at t = 10.766 s.
    const WP waypoints[] = {
        {0.14, 312.65, -178.69},
        {0.66, 308.58, -174.46},
        {1.67, 304.72, -169.79},
        {2.67, 300.79, -164.79},
        {3.72, 296.79, -160.12},
        {4.72, 293.01, -155.01},
        {5.73, 289.01, -150.34},
        {6.73, 284.93, -145.67},
        {7.75, 281.08, -140.55},
        {8.74, 277.30, -135.88},
        {9.74, 273.22, -132.10},
        {10.77, 269.22, -126.98}, // real HO ~here (03:35:14.766 UTC)
        {11.78, 265.14, -122.31},
        {12.79, 261.43, -117.64},
        {13.78, 257.65, -113.86},
        {14.76, 253.95, -109.19},
        {15.79, 250.17, -104.52},
        {16.80, 246.17, -100.30},
    };

    // The UE must already be standing at the first dataset waypoint when the run
    // begins. MobilityHelper defaults to placing a node at the origin, and
    // InitialPositionIsWaypoint turns that placement into a waypoint at t=0, so
    // without an explicit allocator the UE would cover the ~360 m from the
    // origin to the first waypoint at ~115 m/s during the offset window. It
    // holds this position until the trajectory starts at g_tOffset.
    Ptr<ListPositionAllocator> uePosAlloc = CreateObject<ListPositionAllocator>();
    uePosAlloc->Add(Vector(waypoints[0].x, waypoints[0].y, 1.5));

    MobilityHelper ueMobility;
    ueMobility.SetMobilityModel("ns3::WaypointMobilityModel",
                                "InitialPositionIsWaypoint",
                                BooleanValue(true));
    ueMobility.SetPositionAllocator(uePosAlloc);
    ueMobility.Install(ueNodes);

    Ptr<WaypointMobilityModel> waypointMob = ueNodes.Get(0)->GetObject<WaypointMobilityModel>();

    for (const auto& wp : waypoints)
    {
        waypointMob->AddWaypoint(Waypoint(Seconds(wp.t + g_tOffset), Vector(wp.x, wp.y, 1.5)));
    }

    // --- Antenna configuration ---
    // gNB: 8x8 cross-pol panel, 6 deg downtilt, 45 deg pol slant
    nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(8));
    nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(8));
    nrHelper->SetGnbAntennaAttribute("IsDualPolarized", BooleanValue(true));
    nrHelper->SetGnbAntennaAttribute("NumHorizontalPorts", UintegerValue(4));
    nrHelper->SetGnbAntennaAttribute("NumVerticalPorts", UintegerValue(4));
    nrHelper->SetGnbAntennaAttribute("DowntiltAngle", DoubleValue(6.0 * M_PI / 180.0));
    nrHelper->SetGnbAntennaAttribute("PolSlantAngle", DoubleValue(45.0 * M_PI / 180.0));
    // UE: 2x2
    nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(2));
    nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(2));

    // --- Channel model: 3GPP UMa with shadowing ---
    Ptr<NrChannelHelper> channelHelper = CreateObject<NrChannelHelper>();
    channelHelper->ConfigureFactories("UMa", "Default", "ThreeGpp");

    // --- Band n78: 3540 MHz center, 100 MHz BW, mu=1 ---
    CcBwpCreator ccBwpCreator;
    CcBwpCreator::SimpleOperationBandConf bandConf(3.54e9,
                                                   bandwidthHz,
                                                   static_cast<uint8_t>(numerology));
    OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    channelHelper->AssignChannelsToBands({band});

    BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({band});

    // --- Install devices ---
    NetDeviceContainer gnbDevs = nrHelper->InstallGnbDevice(gnbNodes, allBwps);
    NetDeviceContainer ueDevs = nrHelper->InstallUeDevice(ueNodes, allBwps);

    // --- Per-gNB antenna bearing (set after install) ---
    // Azimuth convention: dataset = clockwise from North
    // ns-3 BearingAngle = counter-clockwise from East = (90 - azimuth) deg
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
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    internet.Install(ueNodes);
    Ipv4InterfaceContainer ueIpIfaces = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueDevs));

    // --- X2 + initial attachment to gNB 13 (PCI 331) ---
    nrHelper->AddX2Interface(gnbNodes);
    nrHelper->AttachToGnb(ueDevs.Get(0), gnbDevs.Get(0));

    Ptr<NrUeNetDevice> ueNetDev = ueDevs.Get(0)->GetObject<NrUeNetDevice>();
    std::cout << "Initial attachment: cellId=" << ueNetDev->GetCellId()
              << " PCI=" << CellIdToPci(ueNetDev->GetCellId()) << std::endl;

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

    // =======================================================================
    // O-RAN setup
    // =======================================================================
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
    oranHelper->SetAttribute("LmQueryLateCommandPolicy", EnumValue(OranNearRtRic::DROP));
    oranHelper->SetAttribute("RicTransmissionDelayRv",
                             StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));

    oranHelper->SetDataRepository("ns3::OranDataRepositorySqlite",
                                  "DatabaseFile",
                                  StringValue(dbFileName));

    // There is no Hysteresis, A3Offset, or TimeToTrigger attribute here: all
    // three live in the Event A3 reporting configuration installed on the gNBs
    // below, and all three are applied by the UE RRC before the report is sent.
    // MaxReportAge is the only knob the Logic Module has.
    oranHelper->SetDefaultLogicModule("ns3::OranLmNr2NrA3RsrpHandover",
                                      "MaxReportAge",
                                      TimeValue(maxReportAge),
                                      "ProcessingDelayRv",
                                      StringValue(lmDelayRv));

    oranHelper->SetConflictMitigationModule("ns3::OranCmmHandover");

    nearRtRic = oranHelper->CreateNearRtRic();

    // --- UE E2 terminators ---
    // Built by hand rather than through OranHelper::DeployTerminators, because
    // the RSRP/RSRQ and SINR Reporters have to be connected to the trace sources
    // of this specific UE PHY, which the helper cannot do.
    //
    // The Event A3 Logic Module does not read these: it works entirely from the
    // RRC Measurement Reports the gNBs forward. They are here because this
    // example is about characterising transient crossings, and to see one you
    // need the per-cell RSRP that produced it. OranReporterNrUeRsrpRsrq fills
    // nruersrprsrq with one row per measured cell every 100 ms whether or not a
    // handover occurs, which is also the feature set a learned policy trains on.
    for (uint32_t idx = 0; idx < ueNodes.GetN(); idx++)
    {
        Ptr<OranE2NodeTerminatorNrUe> ueTerminator = CreateObject<OranE2NodeTerminatorNrUe>();
        ueTerminator->SetAttribute("NearRtRic", PointerValue(nearRtRic));
        ueTerminator->SetAttribute("RegistrationIntervalRv",
                                   StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        ueTerminator->SetAttribute("SendIntervalRv", StringValue(e2SendRv));
        ueTerminator->SetAttribute("TransmissionDelayRv",
                                   StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));

        Ptr<OranReporterLocation> locationReporter = CreateObject<OranReporterLocation>();
        locationReporter->SetAttribute("Terminator", PointerValue(ueTerminator));
        locationReporter->SetAttribute("Trigger", StringValue("ns3::OranReportTriggerPeriodic"));

        Ptr<OranReporterNrUeCellInfo> cellReporter = CreateObject<OranReporterNrUeCellInfo>();
        cellReporter->SetAttribute("Terminator", PointerValue(ueTerminator));
        cellReporter->SetAttribute(
            "Trigger",
            StringValue("ns3::OranReportTriggerNrUeHandover[InitialReport=true]"));

        Ptr<OranReporterNrUeRsrpRsrq> rsrpReporter = CreateObject<OranReporterNrUeRsrpRsrq>();
        rsrpReporter->SetAttribute("Terminator", PointerValue(ueTerminator));
        rsrpReporter->SetAttribute("Trigger", StringValue("ns3::OranReportTriggerPeriodic"));

        Ptr<OranReporterNrUeSinr> sinrReporter = CreateObject<OranReporterNrUeSinr>();
        sinrReporter->SetAttribute("Terminator", PointerValue(ueTerminator));
        sinrReporter->SetAttribute("Trigger", StringValue("ns3::OranReportTriggerPeriodic"));

        bool wired = false;
        for (uint32_t d = 0; d < ueNodes.Get(idx)->GetNDevices(); d++)
        {
            Ptr<NrUeNetDevice> nrUeDev =
                ueNodes.Get(idx)->GetDevice(d)->GetObject<NrUeNetDevice>();
            if (nrUeDev)
            {
                Ptr<NrUePhy> uePhy = nrUeDev->GetPhy(0);
                uePhy->TraceConnectWithoutContext(
                    "ReportUeMeasurements",
                    MakeCallback(&OranReporterNrUeRsrpRsrq::ReportRsrpRsrq, rsrpReporter));
                uePhy->TraceConnectWithoutContext(
                    "DlCtrlSinr",
                    MakeCallback(&OranReporterNrUeSinr::ReportSinr, sinrReporter));
                wired = true;
                break;
            }
        }
        NS_ABORT_MSG_IF(!wired, "Could not find the NR UE PHY to attach the Reporters to");

        ueTerminator->AddReporter(locationReporter);
        ueTerminator->AddReporter(cellReporter);
        ueTerminator->AddReporter(rsrpReporter);
        ueTerminator->AddReporter(sinrReporter);

        ueTerminator->Attach(ueNodes.Get(idx));
        e2TermsUes.Add(ueTerminator);
    }

    // --- Event A3 reporting configuration ---
    // A neighbour cell became offset better than the primary cell. This mirrors
    // what NrA3RsrpHandoverAlgorithm::DoInitialize installs.
    //
    // A non-negative margin is applied as hysteresis. A negative margin cannot
    // be a hysteresis, since that IE is unsigned, so it is applied as the
    // signed A3 offset.
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

    // --- gNB E2 terminators ---
    // Built by hand rather than through OranHelper::AddReporter, because each
    // Measurement Report Reporter has to be given the measurement IDs of the
    // reporting configurations installed on its own gNB, and connected to that
    // gNB's RRC trace source.
    for (uint32_t idx = 0; idx < gnbNodes.GetN(); idx++)
    {
        Ptr<OranE2NodeTerminatorNrGnb> gnbTerminator = CreateObject<OranE2NodeTerminatorNrGnb>();
        gnbTerminator->SetAttribute("NearRtRic", PointerValue(nearRtRic));
        gnbTerminator->SetAttribute("RegistrationIntervalRv",
                                    StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        gnbTerminator->SetAttribute("SendIntervalRv", StringValue(e2SendRv));
        gnbTerminator->SetAttribute("TransmissionDelayRv",
                                    StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));

        Ptr<OranReporterLocation> locationReporter = CreateObject<OranReporterLocation>();
        locationReporter->SetAttribute("Terminator", PointerValue(gnbTerminator));
        locationReporter->SetAttribute("Trigger", StringValue("ns3::OranReportTriggerPeriodic"));

        Ptr<OranReporterNrGnbMeasReport> measReporter =
            CreateObject<OranReporterNrGnbMeasReport>();
        measReporter->SetAttribute("Terminator", PointerValue(gnbTerminator));
        measReporter->SetAttribute("Trigger", StringValue("ns3::OranReportTriggerPeriodic"));

        Ptr<NrGnbNetDevice> gnbDev = gnbDevs.Get(idx)->GetObject<NrGnbNetDevice>();
        NS_ABORT_MSG_IF(gnbDev == nullptr, "Unable to find the NR gNB network device");
        Ptr<NrGnbRrc> gnbRrc = gnbDev->GetRrc();

        // Install the reporting configuration and record which measurement IDs
        // it produced, so that the Logic Module can tell an Event A3 report from
        // any other. This has to happen before the simulation starts.
        measReporter->AddMeasIds(OranReportNrGnbMeasReport::EVENT_A3,
                                 gnbRrc->AddUeMeasReportConfig(reportConfigA3));

        gnbRrc->TraceConnectWithoutContext(
            "RecvMeasurementReport",
            MakeCallback(&OranReporterNrGnbMeasReport::ReportMeasurements, measReporter));

        gnbTerminator->AddReporter(locationReporter);
        gnbTerminator->AddReporter(measReporter);

        gnbTerminator->Attach(gnbNodes.Get(idx));
        e2TermsGnbs.Add(gnbTerminator);
    }

    if (verbose)
    {
        nearRtRic->Data()->TraceConnectWithoutContext("QueryRc", MakeCallback(&QueryRcSink));
    }

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
    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverStart",
                    MakeCallback(&NotifyHandoverStart));
    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverEndOk",
                    MakeCallback(&NotifyHandoverEndOk));
    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverEndError",
                    MakeCallback(&NotifyHandoverEndError));

    std::cout << "RngRun " << rngRun << ", Event A3 hysteresis " << hysteresis
              << " dB, time-to-trigger " << timeToTrigger.GetMilliSeconds()
              << " ms, waypoint offset " << g_tOffset << " s (real HO expected at t="
              << 10.766 + g_tOffset << " s)" << std::endl;

    // --- Run ---
    Simulator::Stop(simTime);
    Simulator::Run();

    std::cout << "\n=== Simulation complete ===" << std::endl;
    std::cout << "Handovers started:   " << g_handoverStartCount << std::endl;
    std::cout << "Handovers completed: " << g_handoverEndOkCount << std::endl;
    std::cout << "Handovers failed:    " << g_handoverEndErrorCount << std::endl;
    std::cout << "SQLite DB:           " << dbFileName << std::endl;
    // One machine-readable line per run, so a sweep over --rng-run can be
    // grepped without opening each database.
    std::cout << "SWEEP rngRun=" << rngRun << " hysteresis=" << hysteresis
              << " ttt=" << timeToTrigger.GetMilliSeconds() << " handovers="
              << g_handoverEndOkCount << std::endl;

    Simulator::Destroy();

    return 0;
}
