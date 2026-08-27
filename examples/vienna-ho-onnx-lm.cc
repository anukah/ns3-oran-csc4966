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
 * Closed-loop test of an ONNX handover classifier on the Vienna fitted
 * ping-pong scenario: PCI 331 and PCI 286.
 *
 * The channel, the geometry, the trajectory and the RIC timing are identical to
 * vienna-ho-fitted-pingpong.cc. The only thing --lm changes is WHO decides, so
 * the three arms are directly comparable on one scenario:
 *
 *   ran    The gNB's own NrA3RsrpHandoverAlgorithm. No RIC decision at all: the
 *          RIC still registers every node and stores every Report, so the
 *          database is comparable, but its Logic Module is OranLmNoop. This is
 *          the zero-latency reference, since the gNB acts on the Measurement
 *          Report the instant it arrives.
 *
 *   a3     The stock rule-based RIC Logic Module, OranLmNr2NrA3RsrpHandover.
 *          No model. Same A3 rule as "ran", but the decision now crosses E2 and
 *          waits on the RIC's query loop, which costs 155 to 305 ms on this
 *          scenario. This is the arm that produced the training labels.
 *
 *   onnx   The same RIC pipeline, but OranLmNr2NrRsrpLagOnnxHandover replaces
 *          the rule with an ONNX classifier.
 *
 * So "ran" versus "a3" isolates the cost of putting the decision in the RIC,
 * and "a3" versus "onnx" isolates the cost of replacing the rule with a model.
 *
 * === THE MODEL ===
 *
 * Six features, all of them the same quantity at different ages:
 *
 *     d(t), d(t-1), d(t-2), d(t-3), d(t-4), d(t-5)
 *
 * with d = RSRP(best neighbour) - RSRP(serving) in dB, signed so that positive
 * means the neighbour is stronger. No SINR, no position, no distance. Trained
 * by examples/vienna_ho_rf_lag.py on vienna-fitted-dataset.csv, which is the
 * labelled dataset this same scenario produces under --handover=ric.
 *
 * WHY THE HISTORY IS THE WHOLE MODEL. The label in that dataset marks the
 * instant the RIC dispatched the Command, which is 256 ms of time-to-trigger
 * plus 155 to 305 ms of RIC pipeline after the A3 condition first held: four
 * to six samples later. So at the positive row, d is around 3.1 to 3.8 dB, and
 * at the four to six negative rows immediately before it, d is also around 3.0
 * to 3.8 dB. On the instantaneous value alone those rows are not separable at
 * all. The lag window is the only thing that distinguishes them, because it
 * shows how LONG d has been above the margin.
 *
 * The consequence is that this model has not learned "hand over when the
 * neighbour is 3 dB stronger". It has learned "hand over when the neighbour
 * has been about 3 dB stronger for roughly half a second", which folds the
 * time-to-trigger AND the RIC's own queueing latency into the classifier. Those
 * are properties of the configuration that produced the training set, not of
 * the radio environment, so a model trained at one --lm-query-interval or
 * --e2-report-interval does not transfer to another.
 *
 * === OPEN LOOP VERSUS CLOSED LOOP ===
 *
 * This is the part worth watching, and it is why the example exists rather than
 * a second notebook cell.
 *
 * The training set was recorded with the A3 Logic Module driving. Replaying the
 * model over that CSV is an OPEN-LOOP test: every feature row was produced by
 * decisions the model did not make. Here the model is IN the loop, so the
 * moment it fires at even a slightly different instant than A3 did, the serving
 * cell changes at a different time, d flips sign at a different time, and every
 * subsequent feature vector is drawn from a distribution the model never saw.
 * Errors compound instead of averaging out.
 *
 * A model that scores perfectly on the CSV can therefore still diverge here,
 * and that divergence is the honest measure of whether it learned the decision
 * or memorised the trajectory. Compare the handover count and times against
 * --lm=a3 on the same run.
 *
 * === RUNNING IT ===
 *
 *   python3 contrib/oran/examples/vienna_ho_rf_lag.py \
 *       --csv vienna-fitted-dataset.csv
 *   ./ns3 run "vienna-ho-onnx-lm --lm=onnx"
 *   ./ns3 run "vienna-ho-onnx-lm --lm=a3"      # baseline
 *
 * The training script writes the model into examples/, and the default
 * --onnx-model path points there, so nothing has to be copied to the ns-3
 * root. The path is resolved relative to the working directory, which is the
 * ns-3 root under `./ns3 run`.
 *
 * THE QUERY INTERVAL IS PART OF THE MODEL. The lag features are spaced by one
 * Logic Module query, not by a fixed time, so --lm-query-interval must stay at
 * the 0.1 s the training CSV was sampled at. The example refuses to run the
 * ONNX Logic Module at any other value rather than silently feeding the model
 * a rescaled history.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/isotropic-antenna-model.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/nr-module.h"
