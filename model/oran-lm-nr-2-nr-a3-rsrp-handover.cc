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

#include "oran-lm-nr-2-nr-a3-rsrp-handover.h"

#include "oran-command-nr-2-nr-handover.h"
#include "oran-data-repository.h"
#include "oran-report-nr-gnb-meas-report.h"

#include "ns3/abort.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OranLmNr2NrA3RsrpHandover");
NS_OBJECT_ENSURE_REGISTERED(OranLmNr2NrA3RsrpHandover);

TypeId
OranLmNr2NrA3RsrpHandover::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::OranLmNr2NrA3RsrpHandover")
            .SetParent<OranLm>()
            .AddConstructor<OranLmNr2NrA3RsrpHandover>()
            .AddAttribute("MaxReportAge",
                          "Measurement Reports older than this are ignored. Because the UE "
                          "re-sends the Event A3 report every reporting interval for as long "
                          "as the condition holds, this doubles as a check that the condition "
                          "still holds, and should be set a little above the reporting "
                          "interval of the Event A3 configuration.",
                          TimeValue(Seconds(1.5)),
                          MakeTimeAccessor(&OranLmNr2NrA3RsrpHandover::m_maxReportAge),
                          MakeTimeChecker());

    return tid;
}

OranLmNr2NrA3RsrpHandover::OranLmNr2NrA3RsrpHandover()
    : OranLm()
{
    NS_LOG_FUNCTION(this);

    m_name = "OranLmNr2NrA3RsrpHandover";
}

OranLmNr2NrA3RsrpHandover::~OranLmNr2NrA3RsrpHandover()
{
    NS_LOG_FUNCTION(this);
}

std::vector<Ptr<OranCommand>>
OranLmNr2NrA3RsrpHandover::Run()
{
    NS_LOG_FUNCTION(this);

    std::vector<Ptr<OranCommand>> commands;

    if (m_active)
    {
        NS_ABORT_MSG_IF(m_nearRtRic == nullptr,
                        "Attempting to run LM (" + m_name + ") with NULL Near-RT RIC");

        Ptr<OranDataRepository> data = m_nearRtRic->Data();
        std::map<uint16_t, uint64_t> gnbCellIds = GetGnbCellIds(data);

        for (auto ueE2NodeId : data->GetNrUeE2NodeIds())
        {
            Ptr<OranCommand> command = EvaluateHandover(data, ueE2NodeId, gnbCellIds);

            if (command != nullptr)
            {
                commands.push_back(command);
            }
        }
    }

    return commands;
}

std::map<uint16_t, uint64_t>
OranLmNr2NrA3RsrpHandover::GetGnbCellIds(Ptr<OranDataRepository> data) const
{
    NS_LOG_FUNCTION(this << data);

    std::map<uint16_t, uint64_t> gnbCellIds;

    for (auto gnbE2NodeId : data->GetNrGnbE2NodeIds())
    {
        bool found;
        uint16_t cellId;
        std::tie(found, cellId) = data->GetNrGnbCellInfo(gnbE2NodeId);

        if (found)
        {
            gnbCellIds[cellId] = gnbE2NodeId;
        }
        else
        {
            NS_LOG_INFO("Could not find NR gNB cell info for E2 Node ID = " << gnbE2NodeId);
        }
    }

    return gnbCellIds;
}

