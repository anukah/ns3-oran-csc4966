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
 * Vienna handover replay driven by MEASURED pathloss: PCI 331 -> PCI 286.
 *
 * Reproduces the inter-gNB handover of 2025-02-08 03:35:14.766 UTC by replaying
 * the pathloss the drive test actually measured, instead of predicting it from
 * geometry. The O-RAN Near-RT RIC runs OranLmNr2NrA3RsrpHandover, exactly as in
 * the geometric replays, so what is being tested is the handover logic and not
 * the channel model.
 *
 * === WHY THIS EXAMPLE EXISTS ===
 *
 * The geometric examples cannot produce this handover, and the reason is not a
 * tuning failure. Across the replay window the UE walks TOWARDS PCI 331 and
 * AWAY from PCI 286, so distance favours the serving cell by a margin that only
 * grows, and the UE sits 158 degrees off PCI 286's recorded boresight, where
 * the antenna model buries it. Measured in vienna-ho-replay-a3-no-ttt.cc, the
 * simulated RSRP of PCI 286 never comes within 23.3 dB of PCI 331 over 221
 * samples. No hysteresis, time-to-trigger, beamwidth or azimuth reaches across
 * that, and sweeping them was tried at length.
 *
 * The measurements say something quite different. Interpolating the measured
 * pathloss of the two cells over the same window:
 *
 *   t [s]   PL 331   PL 286   RSRP 331   RSRP 286   d(286-331)
 *    3.0     120.1    124.1     -101.8     -105.8       -4.0
 *    5.0     121.0    122.4     -102.7     -104.1       -1.4
 *    9.0     122.9    119.6     -104.6     -101.3       +3.3
 *   10.77    122.1    122.3     -103.8     -104.0       -0.2   <- dataset HO
 *   14.0     118.2    124.6      -99.9     -106.3       -6.5
 *   20.0     115.1    126.1      -96.8     -107.8      -10.9
 *
 * The two cells run within a few dB of one another from t=3 to t=12, PCI 286
 * goes 3.3 dB clear around t=9, and after t=12 PCI 331 pulls away for good.
 * That is a contested window followed by a clean resolution, which is the shape
 * that produces one handover and no ping-pong back. The geometric model puts
 * the same instants 34 dB apart and never crossing.
 *
 * === HOW THE MEASURED PATHLOSS IS APPLIED ===
 *
 * ViennaTracePropagationLossModel below replaces the TR 38.901 UMa model. It
 * holds one time series per cell and returns
 *
 *     rxPowerDbm = txPowerDbm - PL_measured(cell, now)
 *
 * with PL_measured linearly interpolated between samples and held flat outside
 * their range. Cells are identified by the identity of their MobilityModel, not
 * by position, which is what lets co-sited sectors be told apart at all.
 *
 * THE ANTENNAS ARE DELIBERATELY 1x1 ISOTROPIC. This is not a simplification, it
 * is required for correctness. The dataset's pathloss_db is
 * ss-PBCH-BlockPower - SS-RSRP, an ANTENNA-INCLUSIVE quantity: SS-RSRP is
 * measured at the UE connector after the gNB's SSB beam gain and the UE's own
 * antenna gain, so both are already folded into the number with the opposite
 * sign. An ns-3 propagation loss model, by contrast, returns isotropic
 * large-scale loss only, with array and element gains applied afterwards inside
 * ThreeGppSpectrumPropagationLossModel. Installing a UniformPlanarArray on top
 * of a measured antenna-inclusive pathloss would therefore count the antenna
 * twice, to the tune of the +15 to +24 dB an 8x8 dual-pol panel contributes.
 * Using isotropic 1x1 arrays makes the trace authoritative and the arithmetic
 * exact.
 *
 * A useful side effect is that every antenna question that blocked the
 * geometric examples -- array factor, element beamwidth, the front-to-back
 * ratio, and gNB 11's untrustworthy 155 degree azimuth -- stops applying. None
 * of them can be wrong here because none of them are used.
 *
 * MEASURED CONSEQUENCE OF GETTING THIS WRONG. In 5G-LENA the array contributes
 * nothing through the propagation loss model: the pathloss model returns
 * isotropic loss, and array gain, beamforming and fading are all applied later
 * by the PhasedArraySpectrumPropagationLossModel. ConfigurePropagationFactory
 * sets only the former, so with no spectrum model installed the array is inert
 * and --antenna-rows/--antenna-cols change nothing -- an 8x8 run reproduces the
 * 1x1 run bit for bit across all 1194 RSRP rows.
 *
 * Installing the spectrum model makes the array live, and then the double count
 * is real and large. Measured, with 8x8 dual-pol and the dataset azimuths:
 *
 *                        trace only    + spectrum, 8x8    array contributed
 *   PCI 331 mean RSRP     -108.9 dBm       -97.9 dBm            +11.0 dB
 *   PCI 286 mean RSRP     -115.0 dBm      -135.8 dBm            -20.8 dB
 *   mean d(286-331)         -6.1 dB        -37.9 dB             -31.8 dB
 *   handovers                    2               0
 *
 * PCI 331 is 30 degrees off its boresight and gains; PCI 286 is 158 degrees
 * off, behind the panel, and loses. The differential moves by 31.8 dB and the
 * handover disappears, which is the same wall the geometric examples hit. To
 * run a live array here the propagation model would have to return
 * PL_measured + G_array(cell, t) so that the spectrum model's gain cancels;
 * section 5 of contrib/nr/examples/vienna-pathloss-331-286.md describes that
 * reconciliation. Until that exists, leave --spectrum off.
 *
 * Small-scale fading is left off for the same reason: the measured series
 * already contains it. The samples swing 9 dB peak to peak within a couple of
 * seconds and have a standard deviation of 4.4 dB per cell, so the differential
 * carries a spread of roughly 6 dB without any model fading added. Compare the
 * geometric scenario, where ThreeGppChannelModel::UpdatePeriod defaults to zero
 * and freezes the fading realisation for the whole run, leaving a differential
 * standard deviation of only 1.5 to 4.6 dB.
 *
 * === WHO DECIDES THE HANDOVER ===
 *
 * Selected with --handover, so the same measured channel can be driven by
 * either decision maker:
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
 * events at the same instants. What differs is everything after the report: the
 * RAN acts on it at once, unless --ran-decision-delay / --ran-triggering-delay
 * are set (TR 36.839 uses 50 ms and 40 ms), whereas the RIC has to wait for the
 * gNB terminator's next --e2-report-interval send, then for the next
 * --lm-query-interval query, then for LM processing and conflict mitigation,
 * before the Command comes back over E2.
 *
 * MEASURED, at the defaults, both modes find the SAME two handovers, and the
 * RIC takes each one 305 ms later:
 *
 *              HO 331->286     HO 286->331
 *     ran        11.712 s        14.712 s
 *     ric        12.017 s        15.017 s
 *
 * The penalty decomposes, and the decomposition reproduces the measurement to
 * the millisecond:
 *
 *     lag = W_e2 + W_lm + D_lm + 2 D_tx
 *
 *     W_e2   wait for the gNB terminator's next E2 send, in [0, 200) ms
 *     W_lm   wait for the RIC's next Logic Module query, in [0, 100) ms
 *     D_lm   --lm-processing-delay, 10 ms, which enters 1:1
 *     D_tx   1 ms of E2 transmission each way, the only irreducible part
 *
 * The 305 ms above is 194 + 99 + 10 + 2. Note that the E2 reporting interval
 * dominates, not the query loop, and that the query loop costs a FULL period
 * rather than half of one: 200 ms is an exact multiple of 100 ms, so the E2
 * sends land on query instants and a report always arrives just after a query
 * has fired and waits 99 ms for the next. A query interval that does not divide
 * the E2 period unlocks that and averages much lower. Sweeping --timeToTrigger
 * walks the handover across the E2 grid and traces the sawtooth: 111 ms to
 * 310 ms, mean 212 ms. See vienna-ho-fitted-pingpong.cc for the full treatment.
 *
 * None of it shows up at the flow level. FlowMonitor on the downlink flow gives
 * the two modes identical loss at every resolution tried here -- 3 packets at
 * --dl-interval=10 ms, 34 at 1 ms, 84 at 0.5 ms, the same in both -- and
 * identical throughput, because the interruption belongs to the X2 procedure
 * rather than to whoever asked for it. Deciding 305 ms later moves the outage
 * without changing it.
 *
 * Both are early against the dataset's 03:35:14.766, which is t=13.766 s here.
 * That is a property of the replay and not of either decision maker: the
 * interpolated curves cross around t=9 (see the table above) and the phone did
 * not hand over until 10.77, so any A3 algorithm acting on this pathloss fires
 * before the real network did. The second handover back to PCI 331 is likewise
 * the interpolation resolving after t=12, not a ping-pong the drive test saw.
 *
 * === PROVENANCE OF THE PATHLOSS ===
 *
 * From contrib/nr/examples/vienna-pathloss-331-286.csv and the analysis in the
 * companion .md, which should be read before changing anything here. Two source
 * types appear in the table below:
 *
 *   ue_log    the phone's own pathloss_db, available only while the cell was
 *             serving. 32 samples for PCI 331, 4 for PCI 286.
 *   scanner   derived as P_ref - (max over SSB beams) RSRP with
 *             P_ref = 18.3 dBm, the network-signalled reference power verified
 *             on both cells independently. Available regardless of serving
 *             relation, which is the only reason PCI 286 has any coverage
 *             outside the 1.5 s it was serving.
 *
 * === WHAT THIS EXAMPLE DOES AND DOES NOT ESTABLISH ===
 *
 * It is a REPLAY. The channel is asserted from measurement rather than
 * predicted, so a handover occurring here validates the decision logic, the E2
 * reporting path and the RIC pipeline. It says nothing whatever about whether
 * any propagation model is right, and it cannot be used to argue that the
 * scenario would behave this way at a different place or time.
 *
 * Three limitations to keep in view:
 *
 * - PCI 286 has 9 samples across 32 s, of which only 4 are from the UE. Its
 *   curve between t=12.3 and t=17.0 and beyond t=20.8 is interpolation, not
 *   measurement.
 * - The two source types are not the same measurement. The scanner sample at
 *   t=7.89 puts PCI 331 at 104.9 dB where the UE rows on either side read
 *   116.7 and 121.6 dB, so mixing them introduces steps of over 10 dB.
 *
 *   --source=ue drops the scanner rows, but it does NOT give a cleaner version
 *   of this handover: it removes the handover altogether. A phone reports
 *   pathloss_db only for the cell it is currently serving on, so PCI 286's four
 *   UE rows all fall inside the 1.5 s it was already serving, at t=10.77 to
 *   12.27. Before t=10.77 there is no UE measurement of PCI 286 in existence,
 *   and the model flat-holds 122.3 dB backwards over the whole approach.
 *   Measured, PCI 286 is then a constant while PCI 331 wanders from 119.3 to
 *   130.6 dB, so d(286-331) stays within [-7.0, +0.56] dB across the entire
 *   contested window and never reaches the 3 dB margin. The run yields ONE
 *   handover, at t=29 rather than 10.77, and it fires only because PCI 331 has
 *   degraded to 129.4 dB by then, making the flat-held neighbour look better by
 *   4.3 dB. That is an artefact of the hold, not a measured crossing.
 *
 *   The general point: a pre-handover neighbour signal cannot be reconstructed
 *   from serving-cell-only measurements, which is exactly why the scanner rows
 *   exist. For this event --source=all is the only usable setting, scanner
 *   inconsistency and all. Use --source=ue to see how much of the result rests
 *   on the scanner, not as a cleaner alternative.
 * - Interpolating linearly between samples up to 7 s apart smooths away
 *   whatever happened in between.
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
#include "ns3/uniform-planar-array.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ViennaHoReplayTrace");