#include "ns3/oran-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/propagation-loss-model.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ViennaHoOnnxLm");

// ===========================================================================
// Scenario constants, identical to vienna-ho-fitted-pingpong.cc
// ===========================================================================

/// Real PCIs, in the order the gNB nodes are created.
static const uint16_t PCIS[2] = {331, 286};

/// gNB positions in ENU metres, origin at the PCI 331 site.
static const Vector SITES[2] = {Vector(0.0, 0.0, 36.0), Vector(592.62, -431.55, 58.0)};

/// Sector azimuths, degrees clockwise from North, per estimated_cell_info.
static const double AZIMUTHS[2] = {145.0, 155.0};

/// Fitted pathloss coefficients: PL = A + 10 n log10(d3D) + k A(phi).
static const double FIT_A[2] = {80.5, 36.7};
static const double FIT_N[2] = {1.44, 2.83};
static const double FIT_K[2] = {0.28, 0.51};

/// UE antenna height in metres.
static const double UE_HEIGHT = 1.5;

/// Offset applied to the generated trajectory, to let the RIC come up first.
static double g_tOffset = 3.0;

// ===========================================================================
// ViennaFittedPropagationLossModel
// ===========================================================================

/**
 * @ingroup examples
 *
 * Pathloss from a per-cell fit to the Vienna drive test, as a function of 3D
 * distance and off-boresight angle, with optional spatially correlated
 * shadowing. See vienna-ho-fitted-pingpong.cc for the derivation of the
 * coefficients and for why the antennas must stay isotropic.
 */
class ViennaFittedPropagationLossModel : public PropagationLossModel
{
  public:
    /**
     * Gets the TypeId of the ViennaFittedPropagationLossModel class.
     *
     * @return The TypeId.
     */
    static TypeId GetTypeId();

    ViennaFittedPropagationLossModel() = default;

    /**
     * Registers a transmitter with its site geometry and fitted coefficients.
     *
     * @param mob The MobilityModel of the transmitting node.
     * @param idx Index into the SITES, AZIMUTHS and FIT_ arrays.
     * @param shadowingSigmaDb Shadowing standard deviation in dB, 0 to disable.
     * @param decorrDistM Shadowing decorrelation distance in metres.
     */
    static void RegisterCell(Ptr<const MobilityModel> mob,
                             uint32_t idx,
                             double shadowingSigmaDb,
                             double decorrDistM);

    /**
     * Evaluates the fitted pathloss of a registered cell at a position,
     * excluding shadowing.
     *
     * @param idx Index into the SITES, AZIMUTHS and FIT_ arrays.
     * @param x The ENU east coordinate in metres.
     * @param y The ENU north coordinate in metres.
     * @return The pathloss in dB.
     */
    static double MeanPathlossDb(uint32_t idx, double x, double y);

  private:
    double DoCalcRxPower(double txPowerDbm,
                         Ptr<MobilityModel> a,
                         Ptr<MobilityModel> b) const override;
    int64_t DoAssignStreams(int64_t stream) override;

    /// Per-cell registration and shadowing state.
    struct Cell
    {
        uint32_t idx;               ///< Index into the scenario constant arrays
        double sigma;               ///< Shadowing standard deviation in dB
        double decorr;              ///< Shadowing decorrelation distance in metres
        mutable double shadow{0};   ///< Current shadowing value in dB
        mutable bool primed{false}; ///< Whether shadow has been drawn at least once
        mutable Vector lastPos;     ///< UE position at the last shadowing update
    };

    /// Registered cells, keyed by the transmitter's MobilityModel.
    static std::map<Ptr<const MobilityModel>, Cell> m_cells;
    /// Random stream for shadowing.
    static Ptr<NormalRandomVariable> m_norm;
};

std::map<Ptr<const MobilityModel>, ViennaFittedPropagationLossModel::Cell>
    ViennaFittedPropagationLossModel::m_cells;
