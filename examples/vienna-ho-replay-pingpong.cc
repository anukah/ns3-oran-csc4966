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
 * Vienna handover ping-pong replay: PCI 331 / 285 / 286, Event A3 at the RIC.
 *
 * Replays the full sequence of five handovers observed between 03:35:04 and
 * 03:35:32 UTC on 2025-02-08 in the Vienna 5G drive-test dataset, rather than
 * the single 331 -> 286 transition that vienna-ho-replay.cc and
 * vienna-ho-replay-trace.cc target.
 *
 * === WHY THIS EXAMPLE EXISTS ===
 *
 * The other two Vienna examples treat 03:35:14.766 as a stable inter-gNB
 * handover from PCI 331 to PCI 286. Checking the dataset shows it is not. PCI
 * 286 served the UE for 1.5 seconds, at -99.7 dBm and 5.3 dB SINR, and the UE
 * then moved to PCI 285. The whole window is a ping-pong:
 *
 *   sim t     UTC             serving   RSRP dBm   SINR dB   note
 *    3.145    03:35:04.145    PCI 331     -102.2       4.9   initial
 *    5.669    03:35:06.669    PCI 285      -94.3      19.1
 *    9.222    03:35:10.222    PCI 331     -102.5       8.5
 *   13.766    03:35:14.766    PCI 286      -99.7       5.3   1.5 s only
 *   15.786    03:35:16.786    PCI 285      -97.0      16.4
 *   25.930    03:35:26.930    PCI 331     -100.6       9.0   at a street corner
 *
 * (Simulation times include the g_tOffset shift applied to the waypoints.)
 *
 * Reproducing a ping-pong is a far better test of an Event A3 Logic Module than
 * reproducing one clean handover, because suppressing ping-pong is exactly what
 * the A3 hysteresis and time-to-trigger exist to do. Sweeping --hysteresis and
 * --timeToTrigger here trades handover count against how closely the sequence
 * tracks the real one, which is the trade-off a real network is tuned on.
 *
 * === THE THIRD CELL, AND WHY THE ANTENNA ELEMENT CHANGES ===
 *
 * PCI 285 and PCI 286 are two sectors of the SAME physical site (gnb_id 4783,
 * dummy gNB 11). They share position and height exactly and differ only in
 * azimuth: 20 degrees for PCI 285, 155 degrees for PCI 286.
 *
 * That has a hard consequence for the antenna configuration. The other Vienna
 * examples never set AntennaElement, so PhasedArrayModel's default of
 * ns3::IsotropicAntennaModel applies and the mechanical azimuths do nothing.
 * Here that would make PCI 285 and PCI 286 the SAME CELL as far as the
 * propagation model is concerned: identical position, identical distance,
 * identical gain, differing only by an independent shadowing draw. The sector
 * azimuth is the only thing that distinguishes them, so it has to be a
 * quantity the model actually responds to.
 *
 * This example therefore installs a directional element on the gNBs. The UE
 * element is left isotropic, since handsets are roughly omnidirectional and the
 * dataset carries no UE orientation.
 *
 * === PER-CELL BEAMWIDTH, AND WHERE IT COMES FROM ===
 *
 * The element is ns3::ParabolicAntennaModel with a DIFFERENT beamwidth per cell,
 * rather than the 65 degrees that ns3::ThreeGppAntennaModel hard-codes for every
 * sector. That is not a free parameter: it is read off the coverage the drive
 * test actually observed, published with the dataset in pub_pci331_286.html:
 *
 *   PCI 331   observed over a  99 degree arc   (az 145, h 36 m)
 *   PCI 286   observed over a 219 degree arc   (az 155, h 58 m)
 *
 * A 65 degree sector cannot serve users across 219 degrees. Modelling both cells
 * with the same narrow element therefore over-penalises PCI 285 and PCI 286 for
 * being 67 and 158 degrees off their boresights, which is most of the reason
 * they sit 24 and 39 dB below PCI 331 here while the drive test has all three
 * within a few dB.
 *
 * The useful property is that the RATIO of the two observed widths does not
 * depend on how weak a cell may be and still count as observed. For any
 * parabolic pattern the observed width is 2 * bw * sqrt(drop / 12), so the
 * unknown detection depth cancels:
 *
 *   bw(286) / bw(331) = 219 / 99 = 2.2
 *
 * Anchoring PCI 331 at the standard 65 degrees gives 144 degrees for the two
 * gNB 11 sectors, which is the default of --beamwidths. This recovers about
 * 10 dB on PCI 285 and about 15 dB on PCI 286, and brings the differential at
 * the first real 331 -> 285 handover instant from -13.5 dB to -3.2 dB, which is
 * within reach of fading.
 *
 * WHAT THIS IS AND IS NOT. It is a physical parameter set from published
 * measurements of these two cells, not a constant tuned until a handover
 * appeared, and it is a property of the environment that every handover
 * algorithm evaluated here shares. What it is not is a datasheet: a 219 degree
 * observed arc may partly be signal reflected off buildings rather than the
 * panel genuinely radiating that wide, so the widths should be read as an
 * inference from coverage data. Pass --beamwidths="65,65,65" to get the
 * conventional 3GPP sector back.
 *
 * ParabolicAntennaModel has no vertical pattern, which costs nothing in this
 * geometry: both sites see the UE within 1.3 degrees of their downtilt axis, so
 * the vertical loss either way is below 0.005 dB.
 *
 * === PARAMETER PROVENANCE ===
 *
 * FROM DATASET (Vienna drive-test 2025-02-08, estimated_cell_info/):
 *   - PCI 331  gNB 13, ENU origin,           height 36 m, azimuth 145 deg
 *   - PCI 285  gNB 11, (592.62, -431.55),    height 58 m, azimuth  20 deg
 *   - PCI 286  gNB 11, (592.62, -431.55),    height 58 m, azimuth 155 deg
 *   - Center frequency          3540 MHz (n78, channel 636000)
 *   - UE waypoints (29 pts)     from GPS, height 1.5 m, 03:35:04 to 03:35:32
 *   - Handover instants         as tabulated above
 *
 * NOTE ON PROVENANCE QUALITY: the cell records are in a directory named
 * estimated_cell_info, and its README states they are "not operator ground
 * truth" but "best-effort estimates derived from measurement-based inference".
 * No uncertainty is quoted. The dataset contradicts itself on PCI 331, listing
 * azimuth 145 deg on n78 and 90 deg on n1 for what the scanner confirms is the
 * same physical sector: a 55 degree disagreement, which is a fair estimate of
 * how much the azimuths can be trusted.
 *
 * 3GPP DEFAULTS / ASSUMED (calibration knobs):
 *   - gNB Tx power              49 dBm  (3GPP 38.104 Table 6.2.1-1, macro)
 *   - UE Tx power               23 dBm  (3GPP 38.101 default)
 *   - Bandwidth                 100 MHz (n78 typical, not from dataset)
 *   - TDD pattern               DDDSU   (common n78 config, not measured)
 *   - Numerology                mu=1, 30 kHz SCS
 *   - gNB antenna               8x8 cross-pol, parabolic element
 *   - Element beamwidth         65 / 144 / 144 deg  (from observed sector
 *                               widths; see the section above)
 *   - Front-to-back cap         20 dB   (assumed; the drive test shows about
 *                               18 dB for PCI 286)
 *   - UE antenna                2x2, isotropic element
 *   - Downtilt                  6 deg   (typical urban macro, NOT in dataset)
 *   - Channel update period     10 ms   (ns-3 defaults to 0, which disables
 *                               fading updates entirely; see below)
 *   - A3 hysteresis             3.0 dB  (NrA3RsrpHandoverAlgorithm default)
 *   - A3 time-to-trigger        256 ms  (NrA3RsrpHandoverAlgorithm default)
 *   - Building model            NONE (see below)
 *
 * === EXPECTED OUTCOME, AND WHY ===
 *
 * The target is the 331 <-> 285 ping-pong: the real network handed over to
 * PCI 285 at 03:35:06.669 and back to PCI 331 3.55 s later. Reproducing that
 * pair, rather than all five transitions, is what this scenario is tuned for.
 * The PCI 286 excursion is not reachable here and is not expected; see below.
 *
 * Two things had to be right for any handover to be possible at all, and the
 * history of getting them wrong is worth keeping.
 *
 * FIRST, THE MEAN LEVEL. With a uniform 65 degree element PCI 285 sits about
 * 24 dB below PCI 331 and PCI 286 about 39 dB below, where the drive test has
 * all three within a few dB. The discrepancy is NOT propagation: the dataset
 * carries a measured pathloss_db per sample, and against TR 38.901 UMa NLOS it
 * averages +1.2 dB for PCI 331, -8.4 dB for PCI 285 and -3.9 dB for PCI 286, so
 * no path shows excess loss and a building model has nothing to add. It is the
 * antenna, which is what the per-cell beamwidth above corrects. An earlier
 * revision instead carried per-cell transmit power offsets of +22 and +37 dB;
 * they were removed, because ns3::NrGnbPhy::TxPower is conducted power rather
 * than EIRP, so a large constant calibrates no physical quantity, is
 * direction-independent while the error it compensates is not, and inflates the
 * offending cell's interference everywhere.
 *
 * SECOND, THE VARIABILITY. Even at the right mean level a smooth curve crosses
 * a hysteresis band once, giving one handover rather than a ping-pong. Measured
 * over an early run, the standard deviation of RSRP(285) - RSRP(331) was only
 * 1.5 to 4.6 dB and all of it slow drift, against the 5 to 10 dB swings on a 1
 * to 2 second timescale that the drive test shows. The cause is that
 * ns3::ThreeGppChannelModel::UpdatePeriod defaults to zero, which disables
 * channel updates outright, so the small-scale realisation is generated once per
 * link and frozen for the whole simulation. There was no fast fading at all.
 * That is why sweeping the time-to-trigger changed WHEN a handover fired but
 * never HOW MANY fired: with only one crossing in the run, there was nothing for
 * a dwell requirement to filter.
 *
 * With both corrections the differential at the first real 331 -> 285 instant is
 * about -3.2 dB, and the fading has to carry it the rest of the way. That makes
 * the outcome genuinely stochastic: expect roughly one to three handovers per
 * run and a ping-pong in something over half of seeds, not a fixed sequence.
 * Sweep --rng-run and report the distribution rather than any single run.
 *
 * WHAT TO CHECK FIRST if no handover occurs: read the standard deviation of the
 * differential out of the nruersrprsrq table, not the handover count. RSRP is
 * averaged across the whole 100 MHz carrier, which smooths frequency-selective
 * fading, so the realised spread may come out below what the drive test shows
 * even with updates enabled. If it does, the shortfall is variability and not
 * level, and --channel-update-period is the knob.
 *
 * PCI 286 REMAINS OUT OF REACH. Even at 144 degrees it stays about 19 dB below
 * PCI 331 at the moment of the real 331 -> 286 handover, because the UE is
 * 158 degrees off its recorded boresight. That recorded azimuth of 155 degrees
 * is itself hard to reconcile with a 219 degree observed arc centred on it, so
 * the likeliest explanation is an error in the azimuth rather than anything this
 * example can model. Treat the 331 <-> 285 pair as the reproducible event.
 *
 * The final handover back to PCI 331 at 03:35:26.930 lands exactly where the
 * UE turns a corner, which the waypoints show as x reversing from decreasing to
 * increasing while y keeps rising. That one is geometry-specific and is the
 * least likely of the five to reproduce.
 *
 * The run prints a comparison of the handovers it produced against the real
 * ones, so the gap is visible directly rather than having to be reconstructed
 * from the database.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/isotropic-antenna-model.h"