// ===========================================================================
// Measured pathloss tables
// ===========================================================================

/// One measured pathloss sample.
struct PlSample
{
    double t;    ///< Time in seconds relative to 03:35:04.000 UTC
    double plDb; ///< Measured pathloss in dB
    bool fromUe; ///< true for a phone pathloss_db row, false for a scanner row
};

/// Measured pathloss of PCI 331 over the replay window.
static const PlSample PL_331[] = {
    {0.14, 119.3, true},  {0.66, 119.8, true},  {1.16, 121.4, true},  {1.67, 120.3, true},
    {2.17, 120.1, true},  {3.92, 120.2, false}, {6.22, 122.0, true},  {6.73, 117.1, true},
    {7.24, 113.7, true},  {7.75, 116.7, true},  {7.89, 104.9, false}, {8.24, 121.6, true},
    {8.74, 122.7, true},  {9.24, 123.0, true},  {9.74, 121.6, true},  {10.26, 122.7, true},
    {17.02, 114.5, false}, {20.82, 115.3, false}, {22.93, 120.6, true}, {23.46, 119.6, true},
    {23.95, 121.7, true}, {24.46, 122.2, true}, {24.97, 123.3, true}, {25.46, 125.9, true},
    {25.97, 127.1, true}, {26.48, 127.0, true}, {26.97, 127.2, true}, {27.49, 127.0, true},
    {27.99, 127.6, true}, {28.50, 128.1, true}, {29.03, 129.5, true}, {29.54, 130.4, true},
    {29.78, 122.9, false}, {30.06, 130.6, true}, {30.56, 131.1, true}, {31.06, 129.4, true},
    {31.55, 130.3, true},
};

