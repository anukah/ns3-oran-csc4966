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

#include "oran-lm-nr-2-nr-rsrp-sinr-handover.h"

#include "oran-command-nr-2-nr-handover.h"
#include "oran-data-repository.h"

#include "ns3/abort.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"

#include <cfloat>
#include <cmath>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OranLmNr2NrRsrpSinrHandover");

NS_OBJECT_ENSURE_REGISTERED(OranLmNr2NrRsrpSinrHandover);

TypeId
OranLmNr2NrRsrpSinrHandover::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::OranLmNr2NrRsrpSinrHandover")
            .SetParent<OranLm>()
            .AddConstructor<OranLmNr2NrRsrpSinrHandover>()
            .AddAttribute("SinrThresholdDb",
                          "Serving-cell SINR (dB) below which handover is considered. "
                          "If serving SINR is above this value, no handover is triggered.",
                          DoubleValue(5.0),
                          MakeDoubleAccessor(&OranLmNr2NrRsrpSinrHandover::m_sinrThresholdDb),
                          MakeDoubleChecker<double>())
            .AddAttribute("HysteresisDb",
                          "RSRP hysteresis margin (dB). A neighbor must exceed the serving "
                          "cell RSRP by this margin to trigger a handover.",
                          DoubleValue(3.0),
                          MakeDoubleAccessor(&OranLmNr2NrRsrpSinrHandover::m_hysteresisDb),
                          MakeDoubleChecker<double>(0.0));

    return tid;
}

OranLmNr2NrRsrpSinrHandover::OranLmNr2NrRsrpSinrHandover()
    : OranLm(),
      m_sinrThresholdDb(5.0),
      m_hysteresisDb(3.0)
{
    NS_LOG_FUNCTION(this);

    m_name = "OranLmNr2NrRsrpSinrHandover";
}

OranLmNr2NrRsrpSinrHandover::~OranLmNr2NrRsrpSinrHandover()
{
    NS_LOG_FUNCTION(this);
}

std::vector<Ptr<OranCommand>>
OranLmNr2NrRsrpSinrHandover::Run()
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

std::vector<OranLmNr2NrRsrpSinrHandover::UeInfo>
OranLmNr2NrRsrpSinrHandover::GetUeInfos(Ptr<OranDataRepository> data) const
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

