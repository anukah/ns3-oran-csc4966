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

#ifndef ORAN_REPORT_NR_GNB_MEAS_REPORT_H
#define ORAN_REPORT_NR_GNB_MEAS_REPORT_H

#include "oran-report.h"

#include <string>

namespace ns3
{

/**
 * @ingroup oran
 *
 * Report with a single measured cell from a 3GPP RRC Measurement Report that
 * an NR gNB received from a UE.
 *
 * A 3GPP Measurement Report carries the measurement of the primary cell plus,
 * optionally, a list of neighbour cell measurements. This Report models one
 * measured cell, so a single Measurement Report is flattened by the Reporter
 * into one Report for the primary cell (with the serving cell flag set) plus
 * one Report per neighbour cell. All of the Reports generated from the same
 * Measurement Report share the same reporter E2 node ID, time, IMSI, RNTI,
 * measurement ID, and event ID, which is how they are grouped again when read
 * back from the data repository.
 *
 * The RSRP and RSRQ values are kept in the quantized ranges used by the RRC
 * measurement report (see Table 10.1.6.1-1 of 3GPP TS 38.133), and not
 * converted to dBm or dB. These are the values that the gNB handover
 * algorithms compare, so keeping them unconverted is what allows a Logic
 * Module to reproduce a handover algorithm decision exactly.
 */
class OranReportNrGnbMeasReport : public OranReport
{
  public:
    /**
     * The measurement reporting event that triggered the Measurement Report.
     *
     * These mirror the events of Section 5.5.4 of 3GPP TS 36.331, and are
     * recorded so that a Logic Module can tell which of the reporting
     * configurations that were installed on the gNB produced the Report.
     */
    enum MeasEventId : uint8_t
    {
        EVENT_UNKNOWN = 0, //!< The event is not known to the Reporter
        EVENT_A1,          //!< Serving becomes better than threshold
        EVENT_A2,          //!< Serving becomes worse than threshold
        EVENT_A3,          //!< Neighbour becomes offset better than PCell
        EVENT_A4,          //!< Neighbour becomes better than threshold
        EVENT_A5           //!< PCell worse than threshold1 and neighbour better than threshold2
    };

    /**
     * Get the TypeId of the OranReportNrGnbMeasReport class.
     *
     * @return The TypeId.
     */
    static TypeId GetTypeId();
    /**
     * Constructor of the OranReportNrGnbMeasReport class.
     */
    OranReportNrGnbMeasReport();
    /**
     * Destructor of the OranReportNrGnbMeasReport class.
     */
    ~OranReportNrGnbMeasReport() override;
    /**
     * Get a string representation of this Report.
     *
     * @return A string representation of this Report.
     */
    std::string ToString() const override;
    /**
     * Gets the IMSI of the UE that the Measurement Report came from.
     *
     * @return The IMSI.
     */
    uint64_t GetImsi() const;
    /**
     * Gets the RNTI of the UE that the Measurement Report came from.
     *
     * @return The RNTI.
     */
    uint16_t GetRnti() const;
    /**
     * Gets the measurement ID of the reporting configuration that triggered
     * the Measurement Report.
     *
     * @return The measurement ID.
     */
    uint8_t GetMeasId() const;
    /**
     * Gets the measurement reporting event that triggered the Measurement
     * Report.
     *
     * @return The event ID.
     */
    uint8_t GetEventId() const;
    /**
     * Gets the ID of the cell that this measurement is for.
     *
     * @return The cell ID.
     */
    uint16_t GetCellId() const;
    /**
     * Gets the quantized RSRP of the measured cell.
     *
     * @return The RSRP, in the range [0..127].
     */
    uint8_t GetRsrpResult() const;
    /**
     * Gets the quantized RSRQ of the measured cell.
     *
     * @return The RSRQ, in the range [0..127].
     */
    uint8_t GetRsrqResult() const;
    /**
     * Gets the flag that indicates if the RSRP of the measured cell is valid.
     *
     * @return True, if the RSRP was included in the Measurement Report;
     *         otherwise, false.
     */
    bool GetHaveRsrpResult() const;
    /**
     * Gets the flag that indicates if the RSRQ of the measured cell is valid.
     *
     * @return True, if the RSRQ was included in the Measurement Report;
     *         otherwise, false.
     */
    bool GetHaveRsrqResult() const;
    /**
     * Gets the flag that indicates if the measured cell is the serving cell.
     *
     * @return True, if this is the measurement of the primary cell; otherwise,
     *         false.
     */
    bool GetIsServingCell() const;

  private:
    /**
     * The IMSI of the UE that the Measurement Report came from.
     */
    uint64_t m_imsi;
    /**
     * The RNTI of the UE that the Measurement Report came from.
     */
    uint16_t m_rnti;
    /**
     * The measurement ID of the reporting configuration.
     */
    uint8_t m_measId;
    /**
     * The measurement reporting event that triggered the Measurement Report.
     */
    uint8_t m_eventId;
    /**
     * The ID of the measured cell.
     */
    uint16_t m_cellId;
    /**
     * The quantized RSRP of the measured cell.
     */
    uint8_t m_rsrpResult;
    /**
     * The quantized RSRQ of the measured cell.
     */
    uint8_t m_rsrqResult;
    /**
     * A flag that indicates if the RSRP is valid.
     */
    bool m_haveRsrpResult;
    /**
     * A flag that indicates if the RSRQ is valid.
     */
    bool m_haveRsrqResult;
    /**
     * A flag that indicates if the measured cell is the serving cell.
     */
    bool m_isServingCell;

}; // class OranReportNrGnbMeasReport

} // namespace ns3

#endif // ORAN_REPORT_NR_GNB_MEAS_REPORT_H
