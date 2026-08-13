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

#include "oran-reporter-nr-gnb-meas-report.h"

#include "oran-report-nr-gnb-meas-report.h"

#include "ns3/abort.h"
#include "ns3/boolean.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OranReporterNrGnbMeasReport");
NS_OBJECT_ENSURE_REGISTERED(OranReporterNrGnbMeasReport);

TypeId
OranReporterNrGnbMeasReport::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::OranReporterNrGnbMeasReport")
            .SetParent<OranReporter>()
            .AddConstructor<OranReporterNrGnbMeasReport>()
            .AddAttribute(
                "ReportUnknownMeasIds",
                "Indicates if Measurement Reports with a measurement ID that was not "
                "associated with an event using AddMeasIds should be reported.",
                BooleanValue(false),
                MakeBooleanAccessor(&OranReporterNrGnbMeasReport::m_reportUnknownMeasIds),
                MakeBooleanChecker());

    return tid;
}

OranReporterNrGnbMeasReport::OranReporterNrGnbMeasReport()
{
    NS_LOG_FUNCTION(this);
}

OranReporterNrGnbMeasReport::~OranReporterNrGnbMeasReport()
{
    NS_LOG_FUNCTION(this);
}

void
OranReporterNrGnbMeasReport::AddMeasIds(OranReportNrGnbMeasReport::MeasEventId eventId,
                                        const std::vector<uint8_t>& measIds)
{
    NS_LOG_FUNCTION(this << +eventId);

    for (const auto& measId : measIds)
    {
        m_measIdEvents[measId] = eventId;
    }
}

OranReportNrGnbMeasReport::MeasEventId
OranReporterNrGnbMeasReport::GetEventId(uint8_t measId) const
{
    NS_LOG_FUNCTION(this << +measId);

    auto it = m_measIdEvents.find(measId);

    if (it == m_measIdEvents.end())
    {
        return OranReportNrGnbMeasReport::EVENT_UNKNOWN;
    }

    return it->second;
}

void
OranReporterNrGnbMeasReport::ReportMeasurements(uint64_t imsi,
                                                uint16_t cellId,
                                                uint16_t rnti,
                                                NrRrcSap::MeasurementReport measReport)
{
    NS_LOG_FUNCTION(this << imsi << +cellId << +rnti);

    if (!m_active)
    {
        return;
    }

    NS_ABORT_MSG_IF(m_terminator == nullptr,
                    "Attempting to generate reports in reporter with NULL E2 Terminator");

    const NrRrcSap::MeasResults& measResults = measReport.measResults;
    OranReportNrGnbMeasReport::MeasEventId eventId = GetEventId(measResults.measId);

    if (eventId == OranReportNrGnbMeasReport::EVENT_UNKNOWN && !m_reportUnknownMeasIds)
    {
        NS_LOG_LOGIC("Ignoring Measurement Report with unknown measId "
                     << +measResults.measId);
        return;
    }

    Time now = Simulator::Now();
    uint64_t e2NodeId = m_terminator->GetE2NodeId();

    // The measurement of the primary cell. MeasResultPCell does not carry a
    // cell ID, so the ID of the cell that received the report is used.
    Ptr<OranReportNrGnbMeasReport> pCellReport = CreateObject<OranReportNrGnbMeasReport>();
    pCellReport->SetAttribute("ReporterE2NodeId", UintegerValue(e2NodeId));
    pCellReport->SetAttribute("Time", TimeValue(now));
    pCellReport->SetAttribute("Imsi", UintegerValue(imsi));
    pCellReport->SetAttribute("Rnti", UintegerValue(rnti));
    pCellReport->SetAttribute("MeasId", UintegerValue(measResults.measId));
    pCellReport->SetAttribute("EventId", UintegerValue(eventId));
    pCellReport->SetAttribute("CellId", UintegerValue(cellId));
    pCellReport->SetAttribute("RsrpResult", UintegerValue(measResults.measResultPCell.rsrpResult));
    pCellReport->SetAttribute("RsrqResult", UintegerValue(measResults.measResultPCell.rsrqResult));
    pCellReport->SetAttribute("HaveRsrpResult", BooleanValue(true));
    pCellReport->SetAttribute("HaveRsrqResult", BooleanValue(true));
    pCellReport->SetAttribute("IsServingCell", BooleanValue(true));

    m_reports.push_back(pCellReport);

    // The measurements of the neighbour cells, if the report carries any.
    if (measResults.haveMeasResultNeighCells)
    {
        for (const auto& neighbour : measResults.measResultListEutra)
        {
            Ptr<OranReportNrGnbMeasReport> neighbourReport =
                CreateObject<OranReportNrGnbMeasReport>();
            neighbourReport->SetAttribute("ReporterE2NodeId", UintegerValue(e2NodeId));
            neighbourReport->SetAttribute("Time", TimeValue(now));
            neighbourReport->SetAttribute("Imsi", UintegerValue(imsi));
            neighbourReport->SetAttribute("Rnti", UintegerValue(rnti));
            neighbourReport->SetAttribute("MeasId", UintegerValue(measResults.measId));
            neighbourReport->SetAttribute("EventId", UintegerValue(eventId));
            neighbourReport->SetAttribute("CellId", UintegerValue(neighbour.physCellId));
            neighbourReport->SetAttribute(
                "RsrpResult",
                UintegerValue(neighbour.haveRsrpResult ? neighbour.rsrpResult : 0));
            neighbourReport->SetAttribute(
                "RsrqResult",
                UintegerValue(neighbour.haveRsrqResult ? neighbour.rsrqResult : 0));
            neighbourReport->SetAttribute("HaveRsrpResult", BooleanValue(neighbour.haveRsrpResult));
            neighbourReport->SetAttribute("HaveRsrqResult", BooleanValue(neighbour.haveRsrqResult));
            neighbourReport->SetAttribute("IsServingCell", BooleanValue(false));

            m_reports.push_back(neighbourReport);
        }
    }
}

std::vector<Ptr<OranReport>>
OranReporterNrGnbMeasReport::GenerateReports()
{
    NS_LOG_FUNCTION(this);

    std::vector<Ptr<OranReport>> reports;

    if (m_active)
    {
        reports = m_reports;
        m_reports.clear();
    }

    return reports;
}

} // namespace ns3