Ptr<NormalRandomVariable> ViennaFittedPropagationLossModel::m_norm;

NS_OBJECT_ENSURE_REGISTERED(ViennaFittedPropagationLossModel);

TypeId
ViennaFittedPropagationLossModel::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ViennaFittedPropagationLossModel")
                            .SetParent<PropagationLossModel>()
                            .SetGroupName("Propagation")
                            .AddConstructor<ViennaFittedPropagationLossModel>();
    return tid;
}

void
ViennaFittedPropagationLossModel::RegisterCell(Ptr<const MobilityModel> mob,
                                               uint32_t idx,
                                               double shadowingSigmaDb,
                                               double decorrDistM)
{
    if (!m_norm)
    {
        m_norm = CreateObject<NormalRandomVariable>();
    }
    m_cells[mob] = Cell{idx, shadowingSigmaDb, decorrDistM, 0.0, false, Vector()};
}

double
ViennaFittedPropagationLossModel::MeanPathlossDb(uint32_t idx, double x, double y)
{
    const Vector& s = SITES[idx];
    const double dx = x - s.x;
    const double dy = y - s.y;
    const double d2d = std::hypot(dx, dy);
    const double d3d = std::hypot(d2d, s.z - UE_HEIGHT);

    double bearing = std::atan2(dx, dy) * 180.0 / M_PI;
    double phi = bearing - AZIMUTHS[idx];
    while (phi > 180.0)
    {
        phi -= 360.0;
    }
    while (phi < -180.0)
    {
        phi += 360.0;
    }

    const double sector = std::min(12.0 * std::pow(std::abs(phi) / 65.0, 2.0), 30.0);
    return FIT_A[idx] + 10.0 * FIT_N[idx] * std::log10(std::max(d3d, 1.0)) + FIT_K[idx] * sector;
}

double
ViennaFittedPropagationLossModel::DoCalcRxPower(double txPowerDbm,
                                                Ptr<MobilityModel> a,
                                                Ptr<MobilityModel> b) const
{
    auto it = m_cells.find(a);
    Ptr<MobilityModel> ue = b;
    if (it == m_cells.end())
    {
        it = m_cells.find(b);
        ue = a;
    }
    if (it == m_cells.end())
    {
        return txPowerDbm - 200.0; // unregistered: treat as blocked
    }

    const Cell& c = it->second;
    const Vector p = ue->GetPosition();
    double loss = MeanPathlossDb(c.idx, p.x, p.y);

    if (c.sigma > 0.0)
    {
        if (!c.primed)
        {
            c.shadow = m_norm->GetValue(0.0, c.sigma * c.sigma);
            c.primed = true;
            c.lastPos = p;
        }
        else
        {
            const double step = CalculateDistance(p, c.lastPos);
            if (step > 0.0)
            {
                const double rho = std::exp(-step / std::max(c.decorr, 1.0));
                c.shadow = rho * c.shadow +
                           std::sqrt(1.0 - rho * rho) * m_norm->GetValue(0.0, c.sigma * c.sigma);
                c.lastPos = p;
            }
        }
        loss += c.shadow;
    }

    return txPowerDbm - loss;
}

int64_t
ViennaFittedPropagationLossModel::DoAssignStreams(int64_t stream)
{
    if (m_norm)
    {
        m_norm->SetStream(stream);
        return 1;
    }
    return 0;
}

// ===========================================================================
// Trajectory generation
// ===========================================================================

/**
 * Finds the x at which the two cells are equal on a given y line, by bisection.
 *
 * @param y The ENU north coordinate in metres.
 * @return The ENU east coordinate of the boundary in metres.
 */
