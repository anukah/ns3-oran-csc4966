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

#ifndef ORAN_LM_INTER_RAT_RSRP_HANDOVER_H
#define ORAN_LM_INTER_RAT_RSRP_HANDOVER_H

#include "oran-lm.h"

#include <map>

namespace ns3
{

/**
 * @ingroup oran
 *
 * O-RAN Logic Module implementing inter-RAT (NR ↔ LTE) SCG add/release
 * decisions for NSA EN-DC networks, based on NR RSRP thresholds.
 *
 * Architecture (NSA EN-DC):
 *   - LTE eNB is the Master Node (MCG); NR gNB is the Secondary Node (SCG)
 *   - All inter-RAT commands are routed to the LTE eNB E2 terminator
 *   - The LTE anchor is assumed always available; only NR quality drives decisions
 *
 * Decision logic (runs every RIC QueryInterval):
 *   - NR→LTE (SCG release): NR serving RSRP < ThresholdA2Nr − HysteresisDb
 *   - LTE→NR (SCG re-add):  NR serving RSRP > ThresholdA1Nr + HysteresisDb
 *
 * Per-UE state prevents duplicate commands for the same transition.
 */
class OranLmInterRatRsrpHandover : public OranLm
{
  public:
    static TypeId GetTypeId();
    OranLmInterRatRsrpHandover();
    ~OranLmInterRatRsrpHandover() override;

    /**
     * Runs the inter-RAT handover decision logic.
     * Queries NR RSRP from the data repository for each registered NR UE,
     * compares against thresholds, and issues Nr2Lte or Lte2Nr commands
     * to the LTE eNB master as appropriate.
     *
     * @return Vector of handover commands (OranCommandNr2LteHandover or
     *         OranCommandLte2NrHandover), possibly empty.
     */
    std::vector<Ptr<OranCommand>> Run() override;

  private:
    double m_thresholdA2Nr;  //!< NR RSRP below this (minus hysteresis) → SCG release (dBm)
    double m_thresholdA1Nr;  //!< NR RSRP above this (plus hysteresis) → SCG re-add (dBm)
    double m_hysteresisDb;   //!< Hysteresis margin to prevent ping-pong (dB)

    /**
     * Per-UE RAT state: true = UE currently has NR SCG active (EN-DC),
     * false = UE is on LTE-only (SCG released). Keyed by NR UE E2 node ID.
     */
    std::map<uint64_t, bool> m_ueOnNr;
}; // class OranLmInterRatRsrpHandover

} // namespace ns3

#endif /* ORAN_LM_INTER_RAT_RSRP_HANDOVER_H */
