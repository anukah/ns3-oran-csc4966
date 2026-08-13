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

#include "oran-report-nr-gnb-meas-report.h"

#include "oran-report.h"

#include "ns3/boolean.h"
#include "ns3/log.h"
#include "ns3/uinteger.h"

#include <sstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OranReportNrGnbMeasReport");
NS_OBJECT_ENSURE_REGISTERED(OranReportNrGnbMeasReport);

TypeId
OranReportNrGnbMeasReport::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::OranReportNrGnbMeasReport")
            .SetParent<OranReport>()
            .AddConstructor<OranReportNrGnbMeasReport>()
            .AddAttribute("Imsi",
                          "The IMSI of the UE that the Measurement Report came from.",
                          UintegerValue(),
                          MakeUintegerAccessor(&OranReportNrGnbMeasReport::m_imsi),
                          MakeUintegerChecker<uint64_t>())
            .AddAttribute("Rnti",
                          "The RNTI of the UE that the Measurement Report came from.",
                          UintegerValue(),
                          MakeUintegerAccessor(&OranReportNrGnbMeasReport::m_rnti),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("MeasId",
                          "The measurement ID of the reporting configuration.",
                          UintegerValue(),
                          MakeUintegerAccessor(&OranReportNrGnbMeasReport::m_measId),
                          MakeUintegerChecker<uint8_t>())
            .AddAttribute("EventId",
                          "The measurement reporting event that triggered the report.",
                          UintegerValue(OranReportNrGnbMeasReport::EVENT_UNKNOWN),
                          MakeUintegerAccessor(&OranReportNrGnbMeasReport::m_eventId),
                          MakeUintegerChecker<uint8_t>())
            .AddAttribute("CellId",
                          "The ID of the measured cell.",
                          UintegerValue(),
                          MakeUintegerAccessor(&OranReportNrGnbMeasReport::m_cellId),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("RsrpResult",
                          "The quantized RSRP of the measured cell.",
                          UintegerValue(),
                          MakeUintegerAccessor(&OranReportNrGnbMeasReport::m_rsrpResult),
                          MakeUintegerChecker<uint8_t>())
            .AddAttribute("RsrqResult",
                          "The quantized RSRQ of the measured cell.",
                          UintegerValue(),
                          MakeUintegerAccessor(&OranReportNrGnbMeasReport::m_rsrqResult),
                          MakeUintegerChecker<uint8_t>())
            .AddAttribute("HaveRsrpResult",
                          "The flag that indicates if the RSRP is valid.",
                          BooleanValue(),
                          MakeBooleanAccessor(&OranReportNrGnbMeasReport::m_haveRsrpResult),
                          MakeBooleanChecker())
            .AddAttribute("HaveRsrqResult",
                          "The flag that indicates if the RSRQ is valid.",
                          BooleanValue(),
                          MakeBooleanAccessor(&OranReportNrGnbMeasReport::m_haveRsrqResult),
                          MakeBooleanChecker())
            .AddAttribute("IsServingCell",
                          "The flag that indicates if the measured cell is the serving cell.",
                          BooleanValue(),
                          MakeBooleanAccessor(&OranReportNrGnbMeasReport::m_isServingCell),
                          MakeBooleanChecker());

    return tid;
}

OranReportNrGnbMeasReport::OranReportNrGnbMeasReport()
{
    NS_LOG_FUNCTION(this);
}

OranReportNrGnbMeasReport::~OranReportNrGnbMeasReport()
{
    NS_LOG_FUNCTION(this);
}

std::string
OranReportNrGnbMeasReport::ToString() const
{
    NS_LOG_FUNCTION(this);

    std::stringstream ss;
    Time time = GetTime();

    ss << "OranReportNrGnbMeasReport("
       << "E2NodeId=" << GetReporterE2NodeId() << ";Time=" << time.As(Time::S)
       << ";IMSI=" << m_imsi << ";RNTI=" << +m_rnti << ";Meas ID=" << +m_measId
       << ";Event ID=" << +m_eventId << ";Cell ID=" << +m_cellId
       << ";RSRP Result=" << +m_rsrpResult << ";RSRQ Result=" << +m_rsrqResult
       << ";Have RSRP=" << m_haveRsrpResult << ";Have RSRQ=" << m_haveRsrqResult
       << ";Is Serving Cell=" << m_isServingCell << ")";

    return ss.str();
}

uint64_t
OranReportNrGnbMeasReport::GetImsi() const
{
    NS_LOG_FUNCTION(this);

    return m_imsi;
}

uint16_t
OranReportNrGnbMeasReport::GetRnti() const
{
    NS_LOG_FUNCTION(this);

    return m_rnti;
}

uint8_t
OranReportNrGnbMeasReport::GetMeasId() const
{
    NS_LOG_FUNCTION(this);

    return m_measId;
}

uint8_t
OranReportNrGnbMeasReport::GetEventId() const
{
    NS_LOG_FUNCTION(this);

    return m_eventId;
}

uint16_t
OranReportNrGnbMeasReport::GetCellId() const
{
    NS_LOG_FUNCTION(this);

    return m_cellId;
}

uint8_t
OranReportNrGnbMeasReport::GetRsrpResult() const
{
    NS_LOG_FUNCTION(this);

    return m_rsrpResult;
}

uint8_t
OranReportNrGnbMeasReport::GetRsrqResult() const
{
    NS_LOG_FUNCTION(this);

    return m_rsrqResult;
}

bool
OranReportNrGnbMeasReport::GetHaveRsrpResult() const
{
    NS_LOG_FUNCTION(this);

    return m_haveRsrpResult;
}

bool
OranReportNrGnbMeasReport::GetHaveRsrqResult() const
{
    NS_LOG_FUNCTION(this);

    return m_haveRsrqResult;
}

bool
OranReportNrGnbMeasReport::GetIsServingCell() const
{
    NS_LOG_FUNCTION(this);

    return m_isServingCell;
}

} // namespace ns3