static double
BoundaryX(double y)
{
    double lo = 60.0;
    double hi = 1400.0;
    for (int i = 0; i < 70; i++)
    {
        const double mid = 0.5 * (lo + hi);
        const double d = ViennaFittedPropagationLossModel::MeanPathlossDb(0, mid, y) -
                         ViennaFittedPropagationLossModel::MeanPathlossDb(1, mid, y);
        if (d < 0.0)
        {
            lo = mid;
        }
        else
        {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

// ===========================================================================
// Reporting
// ===========================================================================

/// cellId to PCI mapping, populated after InstallGnbDevice.
static std::map<uint16_t, uint16_t> g_cellIdToPci;
/// Handover counters for the final summary.
static uint32_t g_handoverStartCount = 0;
static uint32_t g_handoverEndOkCount = 0;
static uint32_t g_handoverEndErrorCount = 0;
/// Times at which a handover completed, to count reversals.
static std::vector<double> g_handoverTimes;
/// PCI the UE was attached to before the handover currently in flight.
static uint16_t g_pendingFromPci = 0;

/**
 * Translate an ns-3 cell ID into the real PCI it stands in for.
 *
 * @param cellId The ns-3 cell ID.
 * @return The real PCI, or 0 if unknown.
 */
static uint16_t
CellIdToPci(uint16_t cellId)
{
    auto it = g_cellIdToPci.find(cellId);
    return (it != g_cellIdToPci.end()) ? it->second : 0;
}

/**
 * Record a handover that the UE started.
 *
 * @param context The trace context.
 * @param imsi The IMSI of the UE.
 * @param cellId The ID of the cell handed over from.
 * @param rnti The RNTI of the UE.
 * @param targetCellId The ID of the cell handed over to.
 */
void
NotifyHandoverStart(std::string context,
                    uint64_t imsi,
                    uint16_t cellId,
                    uint16_t rnti,
                    uint16_t targetCellId)
{
    g_handoverStartCount++;
    g_pendingFromPci = CellIdToPci(cellId);
}

/**
 * Print and record a handover that the UE completed.
 *
 * @param context The trace context.
 * @param imsi The IMSI of the UE.
 * @param cellId The ID of the cell handed over to.
 * @param rnti The RNTI of the UE.
 */
void
NotifyHandoverEndOk(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
    g_handoverEndOkCount++;
    const double t = Simulator::Now().GetSeconds();
    g_handoverTimes.push_back(t);
    std::cout << t << "s HO: PCI " << g_pendingFromPci << " -> " << CellIdToPci(cellId)
              << std::endl;
}

/**
 * Print a handover that failed.
 *
 * @param context The trace context.
 * @param imsi The IMSI of the UE.
 * @param cellId The ID of the cell that was the target.
 * @param rnti The RNTI of the UE.
 */
void
NotifyHandoverEndError(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
    g_handoverEndErrorCount++;
    std::cout << Simulator::Now().GetSeconds() << "s HO FAILED: -> PCI " << CellIdToPci(cellId)
              << std::endl;
}

/**
 * Print end-to-end flow statistics.
 *
 * @param monitor The FlowMonitor.
 * @param helper The FlowMonitorHelper that installed it, for the classifier.
 * @param label A tag printed on the machine-readable summary line.
 */
void
PrintFlowStats(Ptr<FlowMonitor> monitor, FlowMonitorHelper& helper, const std::string& label)
{
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(helper.GetClassifier());

    std::cout << "\n=== Flow monitor ===" << std::endl;
    for (const auto& [id, s] : monitor->GetFlowStats())
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(id);
        const double span = s.timeLastRxPacket.GetSeconds() - s.timeFirstTxPacket.GetSeconds();
        const double mbps = (span > 0.0) ? s.rxBytes * 8.0 / span / 1e6 : 0.0;
        const uint64_t missing = s.txPackets - s.rxPackets;
        const double lossPct = (s.txPackets > 0) ? 100.0 * missing / s.txPackets : 0.0;
        const double meanDelayMs =
            (s.rxPackets > 0) ? s.delaySum.GetSeconds() * 1000.0 / s.rxPackets : 0.0;
        const double meanJitterMs =
            (s.rxPackets > 1) ? s.jitterSum.GetSeconds() * 1000.0 / (s.rxPackets - 1) : 0.0;

        std::cout << "Flow " << id << "  " << t.sourceAddress << ":" << t.sourcePort << " -> "
                  << t.destinationAddress << ":" << t.destinationPort << std::endl;
        std::cout << "  Tx packets      " << s.txPackets << std::endl;
        std::cout << "  Rx packets      " << s.rxPackets << std::endl;
        std::cout << "  Not received    " << missing << "  (" << lossPct << " %)" << std::endl;
        std::cout << "  Throughput      " << mbps << " Mb/s" << std::endl;
        std::cout << "  Mean delay      " << meanDelayMs << " ms" << std::endl;
        std::cout << "  Mean jitter     " << meanJitterMs << " ms" << std::endl;
        std::cout << "FLOW " << label << " id=" << id << " tx=" << s.txPackets
                  << " rx=" << s.rxPackets << " lost=" << missing << " lossPct=" << lossPct
                  << " mbps=" << mbps << " delayMs=" << meanDelayMs
                  << " jitterMs=" << meanJitterMs << std::endl;
    }
}

int
main(int argc, char* argv[])
{
    std::string dbFileName = "vienna-ho-onnx.db";
    std::string lmMode = "onnx";
    std::string onnxModelPath = "contrib/oran/examples/vienna_ho_rf_lag.onnx";
    uint32_t numLags = 5;
    double decisionThreshold = 0.5;
    double gnbTxPower = 49.0;
    double ueTxPower = 23.0;
    double bandwidthHz = 100e6;
    uint8_t numerology = 1;
    double hysteresis = 3.0;
    Time timeToTrigger = MilliSeconds(256);
    Time maxReportAge = Seconds(1.5);
    double lmProcessingDelayMs = 10.0;
    double lmQueryIntervalSec = 0.1;
    double e2ReportIntervalSec = 0.2;
    double pathY0 = -500.0;
    double pathVy = 4.0;
    double pathAmplitude = 85.0;
    double pathPeriod = 20.0;
    double pathDuration = 80.0;
    double pathStep = 0.5;
    double shadowingSigma = 0.0;
    double shadowingDecorr = 50.0;
    uint32_t rngRun = 1;
    bool useFlowMonitor = true;
    double dlIntervalMs = 10.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("db-file", "SQLite database file", dbFileName);
    cmd.AddValue("lm",
                 "Who decides the handover. \"onnx\": the ONNX classifier over lagged RSRP "
                 "differences, running as a RIC Logic Module. \"a3\": the stock rule-based RIC "
                 "Logic Module OranLmNr2NrA3RsrpHandover, no model, which is the baseline the "
                 "training data was recorded under. \"ran\": no RIC decision at all, the gNB's "
                 "own NrA3RsrpHandoverAlgorithm, with the RIC reporting but not acting",
                 lmMode);
    cmd.AddValue("onnx-model",
                 "Path to the ONNX model, resolved relative to the working directory. Generate "
                 "it with examples/vienna_ho_rf_lag.py, which writes it into examples/",
                 onnxModelPath);
    cmd.AddValue("num-lags",
                 "Number of lagged RSRP differences in the feature vector. Must match the "
                 "model: the shipped one was trained with 5, for an input width of 6",
                 numLags);
    cmd.AddValue("decision-threshold",
                 "Probability of the handover class above which the Command is issued. Lower "
                 "to trade precision for recall without re-exporting the model",
                 decisionThreshold);
    cmd.AddValue("flow-monitor", "Install FlowMonitor and print end-to-end statistics",
                 useFlowMonitor);
    cmd.AddValue("dl-interval", "Downlink packet interval (ms)", dlIntervalMs);
    cmd.AddValue("gnb-tx-power", "gNB Tx power (dBm) [3GPP default]", gnbTxPower);
    cmd.AddValue("ue-tx-power", "UE Tx power (dBm) [3GPP default]", ueTxPower);
    cmd.AddValue("bandwidth", "Channel bandwidth (Hz) [assumed]", bandwidthHz);
    cmd.AddValue("numerology", "NR numerology 0-4 [assumed mu=1]", numerology);
    cmd.AddValue("hysteresis", "Event A3 handover margin in dB, used only with --lm=a3",
                 hysteresis);
    cmd.AddValue("timeToTrigger", "Event A3 time-to-trigger, used only with --lm=a3",
                 timeToTrigger);
    cmd.AddValue("maxReportAge", "Measurement Reports older than this are ignored", maxReportAge);
    cmd.AddValue("lm-processing-delay", "LM processing delay (ms)", lmProcessingDelayMs);
    cmd.AddValue("lm-query-interval",
                 "RIC LM query interval (s). This is also the spacing of the ONNX model's lag "
                 "features, so it must stay at the 0.1 s the training set was sampled at",
                 lmQueryIntervalSec);
    cmd.AddValue("e2-report-interval", "E2 report interval (s)", e2ReportIntervalSec);
    cmd.AddValue("path-y0", "Trajectory start, ENU north coordinate (m)", pathY0);
    cmd.AddValue("path-vy", "Trajectory northward rate (m/s)", pathVy);
    cmd.AddValue("path-amplitude", "Sway either side of the equal-RSRP boundary (m)",
                 pathAmplitude);
    cmd.AddValue("path-period", "Sway period (s)", pathPeriod);
    cmd.AddValue("path-duration", "Trajectory duration (s)", pathDuration);
    cmd.AddValue("path-step", "Waypoint spacing along the trajectory (s)", pathStep);
    cmd.AddValue("shadowing-sigma",
                 "Shadowing standard deviation in dB. Non-zero takes the scenario off the "
                 "trajectory the model was trained on, which is the interesting test",
                 shadowingSigma);
    cmd.AddValue("shadowing-decorr", "Shadowing decorrelation distance (m)", shadowingDecorr);
    cmd.AddValue("rng-run", "RNG run number", rngRun);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(lmMode != "onnx" && lmMode != "a3" && lmMode != "ran",
                    "lm must be \"onnx\", \"a3\" or \"ran\"");
    NS_ABORT_MSG_IF(hysteresis < -15.0 || hysteresis > 15.0,
                    "hysteresis must be in the range [-15.0..15.0] dB");
    NS_ABORT_MSG_IF(pathStep <= 0.0 || pathDuration <= 0.0, "path step and duration must be > 0");
    // The lag features are spaced by one Logic Module query, so changing the
    // query interval rescales the model's history window without any error.
    NS_ABORT_MSG_IF(lmMode == "onnx" && std::abs(lmQueryIntervalSec - 0.1) > 1e-9,
                    "The ONNX model's lag features are spaced by --lm-query-interval, and it "
                    "was trained at 0.1 s. Re-train with vienna_ho_rf_lag.py on a dataset "
                    "sampled at the new interval before changing this.");

    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(rngRun);

    const double simTimeSec = pathDuration + g_tOffset + 2.0;
    Time lmQueryInterval = Seconds(lmQueryIntervalSec);
    std::string lmDelayRv = "ns3::ConstantRandomVariable[Constant=" +
                            std::to_string(lmProcessingDelayMs / 1000.0) + "]";
    std::string e2SendRv =
        "ns3::ConstantRandomVariable[Constant=" + std::to_string(e2ReportIntervalSec) + "]";

    Config::SetDefault("ns3::NrGnbPhy::TxPower", DoubleValue(gnbTxPower));
    Config::SetDefault("ns3::NrUePhy::TxPower", DoubleValue(ueTxPower));
    Config::SetDefault("ns3::NrUePhy::EnableUplinkPowerControl", BooleanValue(false));
    Config::SetDefault("ns3::NrUePhy::UeMeasurementsFilterPeriod", TimeValue(MilliSeconds(50)));
    Config::SetDefault("ns3::OranReportTriggerPeriodic::IntervalRv", StringValue(e2SendRv));

    Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();
    Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
    nrHelper->SetEpcHelper(epcHelper);
    nrHelper->SetAttribute("UseIdealRrc", BooleanValue(true));
    // Only "ran" lets the gNB decide; the other two leave the RAN algorithm
    // disabled so that the RIC is the sole decision maker.
    if (lmMode == "ran")
    {
        nrHelper->SetHandoverAlgorithmType("ns3::NrA3RsrpHandoverAlgorithm");
        nrHelper->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(hysteresis));
        nrHelper->SetHandoverAlgorithmAttribute("TimeToTrigger", TimeValue(timeToTrigger));
    }
    else
    {
        nrHelper->SetHandoverAlgorithmType("ns3::NrNoOpHandoverAlgorithm");
    }

    NodeContainer gnbNodes;
    gnbNodes.Create(2);
    NodeContainer ueNodes;
    ueNodes.Create(1);

    Ptr<ListPositionAllocator> gnbPosAlloc = CreateObject<ListPositionAllocator>();
    gnbPosAlloc->Add(SITES[0]);
    gnbPosAlloc->Add(SITES[1]);
    MobilityHelper gnbMobility;
    gnbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    gnbMobility.SetPositionAllocator(gnbPosAlloc);
    gnbMobility.Install(gnbNodes);

    // --- Generate the boundary-weaving trajectory ---
    struct WP
    {
        double t; ///< Time in seconds, before the offset
        double x; ///< ENU east coordinate, metres
        double y; ///< ENU north coordinate, metres
    };

    std::vector<WP> waypoints;
    for (double t = 0.0; t <= pathDuration + 1e-9; t += pathStep)
    {
        const double y = pathY0 + pathVy * t;
        const double x = BoundaryX(y) + pathAmplitude * std::sin(2.0 * M_PI * t / pathPeriod);
        waypoints.push_back({t, x, y});
    }
    NS_ABORT_MSG_IF(waypoints.size() < 2, "trajectory needs at least two waypoints");

    Ptr<ListPositionAllocator> uePosAlloc = CreateObject<ListPositionAllocator>();
    uePosAlloc->Add(Vector(waypoints[0].x, waypoints[0].y, UE_HEIGHT));
    MobilityHelper ueMobility;
    ueMobility.SetMobilityModel("ns3::WaypointMobilityModel",
                                "InitialPositionIsWaypoint",
                                BooleanValue(true));
    ueMobility.SetPositionAllocator(uePosAlloc);
    ueMobility.Install(ueNodes);
    Ptr<WaypointMobilityModel> waypointMob = ueNodes.Get(0)->GetObject<WaypointMobilityModel>();
    for (const auto& wp : waypoints)
    {
        waypointMob->AddWaypoint(
            Waypoint(Seconds(wp.t + g_tOffset), Vector(wp.x, wp.y, UE_HEIGHT)));
    }

    std::vector<Ptr<MobilityModel>> gnbMobs = {gnbNodes.Get(0)->GetObject<MobilityModel>(),
                                               gnbNodes.Get(1)->GetObject<MobilityModel>()};
    for (uint32_t i = 0; i < 2; i++)
    {
        ViennaFittedPropagationLossModel::RegisterCell(gnbMobs[i],
                                                       i,
                                                       shadowingSigma,
                                                       shadowingDecorr);
    }

    // --- Antennas: 1x1 isotropic. The fit is antenna-inclusive.
    nrHelper->SetGnbAntennaAttribute("AntennaElement",
                                     PointerValue(CreateObject<IsotropicAntennaModel>()));
    nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(1));
    nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(1));
    nrHelper->SetGnbAntennaAttribute("IsDualPolarized", BooleanValue(false));
    nrHelper->SetUeAntennaAttribute("AntennaElement",
                                    PointerValue(CreateObject<IsotropicAntennaModel>()));
    nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(1));
    nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(1));

    Ptr<NrChannelHelper> channelHelper = CreateObject<NrChannelHelper>();
    channelHelper->ConfigurePropagationFactory(ViennaFittedPropagationLossModel::GetTypeId());

    CcBwpCreator ccBwpCreator;
    CcBwpCreator::SimpleOperationBandConf bandConf(3.54e9,
                                                   bandwidthHz,
                                                   static_cast<uint8_t>(numerology));
    OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    channelHelper->AssignChannelsToBands({band});
    BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({band});

    NetDeviceContainer gnbDevs = nrHelper->InstallGnbDevice(gnbNodes, allBwps);
    NetDeviceContainer ueDevs = nrHelper->InstallUeDevice(ueNodes, allBwps);

    for (uint32_t i = 0; i < gnbDevs.GetN(); i++)
    {
        Ptr<NrGnbNetDevice> gnbDev = gnbDevs.Get(i)->GetObject<NrGnbNetDevice>();
        g_cellIdToPci[gnbDev->GetCellId()] = PCIS[i];
        NrHelper::GetGnbPhy(gnbDevs.Get(i), 0)
            ->SetAttribute("Pattern", StringValue("DL|DL|DL|S|UL|"));
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
    const double d0 = ViennaFittedPropagationLossModel::MeanPathlossDb(0,
                                                                      waypoints[0].x,
                                                                      waypoints[0].y) -
                      ViennaFittedPropagationLossModel::MeanPathlossDb(1,
                                                                       waypoints[0].x,
                                                                       waypoints[0].y);
    const uint32_t initialCell = (d0 > 0.0) ? 1 : 0;
    nrHelper->AttachToGnb(ueDevs.Get(0), gnbDevs.Get(initialCell));

    uint16_t dlPort = 10000;
    UdpClientHelper dlClient(ueIpIfaces.GetAddress(0), dlPort);
    dlClient.SetAttribute("Interval", TimeValue(Seconds(dlIntervalMs / 1000.0)));
    dlClient.SetAttribute("MaxPackets", UintegerValue(1000000));
    dlClient.SetAttribute("PacketSize", UintegerValue(1400));
    ApplicationContainer clientApps = dlClient.Install(remoteHost);
    clientApps.Start(Seconds(1.0));

    PacketSinkHelper dlSink("ns3::UdpSocketFactory",
                            InetSocketAddress(Ipv4Address::GetAny(), dlPort));
    ApplicationContainer serverApps = dlSink.Install(ueNodes.Get(0));
    serverApps.Start(Seconds(0.5));

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor;
    if (useFlowMonitor)
    {
        NodeContainer endpoints;
        endpoints.Add(remoteHost);
        endpoints.Add(ueNodes);
        flowMonitor = flowHelper.Install(endpoints);
    }

    // =======================================================================
    // O-RAN setup
    // =======================================================================
    if (!dbFileName.empty())
    {
        std::remove(dbFileName.c_str());
    }

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

    if (lmMode == "onnx")
    {
        oranHelper->SetDefaultLogicModule("ns3::OranLmNr2NrRsrpLagOnnxHandover",
                                          "OnnxModelPath",
                                          StringValue(onnxModelPath),
                                          "NumLags",
                                          UintegerValue(numLags),
                                          "DecisionThreshold",
                                          DoubleValue(decisionThreshold),
                                          "ProcessingDelayRv",
                                          StringValue(lmDelayRv));
    }
    else if (lmMode == "a3")
    {
        oranHelper->SetDefaultLogicModule("ns3::OranLmNr2NrA3RsrpHandover",
                                          "MaxReportAge",
                                          TimeValue(maxReportAge),
                                          "ProcessingDelayRv",
                                          StringValue(lmDelayRv));
    }
    else
    {
        // "ran": the RIC still registers the nodes and stores every Report, so
        // the database is directly comparable, but it issues no Commands.
        oranHelper->SetDefaultLogicModule("ns3::OranLmNoop");
    }
    oranHelper->SetConflictMitigationModule("ns3::OranCmmHandover");

    Ptr<OranNearRtRic> nearRtRic = oranHelper->CreateNearRtRic();

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

        // The ONNX Logic Module's only input comes from this Reporter.
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

    // Event A3 reporting configuration. Only the A3 Logic Module consumes it,
    // but it is installed in both modes so the two are otherwise identical.
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

    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverStart",
                    MakeCallback(&NotifyHandoverStart));
    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverEndOk",
                    MakeCallback(&NotifyHandoverEndOk));
    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverEndError",
                    MakeCallback(&NotifyHandoverEndError));

    std::cout << "Decision maker: " << lmMode;
    if (lmMode == "onnx")
    {
        std::cout << "  [RIC, ONNX model] model=" << onnxModelPath << "  numLags=" << numLags
                  << "  threshold=" << decisionThreshold;
    }
    else if (lmMode == "a3")
    {
        std::cout << "  [RIC, stock rule, no model] hysteresis=" << hysteresis << " dB  ttt="
                  << timeToTrigger.GetMilliSeconds() << " ms";
    }
    else
    {
        std::cout << "  [gNB RAN, no RIC decision] hysteresis=" << hysteresis << " dB  ttt="
                  << timeToTrigger.GetMilliSeconds() << " ms";
    }
    std::cout << std::endl;
    std::cout << "Trajectory: " << waypoints.size() << " waypoints over " << pathDuration
              << " s, initial cell PCI " << PCIS[initialCell] << "; shadowing sigma "
              << shadowingSigma << " dB; RngRun " << rngRun << std::endl;

    Simulator::Stop(Seconds(simTimeSec));
    Simulator::Run();

    uint32_t reversals = 0;
    for (size_t i = 1; i < g_handoverTimes.size(); i++)
    {
        if (g_handoverTimes[i] - g_handoverTimes[i - 1] <= 12.0)
        {
            reversals++;
        }
    }

    std::cout << "\n=== Simulation complete ===" << std::endl;
    std::cout << "Handovers started:   " << g_handoverStartCount << std::endl;
    std::cout << "Handovers completed: " << g_handoverEndOkCount << std::endl;
    std::cout << "Handovers failed:    " << g_handoverEndErrorCount << std::endl;
    std::cout << "Reversals <=12 s:    " << reversals << std::endl;
    std::cout << "SQLite DB:           " << dbFileName << std::endl;
    std::cout << "SWEEP lm=" << lmMode << " threshold=" << decisionThreshold
              << " sigma=" << shadowingSigma << " rngRun=" << rngRun
              << " handovers=" << g_handoverEndOkCount << " reversals=" << reversals << std::endl;

    if (flowMonitor)
    {
        PrintFlowStats(flowMonitor, flowHelper, lmMode);
    }

    Simulator::Destroy();
    return 0;
}
