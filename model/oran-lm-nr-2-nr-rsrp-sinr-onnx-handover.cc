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

#include "oran-lm-nr-2-nr-rsrp-sinr-onnx-handover.h"

#include "oran-command-nr-2-nr-handover.h"
#include "oran-data-repository.h"

#include "ns3/abort.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

#include <cfloat>
#include <cmath>
#include <fstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OranLmNr2NrRsrpSinrOnnxHandover");

NS_OBJECT_ENSURE_REGISTERED(OranLmNr2NrRsrpSinrOnnxHandover);

TypeId
OranLmNr2NrRsrpSinrOnnxHandover::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::OranLmNr2NrRsrpSinrOnnxHandover")
            .SetParent<OranLm>()
            .AddConstructor<OranLmNr2NrRsrpSinrOnnxHandover>()
            .AddAttribute("OnnxModelPath",
                          "The file path of the ONNX ML model.",
                          StringValue("nr_rsrp_sinr_logistic.onnx"),
                          MakeStringAccessor(&OranLmNr2NrRsrpSinrOnnxHandover::SetOnnxModelPath),
                          MakeStringChecker());

    return tid;
}

OranLmNr2NrRsrpSinrOnnxHandover::OranLmNr2NrRsrpSinrOnnxHandover()
{
    NS_LOG_FUNCTION(this);

    m_name = "OranLmNr2NrRsrpSinrOnnxHandover";
}

OranLmNr2NrRsrpSinrOnnxHandover::~OranLmNr2NrRsrpSinrOnnxHandover()
{
    NS_LOG_FUNCTION(this);
}

std::vector<Ptr<OranCommand>>
OranLmNr2NrRsrpSinrOnnxHandover::Run()
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

void
OranLmNr2NrRsrpSinrOnnxHandover::SetOnnxModelPath(const std::string& onnxModelPath)
{
    std::ifstream f(onnxModelPath.c_str());
    NS_ABORT_MSG_IF(!f.good(),
                    "ONNX model file \""
                        << onnxModelPath << "\" not found."
                        << " Model \"nr_rsrp_sinr_logistic.onnx\""
                        << " can be copied from the example folder to the working directory.");
    f.close();

    m_session = Ort::Session(m_env, onnxModelPath.c_str(), Ort::SessionOptions{});
}

std::vector<OranLmNr2NrRsrpSinrOnnxHandover::UeInfo>
OranLmNr2NrRsrpSinrOnnxHandover::GetUeInfos(Ptr<OranDataRepository> data) const
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

std::vector<OranLmNr2NrRsrpSinrOnnxHandover::GnbInfo>
OranLmNr2NrRsrpSinrOnnxHandover::GetGnbInfos(Ptr<OranDataRepository> data) const
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
OranLmNr2NrRsrpSinrOnnxHandover::GetHandoverCommands(
    Ptr<OranDataRepository> data,
    std::vector<OranLmNr2NrRsrpSinrOnnxHandover::UeInfo> ueInfos,
    std::vector<OranLmNr2NrRsrpSinrOnnxHandover::GnbInfo> gnbInfos)
{
    NS_LOG_FUNCTION(this << data);

    std::vector<Ptr<OranCommand>> commands;

    for (const auto& ueInfo : ueInfos)
    {
        // Retrieve serving-cell SINR
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

        // Retrieve RSRP measurements and separate serving from neighbors
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
            }
            else
            {
                if (rsrp > bestNeighborRsrp)
                {
                    bestNeighborRsrp = rsrp;
                    bestNeighborCellId = cellId;
                }
            }
        }

        if (bestNeighborCellId == ueInfo.cellId || servingRsrp <= -DBL_MAX)
        {
            LogLogicToRepository("Insufficient RSRP data for UE E2NodeId=" +
                                 std::to_string(ueInfo.nodeId) + ", skipping");
            continue;
        }

        // Compute input features: [sinr_serving_db, rsrp_diff]
        float sinrFeature = static_cast<float>(sinrDb);
        float rsrpDiff = static_cast<float>(std::abs(servingRsrp - bestNeighborRsrp));
        std::vector<float> inputv = {sinrFeature, rsrpDiff};

        LogLogicToRepository("UE RNTI=" + std::to_string(ueInfo.rnti) +
                             " SINR=" + std::to_string(sinrDb) +
                             " dB, RSRP_diff=" + std::to_string(rsrpDiff) + " dB");

        // Run ONNX inference — shape is [1, 2] (single sample, 2 features)
        std::array<int64_t, 2> inputShape = {1, 2};
        const auto inputTensor = Ort::Value::CreateTensor<float>(m_memoryInfo,
                                                                 inputv.data(),
                                                                 inputv.size(),
                                                                 inputShape.data(),
                                                                 inputShape.size());

        const auto inputName = m_session.GetInputNameAllocated(0UL, m_allocator);
        std::array<const char*, 1> inputNames{inputName.get()};

        const auto outputName = m_session.GetOutputNameAllocated(0UL, m_allocator);
        std::array<const char*, 1> outputNames{outputName.get()};

        const auto output = m_session.Run(Ort::RunOptions{},
                                          inputNames.data(),
                                          &inputTensor,
                                          1UL,
                                          outputNames.data(),
                                          1);

        const auto outputData = output[0].GetTensorData<int64_t>();
        int64_t predictedLabel = *outputData;

        LogLogicToRepository("ML prediction for UE RNTI=" + std::to_string(ueInfo.rnti) +
                             ": label=" + std::to_string(predictedLabel));

        if (predictedLabel == 1)
        {
            // Find serving gNB E2 node ID
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

            Ptr<OranCommandNr2NrHandover> handoverCommand =
                CreateObject<OranCommandNr2NrHandover>();
            handoverCommand->SetAttribute("TargetE2NodeId", UintegerValue(oldCellNodeId));
            handoverCommand->SetAttribute("TargetRnti", UintegerValue(ueInfo.rnti));
            handoverCommand->SetAttribute("TargetCellId", UintegerValue(bestNeighborCellId));
            data->LogCommandLm(m_name, handoverCommand);
            commands.push_back(handoverCommand);

            LogLogicToRepository("HANDOVER: UE RNTI=" + std::to_string(ueInfo.rnti) +
                                 " from CellID=" + std::to_string(ueInfo.cellId) +
                                 " to CellID=" + std::to_string(bestNeighborCellId));
        }
    }

    return commands;
}

} // namespace ns3