std::vector<OranLmNr2NrRsrpSinrHandover::GnbInfo>
OranLmNr2NrRsrpSinrHandover::GetGnbInfos(Ptr<OranDataRepository> data) const
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
OranLmNr2NrRsrpSinrHandover::GetHandoverCommands(
    Ptr<OranDataRepository> data,
    std::vector<OranLmNr2NrRsrpSinrHandover::UeInfo> ueInfos,
    std::vector<OranLmNr2NrRsrpSinrHandover::GnbInfo> gnbInfos) const
{
    NS_LOG_FUNCTION(this << data);

    std::vector<Ptr<OranCommand>> commands;

    for (const auto& ueInfo : ueInfos)
    {
        // ──────────────────────────────────────────────────────────
        // STEP 1: SINR GATE — check serving-cell quality
        // ──────────────────────────────────────────────────────────
        auto sinrMeasurements = data->GetNrUeSinr(ueInfo.nodeId);

        if (sinrMeasurements.empty())
        {
            LogLogicToRepository("No SINR data for UE E2NodeId=" +
                                 std::to_string(ueInfo.nodeId) + ", skipping");
            continue;
        }

        uint16_t sinrCellId;
        uint16_t sinrRnti;
        double sinrDb;
        uint16_t sinrBwpId;
        std::tie(sinrCellId, sinrRnti, sinrDb, sinrBwpId) = sinrMeasurements.back();

        LogLogicToRepository("UE RNTI=" + std::to_string(ueInfo.rnti) + " serving CellID=" +
                             std::to_string(ueInfo.cellId) + " SINR=" +
                             std::to_string(sinrDb) + " dB (threshold=" +
                             std::to_string(m_sinrThresholdDb) + " dB)");

        if (sinrDb >= m_sinrThresholdDb)
        {
            LogLogicToRepository("Serving SINR is above threshold, no handover needed");
            continue;
        }

        LogLogicToRepository("Serving SINR is BELOW threshold, evaluating RSRP for handover");

        // ──────────────────────────────────────────────────────────
        // STEP 2: RSRP COMPARISON — separate serving from neighbors
        // ──────────────────────────────────────────────────────────
        auto rsrpMeasurements = data->GetNrUeRsrpRsrq(ueInfo.nodeId);

        double servingRsrp = -DBL_MAX;
        double bestNeighborRsrp = -DBL_MAX;
        uint16_t bestNeighborCellId = ueInfo.cellId;

        for (const auto& measurement : rsrpMeasurements)
        {
            uint16_t rnti;
            uint16_t cellId;
            double rsrp;
            double rsrq;
            bool isServingCell;
            uint16_t componentCarrierId;
            std::tie(rnti, cellId, rsrp, rsrq, isServingCell, componentCarrierId) = measurement;

            if (isServingCell || cellId == ueInfo.cellId)
            {
                servingRsrp = rsrp;
                LogLogicToRepository("Serving cell CellID=" + std::to_string(cellId) +
                                     " RSRP=" + std::to_string(rsrp));
            }
            else
            {
                LogLogicToRepository("Neighbor CellID=" + std::to_string(cellId) +
                                     " RSRP=" + std::to_string(rsrp));
                if (rsrp > bestNeighborRsrp)
                {
                    bestNeighborRsrp = rsrp;
                    bestNeighborCellId = cellId;
                }
            }
        }

        // ──────────────────────────────────────────────────────────
        // STEP 3: HYSTERESIS CHECK — neighbor must be clearly better
        // ──────────────────────────────────────────────────────────
        if (bestNeighborCellId == ueInfo.cellId)
        {
            LogLogicToRepository("No neighbor with RSRP data found, skipping");
            continue;
        }

        if (bestNeighborRsrp <= servingRsrp + m_hysteresisDb)
        {
            LogLogicToRepository("Best neighbor CellID=" + std::to_string(bestNeighborCellId) +
                                 " RSRP=" + std::to_string(bestNeighborRsrp) +
                                 " does not exceed serving (" + std::to_string(servingRsrp) +
                                 ") + hysteresis (" + std::to_string(m_hysteresisDb) +
                                 " dB). No handover.");
            continue;
        }

        // ──────────────────────────────────────────────────────────
        // STEP 4: ISSUE HANDOVER — find the serving gNB's E2 node ID
        // ──────────────────────────────────────────────────────────
        uint64_t oldCellNodeId = UINT64_MAX;
        for (const auto& gnbInfo : gnbInfos)
        {
            if (ueInfo.cellId == gnbInfo.cellId)
            {
                oldCellNodeId = gnbInfo.nodeId;
                break;
            }
        }

        if (oldCellNodeId == UINT64_MAX)
        {
            LogLogicToRepository("Could not find serving gNB E2 Node ID for CellID=" +
                                 std::to_string(ueInfo.cellId) + ", skipping");
            continue;
        }

        Ptr<OranCommandNr2NrHandover> handoverCommand = CreateObject<OranCommandNr2NrHandover>();
        handoverCommand->SetAttribute("TargetE2NodeId", UintegerValue(oldCellNodeId));
        handoverCommand->SetAttribute("TargetRnti", UintegerValue(ueInfo.rnti));
        handoverCommand->SetAttribute("TargetCellId", UintegerValue(bestNeighborCellId));
        data->LogCommandLm(m_name, handoverCommand);
        commands.push_back(handoverCommand);

        LogLogicToRepository("HANDOVER: UE RNTI=" + std::to_string(ueInfo.rnti) +
                             " from CellID=" + std::to_string(ueInfo.cellId) +
                             " to CellID=" + std::to_string(bestNeighborCellId) +
                             " (SINR=" + std::to_string(sinrDb) + " dB < threshold=" +
                             std::to_string(m_sinrThresholdDb) + " dB, neighbor RSRP=" +
                             std::to_string(bestNeighborRsrp) + " > serving RSRP=" +
                             std::to_string(servingRsrp) + " + " +
                             std::to_string(m_hysteresisDb) + " dB)");
    }
    return commands;
}

} // namespace ns3