#include "ns3/parabolic-antenna-model.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/nr-module.h"
#include "ns3/oran-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/uniform-planar-array.h"

#include <cmath>
#include <cstdio>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ViennaHoPingPong");

/// Number of cells in the scenario.
static const uint32_t N_CELLS = 3;

/// Real PCIs, in the order the gNB nodes are created.
static const uint16_t PCIS[N_CELLS] = {331, 285, 286};

/// Sector azimuths in degrees clockwise from true North, per estimated_cell_info.
static const double AZIMUTHS[N_CELLS] = {145.0, 20.0, 155.0};

/// Site positions in ENU metres. Index 1 and 2 are co-sited sectors of gNB 11.
static const Vector SITES[N_CELLS] = {
    Vector(0.0, 0.0, 36.0),          // gNB 13, PCI 331
    Vector(592.62, -431.55, 58.0),   // gNB 11, PCI 285
    Vector(592.62, -431.55, 58.0),   // gNB 11, PCI 286
};

/// Offset applied to every dataset waypoint, to let the RIC come up first.
static double g_tOffset = 3.0;

// --- cellId to PCI mapping (populated after InstallGnbDevice) ---------------
static std::map<uint16_t, uint16_t> g_cellIdToPci;

/// One observed handover, for the end-of-run comparison.
struct HoEvent
{
    double t;         ///< Simulation time, in seconds
    uint16_t fromPci; ///< PCI handed away from
    uint16_t toPci;   ///< PCI handed over to
};

