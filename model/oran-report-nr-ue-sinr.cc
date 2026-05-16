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

#include "oran-report-nr-ue-sinr.h"

#include "oran-report.h"

#include "ns3/abort.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OranReportNrUeSinr");
NS_OBJECT_ENSURE_REGISTERED(OranReportNrUeSinr);

TypeId
OranReportNrUeSinr::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::OranReportNrUeSinr")
            .SetParent<OranReport>()
            .AddConstructor<OranReportNrUeSinr>()
            .AddAttribute("CellId",
                          "The cell ID.",
                          UintegerValue(),
                          MakeUintegerAccessor(&OranReportNrUeSinr::m_cellId),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("Rnti",
                          "The RNTI.",
                          UintegerValue(),
                          MakeUintegerAccessor(&OranReportNrUeSinr::m_rnti),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("Sinr",
                          "The DL CTRL SINR (linear).",
                          DoubleValue(),
                          MakeDoubleAccessor(&OranReportNrUeSinr::m_sinr),
                          MakeDoubleChecker<double>())
            .AddAttribute("BwpId",
                          "The bandwidth part ID.",
                          UintegerValue(),
                          MakeUintegerAccessor(&OranReportNrUeSinr::m_bwpId),
                          MakeUintegerChecker<uint16_t>());

    return tid;
}

OranReportNrUeSinr::OranReportNrUeSinr()
{
    NS_LOG_FUNCTION(this);
}

OranReportNrUeSinr::~OranReportNrUeSinr()
{
    NS_LOG_FUNCTION(this);
}

std::string
OranReportNrUeSinr::ToString() const
{
    NS_LOG_FUNCTION(this);

    std::stringstream ss;
    Time time = GetTime();

    ss << "OranReportNrUeSinr("
       << "E2NodeId=" << GetReporterE2NodeId() << ";Time=" << time.As(Time::S)
       << ";Cell ID=" << +m_cellId << ";RNTI=" << +m_rnti << ";SINR=" << m_sinr
       << ";BWP ID=" << +m_bwpId << ")";

    return ss.str();
}

uint16_t
OranReportNrUeSinr::GetCellId() const
{
    NS_LOG_FUNCTION(this);

    return m_cellId;
}

uint16_t
OranReportNrUeSinr::GetRnti() const
{
    NS_LOG_FUNCTION(this);

    return m_rnti;
}

double
OranReportNrUeSinr::GetSinr() const
{
    NS_LOG_FUNCTION(this);

    return m_sinr;
}

uint16_t
OranReportNrUeSinr::GetBwpId() const
{
    NS_LOG_FUNCTION(this);

    return m_bwpId;
}

} // namespace ns3
