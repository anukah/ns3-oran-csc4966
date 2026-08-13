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

#ifndef ORAN_REPORTER_NR_GNB_MEAS_REPORT_H
#define ORAN_REPORTER_NR_GNB_MEAS_REPORT_H

#include "oran-report-nr-gnb-meas-report.h"
#include "oran-report.h"
#include "oran-reporter.h"

#include "ns3/nr-rrc-sap.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <map>
#include <vector>

namespace ns3
{

/**
 * @ingroup oran
 *
 * A Reporter that attaches to an NR gNB and captures the 3GPP RRC Measurement
 * Reports that the gNB receives from the UEs it serves.
 *
 * Unlike the Reporters that read UE PHY traces, the measurements captured here
 * have already been through the full 3GPP measurement procedure in the UE RRC:
 * layer 3 filtering, the event entry and leaving conditions with their
 * hysteresis and cell individual offsets, and the time-to-trigger. They are
 * also quantized exactly as the RRC reports them. These are therefore the same
 * measurements that a gNB handover algorithm such as
 * `NrA2A4RsrpHandoverAlgorithm` or `NrA3RsrpHandoverAlgorithm` receives through
 * its Handover Management SAP, which is what allows a Logic Module to reproduce
 * such an algorithm's decision at the Near-RT RIC instead of at the gNB.
 *
 * The measurement reporting configurations that produce these reports are not
 * installed by this Reporter. The scenario must install them on the gNB RRC
 * with `NrGnbRrc::AddUeMeasReportConfig` before the simulation starts (that
 * method aborts if it is called after time 0), pass the returned measurement
 * IDs to `AddMeasIds`, and connect the `RecvMeasurementReport` trace source of
 * the gNB RRC to `ReportMeasurements`. Note that the handover algorithm
 * installed on the gNB is what normally registers those configurations, so a
 * scenario that uses `ns3::NrNoOpHandoverAlgorithm` to leave handover decisions
 * to the RIC has to register them explicitly.
 */
class OranReporterNrGnbMeasReport : public OranReporter
{
  public:
    /**
     * Get the TypeId of the OranReporterNrGnbMeasReport class.
     *
     * @return The TypeId.
     */
    static TypeId GetTypeId();
    /**
     * Constructor of the OranReporterNrGnbMeasReport class.
     */
    OranReporterNrGnbMeasReport();
    /**
     * Destructor of the OranReporterNrGnbMeasReport class.
     */
    ~OranReporterNrGnbMeasReport() override;
    /**
     * Associate a set of measurement IDs with the measurement reporting event
     * that they were configured for.
     *
     * The measurement IDs are the ones returned by
     * `NrGnbRrc::AddUeMeasReportConfig`. Recording the event that each
     * measurement ID belongs to is what lets a Logic Module tell, for example,
     * an Event A2 report apart from an Event A4 report.
     *
     * @param eventId The measurement reporting event.
     * @param measIds The measurement IDs configured for that event.
     */
    void AddMeasIds(OranReportNrGnbMeasReport::MeasEventId eventId,
                    const std::vector<uint8_t>& measIds);
    /**
     * Captures a Measurement Report received by the gNB.
     * Callback signature matches NrGnbRrc::ReceiveReportTracedCallback.
     *
     * The Measurement Report is flattened into one Report for the primary cell
     * and one Report per neighbour cell included in the report.
     *
     * @param imsi The IMSI of the UE that sent the Measurement Report.
     * @param cellId The ID of the cell that received the Measurement Report.
     * @param rnti The RNTI of the UE that sent the Measurement Report.
     * @param measReport The Measurement Report.
     */
    void ReportMeasurements(uint64_t imsi,
                            uint16_t cellId,
                            uint16_t rnti,
                            NrRrcSap::MeasurementReport measReport);

  protected:
    /**
     * Returns the generated OranReportNrGnbMeasReport reports.
     *
     * @return The generated Reports.
     */
    std::vector<Ptr<OranReport>> GenerateReports() override;

  private:
    /**
     * Look up the measurement reporting event that a measurement ID was
     * configured for.
     *
     * @param measId The measurement ID.
     * @return The event ID, or EVENT_UNKNOWN if the measurement ID was never
     *         passed to AddMeasIds.
     */
    OranReportNrGnbMeasReport::MeasEventId GetEventId(uint8_t measId) const;

    /**
     * The reports.
     */
    std::vector<Ptr<OranReport>> m_reports;
    /**
     * The measurement reporting event of each known measurement ID.
     */
    std::map<uint8_t, OranReportNrGnbMeasReport::MeasEventId> m_measIdEvents;
    /**
     * The `ReportUnknownMeasIds` attribute. Indicates if Measurement Reports
     * with a measurement ID that was never passed to AddMeasIds should be
     * reported.
     */
    bool m_reportUnknownMeasIds;

}; // class OranReporterNrGnbMeasReport

} // namespace ns3

#endif // ORAN_REPORTER_NR_GNB_MEAS_REPORT_H
