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
 * Vienna handover ping-pong generator, on a pathloss model FITTED to the drive
 * test: PCI 331 and PCI 286.
 *
 * This is the dataset-generation companion to vienna-ho-replay-trace.cc. The
 * two answer different questions and neither replaces the other:
 *
 *   vienna-ho-replay-trace.cc   replays the measured pathloss TIME SERIES of
 *                               the 03:35:14.766 handover. Faithful to that one
 *                               event, but the UE position does not enter the
 *                               channel at all, so the trajectory cannot be
 *                               changed and the run yields 2 handovers.
 *
 *   this file                   uses a pathloss model fitted to all 4155
 *                               measured samples as a function of DISTANCE and
 *                               OFF-BORESIGHT ANGLE. Position drives RSRP
 *                               again, so trajectories are designable, and the
 *                               default one is engineered to cross the cell
 *                               boundary repeatedly: 8 handovers in 80 s, 7 of
 *                               them reversals within 12 s.
 *
 * === THE FITTED MODEL ===
 *
 * Least squares over every sample in contrib/nr/examples/vienna-pathloss-331-286.csv,
 * in the form PL = A + 10 n log10(d3D) + k A(phi) with the 3GPP horizontal
 * sector term A(phi) = min(12 (phi/65)^2, 30):
 *
 *   PCI 331   A = 80.5   n = 1.44   k = 0.28   sigma =  8.0 dB   (2280 samples)
 *   PCI 286   A = 36.7   n = 2.83   k = 0.51   sigma = 10.5 dB   (1875 samples)
 *
 * The exponents differ sharply because the two cells were driven on different
 * routes relative to their boresights; the companion .md discusses this at
 * length in section 4.2 and should be read before changing the coefficients.
 * Both fits are ANTENNA-INCLUSIVE, because the measured pathloss_db they are
 * fitted to is ss-PBCH-BlockPower - SS-RSRP, taken after the gNB beam gain and
 * the UE antenna gain. The antennas here are therefore 1x1 isotropic and no
 * spectrum model is installed, exactly as in the trace example and for the same
 * reason: installing one would count the antenna twice. That is measured, not
 * assumed -- with an 8x8 array and the spectrum model the 286-vs-331
 * differential moves by 31.8 dB and every handover disappears.
 *
 * === THE TRAJECTORY ===
 *
 * Generated procedurally rather than tabulated, so it stays consistent with the
 * fitted coefficients if those ever change. At each step the boundary where the
 * two cells are equal is found by bisection along x, and the UE is placed a
 * sinusoidal sway either side of it:
 *
 *     y(t) = y0 + vy t
 *     x(t) = x_boundary(y(t)) + amplitude sin(2 pi t / period)
 *
 * The defaults put the UE near y = -500, where the differential gradient is
 * steepest at about 0.85 dB per 10 m, and sway 85 m either side with a 20 s
 * period. That yields a crossing about every 10 s.
 *
 * THIS IS A SYNTHETIC ROUTE. It weaves across the cell boundary because that is
 * what exercises the handover logic; it is not a route anyone drove. Its speed
 * also runs from 4 to 33 m/s, peaking well above the roughly 6 m/s of the real
 * drive, because the boundary gradient sets a floor on how quickly a crossing
 * can be reached at a plausible speed. Reduce --path-amplitude or raise
 * --path-period to slow it down, at the cost of fewer handovers.
 *
 * === WHO DECIDES THE HANDOVER ===
 *
 * Selected with --handover, so the same channel and the same trajectory can be
 * driven by either decision maker:
 *
 *   ric   (default)  The RAN's own handover algorithm is NrNoOpHandoverAlgorithm,
 *                    so the gNB never acts on a Measurement Report by itself. The
 *                    reports go out over E2 and the Near-RT RIC's
 *                    OranLmNr2NrA3RsrpHandover issues every handover Command.
 *
 *   ran              NrA3RsrpHandoverAlgorithm runs inside the gNB RRC and
 *                    triggers the handover directly. The RIC still registers the
 *                    nodes and stores every Report in the database, but its
 *                    Logic Module is OranLmNoop and issues nothing.
 *
 *   both             Both are live. This is a conflict test, not a comparison.
 *                    The RAN wins every race, because it acts on the report at
 *                    once while the RIC is still waiting for its next E2 send;
 *                    by the time the Logic Module runs, the UE has already been
 *                    handed over and the LM's own check that the report still
 *                    describes the serving cell rejects it. Measured, "both"
 *                    reproduces "ran" exactly, with no duplicate and no failed
 *                    handover, which is the evidence that the check works.
 *
 * THE A3 CONDITION IS THE SAME IN EVERY MODE, and in none of them is it the RIC
 * that evaluates it. The entry inequality, its hysteresis or offset, and the
 * time-to-trigger are all applied by the UE RRC before a Measurement Report is
 * ever sent; --hysteresis and --timeToTrigger feed both the reporting
 * configuration and NrA3RsrpHandoverAlgorithm, so the two modes see the same
 * events at the same instants. What differs is everything after the report:
 *
 *   ran   report -> gNB RRC -> handover, with no delay unless
 *         --ran-decision-delay / --ran-triggering-delay are set (TR 36.839 uses
 *         50 ms and 40 ms).
 *
 *   ric   report -> E2, which does not send on arrival but on the gNB
 *         terminator's --e2-report-interval grid -> 1 ms transmission ->
 *         repository -> LM query, itself on a --lm-query-interval grid rather
 *         than on arrival -> LM processing (--lm-processing-delay) -> conflict
 *         mitigation -> 1 ms back to the serving gNB -> handover.
 *
 * MEASURED, at the defaults and with no shadowing, the two modes produce the
 * SAME 8 handovers with the same 7 reversals, in the same order, and the RIC
 * takes each one 155 to 305 ms later than the RAN:
 *
 *     HO      1      2      3      4      5      6      7      8
 *     ran   5.062 15.962 25.312 35.712 45.412 55.862 66.712 77.162
 *     ric   5.217 16.217 25.617 36.017 45.617 56.017 67.017 77.417
 *     lag    155    255    305    305    205    155    305    255   ms
 *
 * That spread is the two grids beating against each other. The whole penalty
 * decomposes, and the decomposition reproduces every measurement above to the
 * millisecond:
 *
 *     lag = W_e2 + W_lm + D_lm + 2 D_tx
 *
 *     W_e2   wait for the gNB terminator's next E2 send, in [0, 200) ms
 *     W_lm   wait for the RIC's next Logic Module query, in [0, 100) ms
 *     D_lm   --lm-processing-delay, 10 ms, which enters 1:1
 *     D_tx   1 ms of E2 transmission each way, the only irreducible part
 *
 * Two things follow that are worth knowing before tuning any of it.
 *
 * FIRST, the E2 reporting interval dominates, not the query loop: it is twice
 * the period and it is the term that varies. Cutting --e2-report-interval to
 * 10 ms drops the lag to about 105 ms; cutting --lm-query-interval to 10 ms
 * instead only reaches 215 ms.
 *
 * SECOND, the query loop here always costs a FULL period rather than half of
 * one, because 200 ms is an exact multiple of 100 ms. The E2 sends land exactly
 * on query instants, so a report arrives 1 ms after a query has just fired and
 * waits 99 ms for the next, every time, never less. Measured: W_lm is pinned at
 * 99 ms with a 100 ms query interval and at 49 ms with a 50 ms one, whereas a
 * period that does not divide 200 -- 30, 70 or 110 ms -- unlocks it and the
 * wait drops to 19 to 69 ms and varies per handover. An interval that divides
 * the E2 period is the worst choice available, which is not obvious from the
 * attribute.
 *
 * Sweeping --timeToTrigger moves the A3 instant 1:1 and so walks the handover
 * across every phase of the E2 grid, tracing the sawtooth directly: over one
 * full 200 ms tooth the lag runs from 111 ms to 310 ms with a mean of 212 ms,
 * against the 211 ms the formula predicts.
 *
 * NONE OF IT SHOWS UP AT THE FLOW LEVEL. FlowMonitor, on the downlink flow, is
 * effectively identical between the two modes. Each handover costs about 12 ms
 * of interruption, and that figure is a property of the X2 procedure, not of
 * whoever asked for it, so deciding 212 ms later moves the outage without
 * changing it. At --dl-interval=1 ms over the 8 handovers:
 *
 *                    ran        ric
 *     Tx packets    84000      84000
 *     lost             99         94
 *     throughput   11.4108    11.4115  Mb/s
 *     mean delay   13.5028    13.5067  ms
 *
 * The loss difference is a handful of packets in 84000, and its SIGN FLIPS at
 * --dl-interval=0.5 ms (198 for ran against 209 for ric), so it is where in the
 * sway the handover happens to land and not a penalty for being late. The only
 * consistent effect is on mean delay and jitter, where the RIC is higher in
 * every run measured, by a few microseconds on a 13.3 ms mean: the UE holds the
 * weakening cell a little longer. Raise --dl-interval resolution before reading
 * anything into a loss count; at the 10 ms default an interruption costs about
 * one packet and the two modes come out bit-identical.
 *
 * So on this trajectory the RIC's latency costs timing, not decisions: a
 * quarter of a second at 4 to 33 m/s is 1 to 8 m of travel, and the boundary
 * gradient here is gentle enough that the same crossing is still the right
 * answer. Trajectories that cross faster, or a --hysteresis small enough that
 * the A3 condition flickers, are where the count itself should start to differ.
 *
 * === WHAT THIS IS FOR, AND WHAT IT IS NOT FOR ===
 *
 * FOR: generating a labelled handover dataset. The scenario produces many
 * decision points per run, --rng-run varies the shadowing realisation so runs
 * are independent samples rather than repeats, and the whole geometry can be
 * changed to widen coverage.
 *
 * NOT FOR: claiming anything about the 2025-02-08 event. The fit is a smooth
 * regression with 8 to 10 dB of residual scatter, so it reproduces the
 * statistics of these two cells over their whole measured coverage and not the
 * particular sequence of any one pass. Use vienna-ho-replay-trace.cc for that.
 *
 * Shadowing is off by default so the base scenario is deterministic and its 8
 * handovers reproduce exactly. Set --shadowing-sigma to the fitted residual
 * (8.0 and 10.5 dB) to reinstate the scatter the fit removed, which is what
 * makes a sweep over --rng-run produce genuinely different runs.
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