/// Handovers that completed during the run.
static std::vector<HoEvent> g_observed;
/// Number of handovers that started during the run.
static uint32_t g_handoverStartCount = 0;
/// Number of handovers that failed during the run.
static uint32_t g_handoverEndErrorCount = 0;
/// PCI the UE was attached to before the handover currently in flight.
static uint16_t g_pendingFromPci = 0;

/**
 * The real handover sequence, in simulation time (dataset time + g_tOffset).
 * Measured from phone/phone_data_5g.csv with beam_type == "Serving beam" and
 * measurement_id < 100000, which drops the duplicate second pass of the drive.
 */
static const HoEvent REAL_SEQUENCE[] = {
    {5.669, 331, 285},
    {9.222, 285, 331},
    {13.766, 331, 286},
    {15.786, 286, 285},
    {25.930, 285, 331},
};

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
 * Record and print a handover that the UE started.
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
    g_pendingFromPci = CellIdToPci(cellId);

    std::cout << Simulator::Now().GetSeconds() << "s HO START: PCI " << g_pendingFromPci << " -> "
              << CellIdToPci(targetCellId) << std::endl;
}

/**
 * Record and print a handover that the UE completed.
 *
 * @param context The trace context.
 * @param imsi The IMSI of the UE.
 * @param cellId The ID of the cell that the UE handed over to.
 * @param rnti The RNTI of the UE.
 */
