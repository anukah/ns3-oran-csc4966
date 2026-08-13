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

#include "oran-lm-nr-2-nr-a2-a4-rsrp-handover.h"

#include "oran-command-nr-2-nr-handover.h"
#include "oran-data-repository.h"
#include "oran-report-nr-gnb-meas-report.h"

#include "ns3/abort.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OranLmNr2NrA2A4RsrpHandover");
NS_OBJECT_ENSURE_REGISTERED(OranLmNr2NrA2A4RsrpHandover);

TypeId
OranLmNr2NrA2A4RsrpHandover::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::OranLmNr2NrA2A4RsrpHandover")
            .SetParent<OranLm>()
            .AddConstructor<OranLmNr2NrA2A4RsrpHandover>()
            .AddAttribute("NeighbourCellOffset",
                          "Minimum offset between the serving and the best neighbour "
                          "cell to trigger the handover. Expressed in quantized "
                          "range of [0..127] as per Table 10.1.6.1-1 of 3GPP TS 38.133.",
                          UintegerValue(1),
                          MakeUintegerAccessor(
                              &OranLmNr2NrA2A4RsrpHandover::m_neighbourCellOffset),
                          MakeUintegerChecker<uint8_t>())
            .AddAttribute("MaxReportAge",
                          "Measurement Reports older than this are ignored. This has no "
                          "equivalent in the gNB handover algorithm, which never ages the "
                          "neighbour cell measurements that it caches.",
                          TimeValue(Seconds(1)),
                          MakeTimeAccessor(&OranLmNr2NrA2A4RsrpHandover::m_maxReportAge),
                          MakeTimeChecker());

    return tid;
}

OranLmNr2NrA2A4RsrpHandover::OranLmNr2NrA2A4RsrpHandover()
    : OranLm()
{
    NS_LOG_FUNCTION(this);

    m_name = "OranLmNr2NrA2A4RsrpHandover";
}

OranLmNr2NrA2A4RsrpHandover::~OranLmNr2NrA2A4RsrpHandover()
{
    NS_LOG_FUNCTION(this);
}

std::vector<Ptr<OranCommand>>
OranLmNr2NrA2A4RsrpHandover::Run()
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
OranLmNr2NrA2A4RsrpHandover::GetGnbCellIds(Ptr<OranDataRepository> data) const
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
OranLmNr2NrA2A4RsrpHandover::EvaluateHandover(
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

    // First condition: the UE reported that the serving cell became worse than
    // the Event A2 threshold. The threshold itself is part of the reporting
    // configuration installed on the gNB and is evaluated by the UE RRC, so the
    // arrival of the report is the condition.
    auto a2Entries = data->GetNrGnbMeasReport(ueE2NodeId,
                                              OranReportNrGnbMeasReport::EVENT_A2,
                                              fromTime,
                                              now);

    if (a2Entries.empty())
    {
        LogLogicToRepository("No recent Event A2 report for E2 Node ID " +
                             std::to_string(ueE2NodeId) + ", skipping");
        return nullptr;
    }

    bool haveServingRsrp = false;
    uint16_t a2CellId = 0;
    uint8_t servingCellRsrp = 0;

    for (const auto& entry : a2Entries)
    {
        if (std::get<5>(entry))
        {
            a2CellId = std::get<2>(entry);
            servingCellRsrp = std::get<3>(entry);
            haveServingRsrp = true;
            break;
        }
    }

    if (!haveServingRsrp)
    {
        LogLogicToRepository("Event A2 report for E2 Node ID " + std::to_string(ueE2NodeId) +
                             " has no serving cell measurement, skipping");
        return nullptr;
    }

    // The Measurement Report describes the cell that the UE was attached to when
    // it was sent. If the UE has moved since, the report no longer describes the
    // radio conditions of the serving cell, so it is discarded.
    if (a2CellId != servingCellId)
    {
        LogLogicToRepository("Event A2 report for E2 Node ID " + std::to_string(ueE2NodeId) +
                             " is for CellID " + std::to_string(a2CellId) +
                             " but the UE is attached to CellID " +
                             std::to_string(servingCellId) + ", skipping");
        return nullptr;
    }

    // Second condition: the best neighbour cell learned from Event A4 reports
    // exceeds the serving cell by at least the configured offset.
    auto a4Entries = data->GetNrGnbMeasReport(ueE2NodeId,
                                              OranReportNrGnbMeasReport::EVENT_A4,
                                              fromTime,
                                              now);

    uint16_t bestNeighbourCellId = 0;
    uint8_t bestNeighbourRsrp = 0;
    bool haveNeighbour = false;

    for (const auto& entry : a4Entries)
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
        LogLogicToRepository("No recent Event A4 neighbour measurement for E2 Node ID " +
                             std::to_string(ueE2NodeId) + ", skipping");
        return nullptr;
    }

    LogLogicToRepository("Best neighbour of E2 Node ID " + std::to_string(ueE2NodeId) +
                         " is CellID " + std::to_string(bestNeighbourCellId) + " with RSRP " +
                         std::to_string(+bestNeighbourRsrp) + " against serving CellID " +
                         std::to_string(servingCellId) + " with RSRP " +
                         std::to_string(+servingCellRsrp));

    if (static_cast<int>(bestNeighbourRsrp) - static_cast<int>(servingCellRsrp) <
        static_cast<int>(m_neighbourCellOffset))
    {
        LogLogicToRepository("Offset to CellID " + std::to_string(bestNeighbourCellId) +
                             " is below NeighbourCellOffset (" +
                             std::to_string(+m_neighbourCellOffset) + "), no handover");
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

    LogLogicToRepository("Both Event A2 and Event A4 conditions hold." +
                         std::string(" Issuing handover of E2 Node ID ") +
                         std::to_string(ueE2NodeId) + " from CellID " +
                         std::to_string(servingCellId) + " to CellID " +
                         std::to_string(bestNeighbourCellId));

    return handoverCommand;
}

} // namespace ns3