NS_LOG_COMPONENT_DEFINE("ViennaHoFittedPingPong");

// ===========================================================================
// Scenario constants
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

/// Fitted residual standard deviation per cell, in dB.
static const double FIT_SIGMA[2] = {8.0, 10.5};

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
 * shadowing.
 *
 * Cells are identified by the identity of their MobilityModel rather than by
 * position, so that co-sited sectors could be distinguished if any were added.
 * An unregistered transmitter is treated as fully blocked.
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
     * Shared by every instance, since the channel helper may build one model
     * per bandwidth part.
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
     * excluding shadowing. Used by the trajectory generator and the trace
     * printer, neither of which should perturb the shadowing state.
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
        uint32_t idx;             ///< Index into the scenario constant arrays
        double sigma;             ///< Shadowing standard deviation in dB
        double decorr;            ///< Shadowing decorrelation distance in metres
        mutable double shadow{0}; ///< Current shadowing value in dB
        mutable bool primed{false};   ///< Whether shadow has been drawn at least once
        mutable Vector lastPos;       ///< UE position at the last shadowing update
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

    // Bearing of the UE from the site, degrees clockwise from North, then the
    // angle off the sector boresight.
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
    // Whichever end is a registered gNB carries the fit; the other is the UE.
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
        // Gauss-Markov shadowing, correlated over decorr metres of UE movement.
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
        // d = PL(331) - PL(286); negative means PCI 331 is stronger.
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
 * Print a handover that the UE started.
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
 * The interesting column here is loss: every handover interrupts the downlink
 * briefly, so the loss count is essentially a packet-level measure of how much
 * the handovers cost, and comparing --handover=ric against --handover=ran shows
 * whether deciding later costs anything beyond the delay itself.
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

