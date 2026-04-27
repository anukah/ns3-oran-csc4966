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

#ifndef ORAN_COMMAND_LTE_2_NR_HANDOVER_H
#define ORAN_COMMAND_LTE_2_NR_HANDOVER_H

#include "oran-command.h"

namespace ns3
{

/**
 * @ingroup oran
 *
 * RIC command instructing the LTE eNB master node (NSA EN-DC) to add an
 * NR Secondary Cell Group (SCG) for a UE — i.e., return a UE from
 * LTE-only back to EN-DC. In NSA architecture the LTE eNB is the Master
 * Node; this command is therefore targeted at the LTE eNB E2 terminator.
 *
 * Carries the LTE RNTI of the UE and the NR cell ID of the target gNB SCG.
 */
class OranCommandLte2NrHandover : public OranCommand
{
  public:
    static TypeId GetTypeId();
    OranCommandLte2NrHandover();
    ~OranCommandLte2NrHandover() override;

    std::string ToString() const override;

    /**
     * Gets the LTE RNTI of the UE to add to the NR SCG.
     */
    uint16_t GetTargetRnti() const;
    /**
     * Gets the NR cell ID of the target gNB to add as SCG.
     */
    uint16_t GetTargetNrCellId() const;

  private:
    uint16_t m_targetRnti;     //!< LTE RNTI of the UE
    uint16_t m_targetNrCellId; //!< NR cell ID of the target gNB SCG
}; // class OranCommandLte2NrHandover

} // namespace ns3

#endif /* ORAN_COMMAND_LTE_2_NR_HANDOVER_H */