void
NotifyHandoverEndOk(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
    double t = Simulator::Now().GetSeconds();
    uint16_t toPci = CellIdToPci(cellId);
    g_observed.push_back({t, g_pendingFromPci, toPci});

    std::cout << t << "s HO COMPLETE: PCI " << g_pendingFromPci << " -> " << toPci << " RNTI "
              << rnti << std::endl;
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
              << std::endl;
}

/**
 * Print a Measurement Report received by a gNB, with the quantized RSRP of each
 * measured cell and its equivalent in dBm.
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
              << nr::EutranMeasurementMapping::RsrpRange2Dbm(results.measResultPCell.rsrpResult)
              << " dBm";

    if (results.haveMeasResultNeighCells)
    {
        for (const auto& neighbour : results.measResultListEutra)
        {
            std::cout << "; PCI " << CellIdToPci(neighbour.physCellId) << " ";
            if (neighbour.haveRsrpResult)
            {
                std::cout << nr::EutranMeasurementMapping::RsrpRange2Dbm(neighbour.rsrpResult)
                          << " dBm";
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

/**
 * Print the handovers the run produced next to the ones the drive-test
 * recorded, so that the gap is visible without querying the database.
 */
void
PrintComparison()
{
    const size_t nReal = sizeof(REAL_SEQUENCE) / sizeof(REAL_SEQUENCE[0]);

    std::cout << "\n=== Real sequence (from the drive-test) ===" << std::endl;
    for (size_t i = 0; i < nReal; i++)
    {
        std::cout << "  " << std::fixed << std::setprecision(3) << std::setw(8)
                  << REAL_SEQUENCE[i].t << "s  PCI " << REAL_SEQUENCE[i].fromPci << " -> PCI "
                  << REAL_SEQUENCE[i].toPci << std::endl;
    }

    std::cout << "\n=== Simulated sequence ===" << std::endl;
    if (g_observed.empty())
    {
        std::cout << "  (none)" << std::endl;
    }
    for (const auto& ho : g_observed)
    {
        // Report the closest real handover with the same direction, if any.
        double bestDelta = 0.0;
        bool haveMatch = false;
        for (size_t i = 0; i < nReal; i++)
        {
            if (REAL_SEQUENCE[i].fromPci == ho.fromPci && REAL_SEQUENCE[i].toPci == ho.toPci)
            {
                double delta = ho.t - REAL_SEQUENCE[i].t;
                if (!haveMatch || std::abs(delta) < std::abs(bestDelta))
                {
                    bestDelta = delta;
                    haveMatch = true;
                }
            }
        }

        std::cout << "  " << std::fixed << std::setprecision(3) << std::setw(8) << ho.t << "s  PCI "
                  << ho.fromPci << " -> PCI " << ho.toPci;
        if (haveMatch)
        {
            std::cout << "   (" << std::showpos << bestDelta << std::noshowpos
                      << " s vs the real one)";
        }
        else
        {
            std::cout << "   (no real handover with this direction)";
        }
        std::cout << std::endl;
    }

    std::cout << "\n--- Handovers ---" << std::endl;
    std::cout << "  Real:      " << nReal << std::endl;
    std::cout << "  Started:   " << g_handoverStartCount << std::endl;
    std::cout << "  Completed: " << g_observed.size() << std::endl;
    std::cout << "  Failed:    " << g_handoverEndErrorCount << std::endl;
}