/// Measured pathloss of PCI 286 over the replay window.
static const PlSample PL_286[] = {
    {3.92, 124.1, false},  {7.89, 117.9, false},  {10.77, 122.3, true}, {11.28, 124.1, true},
    {11.78, 125.8, true},  {12.27, 125.1, true},  {17.02, 123.8, false}, {20.82, 126.7, false},
    {29.78, 140.9, false},
};

// ===========================================================================
// ViennaTracePropagationLossModel
// ===========================================================================

/**
 * @ingroup examples
 *
 * A propagation loss model that returns a measured pathloss time series rather
 * than a predicted one.
 *
 * Transmitters are identified by the identity of their MobilityModel, which is
 * registered by the scenario with RegisterCell(). Position is deliberately not
 * used: co-sited sectors share a position and could not otherwise be told
 * apart. An unregistered transmitter is treated as fully blocked, so a stray
 * link cannot silently pick up some other cell's curve.
 */
class ViennaTracePropagationLossModel : public PropagationLossModel
{
  public:
    /**
     * Gets the TypeId of the ViennaTracePropagationLossModel class.
     *
     * @return The TypeId.
     */
    static TypeId GetTypeId();

    ViennaTracePropagationLossModel() = default;

    /**
     * Associates a transmitter with a measured pathloss series. Shared by every
     * instance, since the channel helper may build one model per bandwidth part.
     *
     * @param mob The MobilityModel of the transmitting node.
     * @param samples The measured samples, ordered by time.
     * @param tOffset Simulation time at which the first dataset second occurs.
     */
    static void RegisterCell(Ptr<const MobilityModel> mob,
                             std::vector<PlSample> samples,
                             double tOffset);