/**
 * Print the fitted pathloss and implied RSRP of both cells at the UE's current
 * position, so the decision the Logic Module faced can be read directly.
 *
 * @param ueMob The MobilityModel of the UE.
 * @param interval The interval between prints.
 */
void
PrintTrace(Ptr<MobilityModel> ueMob, Time interval)
{
    const Vector p = ueMob->GetPosition();
    const double p1 = ViennaFittedPropagationLossModel::MeanPathlossDb(0, p.x, p.y);
    const double p2 = ViennaFittedPropagationLossModel::MeanPathlossDb(1, p.x, p.y);
    std::cout << "  t=" << Simulator::Now().GetSeconds() << "  (" << p.x << ", " << p.y
              << ")  PL331=" << p1 << "  PL286=" << p2 << "  d(286-331)=" << p1 - p2 << " dB"
              << std::endl;
    Simulator::Schedule(interval, &PrintTrace, ueMob, interval);
}

int
main(int argc, char* argv[])
{
    std::string dbFileName = "vienna-ho-fitted.db";
    std::string handoverMode = "ric";
    Time ranDecisionDelay = MilliSeconds(0);
    Time ranTriggeringDelay = MilliSeconds(0);
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
    // Trajectory: sway either side of the equal-RSRP boundary while advancing
    // north. See the header for why these defaults.
    double pathY0 = -500.0;
    double pathVy = 4.0;
    double pathAmplitude = 85.0;
    double pathPeriod = 20.0;
    double pathDuration = 80.0;
    double pathStep = 0.5;
    // Shadowing, off by default so the base scenario is deterministic.
    double shadowingSigma = 0.0;
    double shadowingDecorr = 50.0;
    uint32_t rngRun = 1;
    bool verbose = false;
    bool printTrace = false;
    bool useFlowMonitor = true;
    double dlIntervalMs = 10.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("verbose", "Print SQL query results", verbose);
    cmd.AddValue("printTrace", "Print the fitted pathloss of both cells once per second",
                 printTrace);
    cmd.AddValue("db-file", "SQLite database file", dbFileName);
    cmd.AddValue("flow-monitor",
                 "Install FlowMonitor on the two endpoints of the downlink flow and print "
                 "end-to-end loss, throughput, delay and jitter at the end of the run",
                 useFlowMonitor);
    cmd.AddValue("dl-interval",
                 "Downlink packet interval (ms). This sets the resolution of the FlowMonitor "
                 "loss count: an interruption shorter than one interval may cost no packet at "
                 "all, so the 10 ms default barely resolves a handover. Use 1 ms or less to "
                 "measure interruption time",
                 dlIntervalMs);
    cmd.AddValue("handover",
                 "Who decides the handover: \"ric\" for the Near-RT RIC Logic Module over E2 "
                 "with the RAN algorithm disabled, \"ran\" for NrA3RsrpHandoverAlgorithm in "
                 "the gNB RRC with the RIC reporting but not acting, or \"both\" to run them "
                 "against each other. The A3 condition itself is evaluated by the UE RRC in "
                 "every mode; see the header",
                 handoverMode);
    cmd.AddValue("ran-decision-delay",
                 "NrGnbRrc::HandoverDecisionDelay, applied between the Measurement Report "
                 "arriving and the RAN algorithm seeing it. Only used when --handover "
                 "includes the RAN algorithm. TR 36.839 uses 50 ms; 0 by default",
                 ranDecisionDelay);
    cmd.AddValue("ran-triggering-delay",
                 "NrGnbRrc::HandoverTriggeringDelay, applied between the RAN algorithm "
                 "deciding and the handover procedure starting. TR 36.839 uses 40 ms; 0 by "
                 "default. Together with --ran-decision-delay this is what makes a RAN-vs-RIC "
                 "latency comparison fair",
                 ranTriggeringDelay);
    cmd.AddValue("gnb-tx-power", "gNB Tx power (dBm) [3GPP default]", gnbTxPower);
    cmd.AddValue("ue-tx-power", "UE Tx power (dBm) [3GPP default]", ueTxPower);
    cmd.AddValue("bandwidth", "Channel bandwidth (Hz) [assumed]", bandwidthHz);
    cmd.AddValue("numerology", "NR numerology 0-4 [assumed mu=1]", numerology);
    cmd.AddValue("hysteresis", "Event A3 handover margin in dB", hysteresis);
    cmd.AddValue("timeToTrigger", "Event A3 time-to-trigger", timeToTrigger);
    cmd.AddValue("maxReportAge", "Measurement Reports older than this are ignored", maxReportAge);
    cmd.AddValue("lm-processing-delay", "LM processing delay (ms)", lmProcessingDelayMs);
    cmd.AddValue("lm-query-interval", "RIC LM query interval (s)", lmQueryIntervalSec);
    cmd.AddValue("e2-report-interval", "E2 report interval (s)", e2ReportIntervalSec);
    cmd.AddValue("path-y0", "Trajectory start, ENU north coordinate (m)", pathY0);
    cmd.AddValue("path-vy", "Trajectory northward rate (m/s)", pathVy);
    cmd.AddValue("path-amplitude",
                 "Sway either side of the equal-RSRP boundary (m). Larger gives deeper "
                 "excursions and more certain handovers, at a higher peak speed",
                 pathAmplitude);
    cmd.AddValue("path-period",
                 "Sway period (s). One handover pair per period, so shorter gives more "
                 "ping-pong and a higher peak speed",
                 pathPeriod);
    cmd.AddValue("path-duration", "Trajectory duration (s)", pathDuration);
    cmd.AddValue("path-step", "Waypoint spacing along the trajectory (s)", pathStep);
    cmd.AddValue("shadowing-sigma",
                 "Shadowing standard deviation in dB, applied to both cells. 0 disables it "
                 "and makes the run deterministic. The fitted residuals are 8.0 dB for "
                 "PCI 331 and 10.5 dB for PCI 286",
                 shadowingSigma);
    cmd.AddValue("shadowing-decorr", "Shadowing decorrelation distance (m)", shadowingDecorr);
    cmd.AddValue("rng-run",
                 "RNG run number. Only changes the outcome when --shadowing-sigma is "
                 "non-zero, since the fitted model is otherwise deterministic",
                 rngRun);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(hysteresis < -15.0 || hysteresis > 15.0,
                    "hysteresis must be in the range [-15.0..15.0] dB");
    NS_ABORT_MSG_IF(pathStep <= 0.0 || pathDuration <= 0.0, "path step and duration must be > 0");
    NS_ABORT_MSG_IF(handoverMode != "ric" && handoverMode != "ran" && handoverMode != "both",
                    "handover must be \"ric\", \"ran\" or \"both\"");

    // "ric" runs the Logic Module and disables the RAN algorithm, "ran" the
    // reverse, "both" runs the two against each other.
    const bool ranHandover = (handoverMode == "ran" || handoverMode == "both");
    const bool ricHandover = (handoverMode == "ric" || handoverMode == "both");

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

    // The RAN algorithm is given the same A3 parameters as the reporting
    // configuration below, so both decision makers act on the same events.
    if (ranHandover)
    {
        nrHelper->SetHandoverAlgorithmType("ns3::NrA3RsrpHandoverAlgorithm");
        nrHelper->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(hysteresis));
        nrHelper->SetHandoverAlgorithmAttribute("TimeToTrigger", TimeValue(timeToTrigger));
        Config::SetDefault("ns3::NrGnbRrc::HandoverDecisionDelay", TimeValue(ranDecisionDelay));
        Config::SetDefault("ns3::NrGnbRrc::HandoverTriggeringDelay", TimeValue(ranTriggeringDelay));
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

    double vMax = 0.0;
    for (size_t i = 1; i < waypoints.size(); i++)
    {
        const double d = std::hypot(waypoints[i].x - waypoints[i - 1].x,
                                    waypoints[i].y - waypoints[i - 1].y);
        vMax = std::max(vMax, d / pathStep);
    }

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

    // --- Register the fitted model against each gNB ---
    std::vector<Ptr<MobilityModel>> gnbMobs = {gnbNodes.Get(0)->GetObject<MobilityModel>(),
                                               gnbNodes.Get(1)->GetObject<MobilityModel>()};
    for (uint32_t i = 0; i < 2; i++)
    {
        ViennaFittedPropagationLossModel::RegisterCell(gnbMobs[i],
                                                       i,
                                                       shadowingSigma,
                                                       shadowingDecorr);
    }

    // --- Antennas: 1x1 isotropic. The fit is antenna-inclusive; see the header.
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
    channelHelper->ConfigurePropagationFactory(
        ViennaFittedPropagationLossModel::GetTypeId());

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
    // Attach to whichever cell is stronger at the start, so the run does not
    // open with a handover forced by an arbitrary initial attachment.
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

    // Installed on the two endpoints only, so the statistics are end to end
    // rather than per hop.
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
    // O-RAN setup, identical to vienna-ho-replay-trace.cc
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
    if (ricHandover)
    {
        oranHelper->SetDefaultLogicModule("ns3::OranLmNr2NrA3RsrpHandover",
                                          "MaxReportAge",
                                          TimeValue(maxReportAge),
                                          "ProcessingDelayRv",
                                          StringValue(lmDelayRv));
    }
    else
    {
        // A default Logic Module is mandatory, so the RIC gets the no-op one. It
        // still registers the nodes and stores every Report, which is the point:
        // the dataset is identical, only the decisions are the RAN's.
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

    // This reporting configuration feeds the OranReporterNrGnbMeasReport, and is
    // separate from the one NrA3RsrpHandoverAlgorithm registers for itself when
    // --handover includes the RAN algorithm. The two get different measIds and
    // neither acts on the other's: the algorithm ignores measIds outside its own
    // list, and the Reporter drops measIds it was not given. Same parameters, so
    // they fire together.
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

    if (printTrace)
    {
        Simulator::Schedule(Seconds(g_tOffset),
                            &PrintTrace,
                            ueNodes.Get(0)->GetObject<MobilityModel>(),
                            Seconds(1.0));
    }

    std::cout << "Trajectory: " << waypoints.size() << " waypoints over " << pathDuration
              << " s, peak speed " << vMax << " m/s, initial cell PCI " << PCIS[initialCell]
              << std::endl;
    std::cout << "Event A3 hysteresis " << hysteresis << " dB, time-to-trigger "
              << timeToTrigger.GetMilliSeconds() << " ms; shadowing sigma " << shadowingSigma
              << " dB; RngRun " << rngRun << std::endl;
    std::cout << "Handover decided by: " << handoverMode << std::endl;
    if (ranHandover)
    {
        std::cout << "  RAN NrA3RsrpHandoverAlgorithm, decision delay "
                  << ranDecisionDelay.GetMilliSeconds() << " ms, triggering delay "
                  << ranTriggeringDelay.GetMilliSeconds() << " ms" << std::endl;
    }
    if (ricHandover)
    {
        std::cout << "  RIC OranLmNr2NrA3RsrpHandover, query interval "
                  << lmQueryInterval.GetMilliSeconds() << " ms, LM processing delay "
                  << lmProcessingDelayMs << " ms" << std::endl;
    }
    else
    {
        std::cout << "  RIC OranLmNoop: reporting into the database, issuing no Commands"
                  << std::endl;
    }

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
    std::cout << "SWEEP handover=" << handoverMode << " rngRun=" << rngRun
              << " sigma=" << shadowingSigma << " handovers=" << g_handoverEndOkCount
              << " reversals=" << reversals << std::endl;

    if (flowMonitor)
    {
        PrintFlowStats(flowMonitor, flowHelper, handoverMode);
    }

    Simulator::Destroy();
    return 0;
}
