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

#include "oran-lm-inter-rat-rsrp-handover.h"

#include "oran-command-lte-2-nr-handover.h"
#include "oran-command-nr-2-lte-handover.h"
#include "oran-data-repository.h"

#include "ns3/abort.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/uinteger.h"

#include <cfloat>
#include <tuple>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OranLmInterRatRsrpHandover");

NS_OBJECT_ENSURE_REGISTERED(OranLmInterRatRsrpHandover);

TypeId
OranLmInterRatRsrpHandover::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::OranLmInterRatRsrpHandover")
            .SetParent<OranLm>()
            .AddConstructor<OranLmInterRatRsrpHandover>()
            .AddAttribute("ThresholdA2Nr",
                          "NR serving RSRP (dBm) below which NR SCG release is triggered "
                          "(equivalent to 3GPP A2 event). Hysteresis is subtracted before "
                          "comparison.",
                          DoubleValue(-110.0),
                          MakeDoubleAccessor(&OranLmInterRatRsrpHandover::m_thresholdA2Nr),
                          MakeDoubleChecker<double>())
            .AddAttribute("ThresholdA1Nr",
                          "NR serving RSRP (dBm) above which NR SCG re-add is triggered "
                          "(equivalent to 3GPP A1 event). Hysteresis is added before comparison.",
                          DoubleValue(-95.0),
                          MakeDoubleAccessor(&OranLmInterRatRsrpHandover::m_thresholdA1Nr),
                          MakeDoubleChecker<double>())
            .AddAttribute("HysteresisDb",
                          "Hysteresis margin (dB) applied to both thresholds to prevent "
                          "ping-pong between NR and LTE.",
                          DoubleValue(3.0),
                          MakeDoubleAccessor(&OranLmInterRatRsrpHandover::m_hysteresisDb),
                          MakeDoubleChecker<double>(0.0, 30.0));

    return tid;
}

OranLmInterRatRsrpHandover::OranLmInterRatRsrpHandover()
    : OranLm()
{
    NS_LOG_FUNCTION(this);
    m_name = "OranLmInterRatRsrpHandover";
}

OranLmInterRatRsrpHandover::~OranLmInterRatRsrpHandover()
{
    NS_LOG_FUNCTION(this);
}

