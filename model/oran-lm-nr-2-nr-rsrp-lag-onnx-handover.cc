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

#include "oran-lm-nr-2-nr-rsrp-lag-onnx-handover.h"

#include "oran-command-nr-2-nr-handover.h"
#include "oran-data-repository.h"

#include "ns3/abort.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

#include <cfloat>
#include <fstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OranLmNr2NrRsrpLagOnnxHandover");

NS_OBJECT_ENSURE_REGISTERED(OranLmNr2NrRsrpLagOnnxHandover);

TypeId
OranLmNr2NrRsrpLagOnnxHandover::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::OranLmNr2NrRsrpLagOnnxHandover")
            .SetParent<OranLm>()
            .AddConstructor<OranLmNr2NrRsrpLagOnnxHandover>()
            .AddAttribute("OnnxModelPath",
                          "The file path of the ONNX ML model.",
                          StringValue("contrib/oran/examples/vienna_ho_rf_lag.onnx"),
                          MakeStringAccessor(&OranLmNr2NrRsrpLagOnnxHandover::SetOnnxModelPath),
                          MakeStringChecker())
            .AddAttribute("NumLags",
                          "Number of lagged RSRP differences appended to the feature vector. "
                          "The model input width is this plus one, and the lags are spaced by "
                          "the Near-RT RIC's LmQueryInterval rather than by a fixed time.",
                          UintegerValue(5),
                          MakeUintegerAccessor(&OranLmNr2NrRsrpLagOnnxHandover::m_numLags),
                          MakeUintegerChecker<uint32_t>(0, 64))
            .AddAttribute("DecisionThreshold",
                          "Probability of class 1 above which a handover Command is issued.",
                          DoubleValue(0.5),
                          MakeDoubleAccessor(
                              &OranLmNr2NrRsrpLagOnnxHandover::m_decisionThreshold),
                          MakeDoubleChecker<double>(0.0, 1.0));

    return tid;
}

OranLmNr2NrRsrpLagOnnxHandover::OranLmNr2NrRsrpLagOnnxHandover()
{
    NS_LOG_FUNCTION(this);

    m_name = "OranLmNr2NrRsrpLagOnnxHandover";
}

OranLmNr2NrRsrpLagOnnxHandover::~OranLmNr2NrRsrpLagOnnxHandover()
{
    NS_LOG_FUNCTION(this);
}

void
OranLmNr2NrRsrpLagOnnxHandover::SetOnnxModelPath(const std::string& onnxModelPath)
{
    NS_LOG_FUNCTION(this << onnxModelPath);

    std::ifstream f(onnxModelPath.c_str());
    NS_ABORT_MSG_IF(!f.good(),
                    "ONNX model file \""
                        << onnxModelPath << "\" not found."
                        << " Generate it with examples/vienna_ho_rf_lag.py and copy it to the"
                        << " working directory.");
    f.close();

    m_session = Ort::Session(m_env, onnxModelPath.c_str(), Ort::SessionOptions{});

    NS_ABORT_MSG_IF(m_session.GetOutputCount() < 2,
                    "The ONNX model exposes " << m_session.GetOutputCount()
                                              << " output(s); this Logic Module needs a second"
                                                 " output holding class probabilities. Export"
                                                 " with ZipMap disabled.");
}

std::vector<Ptr<OranCommand>>
OranLmNr2NrRsrpLagOnnxHandover::Run()
{
    NS_LOG_FUNCTION(this);

    std::vector<Ptr<OranCommand>> commands;

    if (m_active)
    {
        NS_ABORT_MSG_IF(m_nearRtRic == nullptr,
                        "Attempting to run LM (" + m_name + ") with NULL Near-RT RIC");

        Ptr<OranDataRepository> data = m_nearRtRic->Data();
        std::vector<GnbInfo> gnbInfos = GetGnbInfos(data);

        for (const auto& ueInfo : GetUeInfos(data))
        {
            Ptr<OranCommand> command = EvaluateUe(data, ueInfo, gnbInfos);

            if (command != nullptr)
            {
                commands.push_back(command);
            }
        }
    }

    return commands;
}

std::vector<OranLmNr2NrRsrpLagOnnxHandover::UeInfo>
OranLmNr2NrRsrpLagOnnxHandover::GetUeInfos(Ptr<OranDataRepository> data) const
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
            ueInfos.push_back(ueInfo);
        }
        else
        {
            NS_LOG_INFO("Could not find NR UE cell info for E2 Node ID = " << ueInfo.nodeId);
        }
    }
    return ueInfos;
}

std::vector<OranLmNr2NrRsrpLagOnnxHandover::GnbInfo>
OranLmNr2NrRsrpLagOnnxHandover::GetGnbInfos(Ptr<OranDataRepository> data) const
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
            gnbInfos.push_back(gnbInfo);
        }
        else
        {
            NS_LOG_INFO("Could not find NR gNB cell info for E2 Node ID = " << gnbInfo.nodeId);
        }
    }
    return gnbInfos;
}