Ptr<OranCommand>
OranLmNr2NrA3RsrpHandover::EvaluateHandover(
    Ptr<OranDataRepository> data,
    uint64_t ueE2NodeId,
    const std::map<uint16_t, uint64_t>& gnbCellIds) const
{
    NS_LOG_FUNCTION(this << data << ueE2NodeId);

    bool found;
    uint16_t servingCellId;
    uint16_t rnti;
    std::tie(found, servingCellId, rnti) = data->GetNrUeCellInfo(ueE2NodeId);

    if (!found)
    {
        NS_LOG_INFO("Could not find NR UE cell info for E2 Node ID = " << ueE2NodeId);
        return nullptr;
    }

    Time now = Simulator::Now();
    Time fromTime = (now > m_maxReportAge) ? now - m_maxReportAge : Seconds(0);

    // The only condition of the algorithm. The A3 entry inequality, its
    // hysteresis and offset, and the time-to-trigger are all applied by the UE
    // RRC before the report is sent, so the existence of a recent report is the
    // condition.
    auto a3Entries = data->GetNrGnbMeasReport(ueE2NodeId,
                                              OranReportNrGnbMeasReport::EVENT_A3,
                                              fromTime,
                                              now);

    if (a3Entries.empty())
    {
        LogLogicToRepository("No recent Event A3 report for E2 Node ID " +
                             std::to_string(ueE2NodeId) + ", skipping");
        return nullptr;
    }

    // The Measurement Report describes the cell that the UE was attached to when
    // it was sent. If the UE has moved since, the report no longer describes the
    // radio conditions of the serving cell, so it is discarded.
    bool haveServingCell = false;
    uint16_t reportCellId = 0;

    for (const auto& entry : a3Entries)
    {
        if (std::get<5>(entry))
        {
            reportCellId = std::get<2>(entry);
            haveServingCell = true;
            break;
        }
    }

    if (haveServingCell && reportCellId != servingCellId)
    {
        LogLogicToRepository("Event A3 report for E2 Node ID " + std::to_string(ueE2NodeId) +
                             " is for CellID " + std::to_string(reportCellId) +
                             " but the UE is attached to CellID " +
                             std::to_string(servingCellId) + ", skipping");
        return nullptr;
    }

    // Pick the strongest reported neighbour, exactly as
    // NrA3RsrpHandoverAlgorithm::DoReportUeMeas does.
    uint16_t bestNeighbourCellId = 0;
    uint8_t bestNeighbourRsrp = 0;
    bool haveNeighbour = false;

    for (const auto& entry : a3Entries)
    {
        bool isServingCell = std::get<5>(entry);
        uint16_t cellId = std::get<2>(entry);
        uint8_t rsrp = std::get<3>(entry);

        if (isServingCell || cellId == servingCellId)
        {
            continue;
        }

        // Only cells that have a gNB registered with the RIC are valid targets.
        if (gnbCellIds.find(cellId) == gnbCellIds.end())
        {
            LogLogicToRepository("CellID " + std::to_string(cellId) +
                                 " has no registered NR gNB, not a valid handover target");
            continue;
        }

        if (!haveNeighbour || rsrp > bestNeighbourRsrp)
        {
            bestNeighbourCellId = cellId;
            bestNeighbourRsrp = rsrp;
            haveNeighbour = true;
        }
    }

    if (!haveNeighbour)
    {
        LogLogicToRepository("Event A3 report for E2 Node ID " + std::to_string(ueE2NodeId) +
                             " has no valid neighbour cell measurement, skipping");
        return nullptr;
    }

    // The handover Command is executed by the serving gNB, so it is addressed to
    // the E2 node of the cell that the UE is currently attached to.
    auto servingGnbIt = gnbCellIds.find(servingCellId);

    if (servingGnbIt == gnbCellIds.end())
    {
        LogLogicToRepository("Could not find serving gNB E2 Node ID for CellID=" +
                             std::to_string(servingCellId) + ", skipping");
        return nullptr;
    }

    Ptr<OranCommandNr2NrHandover> handoverCommand = CreateObject<OranCommandNr2NrHandover>();
    handoverCommand->SetAttribute("TargetE2NodeId", UintegerValue(servingGnbIt->second));
    handoverCommand->SetAttribute("TargetRnti", UintegerValue(rnti));
    handoverCommand->SetAttribute("TargetCellId", UintegerValue(bestNeighbourCellId));
    data->LogCommandLm(m_name, handoverCommand);

    LogLogicToRepository("Event A3 condition holds. Issuing handover of E2 Node ID " +
                         std::to_string(ueE2NodeId) + " from CellID " +
                         std::to_string(servingCellId) + " to CellID " +
                         std::to_string(bestNeighbourCellId) + " with RSRP " +
                         std::to_string(+bestNeighbourRsrp));

    return handoverCommand;
}

} // namespace ns3