std::vector<Ptr<OranCommand>>
OranLmInterRatRsrpHandover::Run()
{
    NS_LOG_FUNCTION(this);

    std::vector<Ptr<OranCommand>> commands;

    if (!m_active)
    {
        return commands;
    }

    NS_ABORT_MSG_IF(m_nearRtRic == nullptr,
                    "Attempting to run LM (" + m_name + ") with NULL Near-RT RIC");

    Ptr<OranDataRepository> data = m_nearRtRic->Data();

    // Find the LTE eNB master node. In NSA all inter-RAT commands target it.
    std::vector<uint64_t> lteEnbIds = data->GetLteEnbE2NodeIds();
    if (lteEnbIds.empty())
    {
        NS_LOG_WARN("No LTE eNB registered with RIC — cannot issue inter-RAT commands");
        return commands;
    }
    uint64_t lteEnbE2NodeId = lteEnbIds.front();

    // Get LTE UE registrations to retrieve LTE RNTI (used in command payload).
    // Assumes 1:1 correspondence with NR UEs (single UE scenario).
    std::vector<uint64_t> lteUeIds = data->GetLteUeE2NodeIds();

    for (uint64_t nrUeId : data->GetNrUeE2NodeIds())
    {
        // Get the NR UE's current cell info (cell ID + RNTI on NR side).
        bool nrInfoFound;
        uint16_t nrCellId;
        uint16_t nrRnti;
        std::tie(nrInfoFound, nrCellId, nrRnti) = data->GetNrUeCellInfo(nrUeId);

        if (!nrInfoFound)
        {
            NS_LOG_INFO("No NR cell info for NR UE E2 node " << nrUeId << " — skipping");
            continue;
        }

        // Find the serving NR RSRP for this UE.
        double servingNrRsrp = -DBL_MAX;
        bool servingFound = false;
        for (auto& meas : data->GetNrUeRsrpRsrq(nrUeId))
        {
            uint16_t rnti;
            uint16_t cellId;
            double rsrp;
            double rsrq;
            bool isServing;
            uint8_t ccId;
            std::tie(rnti, cellId, rsrp, rsrq, isServing, ccId) = meas;
            if (isServing)
            {
                servingNrRsrp = rsrp;
                servingFound = true;
                break;
            }
        }

        if (!servingFound)
        {
            NS_LOG_INFO("No serving NR RSRP measurement for NR UE E2 node " << nrUeId
                                                                             << " — skipping");
            continue;
        }

        LogLogicToRepository("NR UE E2NodeId=" + std::to_string(nrUeId) +
                             " serving NR RSRP=" + std::to_string(servingNrRsrp) + " dBm");

        // Initialise per-UE state on first encounter (assume EN-DC active).
        if (m_ueOnNr.find(nrUeId) == m_ueOnNr.end())
        {
            m_ueOnNr[nrUeId] = true;
        }

        // Retrieve LTE RNTI. Use the first registered LTE UE (single-UE scenario).
        uint16_t lteRnti = 1;
        if (!lteUeIds.empty())
        {
            bool lteInfoFound;
            uint16_t lteCellId;
            std::tie(lteInfoFound, lteCellId, lteRnti) = data->GetLteUeCellInfo(lteUeIds.front());
            if (!lteInfoFound)
            {
                NS_LOG_INFO("No LTE cell info yet — using default RNTI=1");
                lteRnti = 1;
            }
        }

        if (m_ueOnNr[nrUeId])
        {
            // UE currently on EN-DC. Check fallback condition: A2 with hysteresis.
            double fallbackThreshold = m_thresholdA2Nr - m_hysteresisDb;
            if (servingNrRsrp < fallbackThreshold)
            {
                NS_LOG_INFO("NR UE " << nrUeId << ": RSRP=" << servingNrRsrp
                                     << " dBm < " << fallbackThreshold
                                     << " dBm — issuing NR→LTE SCG release");

                LogLogicToRepository("NR RSRP=" + std::to_string(servingNrRsrp) +
                                     " dBm below fallback threshold=" +
                                     std::to_string(fallbackThreshold) +
                                     " dBm. Issuing OranCommandNr2LteHandover.");

                Ptr<OranCommandNr2LteHandover> cmd = CreateObject<OranCommandNr2LteHandover>();
                cmd->SetAttribute("TargetE2NodeId", UintegerValue(lteEnbE2NodeId));
                cmd->SetAttribute("TargetRnti", UintegerValue(lteRnti));
                data->LogCommandLm(m_name, cmd);
                commands.push_back(cmd);
                m_ueOnNr[nrUeId] = false;
            }
        }
        else
        {
            // UE currently on LTE-only. Check SCG re-add condition: A1 with hysteresis.
            double returnThreshold = m_thresholdA1Nr + m_hysteresisDb;
            if (servingNrRsrp > returnThreshold)
            {
                NS_LOG_INFO("NR UE " << nrUeId << ": RSRP=" << servingNrRsrp
                                     << " dBm > " << returnThreshold
                                     << " dBm — issuing LTE→NR SCG re-add");

                LogLogicToRepository("NR RSRP=" + std::to_string(servingNrRsrp) +
                                     " dBm above return threshold=" +
                                     std::to_string(returnThreshold) +
                                     " dBm. Issuing OranCommandLte2NrHandover.");

                Ptr<OranCommandLte2NrHandover> cmd = CreateObject<OranCommandLte2NrHandover>();
                cmd->SetAttribute("TargetE2NodeId", UintegerValue(lteEnbE2NodeId));
                cmd->SetAttribute("TargetRnti", UintegerValue(lteRnti));
                cmd->SetAttribute("TargetNrCellId", UintegerValue(nrCellId));
                data->LogCommandLm(m_name, cmd);
                commands.push_back(cmd);
                m_ueOnNr[nrUeId] = true;
            }
        }
    }

    return commands;
}

} // namespace ns3