    /**
     * Looks up the measured pathloss of a registered cell at a given time.
     *
     * @param mob The MobilityModel of the transmitting node.
     * @param t The simulation time in seconds.
     * @return The interpolated pathloss in dB, or a large value if unregistered.
     */
    static double PathlossAt(Ptr<const MobilityModel> mob, double t);

  private:
    double DoCalcRxPower(double txPowerDbm,
                         Ptr<MobilityModel> a,
                         Ptr<MobilityModel> b) const override;
    int64_t DoAssignStreams(int64_t stream) override;

    /// One registered cell: its samples and the offset applied to their times.
    struct Series
    {
        std::vector<PlSample> samples; ///< Measured samples, ordered by time
        double tOffset;                ///< Offset from dataset time to simulation time
    };

    /// Registered cells, keyed by the transmitter's MobilityModel.
    static std::map<Ptr<const MobilityModel>, Series> m_series;
};

std::map<Ptr<const MobilityModel>, ViennaTracePropagationLossModel::Series>
    ViennaTracePropagationLossModel::m_series;

NS_OBJECT_ENSURE_REGISTERED(ViennaTracePropagationLossModel);

TypeId
ViennaTracePropagationLossModel::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ViennaTracePropagationLossModel")
                            .SetParent<PropagationLossModel>()
                            .SetGroupName("Propagation")
                            .AddConstructor<ViennaTracePropagationLossModel>();
    return tid;
}

void
ViennaTracePropagationLossModel::RegisterCell(Ptr<const MobilityModel> mob,
                                              std::vector<PlSample> samples,
                                              double tOffset)
{
    NS_ABORT_MSG_IF(samples.empty(), "Cannot register a cell with no measured samples");
    std::sort(samples.begin(), samples.end(), [](const PlSample& l, const PlSample& r) {
        return l.t < r.t;
    });
    m_series[mob] = Series{std::move(samples), tOffset};
}

double
ViennaTracePropagationLossModel::PathlossAt(Ptr<const MobilityModel> mob, double t)
{
    auto it = m_series.find(mob);
    if (it == m_series.end())
    {
        // Not a registered cell. Return a loss large enough that the link is
        // dead, rather than silently borrowing another cell's measurements.
        return 200.0;
    }

    const auto& s = it->second.samples;
    const double td = t - it->second.tOffset; // back to dataset time

    if (td <= s.front().t)
    {
        return s.front().plDb;
    }
    if (td >= s.back().t)
    {
        return s.back().plDb;
    }

    for (size_t i = 0; i + 1 < s.size(); i++)
    {
        if (td >= s[i].t && td <= s[i + 1].t)
        {
            const double span = s[i + 1].t - s[i].t;
            const double f = (span > 0.0) ? (td - s[i].t) / span : 0.0;
            return s[i].plDb * (1.0 - f) + s[i + 1].plDb * f;
        }
    }
    return s.back().plDb;
}