int
main(int argc, char* argv[])
{
    // --- Configurable parameters (all with 3GPP defaults unless noted) ---
    std::string dbFileName = "vienna-ho-pingpong.db";
    double gnbTxPower = 49.0;
    double ueTxPower = 23.0;
    double bandwidthHz = 100e6;
    uint8_t numerology = 1;
    // Defaults of ns3::NrA3RsrpHandoverAlgorithm.
    double hysteresis = 3.0;
    Time timeToTrigger = MilliSeconds(256);
    Time maxReportAge = Seconds(1.5);
    double downtiltDeg = 6.0;
    // Per-cell antenna element 3 dB beamwidth in degrees, in the order of PCIS,
    // calibrated from the observed sector widths. See the header.
    std::string beamwidthsStr = "65,144,144";
    double maxAttenuationDb = 20.0;
    // Small-scale channel update period. Zero, the ns-3 default, disables
    // channel updates altogether and freezes the fading realisation for the
    // whole run. See the header.
    Time channelUpdatePeriod = MilliSeconds(10);
    // Whether the handover occurs at all is decided by the fading realisation,
    // so the run number is a first-class parameter here. Sweep it.
    uint32_t rngRun = 1;
    double lmProcessingDelayMs = 10.0;
    double lmQueryIntervalSec = 0.1;
    double e2ReportIntervalSec = 0.1;
    // The last waypoint is at 27.49 s plus the offset, so the run has to reach
    // past 30.5 s for the whole sequence to be in scope.
    double simTimeSec = 32.0;
    bool verbose = false;
    bool printMeasReports = false;

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
    cmd.AddValue("downtilt", "gNB antenna downtilt in degrees [assumed]", downtiltDeg);
    cmd.AddValue("beamwidths",
                 "Comma-separated antenna element 3 dB beamwidth in degrees per cell, in "
                 "the order 331,285,286. Calibrated from the observed sector widths of "
                 "99 and 219 degrees [calibration knob]",
                 beamwidthsStr);
    cmd.AddValue("max-attenuation",
                 "Front-to-back attenuation cap of the antenna element in dB [assumed]",
                 maxAttenuationDb);
    cmd.AddValue("channel-update-period",
                 "Small-scale channel update period. Zero disables channel updates and "
                 "freezes the fading realisation, which is the ns-3 default and leaves the "
                 "scenario with no fast fading at all",
                 channelUpdatePeriod);
    cmd.AddValue("hysteresis",
                 "Event A3 handover margin in dB, rounded to the nearest 0.5 dB. A "
                 "non-negative value is applied as the A3 hysteresis IE; a negative value "
                 "is applied as the signed A3 offset IE [calibration knob]",
                 hysteresis);
    cmd.AddValue("timeToTrigger",
                 "Event A3 time-to-trigger: how long the entry condition must hold "
                 "continuously in the UE RRC before a Measurement Report is sent. Raising "
                 "this is the primary way to suppress ping-pong [calibration knob]",
                 timeToTrigger);
    cmd.AddValue("maxReportAge", "Measurement Reports older than this are ignored", maxReportAge);
    cmd.AddValue("lm-processing-delay", "LM processing delay (ms)", lmProcessingDelayMs);
    cmd.AddValue("lm-query-interval", "RIC LM query interval (s)", lmQueryIntervalSec);
    cmd.AddValue("e2-report-interval", "E2 report interval (s)", e2ReportIntervalSec);
    cmd.AddValue("sim-time", "Total simulation time (s)", simTimeSec);
    cmd.AddValue("rng-run",
                 "RNG run number. Selects the fading realisation, which decides whether the "
                 "handover occurs at all. Sweep it rather than reading any single run as "
                 "the result",
                 rngRun);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(hysteresis < -15.0 || hysteresis > 15.0,
                    "hysteresis must be in the range [-15.0..15.0] dB");

    // Parse the per-cell element beamwidths.
    std::vector<double> beamwidths;
    {
        std::stringstream ss(beamwidthsStr);
        std::string token;
        while (std::getline(ss, token, ','))
        {
            beamwidths.push_back(std::stod(token));
        }
    }
    NS_ABORT_MSG_IF(beamwidths.size() != N_CELLS,
                    "beamwidths needs exactly " + std::to_string(N_CELLS) +
                        " comma-separated values, one per cell");
    for (auto bw : beamwidths)
    {
        NS_ABORT_MSG_IF(bw <= 0.0 || bw > 360.0, "each beamwidth must be in (0..360] degrees");
    }


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

    // Small-scale channel updates. The ns-3 default of zero disables them
    // outright, which freezes the fading realisation of each link for the whole
    // run and leaves the differential between two cells varying only by slow
    // spatial drift. With a non-zero period the model evolves the channel per
    // TR 38.901 Procedure A and regenerates a realisation whenever an endpoint
    // moves more than a metre, which at the ~6 m/s of this trajectory is roughly
    // every 170 ms.
    Config::SetDefault("ns3::ThreeGppChannelModel::UpdatePeriod",
                       TimeValue(channelUpdatePeriod));

    // The Reporters accumulate Reports and hand them to the E2 Terminator when
    // their trigger fires, so this interval bounds how quickly a Measurement
    // Report can reach the RIC. The default of 1 s would dominate everything.
    Config::SetDefault("ns3::OranReportTriggerPeriodic::IntervalRv", StringValue(e2SendRv));

    // --- NR helpers ---
    Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();
    Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
    nrHelper->SetEpcHelper(epcHelper);
    nrHelper->SetAttribute("UseIdealRrc", BooleanValue(true));
    nrHelper->SetHandoverAlgorithmType("ns3::NrNoOpHandoverAlgorithm");

    // --- Create nodes ---
    NodeContainer gnbNodes;
    gnbNodes.Create(N_CELLS);
    NodeContainer ueNodes;
    ueNodes.Create(1);

    // --- gNB placement. Index 1 and 2 are co-sited sectors of gNB 11. ---
    Ptr<ListPositionAllocator> gnbPosAlloc = CreateObject<ListPositionAllocator>();
    for (uint32_t i = 0; i < N_CELLS; i++)
    {
        gnbPosAlloc->Add(SITES[i]);
    }

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

    // Measured from phone/phone_data_5g.csv, rows with beam_type == "Serving
    // beam" and measurement_id < 100000, window 03:35:04.000 to 03:35:32.000
    // UTC. ENU origin is the PCI 331 site. Note the UE turns a corner at
    // t = 22.93: x stops decreasing and starts increasing while y keeps rising.
    const WP waypoints[] = {
        {0.14, 312.65, -178.65},
        {0.66, 308.57, -174.43},
        {1.67, 304.72, -169.76},
        {2.67, 300.79, -164.75}, // real HO 331 -> 285 at 2.669
        {3.71, 296.79, -160.08},
        {4.72, 293.01, -154.97},
        {5.73, 289.00, -150.30}, // real HO 285 -> 331 at 6.222
        {6.73, 284.93, -145.63},
        {7.75, 281.07, -140.51},
        {8.74, 277.29, -135.84},
        {9.74, 273.21, -132.06},
        {10.77, 269.21, -126.95}, // real HO 331 -> 286 at 10.766
        {11.78, 265.14, -122.28},
        {12.79, 261.43, -117.61}, // real HO 286 -> 285 at 12.786
        {13.78, 257.65, -113.82},
        {14.76, 253.94, -109.15},
        {15.79, 250.16, -104.48},
        {16.80, 246.16, -100.26},
        {17.81, 242.08, -96.37},
        {18.84, 238.82, -91.70},
        {19.86, 235.86, -87.92},
        {20.88, 233.48, -84.91},
        {21.91, 231.78, -82.02},
        {22.93, 230.52, -79.91}, // real HO 285 -> 331 at 22.930, at the corner
        {23.95, 232.60, -76.02},
        {24.46, 235.19, -72.68},
        {25.97, 238.97, -69.24},
        {26.97, 243.19, -65.90},
        {27.49, 247.20, -62.01},
    };

    // The UE must already be standing at the first dataset waypoint when the
    // run begins. MobilityHelper otherwise places a node at the origin, and
    // InitialPositionIsWaypoint turns that placement into a waypoint at t=0,
    // which would send the UE across the ~360 m to the first waypoint at
    // ~115 m/s and produce entirely spurious handovers.
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
    // A directional element is mandatory here, not cosmetic: PCI 285 and PCI 286
    // are co-sited, so their azimuths are the ONLY thing that tells them apart.
    // With the default isotropic element they would be the same cell.
    //
    // ParabolicAntennaModel rather than ThreeGppAntennaModel because the latter
    // has no configurable beamwidth, and a per-cell beamwidth is exactly what
    // the coverage data calls for (see the header). The parabolic model has no
    // vertical pattern, which costs nothing in this geometry: both sites see the
    // UE within 1.3 deg of their downtilt axis, for a vertical loss below
    // 0.005 dB. The per-cell element is installed after InstallGnbDevice, since
    // SetGnbAntennaAttribute applies to every gNB alike.
    nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(8));
    nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(8));
    nrHelper->SetGnbAntennaAttribute("IsDualPolarized", BooleanValue(true));
    nrHelper->SetGnbAntennaAttribute("NumHorizontalPorts", UintegerValue(4));
    nrHelper->SetGnbAntennaAttribute("NumVerticalPorts", UintegerValue(4));
    nrHelper->SetGnbAntennaAttribute("DowntiltAngle", DoubleValue(downtiltDeg * M_PI / 180.0));
    nrHelper->SetGnbAntennaAttribute("PolSlantAngle", DoubleValue(45.0 * M_PI / 180.0));
    // UE: 2x2, isotropic element, since the dataset carries no UE orientation.
    nrHelper->SetUeAntennaAttribute("AntennaElement",
                                    PointerValue(CreateObject<IsotropicAntennaModel>()));
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

    // --- Per-sector antenna bearing (set after install) ---
    // Azimuth convention: dataset = degrees clockwise from true North, per
    // estimated_cell_info/README.md. ns-3 BearingAngle is counter-clockwise
    // from East, so BearingAngle = (90 - azimuth) deg.
    for (uint32_t i = 0; i < gnbDevs.GetN(); i++)
    {
        Ptr<NrGnbNetDevice> gnbDev = gnbDevs.Get(i)->GetObject<NrGnbNetDevice>();
        uint16_t cellId = gnbDev->GetCellId();

        g_cellIdToPci[cellId] = PCIS[i];

        double bearingRad = (90.0 - AZIMUTHS[i]) * M_PI / 180.0;
        Ptr<NrGnbPhy> phy = NrHelper::GetGnbPhy(gnbDevs.Get(i), 0);
        Ptr<UniformPlanarArray> antenna =
            DynamicCast<UniformPlanarArray>(phy->GetSpectrumPhy()->GetAntenna());
        antenna->SetAttribute("BearingAngle", DoubleValue(bearingRad));

        // Per-cell element beamwidth, calibrated from the observed sector widths
        // (see the header). The element Orientation stays at 0: the array itself
        // is already rotated by BearingAngle, and UniformPlanarArray converts an
        // incoming angle into the element's local frame before asking it for a
        // gain, so orienting the element too would rotate the pattern twice.
        Ptr<ParabolicAntennaModel> element = CreateObject<ParabolicAntennaModel>();
        element->SetAttribute("Beamwidth", DoubleValue(beamwidths[i]));
        element->SetAttribute("MaxAttenuation", DoubleValue(maxAttenuationDb));
        antenna->SetAttribute("AntennaElement", PointerValue(element));

        // TDD pattern: DDDSU
        phy->SetAttribute("Pattern", StringValue("DL|DL|DL|S|UL|"));

        std::cout << "gNB[" << i << "] cellId=" << cellId << " PCI=" << PCIS[i]
                  << " azimuth=" << AZIMUTHS[i] << " deg  beamwidth=" << beamwidths[i]
                  << " deg  site=(" << SITES[i].x << ", " << SITES[i].y << ", " << SITES[i].z
                  << ")" << std::endl;
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

    // --- X2 + initial attachment to PCI 331, which serves at 03:35:04 ---
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

    // The Event A3 hysteresis and time-to-trigger live in the reporting
    // configuration installed on the gNBs below, and are applied by the UE RRC.
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
    // the RSRP/RSRQ and SINR Reporters have to be connected to the trace
    // sources of this specific UE PHY, which the helper cannot do.
    //
    // The Event A3 Logic Module does not read these: it works entirely from the
    // RRC Measurement Reports the gNBs forward. They exist for observability.
    // OranReporterNrUeRsrpRsrq fills the nruersrprsrq table and
    // OranReporterNrUeSinr fills nruesinr on every periodic trigger, whether or
    // not any Event A3 ever fires, which is what makes a run that produces no
    // handovers diagnosable at all. They are also the per-cell features a
    // learned handover policy would train on.
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

        // Connect the NrUePhy trace sources to the Reporters. The RSRP/RSRQ
        // reporter receives one entry per measured cell, so all three cells
        // appear in nruersrprsrq, not just the serving one.
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
    // Built by hand because each Measurement Report Reporter has to be given the
    // measurement IDs of the configurations installed on its own gNB, and
    // connected to that gNB's RRC trace source.
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

    std::cout << "Channel update period " << channelUpdatePeriod.GetMilliSeconds()
              << " ms (0 = fading frozen)" << std::endl;
    std::cout << "Event A3 hysteresis " << hysteresis << " dB, time-to-trigger "
              << timeToTrigger.GetMilliSeconds() << " ms, waypoint offset " << g_tOffset << " s"
              << std::endl;

    // --- Run ---
    Simulator::Stop(simTime);
    Simulator::Run();

    PrintComparison();
    std::cout << "\nSQLite DB: " << dbFileName << std::endl;

    Simulator::Destroy();

    return 0;
}