Ptr<OranCommand>
OranLmNr2NrRsrpLagOnnxHandover::EvaluateUe(Ptr<OranDataRepository> data,
                                           const UeInfo& ueInfo,
                                           const std::vector<GnbInfo>& gnbInfos)
{
    NS_LOG_FUNCTION(this << data << ueInfo.nodeId);

    // Separate the serving cell from the strongest neighbour.
    double servingRsrp = -DBL_MAX;
    double bestNeighborRsrp = -DBL_MAX;
    uint16_t bestNeighborCellId = ueInfo.cellId;

    for (const auto& measurement : data->GetNrUeRsrpRsrq(ueInfo.nodeId))
    {
        uint16_t rnti;
        uint16_t cellId;
        double rsrp;
        double rsrq;
        bool isServingCell;
        uint8_t componentCarrierId;
        std::tie(rnti, cellId, rsrp, rsrq, isServingCell, componentCarrierId) = measurement;

        if (isServingCell || cellId == ueInfo.cellId)
        {
            servingRsrp = rsrp;
        }
        else if (rsrp > bestNeighborRsrp)
        {
            bestNeighborRsrp = rsrp;
            bestNeighborCellId = cellId;
        }
    }

    if (servingRsrp <= -DBL_MAX || bestNeighborCellId == ueInfo.cellId)
    {
        LogLogicToRepository("Insufficient RSRP data for UE E2NodeId=" +
                             std::to_string(ueInfo.nodeId) + ", skipping");
        return nullptr;
    }

    // The one and only feature quantity: neighbour minus serving, signed, in dB.
    // Positive means the neighbour is the stronger cell. This sign convention
    // must match examples/vienna_ho_rf_lag.py.
    const double diff = bestNeighborRsrp - servingRsrp;

    // The history is appended only when a usable measurement exists, so a gap
    // in reporting shortens the wall-clock span of the window rather than
    // leaving a hole in it.
    std::deque<double>& history = m_diffHistory[ueInfo.nodeId];
    history.push_front(diff);
    while (history.size() > m_numLags + 1)
    {
        history.pop_back();
    }

    if (history.size() < m_numLags + 1)
    {
        LogLogicToRepository("Only " + std::to_string(history.size()) + " of " +
                             std::to_string(m_numLags + 1) +
                             " samples collected for UE E2NodeId=" +
                             std::to_string(ueInfo.nodeId) + ", waiting");
        return nullptr;
    }

    // Feature vector: [d(t), d(t-1), ..., d(t-NumLags)].
    std::vector<float> inputv;
    inputv.reserve(history.size());
    for (const double d : history)
    {
        inputv.push_back(static_cast<float>(d));
    }

    const std::array<int64_t, 2> inputShape = {1, static_cast<int64_t>(inputv.size())};
    const auto inputTensor = Ort::Value::CreateTensor<float>(m_memoryInfo,
                                                             inputv.data(),
                                                             inputv.size(),
                                                             inputShape.data(),
                                                             inputShape.size());

    const auto inputName = m_session.GetInputNameAllocated(0UL, m_allocator);
    const std::array<const char*, 1> inputNames{inputName.get()};

    const auto labelName = m_session.GetOutputNameAllocated(0UL, m_allocator);
    const auto probName = m_session.GetOutputNameAllocated(1UL, m_allocator);
    const std::array<const char*, 2> outputNames{labelName.get(), probName.get()};

    const auto output = m_session.Run(Ort::RunOptions{},
                                      inputNames.data(),
                                      &inputTensor,
                                      1UL,
                                      outputNames.data(),
                                      outputNames.size());

    // Output 1 is the [1, 2] probability tensor; column 1 is class "handover".
    const float* probabilities = output[1].GetTensorData<float>();
    const double pHandover = probabilities[1];

    std::string features;
    for (const float v : inputv)
    {
        features += (features.empty() ? "" : ", ") + std::to_string(v);
    }
    LogLogicToRepository("UE E2NodeId=" + std::to_string(ueInfo.nodeId) + " RNTI=" +
                         std::to_string(ueInfo.rnti) + " diffs=[" + features + "] p(HO)=" +
                         std::to_string(pHandover));

    if (pHandover <= m_decisionThreshold)
    {
        return nullptr;
    }

    // The handover Command is executed by the serving gNB, so it is addressed
    // to the E2 node of the cell the UE is currently attached to.
    uint64_t servingGnbNodeId = UINT64_MAX;
    for (const auto& gnbInfo : gnbInfos)
    {
        if (gnbInfo.cellId == ueInfo.cellId)
        {
            servingGnbNodeId = gnbInfo.nodeId;
            break;
        }
    }

    if (servingGnbNodeId == UINT64_MAX)
    {
        LogLogicToRepository("Could not find serving gNB E2 Node ID for CellID=" +
                             std::to_string(ueInfo.cellId) + ", skipping");
        return nullptr;
    }

    Ptr<OranCommandNr2NrHandover> handoverCommand = CreateObject<OranCommandNr2NrHandover>();
    handoverCommand->SetAttribute("TargetE2NodeId", UintegerValue(servingGnbNodeId));
    handoverCommand->SetAttribute("TargetRnti", UintegerValue(ueInfo.rnti));
    handoverCommand->SetAttribute("TargetCellId", UintegerValue(bestNeighborCellId));
    data->LogCommandLm(m_name, handoverCommand);

    LogLogicToRepository("HANDOVER: UE E2NodeId=" + std::to_string(ueInfo.nodeId) +
                         " from CellID=" + std::to_string(ueInfo.cellId) + " to CellID=" +
                         std::to_string(bestNeighborCellId) + " with p(HO)=" +
                         std::to_string(pHandover));

    return handoverCommand;
}

} // namespace ns3
