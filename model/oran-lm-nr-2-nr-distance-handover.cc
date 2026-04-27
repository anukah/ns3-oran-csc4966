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

#include "oran-lm-nr-2-nr-distance-handover.h"

#include "oran-command-nr-2-nr-handover.h"
#include "oran-data-repository.h"

#include "ns3/abort.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"

#include <cfloat>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OranLmNr2NrDistanceHandover");

NS_OBJECT_ENSURE_REGISTERED(OranLmNr2NrDistanceHandover);

TypeId
OranLmNr2NrDistanceHandover::GetTypeId()
{
    static TypeId tid = TypeId("ns3::OranLmNr2NrDistanceHandover")
                            .SetParent<OranLm>()
                            .AddConstructor<OranLmNr2NrDistanceHandover>();

    return tid;
}

OranLmNr2NrDistanceHandover::OranLmNr2NrDistanceHandover()
    : OranLm()
{
    NS_LOG_FUNCTION(this);

    m_name = "OranLmNr2NrDistanceHandover";
}

OranLmNr2NrDistanceHandover::~OranLmNr2NrDistanceHandover()
{
    NS_LOG_FUNCTION(this);
}

std::vector<Ptr<OranCommand>>
OranLmNr2NrDistanceHandover::Run()
{
    NS_LOG_FUNCTION(this);

    std::vector<Ptr<OranCommand>> commands;

    if (m_active)
    {
        NS_ABORT_MSG_IF(m_nearRtRic == nullptr,
                        "Attempting to run LM (" + m_name + ") with NULL Near-RT RIC");

        Ptr<OranDataRepository> data = m_nearRtRic->Data();
        std::vector<UeInfo> ueInfos = GetUeInfos(data);
        std::vector<GnbInfo> gnbInfos = GetGnbInfos(data);
        commands = GetHandoverCommands(data, ueInfos, gnbInfos);
    }

    return commands;
}

std::vector<OranLmNr2NrDistanceHandover::UeInfo>
OranLmNr2NrDistanceHandover::GetUeInfos(Ptr<OranDataRepository> data) const
{
    NS_LOG_FUNCTION(this << data);

    std::vector<UeInfo> ueInfos;
    for (auto ueId : data->GetNrUeE2NodeIds())
    {
        UeInfo ueInfo;
        ueInfo.nodeId = ueId;
        bool found;
        std::tie(found, ueInfo.cellId, ueInfo.rnti) = data->GetNrUeCellInfo(ueInfo.nodeId);
        if (found)
        {
            std::map<Time, Vector> nodePositions =
                data->GetNodePositions(ueInfo.nodeId, Seconds(0), Simulator::Now());

            if (!nodePositions.empty())
            {
                ueInfo.position = nodePositions.rbegin()->second;
                ueInfos.push_back(ueInfo);
            }
            else
            {
                NS_LOG_INFO("Could not find NR UE location for E2 Node ID = " << ueInfo.nodeId);
            }
        }
        else
        {
            NS_LOG_INFO("Could not find NR UE cell info for E2 Node ID = " << ueInfo.nodeId);
        }
    }
    return ueInfos;
}

std::vector<OranLmNr2NrDistanceHandover::GnbInfo>
OranLmNr2NrDistanceHandover::GetGnbInfos(Ptr<OranDataRepository> data) const
{
    NS_LOG_FUNCTION(this << data);

    std::vector<GnbInfo> gnbInfos;
    for (auto gnbId : data->GetNrGnbE2NodeIds())
    {
        GnbInfo gnbInfo;
        gnbInfo.nodeId = gnbId;
        bool found;
        std::tie(found, gnbInfo.cellId) = data->GetNrGnbCellInfo(gnbInfo.nodeId);
        if (found)
        {
            std::map<Time, Vector> nodePositions =
                data->GetNodePositions(gnbInfo.nodeId, Seconds(0), Simulator::Now());

            if (!nodePositions.empty())
            {
                gnbInfo.position = nodePositions.rbegin()->second;
                gnbInfos.push_back(gnbInfo);
            }
            else
            {
                NS_LOG_INFO("Could not find NR gNB location for E2 Node ID = " << gnbInfo.nodeId);
            }
        }
        else
        {
            NS_LOG_INFO("Could not find NR gNB cell info for E2 Node ID = " << gnbInfo.nodeId);
        }
    }
    return gnbInfos;
}

std::vector<Ptr<OranCommand>>
OranLmNr2NrDistanceHandover::GetHandoverCommands(
    Ptr<OranDataRepository> data,
    std::vector<OranLmNr2NrDistanceHandover::UeInfo> ueInfos,
    std::vector<OranLmNr2NrDistanceHandover::GnbInfo> gnbInfos) const
{
    NS_LOG_FUNCTION(this << data);

    std::vector<Ptr<OranCommand>> commands;

    for (auto ueInfo : ueInfos)
    {
        double min = DBL_MAX;
        uint64_t oldCellNodeId;
        uint16_t newCellId = ueInfo.cellId;
        for (const auto& gnbInfo : gnbInfos)
        {
            double dist = std::sqrt(std::pow(ueInfo.position.x - gnbInfo.position.x, 2) +
                                    std::pow(ueInfo.position.y - gnbInfo.position.y, 2) +
                                    std::pow(ueInfo.position.z - gnbInfo.position.z, 2));

            LogLogicToRepository("Distance from UE with RNTI " + std::to_string(ueInfo.rnti) +
                                 " in CellID " + std::to_string(ueInfo.cellId) +
                                 " to gNB with CellID " + std::to_string(gnbInfo.cellId) + " is " +
                                 std::to_string(dist));

            if (dist < min)
            {
                min = dist;
                newCellId = gnbInfo.cellId;

                LogLogicToRepository("Distance to gNB with CellID " +
                                     std::to_string(gnbInfo.cellId) + " is shortest so far");
            }

            if (ueInfo.cellId == gnbInfo.cellId)
            {
                oldCellNodeId = gnbInfo.nodeId;
            }
        }

        if (newCellId != ueInfo.cellId)
        {
            Ptr<OranCommandNr2NrHandover> handoverCommand =
                CreateObject<OranCommandNr2NrHandover>();
            handoverCommand->SetAttribute("TargetE2NodeId", UintegerValue(oldCellNodeId));
            handoverCommand->SetAttribute("TargetRnti", UintegerValue(ueInfo.rnti));
            handoverCommand->SetAttribute("TargetCellId", UintegerValue(newCellId));
            data->LogCommandLm(m_name, handoverCommand);
            commands.push_back(handoverCommand);

            LogLogicToRepository("Closest gNB (CellID " + std::to_string(newCellId) + ")" +
                                 " is different than the currently attached gNB" + " (CellID " +
                                 std::to_string(ueInfo.cellId) + ")." +
                                 " Issuing handover command.");
        }
    }
    return commands;
}

} // namespace ns3