double
ViennaTracePropagationLossModel::DoCalcRxPower(double txPowerDbm,
                                               Ptr<MobilityModel> a,
                                               Ptr<MobilityModel> b) const
{
    // The channel calls this in both directions. Whichever end is a registered
    // gNB is the one that carries the measured series.
    const double now = Simulator::Now().GetSeconds();
    auto ita = m_series.find(a);
    Ptr<const MobilityModel> gnb = (ita != m_series.end()) ? a : b;
    return txPowerDbm - PathlossAt(gnb, now);
}

int64_t
ViennaTracePropagationLossModel::DoAssignStreams(int64_t stream)
{
    // Deterministic: the model consumes no randomness.
    return 0;
}

// ===========================================================================
// Scenario
// ===========================================================================

/// Real PCIs, in the order the gNB nodes are created.
static const uint16_t PCIS[2] = {331, 286};

/// gNB positions in ENU metres, origin at the PCI 331 site.
static const Vector SITES[2] = {Vector(0.0, 0.0, 36.0), Vector(592.62, -431.55, 58.0)};

/// Sector azimuths, degrees clockwise from North, per estimated_cell_info.
static const double AZIMUTHS[2] = {145.0, 155.0};

/// Offset applied to every dataset time, to let the RIC come up first.
static double g_tOffset = 3.0;

/// cellId to PCI mapping, populated after InstallGnbDevice.
static std::map<uint16_t, uint16_t> g_cellIdToPci;

/// Number of handovers that started, completed and failed, for the summary.
static uint32_t g_handoverStartCount = 0;
static uint32_t g_handoverEndOkCount = 0;
static uint32_t g_handoverEndErrorCount = 0;

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
    std::cout << Simulator::Now().GetSeconds() << "s HO START: PCI " << g_pendingFromPci << " -> "
              << CellIdToPci(targetCellId) << std::endl;
}

/**
 * Print a handover that the UE completed.
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
    std::cout << t << "s HO COMPLETE: PCI " << g_pendingFromPci << " -> " << CellIdToPci(cellId)
              << "   (dataset handover at t=" << 10.766 + g_tOffset << "s)" << std::endl;
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
 * Print the measured pathloss and the implied RSRP of both cells, so the
 * decision the Logic Module faced can be read directly.
 *
 * @param gnbMobs The MobilityModels of the two gNBs.
 * @param interval The interval between prints.
 */
void
PrintTrace(std::vector<Ptr<MobilityModel>> gnbMobs, Time interval)
{
    const double t = Simulator::Now().GetSeconds();
    const double p1 = ViennaTracePropagationLossModel::PathlossAt(gnbMobs[0], t);
    const double p2 = ViennaTracePropagationLossModel::PathlossAt(gnbMobs[1], t);
    // P_ref = 18.3 dBm per SSB resource element, per the companion analysis.
    std::cout << "  t=" << t << "  PL331=" << p1 << "  PL286=" << p2
              << "  RSRP331=" << 18.3 - p1 << "  RSRP286=" << 18.3 - p2
              << "  d(286-331)=" << p1 - p2 << " dB" << std::endl;
    Simulator::Schedule(interval, &PrintTrace, gnbMobs, interval);
}

int
main(int argc, char* argv[])
{
    std::string dbFileName = "vienna-ho-trace.db";
    std::string source = "all";
    std::string handoverMode = "ric";
    Time ranDecisionDelay = MilliSeconds(0);
    Time ranTriggeringDelay = MilliSeconds(0);
    std::string onnxModelPath = "vienna_ho_rf_lag.onnx";
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
    double simTimeSec = 32.0;
    bool verbose = false;
    bool printTrace = false;
    bool useFlowMonitor = true;
    double dlIntervalMs = 10.0;
    uint32_t antRows = 1;
    uint32_t antCols = 1;
    bool useSpectrum = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("verbose", "Print SQL query results", verbose);
    cmd.AddValue("printTrace", "Print the measured pathloss of both cells as it is applied",
                 printTrace);
    cmd.AddValue("db-file", "SQLite database file", dbFileName);
    cmd.AddValue("source",
                 "Which measured samples to use: \"all\" for the phone and scanner rows "
                 "together, or \"ue\" for the phone rows alone. The two are different "
                 "measurements and mixing them introduces steps of over 10 dB. Note that "
                 "\"ue\" does not clean this handover up, it removes it: PCI 286's only 4 UE "
                 "rows sit inside the 1.5 s it was already serving, so it is flat-held before "
                 "that and the crossing never happens. See the header",
                 source);
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
                 "the gNB RRC with the RIC reporting but not acting, \"both\" to run them "
                 "against each other, or \"onnx\" for the RIC running the ONNX classifier "
                 "OranLmNr2NrRsrpLagOnnxHandover instead of the A3 rule. The A3 condition "
                 "itself is evaluated by the UE RRC in every mode; see the header",
                 handoverMode);
    cmd.AddValue("onnx-model",
                 "Path to the ONNX model for --handover=onnx, resolved relative to the working "
                 "directory. NOTE that the shipped model was trained on the FITTED scenario "
                 "(vienna-ho-fitted-pingpong), not on this one, so running it here is an "
                 "out-of-distribution test rather than a fit",
                 onnxModelPath);
    cmd.AddValue("num-lags", "Number of lagged RSRP differences; must match the model", numLags);
    cmd.AddValue("decision-threshold",
                 "Probability of the handover class above which the Command is issued",
                 decisionThreshold);
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
    cmd.AddValue("sim-time", "Total simulation time (s)", simTimeSec);
    cmd.AddValue("antenna-rows",
                 "gNB antenna array rows. Only has an effect with --spectrum=true, and see "
                 "the warning there before using either",
                 antRows);
    cmd.AddValue("antenna-cols", "gNB antenna array columns; see --antenna-rows", antCols);
    cmd.AddValue("spectrum",
                 "Install the 3GPP spectrum propagation model, which is what evaluates the "
                 "antenna array, beamforming and fading. WARNING: this DOUBLE-COUNTS the "
                 "antenna, because the measured pathloss already includes the real "
                 "network's antenna gain. Measured at 8x8 it swings the 286-vs-331 "
                 "differential by 31.8 dB and the handover disappears. Left off by default; "
                 "see the header",
                 useSpectrum);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(source != "all" && source != "ue", "source must be \"all\" or \"ue\"");
    NS_ABORT_MSG_IF(hysteresis < -15.0 || hysteresis > 15.0,
                    "hysteresis must be in the range [-15.0..15.0] dB");
    NS_ABORT_MSG_IF(handoverMode != "ric" && handoverMode != "ran" && handoverMode != "both" &&
                        handoverMode != "onnx",
                    "handover must be \"ric\", \"ran\", \"both\" or \"onnx\"");

    // "ric" runs the rule-based Logic Module and disables the RAN algorithm,
    // "ran" the reverse, "both" runs the two against each other, and "onnx"
    // swaps the rule for the ML model on the same RIC path.
    const bool ranHandover = (handoverMode == "ran" || handoverMode == "both");
    const bool ricA3Handover = (handoverMode == "ric" || handoverMode == "both");
    const bool ricOnnxHandover = (handoverMode == "onnx");

    if (ricOnnxHandover)
    {
        TypeId onnxTid;
        NS_ABORT_MSG_IF(
            !TypeId::LookupByNameFailSafe("ns3::OranLmNr2NrRsrpLagOnnxHandover", &onnxTid),
            "--handover=onnx needs the ONNX Logic Module, which is only compiled when ONNX "
            "Runtime is found at configure time. Set LIBONNXPATH and re-run ./ns3 configure.");
        // The model's lag features are spaced by one Logic Module query.
        NS_ABORT_MSG_IF(std::abs(lmQueryIntervalSec - 0.1) > 1e-9,
                        "The ONNX model's lag features are spaced by --lm-query-interval, and "
                        "it was trained at 0.1 s.");
    }

    // The channel is a replay, so the run is deterministic and the seed only
    // affects the traffic and the RIC's own random variables.
    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(1);

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
    Config::SetDefault("ns3::OranReportTriggerPeriodic::IntervalRv", StringValue(e2SendRv));

    // --- NR helpers ---
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

    // --- Nodes ---
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

    // --- UE trajectory ---
    // The positions no longer drive the pathloss, but they still drive the
    // location Reports and keep the scenario legible.
    struct WP
    {
        double t; ///< Time of the waypoint, before the offset
        double x; ///< ENU east coordinate, metres
        double y; ///< ENU north coordinate, metres
    };
    const WP waypoints[] = {
        {0.14, 312.65, -178.69},  {0.66, 308.58, -174.46},  {1.67, 304.72, -169.79},
        {2.67, 300.79, -164.79},  {3.72, 296.79, -160.12},  {4.72, 293.01, -155.01},
        {5.73, 289.01, -150.34},  {6.73, 284.93, -145.67},  {7.75, 281.08, -140.55},
        {8.74, 277.30, -135.88},  {9.74, 273.22, -132.10},  {10.77, 269.22, -126.98},
        {11.78, 265.14, -122.31}, {12.79, 261.43, -117.64}, {13.78, 257.65, -113.86},
        {14.76, 253.95, -109.19}, {15.79, 250.17, -104.52}, {16.80, 246.17, -100.30},
    };

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

    // --- Register the measured series against each gNB ---
    auto collect = [&source](const PlSample* first, size_t n) {
        std::vector<PlSample> v;
        for (size_t i = 0; i < n; i++)
        {
            if (source == "all" || first[i].fromUe)
            {
                v.push_back(first[i]);
            }
        }
        return v;
    };
    std::vector<Ptr<MobilityModel>> gnbMobs = {
        gnbNodes.Get(0)->GetObject<MobilityModel>(),
        gnbNodes.Get(1)->GetObject<MobilityModel>(),
    };
    auto s331 = collect(PL_331, sizeof(PL_331) / sizeof(PL_331[0]));
    auto s286 = collect(PL_286, sizeof(PL_286) / sizeof(PL_286[0]));
    NS_ABORT_MSG_IF(s286.empty(), "PCI 286 has no samples for source=" + source);
    ViennaTracePropagationLossModel::RegisterCell(gnbMobs[0], s331, g_tOffset);
    ViennaTracePropagationLossModel::RegisterCell(gnbMobs[1], s286, g_tOffset);
    std::cout << "Measured samples in use: PCI 331 = " << s331.size() << ", PCI 286 = "
              << s286.size() << "  (source=" << source << ")" << std::endl;

    // --- Antennas: 1x1 isotropic, both ends. See the header. ---
    nrHelper->SetGnbAntennaAttribute("AntennaElement",
                                     PointerValue(CreateObject<IsotropicAntennaModel>()));
    nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(antRows));
    nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(antCols));
    if (antRows > 1 || antCols > 1)
    {
        nrHelper->SetGnbAntennaAttribute("IsDualPolarized", BooleanValue(true));
        nrHelper->SetGnbAntennaAttribute("NumHorizontalPorts", UintegerValue(antCols / 2));
        nrHelper->SetGnbAntennaAttribute("NumVerticalPorts", UintegerValue(antRows / 2));
        nrHelper->SetGnbAntennaAttribute("DowntiltAngle", DoubleValue(6.0 * M_PI / 180.0));
        nrHelper->SetGnbAntennaAttribute("PolSlantAngle", DoubleValue(45.0 * M_PI / 180.0));
    }
    else
    {
        nrHelper->SetGnbAntennaAttribute("IsDualPolarized", BooleanValue(false));
    }
    nrHelper->SetUeAntennaAttribute("AntennaElement",
                                    PointerValue(CreateObject<IsotropicAntennaModel>()));
    nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(1));
    nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(1));

    // --- Channel: the measured trace replaces the propagation model ---
    Ptr<NrChannelHelper> channelHelper = CreateObject<NrChannelHelper>();
    if (useSpectrum)
    {
        // ConfigureFactories sets the pathloss, spectrum and channel condition
        // factories together. The pathloss one is overridden immediately below,
        // leaving the 3GPP spectrum model and channel condition model in place so
        // that the antenna array, beamforming and fading are actually evaluated.
        channelHelper->ConfigureFactories("UMa", "Default", "ThreeGpp");
    }
    channelHelper->ConfigurePropagationFactory(
        ViennaTracePropagationLossModel::GetTypeId());

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
        Ptr<NrGnbPhy> phy = NrHelper::GetGnbPhy(gnbDevs.Get(i), 0);
        phy->SetAttribute("Pattern", StringValue("DL|DL|DL|S|UL|"));
        // Sector azimuth, clockwise from North in the dataset, counter-clockwise
        // from East in ns-3. Only has an effect when the spectrum model is
        // installed, since that is what evaluates the array.
        auto antenna = DynamicCast<UniformPlanarArray>(phy->GetSpectrumPhy()->GetAntenna());
        antenna->SetAttribute("BearingAngle",
                              DoubleValue((90.0 - AZIMUTHS[i]) * M_PI / 180.0));
        std::cout << "gNB[" << i << "] cellId=" << gnbDev->GetCellId() << " PCI=" << PCIS[i]
                  << std::endl;
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

    nrHelper->AddX2Interface(gnbNodes);
    nrHelper->AttachToGnb(ueDevs.Get(0), gnbDevs.Get(0));

    Ptr<NrUeNetDevice> ueNetDev = ueDevs.Get(0)->GetObject<NrUeNetDevice>();
    std::cout << "Initial attachment: PCI " << CellIdToPci(ueNetDev->GetCellId()) << std::endl;

    // --- DL traffic ---
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
    // O-RAN setup, identical to the geometric replays
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
    if (ricA3Handover)
    {
        oranHelper->SetDefaultLogicModule("ns3::OranLmNr2NrA3RsrpHandover",
                                          "MaxReportAge",
                                          TimeValue(maxReportAge),
                                          "ProcessingDelayRv",
                                          StringValue(lmDelayRv));
    }
    else if (ricOnnxHandover)
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
    else
    {
        // A default Logic Module is mandatory, so the RIC gets the no-op one. It
        // still registers the nodes and stores every Report, which is the point:
        // the dataset is identical, only the decisions are the RAN's.
        oranHelper->SetDefaultLogicModule("ns3::OranLmNoop");
    }
    oranHelper->SetConflictMitigationModule("ns3::OranCmmHandover");

    Ptr<OranNearRtRic> nearRtRic = oranHelper->CreateNearRtRic();

    // --- UE terminator, with RSRP/SINR reporters for observability ---
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
    // This one feeds the OranReporterNrGnbMeasReport, and is separate from the
    // one NrA3RsrpHandoverAlgorithm registers for itself when --handover includes
    // the RAN algorithm. The two get different measIds and neither acts on the
    // other's: the algorithm ignores measIds outside its own list, and the
    // Reporter drops measIds it was not given. Same parameters, so they fire
    // together.
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
        Simulator::Schedule(Seconds(g_tOffset), &PrintTrace, gnbMobs, Seconds(1.0));
    }

    std::cout << "Event A3 hysteresis " << hysteresis << " dB, time-to-trigger "
              << timeToTrigger.GetMilliSeconds() << " ms; dataset handover expected at t="
              << 10.766 + g_tOffset << " s" << std::endl;
    std::cout << "Handover decided by: " << handoverMode << std::endl;
    if (ranHandover)
    {
        std::cout << "  RAN NrA3RsrpHandoverAlgorithm, decision delay "
                  << ranDecisionDelay.GetMilliSeconds() << " ms, triggering delay "
                  << ranTriggeringDelay.GetMilliSeconds() << " ms" << std::endl;
    }
    if (ricA3Handover)
    {
        std::cout << "  RIC OranLmNr2NrA3RsrpHandover, query interval "
                  << lmQueryInterval.GetMilliSeconds() << " ms, LM processing delay "
                  << lmProcessingDelayMs << " ms" << std::endl;
    }
    else if (ricOnnxHandover)
    {
        std::cout << "  RIC OranLmNr2NrRsrpLagOnnxHandover, model=" << onnxModelPath
                  << ", numLags=" << numLags << ", threshold=" << decisionThreshold
                  << ", query interval " << lmQueryInterval.GetMilliSeconds() << " ms"
                  << std::endl;
        std::cout << "  NOTE: this model was trained on the FITTED scenario, not on this "
                     "replay; it is being run out of distribution"
                  << std::endl;
    }
    else
    {
        std::cout << "  RIC OranLmNoop: reporting into the database, issuing no Commands"
                  << std::endl;
    }

    Simulator::Stop(simTime);
    Simulator::Run();

    std::cout << "\n=== Simulation complete ===" << std::endl;
    std::cout << "Handovers started:   " << g_handoverStartCount << std::endl;
    std::cout << "Handovers completed: " << g_handoverEndOkCount << std::endl;
    std::cout << "Handovers failed:    " << g_handoverEndErrorCount << std::endl;
    std::cout << "SQLite DB:           " << dbFileName << std::endl;

    if (flowMonitor)
    {
        PrintFlowStats(flowMonitor, flowHelper, handoverMode);
    }

    Simulator::Destroy();
    return 0;
}
