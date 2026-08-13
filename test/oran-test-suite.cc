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

// An essential include is test.h
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/isotropic-antenna-model.h"
#include "ns3/mobility-module.h"
#include "ns3/nr-channel-helper.h"
#include "ns3/nr-helper.h"
#include "ns3/nr-point-to-point-epc-helper.h"
#include "ns3/oran-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/test.h"

#include <cmath>

using namespace ns3;

/**
 * @ingroup oran
 *
 * Class that tests that node location is reported, stored, and retrieved as expected.
 */
class OranTestCaseMobility1 : public TestCase
{
  public:
    /**
     * Constructor of the test
     */
    OranTestCaseMobility1();
    /**
     * Destructor of the test
     */
    virtual ~OranTestCaseMobility1();

  private:
    /**
     * Method that runs the simulation for the test
     */
    virtual void DoRun();
};

OranTestCaseMobility1::OranTestCaseMobility1()
    : TestCase("Oran Test Case Mobility 1")
{
}

OranTestCaseMobility1::~OranTestCaseMobility1()
{
}

void
OranTestCaseMobility1::DoRun()
{
    Time simTime = Seconds(14);
    double speed = 2;
    std::string dbFileName = "oran-repository.db";

    NodeContainer nodes;
    nodes.Create(1);

    // Install Mobility Model
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0, 0, 0));

    MobilityHelper mobilityHelper;
    mobilityHelper.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobilityHelper.SetPositionAllocator(positionAlloc);
    mobilityHelper.Install(nodes);

    Ptr<ConstantVelocityMobilityModel> mobility =
        nodes.Get(0)->GetObject<ConstantVelocityMobilityModel>();

    // ORAN Models -- BEGIN
    if (!dbFileName.empty())
    {
        std::remove(dbFileName.c_str());
    }

    Ptr<OranNearRtRic> nearRtRic = nullptr;
    OranE2NodeTerminatorContainer e2NodeTerminators;
    Ptr<OranHelper> oranHelper = CreateObject<OranHelper>();

    oranHelper->SetDataRepository("ns3::OranDataRepositorySqlite",
                                  "DatabaseFile",
                                  StringValue(dbFileName));
    oranHelper->SetDefaultLogicModule("ns3::OranLmNoop");
    oranHelper->SetConflictMitigationModule("ns3::OranCmmNoop");

    nearRtRic = oranHelper->CreateNearRtRic();

    // Terminator nodes setup
    oranHelper->SetE2NodeTerminator("ns3::OranE2NodeTerminatorWired",
                                    "RegistrationIntervalRv",
                                    StringValue("ns3::ConstantRandomVariable[Constant=1]"),
                                    "SendIntervalRv",
                                    StringValue("ns3::ConstantRandomVariable[Constant=1]"));

    oranHelper->AddReporter("ns3::OranReporterLocation",
                            "Trigger",
                            StringValue("ns3::OranReportTriggerPeriodic"));

    e2NodeTerminators.Add(oranHelper->DeployTerminators(nearRtRic, nodes));

    // Activate and the components
    Simulator::Schedule(Seconds(0), &OranHelper::ActivateAndStartNearRtRic, oranHelper, nearRtRic);
    Simulator::Schedule(Seconds(1),
                        &OranHelper::ActivateE2NodeTerminators,
                        oranHelper,
                        e2NodeTerminators);
    Simulator::Schedule(Seconds(2),
                        &ConstantVelocityMobilityModel::SetVelocity,
                        mobility,
                        Vector(speed, speed, 0));
    Simulator::Schedule(Seconds(12),
                        &ConstantVelocityMobilityModel::SetVelocity,
                        mobility,
                        Vector(0, 0, 0));
    // ORAN Models -- END

    Simulator::Stop(simTime);
    Simulator::Run();

    std::map<Time, Vector> nodePositions =
        nearRtRic->Data()->GetNodePositions(1, Seconds(0), simTime, 12);
    Vector firstPosition = nodePositions[Seconds(2)];
    Vector lastPosition = nodePositions[Seconds(12)];

    // Check the node's first reported postion.
    NS_TEST_ASSERT_MSG_EQ_TOL(firstPosition.x,
                              0.0,
                              0.001,
                              "First position x-coordinate does not match.");
    NS_TEST_ASSERT_MSG_EQ_TOL(firstPosition.y,
                              0.0,
                              0.001,
                              "First position y-coordinate does not match.");
    NS_TEST_ASSERT_MSG_EQ_TOL(firstPosition.z,
                              0.0,
                              0.001,
                              "First position z-coordinate does not match.");

    // Check the node's last reported position.
    NS_TEST_ASSERT_MSG_EQ_TOL(lastPosition.x,
                              20.0,
                              0.001,
                              "Last position x-coordinate does not match.");
    NS_TEST_ASSERT_MSG_EQ_TOL(lastPosition.y,
                              20.0,
                              0.001,
                              "Last position y-coordinate does not match.");
    NS_TEST_ASSERT_MSG_EQ_TOL(lastPosition.z,
                              0.0,
                              0.001,
                              "Last position z-coordinate does not match.");

    Simulator::Destroy();
}

namespace
{

Ptr<OranNearRtRic>
CreateNrTestRic(const std::string& dbFileName)
{
    std::remove(dbFileName.c_str());

    Ptr<OranHelper> oranHelper = CreateObject<OranHelper>();
    oranHelper->SetDataRepository("ns3::OranDataRepositorySqlite",
                                  "DatabaseFile",
                                  StringValue(dbFileName));
    oranHelper->SetDefaultLogicModule("ns3::OranLmNoop");
    oranHelper->SetConflictMitigationModule("ns3::OranCmmNoop");
    Ptr<OranNearRtRic> nearRtRic = oranHelper->CreateNearRtRic();
    Simulator::Schedule(Seconds(0),
                        &OranHelper::ActivateAndStartNearRtRic,
                        oranHelper,
                        nearRtRic);
    return nearRtRic;
}

} // anonymous namespace

/**
 * Verify that OranReportNrUeCellInfo stores and returns CellId/Rnti via its
 * attributes and accessors, and that its TypeId resolves correctly.
 */
class OranTestCaseNrUeCellInfoReport : public TestCase
{
  public:
    OranTestCaseNrUeCellInfoReport()
        : TestCase("Oran Test Case NR UE Cell Info Report")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<OranReportNrUeCellInfo> report = CreateObject<OranReportNrUeCellInfo>();
        report->SetAttribute("ReporterE2NodeId", UintegerValue(42));
        report->SetAttribute("Time", TimeValue(Seconds(3.5)));
        report->SetAttribute("CellId", UintegerValue(7));
        report->SetAttribute("Rnti", UintegerValue(11));

        NS_TEST_ASSERT_MSG_EQ(report->GetReporterE2NodeId(), 42, "ReporterE2NodeId mismatch");
        NS_TEST_ASSERT_MSG_EQ(report->GetTime(), Seconds(3.5), "Time mismatch");
        NS_TEST_ASSERT_MSG_EQ(report->GetCellId(), 7, "CellId mismatch");
        NS_TEST_ASSERT_MSG_EQ(report->GetRnti(), 11, "Rnti mismatch");
        NS_TEST_ASSERT_MSG_EQ(report->GetInstanceTypeId().GetName(),
                              "ns3::OranReportNrUeCellInfo",
                              "TypeId mismatch");
    }
};

/**
 * Verify that OranReportNrUeRsrpRsrq preserves every field through attributes
 * and accessors, including the bool IsServingCell and uint8_t
 * ComponentCarrierId.
 */
class OranTestCaseNrUeRsrpRsrqReport : public TestCase
{
  public:
    OranTestCaseNrUeRsrpRsrqReport()
        : TestCase("Oran Test Case NR UE RSRP RSRQ Report")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<OranReportNrUeRsrpRsrq> report = CreateObject<OranReportNrUeRsrpRsrq>();
        report->SetAttribute("ReporterE2NodeId", UintegerValue(9));
        report->SetAttribute("Time", TimeValue(Seconds(1.25)));
        report->SetAttribute("Rnti", UintegerValue(5));
        report->SetAttribute("CellId", UintegerValue(3));
        report->SetAttribute("Rsrp", DoubleValue(-87.5));
        report->SetAttribute("Rsrq", DoubleValue(-9.25));
        report->SetAttribute("IsServingCell", BooleanValue(true));
        report->SetAttribute("ComponentCarrierId", UintegerValue(2));

        NS_TEST_ASSERT_MSG_EQ(report->GetReporterE2NodeId(), 9, "ReporterE2NodeId mismatch");
        NS_TEST_ASSERT_MSG_EQ(report->GetTime(), Seconds(1.25), "Time mismatch");
        NS_TEST_ASSERT_MSG_EQ(report->GetRnti(), 5, "Rnti mismatch");
        NS_TEST_ASSERT_MSG_EQ(report->GetCellId(), 3, "CellId mismatch");
        NS_TEST_ASSERT_MSG_EQ_TOL(report->GetRsrp(), -87.5, 1e-9, "Rsrp mismatch");
        NS_TEST_ASSERT_MSG_EQ_TOL(report->GetRsrq(), -9.25, 1e-9, "Rsrq mismatch");
        NS_TEST_ASSERT_MSG_EQ(report->GetIsServingCell(), true, "IsServingCell mismatch");
        NS_TEST_ASSERT_MSG_EQ(report->GetComponentCarrierId(), 2, "ComponentCarrierId mismatch");
        NS_TEST_ASSERT_MSG_EQ(report->GetInstanceTypeId().GetName(),
                              "ns3::OranReportNrUeRsrpRsrq",
                              "TypeId mismatch");
    }
};

/**
 * Verify that OranCommandNr2NrHandover exposes TargetE2NodeId, TargetCellId,
 * and TargetRnti.
 */
class OranTestCaseNrHandoverCommand : public TestCase
{
  public:
    OranTestCaseNrHandoverCommand()
        : TestCase("Oran Test Case NR Handover Command")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<OranCommandNr2NrHandover> cmd = CreateObject<OranCommandNr2NrHandover>();
        cmd->SetAttribute("TargetE2NodeId", UintegerValue(13));
        cmd->SetAttribute("TargetCellId", UintegerValue(21));
        cmd->SetAttribute("TargetRnti", UintegerValue(8));

        NS_TEST_ASSERT_MSG_EQ(cmd->GetTargetE2NodeId(), 13, "TargetE2NodeId mismatch");
        NS_TEST_ASSERT_MSG_EQ(cmd->GetTargetCellId(), 21, "TargetCellId mismatch");
        NS_TEST_ASSERT_MSG_EQ(cmd->GetTargetRnti(), 8, "TargetRnti mismatch");
        NS_TEST_ASSERT_MSG_EQ(cmd->GetInstanceTypeId().GetName(),
                              "ns3::OranCommandNr2NrHandover",
                              "TypeId mismatch");
    }
};

/**
 * Verify that RegisterNodeNrGnb / RegisterNodeNrUe populate the NR tables and
 * that the list/info accessors return the registered nodes.
 */
class OranTestCaseNrRegistration : public TestCase
{
  public:
    OranTestCaseNrRegistration()
        : TestCase("Oran Test Case NR Registration")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<OranNearRtRic> ric = CreateNrTestRic("oran-test-nr-registration.db");

        Simulator::Schedule(Seconds(0.1), [ric]() {
            ric->Data()->RegisterNodeNrGnb(1, 10);
            ric->Data()->RegisterNodeNrUe(2, 100);
        });

        Simulator::Stop(Seconds(0.2));
        Simulator::Run();

        std::vector<uint64_t> gnbIds = ric->Data()->GetNrGnbE2NodeIds();
        std::vector<uint64_t> ueIds = ric->Data()->GetNrUeE2NodeIds();
        auto [foundGnb, cellId] = ric->Data()->GetNrGnbCellInfo(1);

        NS_TEST_ASSERT_MSG_EQ(gnbIds.size(), 1, "Expected 1 NR gNB registered");
        NS_TEST_ASSERT_MSG_EQ(gnbIds.front(), 1, "Registered gNB E2NodeId mismatch");
        NS_TEST_ASSERT_MSG_EQ(ueIds.size(), 1, "Expected 1 NR UE registered");
        NS_TEST_ASSERT_MSG_EQ(ueIds.front(), 2, "Registered UE E2NodeId mismatch");
        NS_TEST_ASSERT_MSG_EQ(foundGnb, true, "GetNrGnbCellInfo did not find gNB");
        NS_TEST_ASSERT_MSG_EQ(cellId, 10, "GetNrGnbCellInfo cellId mismatch");

        Simulator::Destroy();
    }
};

/**
 * Verify SaveNrUeCellInfo + GetNrUeCellInfo + GetNrUeE2NodeIdFromCellInfo
 * round-trip.
 */
class OranTestCaseNrUeCellInfo : public TestCase
{
  public:
    OranTestCaseNrUeCellInfo()
        : TestCase("Oran Test Case NR UE Cell Info Repository")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<OranNearRtRic> ric = CreateNrTestRic("oran-test-nr-ue-cell-info.db");

        Simulator::Schedule(Seconds(0.1), [ric]() {
            ric->Data()->RegisterNodeNrGnb(1, 10);
            ric->Data()->RegisterNodeNrUe(2, 100);
            ric->Data()->SaveNrUeCellInfo(2, 10, 5, Seconds(1));
        });

        Simulator::Stop(Seconds(0.2));
        Simulator::Run();

        auto [found, cellId, rnti] = ric->Data()->GetNrUeCellInfo(2);
        uint64_t reverse = ric->Data()->GetNrUeE2NodeIdFromCellInfo(10, 5);

        NS_TEST_ASSERT_MSG_EQ(found, true, "GetNrUeCellInfo did not find UE");
        NS_TEST_ASSERT_MSG_EQ(cellId, 10, "cellId mismatch");
        NS_TEST_ASSERT_MSG_EQ(rnti, 5, "rnti mismatch");
        NS_TEST_ASSERT_MSG_EQ(reverse, 2, "Reverse lookup mismatch");

        Simulator::Destroy();
    }
};

/**
 * Verify SaveNrUeRsrpRsrq / GetNrUeRsrpRsrq round-trip across multiple rows,
 * asserting every field (including bool IsServingCell and uint8_t ccId).
 */
class OranTestCaseNrUeRsrpRsrq : public TestCase
{
  public:
    OranTestCaseNrUeRsrpRsrq()
        : TestCase("Oran Test Case NR UE RSRP RSRQ Repository")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<OranNearRtRic> ric = CreateNrTestRic("oran-test-nr-ue-rsrp-rsrq.db");

        Simulator::Schedule(Seconds(0.1), [ric]() {
            ric->Data()->RegisterNodeNrGnb(1, 10);
            ric->Data()->RegisterNodeNrUe(2, 100);
            // All three rows at the same simulation time: GetNrUeRsrpRsrq
            // returns only rows from the latest timestamp (matches LTE).
            ric->Data()->SaveNrUeRsrpRsrq(2, Seconds(1), 5, 10, -90.0, -10.0, true, 0);
            ric->Data()->SaveNrUeRsrpRsrq(2, Seconds(1), 5, 11, -85.0, -9.0, false, 0);
            ric->Data()->SaveNrUeRsrpRsrq(2, Seconds(1), 5, 12, -80.0, -8.0, false, 1);
        });

        Simulator::Stop(Seconds(0.2));
        Simulator::Run();

        auto rows = ric->Data()->GetNrUeRsrpRsrq(2);
        NS_TEST_ASSERT_MSG_EQ(rows.size(), 3, "Expected 3 RSRP/RSRQ rows");

        bool sawServingCell10 = false;
        bool sawNeighbor11 = false;
        bool sawNeighbor12Cc1 = false;
        for (const auto& [rnti, cellId, rsrp, rsrq, isServing, cc] : rows)
        {
            NS_TEST_ASSERT_MSG_EQ(rnti, 5, "rnti mismatch");
            if (cellId == 10)
            {
                NS_TEST_ASSERT_MSG_EQ(isServing, true, "Serving cell flag mismatch");
                NS_TEST_ASSERT_MSG_EQ(cc, 0, "Serving cell cc mismatch");
                NS_TEST_ASSERT_MSG_EQ_TOL(rsrp, -90.0, 1e-9, "Serving cell rsrp mismatch");
                sawServingCell10 = true;
            }
            else if (cellId == 11)
            {
                NS_TEST_ASSERT_MSG_EQ(isServing, false, "Neighbor 11 isServing mismatch");
                NS_TEST_ASSERT_MSG_EQ(cc, 0, "Neighbor 11 cc mismatch");
                sawNeighbor11 = true;
            }
            else if (cellId == 12)
            {
                NS_TEST_ASSERT_MSG_EQ(isServing, false, "Neighbor 12 isServing mismatch");
                NS_TEST_ASSERT_MSG_EQ(cc, 1, "Neighbor 12 cc mismatch");
                sawNeighbor12Cc1 = true;
            }
        }
        NS_TEST_ASSERT_MSG_EQ(sawServingCell10, true, "Missing serving row");
        NS_TEST_ASSERT_MSG_EQ(sawNeighbor11, true, "Missing neighbor cell 11");
        NS_TEST_ASSERT_MSG_EQ(sawNeighbor12Cc1, true, "Missing neighbor cell 12 (cc=1)");

        Simulator::Destroy();
    }
};

/**
 * Exercise the NR branches added to OranNearRtRicE2Terminator::ReceiveReport
 * by constructing NR reports and feeding them to the public entry point.
 */
class OranTestCaseNrReportDispatch : public TestCase
{
  public:
    OranTestCaseNrReportDispatch()
        : TestCase("Oran Test Case NR Report Dispatch")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<OranNearRtRic> ric = CreateNrTestRic("oran-test-nr-report-dispatch.db");

        Simulator::Schedule(Seconds(0.1), [ric]() {
            ric->Data()->RegisterNodeNrGnb(1, 10);
            ric->Data()->RegisterNodeNrUe(2, 100);

            Ptr<OranReportNrUeCellInfo> cellInfo = CreateObject<OranReportNrUeCellInfo>();
            cellInfo->SetAttribute("ReporterE2NodeId", UintegerValue(2));
            cellInfo->SetAttribute("Time", TimeValue(Seconds(0.1)));
            cellInfo->SetAttribute("CellId", UintegerValue(10));
            cellInfo->SetAttribute("Rnti", UintegerValue(5));
            ric->GetE2Terminator()->ReceiveReport(cellInfo);

            Ptr<OranReportNrUeRsrpRsrq> rsrp = CreateObject<OranReportNrUeRsrpRsrq>();
            rsrp->SetAttribute("ReporterE2NodeId", UintegerValue(2));
            rsrp->SetAttribute("Time", TimeValue(Seconds(0.1)));
            rsrp->SetAttribute("Rnti", UintegerValue(5));
            rsrp->SetAttribute("CellId", UintegerValue(10));
            rsrp->SetAttribute("Rsrp", DoubleValue(-75.0));
            rsrp->SetAttribute("Rsrq", DoubleValue(-7.0));
            rsrp->SetAttribute("IsServingCell", BooleanValue(true));
            rsrp->SetAttribute("ComponentCarrierId", UintegerValue(0));
            ric->GetE2Terminator()->ReceiveReport(rsrp);
        });

        Simulator::Stop(Seconds(0.2));
        Simulator::Run();

        auto [foundCell, cellId, rnti] = ric->Data()->GetNrUeCellInfo(2);
        NS_TEST_ASSERT_MSG_EQ(foundCell, true, "Cell info dispatch did not persist");
        NS_TEST_ASSERT_MSG_EQ(cellId, 10, "Dispatched cellId mismatch");
        NS_TEST_ASSERT_MSG_EQ(rnti, 5, "Dispatched rnti mismatch");

        auto rsrpRows = ric->Data()->GetNrUeRsrpRsrq(2);
        NS_TEST_ASSERT_MSG_EQ(rsrpRows.size(), 1, "Expected exactly 1 RSRP/RSRQ row");
        auto [rnti2, cellId2, rsrp, rsrq, isServing, cc] = rsrpRows.front();
        NS_TEST_ASSERT_MSG_EQ(rnti2, 5, "Dispatched rsrp.rnti mismatch");
        NS_TEST_ASSERT_MSG_EQ(cellId2, 10, "Dispatched rsrp.cellId mismatch");
        NS_TEST_ASSERT_MSG_EQ_TOL(rsrp, -75.0, 1e-9, "Dispatched rsrp.rsrp mismatch");
        NS_TEST_ASSERT_MSG_EQ_TOL(rsrq, -7.0, 1e-9, "Dispatched rsrp.rsrq mismatch");
        NS_TEST_ASSERT_MSG_EQ(isServing, true, "Dispatched rsrp.isServing mismatch");
        NS_TEST_ASSERT_MSG_EQ(cc, 0, "Dispatched rsrp.cc mismatch");

        Simulator::Destroy();
    }
};

/**
 * Unit test for OranLmNr2NrRsrpSinrHandover decision logic.
 * Seeds the data repository directly and calls Run() to verify the
 * two-stage gate: (1) SINR threshold, (2) RSRP with hysteresis.
 */
class OranTestCaseNrRsrpSinrLmDecision : public TestCase
{
  public:
    OranTestCaseNrRsrpSinrLmDecision()
        : TestCase("Oran Test Case NR RSRP+SINR LM Decision Logic")
    {
    }

  private:
    void DoRun() override
    {
        // Topology: gNB1 (nodeId=1, cellId=10), gNB2 (nodeId=2, cellId=20)
        //           UE (nodeId=3, imsi=100) attached to gNB1 (cellId=10, rnti=5)
        // LM config: SinrThresholdDb=5.0, HysteresisDb=3.0

        // --- Sub-case 1: SINR above threshold → no handover ---
        {
            std::string dbFile = "oran-test-rsrp-sinr-lm-1.db";
            Ptr<OranNearRtRic> ric = CreateNrTestRic(dbFile);

            Ptr<OranLmNr2NrRsrpSinrHandover> lm =
                CreateObject<OranLmNr2NrRsrpSinrHandover>();
            lm->SetAttribute("NearRtRic", PointerValue(ric));
            lm->SetAttribute("SinrThresholdDb", DoubleValue(5.0));
            lm->SetAttribute("HysteresisDb", DoubleValue(3.0));
            lm->SetAttribute("Verbose", BooleanValue(true));

            Simulator::Schedule(Seconds(0.1), [this, ric, lm]() {
                ric->Data()->RegisterNodeNrGnb(1, 10);
                ric->Data()->RegisterNodeNrGnb(2, 20);
                ric->Data()->RegisterNodeNrUe(3, 100);
                ric->Data()->SavePosition(1, Vector(0, 0, 25), Seconds(0.1));
                ric->Data()->SavePosition(2, Vector(200, 0, 25), Seconds(0.1));
                ric->Data()->SavePosition(3, Vector(170, 0, 1.5), Seconds(0.1));
                ric->Data()->SaveNrUeCellInfo(3, 10, 5, Seconds(0.1));
                // SINR = 10 dB → above 5 dB threshold → no handover
                ric->Data()->SaveNrUeSinr(3, Seconds(0.1), 10, 5, 10.0, 0);
                // Neighbor has much better RSRP, but SINR gate blocks
                ric->Data()->SaveNrUeRsrpRsrq(3, Seconds(0.1), 5, 10, -85.0, -10.0, true, 0);
                ric->Data()->SaveNrUeRsrpRsrq(3, Seconds(0.1), 5, 20, -70.0, -8.0, false, 0);

                lm->Activate();
                auto cmds = lm->Run();
                NS_TEST_ASSERT_MSG_EQ(cmds.size(),
                                      0,
                                      "Sub-case 1: SINR above threshold should produce no HO");
            });

            Simulator::Stop(Seconds(0.2));
            Simulator::Run();
            Simulator::Destroy();
        }

        // --- Sub-case 2: SINR below threshold, neighbor doesn't beat hysteresis → no HO ---
        {
            std::string dbFile = "oran-test-rsrp-sinr-lm-2.db";
            Ptr<OranNearRtRic> ric = CreateNrTestRic(dbFile);

            Ptr<OranLmNr2NrRsrpSinrHandover> lm =
                CreateObject<OranLmNr2NrRsrpSinrHandover>();
            lm->SetAttribute("NearRtRic", PointerValue(ric));
            lm->SetAttribute("SinrThresholdDb", DoubleValue(5.0));
            lm->SetAttribute("HysteresisDb", DoubleValue(3.0));
            lm->SetAttribute("Verbose", BooleanValue(true));

            Simulator::Schedule(Seconds(0.1), [this, ric, lm]() {
                ric->Data()->RegisterNodeNrGnb(1, 10);
                ric->Data()->RegisterNodeNrGnb(2, 20);
                ric->Data()->RegisterNodeNrUe(3, 100);
                ric->Data()->SavePosition(1, Vector(0, 0, 25), Seconds(0.1));
                ric->Data()->SavePosition(2, Vector(200, 0, 25), Seconds(0.1));
                ric->Data()->SavePosition(3, Vector(100, 0, 1.5), Seconds(0.1));
                ric->Data()->SaveNrUeCellInfo(3, 10, 5, Seconds(0.1));
                // SINR = 2 dB → below threshold, proceed to RSRP check
                ric->Data()->SaveNrUeSinr(3, Seconds(0.1), 10, 5, 2.0, 0);
                // Serving RSRP = -82, neighbor RSRP = -83
                // -83 <= -82 + 3 = -79, so hysteresis not exceeded → no HO
                ric->Data()->SaveNrUeRsrpRsrq(3, Seconds(0.1), 5, 10, -82.0, -10.0, true, 0);
                ric->Data()->SaveNrUeRsrpRsrq(3, Seconds(0.1), 5, 20, -83.0, -9.0, false, 0);

                lm->Activate();
                auto cmds = lm->Run();
                NS_TEST_ASSERT_MSG_EQ(
                    cmds.size(),
                    0,
                    "Sub-case 2: neighbor RSRP doesn't beat hysteresis, no HO");
            });

            Simulator::Stop(Seconds(0.2));
            Simulator::Run();
            Simulator::Destroy();
        }

        // --- Sub-case 3: SINR below threshold AND neighbor beats hysteresis → handover ---
        {
            std::string dbFile = "oran-test-rsrp-sinr-lm-3.db";
            Ptr<OranNearRtRic> ric = CreateNrTestRic(dbFile);

            Ptr<OranLmNr2NrRsrpSinrHandover> lm =
                CreateObject<OranLmNr2NrRsrpSinrHandover>();
            lm->SetAttribute("NearRtRic", PointerValue(ric));
            lm->SetAttribute("SinrThresholdDb", DoubleValue(5.0));
            lm->SetAttribute("HysteresisDb", DoubleValue(3.0));
            lm->SetAttribute("Verbose", BooleanValue(true));

            Simulator::Schedule(Seconds(0.1), [this, ric, lm]() {
                ric->Data()->RegisterNodeNrGnb(1, 10);
                ric->Data()->RegisterNodeNrGnb(2, 20);
                ric->Data()->RegisterNodeNrUe(3, 100);
                ric->Data()->SavePosition(1, Vector(0, 0, 25), Seconds(0.1));
                ric->Data()->SavePosition(2, Vector(200, 0, 25), Seconds(0.1));
                ric->Data()->SavePosition(3, Vector(170, 0, 1.5), Seconds(0.1));
                ric->Data()->SaveNrUeCellInfo(3, 10, 5, Seconds(0.1));
                // SINR = 2 dB → below threshold
                ric->Data()->SaveNrUeSinr(3, Seconds(0.1), 10, 5, 2.0, 0);
                // Serving RSRP = -85, neighbor RSRP = -75
                // -75 > -85 + 3 = -82 → hysteresis exceeded → handover!
                ric->Data()->SaveNrUeRsrpRsrq(3, Seconds(0.1), 5, 10, -85.0, -10.0, true, 0);
                ric->Data()->SaveNrUeRsrpRsrq(3, Seconds(0.1), 5, 20, -75.0, -8.0, false, 0);

                lm->Activate();
                auto cmds = lm->Run();
                NS_TEST_ASSERT_MSG_EQ(cmds.size(),
                                      1,
                                      "Sub-case 3: should produce exactly 1 HO command");

                Ptr<OranCommandNr2NrHandover> hoCmd =
                    DynamicCast<OranCommandNr2NrHandover>(cmds.front());
                NS_TEST_ASSERT_MSG_NE(hoCmd, nullptr, "Command should be OranCommandNr2NrHandover");
                NS_TEST_ASSERT_MSG_EQ(hoCmd->GetTargetCellId(),
                                      20,
                                      "HO should target gNB2 (cellId 20)");
                NS_TEST_ASSERT_MSG_EQ(hoCmd->GetTargetRnti(), 5, "HO should target RNTI 5");
                NS_TEST_ASSERT_MSG_EQ(hoCmd->GetTargetE2NodeId(),
                                      1,
                                      "HO command should be sent to serving gNB (E2NodeId 1)");
            });

            Simulator::Stop(Seconds(0.2));
            Simulator::Run();
            Simulator::Destroy();
        }
    }
};

/**
 * Verify that OranLmNr2NrRsrpHandover returns no command when the UE's serving
 * cellId does not match any registered gNB — exercising the oldCellNodeId ==
 * UINT64_MAX guard added to prevent use of an uninitialized variable.
 */
class OranTestCaseNrRsrpLmMissingGnb : public TestCase
{
  public:
    OranTestCaseNrRsrpLmMissingGnb()
        : TestCase("Oran Test Case NR RSRP LM Missing Serving gNB")
    {
    }

  private:
    void DoRun() override
    {
        std::string dbFile = "oran-test-rsrp-lm-missing-gnb.db";
        Ptr<OranNearRtRic> ric = CreateNrTestRic(dbFile);

        Ptr<OranLmNr2NrRsrpHandover> lm = CreateObject<OranLmNr2NrRsrpHandover>();
        lm->SetAttribute("NearRtRic", PointerValue(ric));
        lm->SetAttribute("Verbose", BooleanValue(true));

        Simulator::Schedule(Seconds(0.1), [this, ric, lm]() {
            // Register gNBs with cellId 10 and 20
            ric->Data()->RegisterNodeNrGnb(1, 10);
            ric->Data()->RegisterNodeNrGnb(2, 20);
            ric->Data()->RegisterNodeNrUe(3, 100);
            ric->Data()->SavePosition(1, Vector(0, 0, 25), Seconds(0.1));
            ric->Data()->SavePosition(2, Vector(200, 0, 25), Seconds(0.1));
            ric->Data()->SavePosition(3, Vector(170, 0, 1.5), Seconds(0.1));
            // UE reports serving cellId=99 — no gNB has this cellId
            ric->Data()->SaveNrUeCellInfo(3, 99, 5, Seconds(0.1));
            // RSRP data: neighbor (cellId 20) is much stronger than serving
            ric->Data()->SaveNrUeRsrpRsrq(3, Seconds(0.1), 5, 99, -95.0, -12.0, true, 0);
            ric->Data()->SaveNrUeRsrpRsrq(3, Seconds(0.1), 5, 20, -70.0, -8.0, false, 0);

            lm->Activate();
            auto cmds = lm->Run();
            NS_TEST_ASSERT_MSG_EQ(
                cmds.size(),
                0,
                "No HO command when serving cellId has no matching gNB");
        });

        Simulator::Stop(Seconds(0.2));
        Simulator::Run();
        Simulator::Destroy();
    }
};

/**
 * Verify that OranLmNr2NrDistanceHandover returns no command when the UE's
 * serving cellId does not match any registered gNB — exercising the
 * oldCellNodeId == UINT64_MAX guard.
 */
class OranTestCaseNrDistanceLmMissingGnb : public TestCase
{
  public:
    OranTestCaseNrDistanceLmMissingGnb()
        : TestCase("Oran Test Case NR Distance LM Missing Serving gNB")
    {
    }

  private:
    void DoRun() override
    {
        std::string dbFile = "oran-test-distance-lm-missing-gnb.db";
        Ptr<OranNearRtRic> ric = CreateNrTestRic(dbFile);

        Ptr<OranLmNr2NrDistanceHandover> lm = CreateObject<OranLmNr2NrDistanceHandover>();
        lm->SetAttribute("NearRtRic", PointerValue(ric));
        lm->SetAttribute("Verbose", BooleanValue(true));

        Simulator::Schedule(Seconds(0.1), [this, ric, lm]() {
            // gNBs with cellId 10 and 20
            ric->Data()->RegisterNodeNrGnb(1, 10);
            ric->Data()->RegisterNodeNrGnb(2, 20);
            ric->Data()->RegisterNodeNrUe(3, 100);
            ric->Data()->SavePosition(1, Vector(0, 0, 25), Seconds(0.1));
            ric->Data()->SavePosition(2, Vector(200, 0, 25), Seconds(0.1));
            // UE is near gNB2 but reports serving cellId=99 (no matching gNB)
            ric->Data()->SavePosition(3, Vector(190, 0, 1.5), Seconds(0.1));
            ric->Data()->SaveNrUeCellInfo(3, 99, 5, Seconds(0.1));

            lm->Activate();
            auto cmds = lm->Run();
            NS_TEST_ASSERT_MSG_EQ(
                cmds.size(),
                0,
                "No HO command when serving cellId has no matching gNB");
        });

        Simulator::Stop(Seconds(0.2));
        Simulator::Run();
        Simulator::Destroy();
    }
};

/**
 * Integration test: set up a minimal NR simulation with two gNBs, attach a UE
 * to the farther gNB, and verify that the RIC's distance-based Logic Module
 * triggers a handover to the closer gNB.
 */
class OranTestCaseNrDistanceHandover : public TestCase
{
  public:
    OranTestCaseNrDistanceHandover()
        : TestCase("Oran Test Case NR Distance Handover Integration")
    {
    }

  private:
    void DoRun() override
    {
        std::string dbFileName = "oran-test-nr-distance-ho.db";
        std::remove(dbFileName.c_str());

        Config::SetDefault("ns3::NrGnbPhy::TxPower", DoubleValue(30));
        Config::SetDefault("ns3::NrUePhy::TxPower", DoubleValue(23));
        Config::SetDefault("ns3::NrUePhy::EnableUplinkPowerControl", BooleanValue(false));

        Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();
        Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
        nrHelper->SetEpcHelper(epcHelper);
        nrHelper->SetAttribute("UseIdealRrc", BooleanValue(true));
        nrHelper->SetHandoverAlgorithmType("ns3::NrNoOpHandoverAlgorithm");

        NodeContainer gnbNodes;
        gnbNodes.Create(2);
        NodeContainer ueNodes;
        ueNodes.Create(1);

        Ptr<ListPositionAllocator> gnbPos = CreateObject<ListPositionAllocator>();
        gnbPos->Add(Vector(0, 0, 25));
        gnbPos->Add(Vector(200, 0, 25));

        MobilityHelper gnbMob;
        gnbMob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        gnbMob.SetPositionAllocator(gnbPos);
        gnbMob.Install(gnbNodes);

        Ptr<ListPositionAllocator> uePos = CreateObject<ListPositionAllocator>();
        uePos->Add(Vector(170, 0, 1.5));

        MobilityHelper ueMob;
        ueMob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        ueMob.SetPositionAllocator(uePos);
        ueMob.Install(ueNodes);

        nrHelper->SetUeAntennaTypeId(IsotropicAntennaModel::GetTypeId().GetName());
        nrHelper->SetGnbAntennaTypeId(IsotropicAntennaModel::GetTypeId().GetName());

        Ptr<NrChannelHelper> channelHelper = CreateObject<NrChannelHelper>();
        channelHelper->ConfigurePropagationFactory(FriisPropagationLossModel::GetTypeId());

        CcBwpCreator ccBwpCreator;
        CcBwpCreator::SimpleOperationBandConf bandConf(2.8e9, 10e6, static_cast<uint8_t>(1));
        OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
        channelHelper->AssignChannelsToBands({band});
        BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({band});

        NetDeviceContainer gnbDevs = nrHelper->InstallGnbDevice(gnbNodes, allBwps);
        NetDeviceContainer ueDevs = nrHelper->InstallUeDevice(ueNodes, allBwps);

        // Minimal EPC / internet stack (required for NR attach)
        NodeContainer remoteHostContainer;
        remoteHostContainer.Create(1);
        InternetStackHelper internet;
        internet.Install(remoteHostContainer);

        PointToPointHelper p2ph;
        p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s")));
        p2ph.SetDeviceAttribute("Mtu", UintegerValue(1500));
        p2ph.SetChannelAttribute("Delay", TimeValue(Seconds(0.010)));
        NetDeviceContainer internetDevices = p2ph.Install(epcHelper->GetPgwNode(),
                                                          remoteHostContainer.Get(0));
        Ipv4AddressHelper ipv4h;
        ipv4h.SetBase("1.0.0.0", "255.0.0.0");
        ipv4h.Assign(internetDevices);

        internet.Install(ueNodes);
        epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueDevs));

        nrHelper->AddX2Interface(gnbNodes);
        // Attach UE to gNB1 (the farther one) — RIC should hand it over to gNB2
        nrHelper->AttachToGnb(ueDevs.Get(0), gnbDevs.Get(0));

        // O-RAN setup
        Ptr<OranNearRtRic> nearRtRic = nullptr;
        OranE2NodeTerminatorContainer e2TermGnbs;
        OranE2NodeTerminatorContainer e2TermUes;
        Ptr<OranHelper> oranHelper = CreateObject<OranHelper>();

        oranHelper->SetAttribute("Verbose", BooleanValue(false));
        oranHelper->SetAttribute("LmQueryInterval", TimeValue(Seconds(1)));
        oranHelper->SetAttribute("E2NodeInactivityThreshold", TimeValue(Seconds(2)));
        oranHelper->SetAttribute("E2NodeInactivityIntervalRv",
                                 StringValue("ns3::ConstantRandomVariable[Constant=2]"));
        oranHelper->SetAttribute("LmQueryMaxWaitTime", TimeValue(Seconds(0)));
        oranHelper->SetAttribute("LmQueryLateCommandPolicy", EnumValue(OranNearRtRic::DROP));
        oranHelper->SetAttribute("RicTransmissionDelayRv",
                                 StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));

        oranHelper->SetDataRepository("ns3::OranDataRepositorySqlite",
                                      "DatabaseFile",
                                      StringValue(dbFileName));
        oranHelper->SetDefaultLogicModule(
            "ns3::OranLmNr2NrDistanceHandover",
            "ProcessingDelayRv",
            StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        oranHelper->SetConflictMitigationModule("ns3::OranCmmNoop");

        nearRtRic = oranHelper->CreateNearRtRic();

        // UE terminators
        oranHelper->SetE2NodeTerminator(
            "ns3::OranE2NodeTerminatorNrUe",
            "RegistrationIntervalRv",
            StringValue("ns3::ConstantRandomVariable[Constant=1]"),
            "SendIntervalRv",
            StringValue("ns3::ConstantRandomVariable[Constant=1]"),
            "TransmissionDelayRv",
            StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));
        oranHelper->AddReporter("ns3::OranReporterLocation",
                                "Trigger",
                                StringValue("ns3::OranReportTriggerPeriodic"));
        oranHelper->AddReporter(
            "ns3::OranReporterNrUeCellInfo",
            "Trigger",
            StringValue("ns3::OranReportTriggerNrUeHandover[InitialReport=true]"));
        e2TermUes.Add(oranHelper->DeployTerminators(nearRtRic, ueNodes));

        // gNB terminators
        oranHelper->SetE2NodeTerminator(
            "ns3::OranE2NodeTerminatorNrGnb",
            "RegistrationIntervalRv",
            StringValue("ns3::ConstantRandomVariable[Constant=1]"),
            "SendIntervalRv",
            StringValue("ns3::ConstantRandomVariable[Constant=1]"),
            "TransmissionDelayRv",
            StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));
        oranHelper->AddReporter("ns3::OranReporterLocation",
                                "Trigger",
                                StringValue("ns3::OranReportTriggerPeriodic"));
        e2TermGnbs.Add(oranHelper->DeployTerminators(nearRtRic, gnbNodes));

        Simulator::Schedule(Seconds(1),
                            &OranHelper::ActivateAndStartNearRtRic,
                            oranHelper,
                            nearRtRic);
        Simulator::Schedule(Seconds(1.5),
                            &OranHelper::ActivateE2NodeTerminators,
                            oranHelper,
                            e2TermGnbs);
        Simulator::Schedule(Seconds(2),
                            &OranHelper::ActivateE2NodeTerminators,
                            oranHelper,
                            e2TermUes);

        Simulator::Stop(Seconds(8));
        Simulator::Run();

        // Verify: UE should have handed over to gNB2 (the closer gNB)
        // The UE's E2 node ID depends on registration order; find it
        std::vector<uint64_t> ueIds = nearRtRic->Data()->GetNrUeE2NodeIds();
        NS_TEST_ASSERT_MSG_EQ(ueIds.empty(), false, "No NR UE registered in repository");

        uint64_t ueE2NodeId = ueIds.front();
        auto [found, cellId, rnti] = nearRtRic->Data()->GetNrUeCellInfo(ueE2NodeId);
        NS_TEST_ASSERT_MSG_EQ(found, true, "UE cell info not found in repository");

        // gNB2 has cellId 2 (assigned sequentially by NR helper)
        // The UE started attached to gNB1 (cellId 1) but is at x=170, much
        // closer to gNB2 at x=200 — the distance LM should have triggered HO
        NS_TEST_ASSERT_MSG_EQ(cellId, 2, "UE did not hand over to the closer gNB (cellId 2)");

        Simulator::Destroy();
    }
};

/**
 * Verify the decision logic of OranLmNr2NrA2A4RsrpHandover, which reproduces the
 * decision of the ns3::NrA2A4RsrpHandoverAlgorithm gNB handover algorithm from
 * the RRC Measurement Reports captured at the gNB.
 *
 * The measurements are seeded directly into the data repository in the quantized
 * ranges of Table 10.1.6.1-1 of 3GPP TS 38.133, which is how the Reporter stores
 * them. No radio stack is needed: the arrival of an Event A2 report is itself the
 * first condition of the algorithm, since the A2 threshold is evaluated in the UE
 * RRC and not by the Logic Module.
 */
class OranTestCaseNrA2A4LmDecision : public TestCase
{
  public:
    OranTestCaseNrA2A4LmDecision()
        : TestCase("Oran Test Case NR A2-A4 RSRP LM Decision Logic")
    {
    }

  private:
    /**
     * Register the common topology and the UE cell attachment.
     *
     * Topology: gNB1 (nodeId=1, cellId=10), gNB2 (nodeId=2, cellId=20)
     *           UE (nodeId=3, imsi=100) attached to gNB1 (cellId=10, rnti=5)
     *
     * @param ric The Near-RT RIC.
     * @param t The time to record the entries at.
     */
    void SeedTopology(Ptr<OranNearRtRic> ric, Time t)
    {
        ric->Data()->RegisterNodeNrGnb(1, 10);
        ric->Data()->RegisterNodeNrGnb(2, 20);
        ric->Data()->RegisterNodeNrUe(3, 100);
        ric->Data()->SaveNrUeCellInfo(3, 10, 5, t);
    }

    void DoRun() override
    {
        // --- Sub-case 1: no Event A2 report -> the serving cell is fine, no HO ---
        {
            Ptr<OranNearRtRic> ric = CreateNrTestRic("oran-test-nr-a2a4-lm-1.db");

            Ptr<OranLmNr2NrA2A4RsrpHandover> lm = CreateObject<OranLmNr2NrA2A4RsrpHandover>();
            lm->SetAttribute("NearRtRic", PointerValue(ric));
            lm->SetAttribute("NeighbourCellOffset", UintegerValue(1));
            lm->SetAttribute("MaxReportAge", TimeValue(Seconds(1)));
            lm->SetAttribute("Verbose", BooleanValue(true));

            Simulator::Schedule(Seconds(0.1), [this, ric, lm]() {
                SeedTopology(ric, Seconds(0.1));
                // Only an Event A4 report, with a much stronger neighbour. Without
                // the A2 report the first condition does not hold, so the strong
                // neighbour must be ignored.
                ric->Data()->SaveNrGnbMeasReport(1,
                                                 Seconds(0.1),
                                                 100,
                                                 5,
                                                 2,
                                                 OranReportNrGnbMeasReport::EVENT_A4,
                                                 10,
                                                 40,
                                                 20,
                                                 true,
                                                 true,
                                                 true);
                ric->Data()->SaveNrGnbMeasReport(1,
                                                 Seconds(0.1),
                                                 100,
                                                 5,
                                                 2,
                                                 OranReportNrGnbMeasReport::EVENT_A4,
                                                 20,
                                                 60,
                                                 25,
                                                 true,
                                                 true,
                                                 false);

                lm->Activate();
                auto cmds = lm->Run();
                NS_TEST_ASSERT_MSG_EQ(cmds.size(),
                                      0,
                                      "Sub-case 1: without Event A2 there should be no HO");
            });

            Simulator::Stop(Seconds(0.2));
            Simulator::Run();
            Simulator::Destroy();
        }

        // --- Sub-case 2: A2 present, neighbour does not beat the offset -> no HO ---
        {
            Ptr<OranNearRtRic> ric = CreateNrTestRic("oran-test-nr-a2a4-lm-2.db");

            Ptr<OranLmNr2NrA2A4RsrpHandover> lm = CreateObject<OranLmNr2NrA2A4RsrpHandover>();
            lm->SetAttribute("NearRtRic", PointerValue(ric));
            lm->SetAttribute("NeighbourCellOffset", UintegerValue(3));
            lm->SetAttribute("MaxReportAge", TimeValue(Seconds(1)));
            lm->SetAttribute("Verbose", BooleanValue(true));

            Simulator::Schedule(Seconds(0.1), [this, ric, lm]() {
                SeedTopology(ric, Seconds(0.1));
                // Event A2: the serving cell dropped below the configured threshold.
                ric->Data()->SaveNrGnbMeasReport(1,
                                                 Seconds(0.1),
                                                 100,
                                                 5,
                                                 1,
                                                 OranReportNrGnbMeasReport::EVENT_A2,
                                                 10,
                                                 40,
                                                 20,
                                                 true,
                                                 true,
                                                 true);
                // Event A4: the neighbour is only 2 quantization steps better,
                // which is below the offset of 3.
                ric->Data()->SaveNrGnbMeasReport(1,
                                                 Seconds(0.1),
                                                 100,
                                                 5,
                                                 2,
                                                 OranReportNrGnbMeasReport::EVENT_A4,
                                                 10,
                                                 40,
                                                 20,
                                                 true,
                                                 true,
                                                 true);
                ric->Data()->SaveNrGnbMeasReport(1,
                                                 Seconds(0.1),
                                                 100,
                                                 5,
                                                 2,
                                                 OranReportNrGnbMeasReport::EVENT_A4,
                                                 20,
                                                 42,
                                                 21,
                                                 true,
                                                 true,
                                                 false);

                lm->Activate();
                auto cmds = lm->Run();
                NS_TEST_ASSERT_MSG_EQ(cmds.size(),
                                      0,
                                      "Sub-case 2: neighbour below NeighbourCellOffset, no HO");
            });

            Simulator::Stop(Seconds(0.2));
            Simulator::Run();
            Simulator::Destroy();
        }

        // --- Sub-case 3: both conditions hold -> handover to the best neighbour ---
        {
            Ptr<OranNearRtRic> ric = CreateNrTestRic("oran-test-nr-a2a4-lm-3.db");

            Ptr<OranLmNr2NrA2A4RsrpHandover> lm = CreateObject<OranLmNr2NrA2A4RsrpHandover>();
            lm->SetAttribute("NearRtRic", PointerValue(ric));
            lm->SetAttribute("NeighbourCellOffset", UintegerValue(1));
            lm->SetAttribute("MaxReportAge", TimeValue(Seconds(1)));
            lm->SetAttribute("Verbose", BooleanValue(true));

            Simulator::Schedule(Seconds(0.1), [this, ric, lm]() {
                SeedTopology(ric, Seconds(0.1));
                ric->Data()->SaveNrGnbMeasReport(1,
                                                 Seconds(0.1),
                                                 100,
                                                 5,
                                                 1,
                                                 OranReportNrGnbMeasReport::EVENT_A2,
                                                 10,
                                                 40,
                                                 20,
                                                 true,
                                                 true,
                                                 true);
                ric->Data()->SaveNrGnbMeasReport(1,
                                                 Seconds(0.1),
                                                 100,
                                                 5,
                                                 2,
                                                 OranReportNrGnbMeasReport::EVENT_A4,
                                                 10,
                                                 40,
                                                 20,
                                                 true,
                                                 true,
                                                 true);
                // 55 - 40 = 15, well above the offset of 1.
                ric->Data()->SaveNrGnbMeasReport(1,
                                                 Seconds(0.1),
                                                 100,
                                                 5,
                                                 2,
                                                 OranReportNrGnbMeasReport::EVENT_A4,
                                                 20,
                                                 55,
                                                 24,
                                                 true,
                                                 true,
                                                 false);

                lm->Activate();
                auto cmds = lm->Run();
                NS_TEST_ASSERT_MSG_EQ(cmds.size(),
                                      1,
                                      "Sub-case 3: should produce exactly 1 HO command");

                Ptr<OranCommandNr2NrHandover> hoCmd =
                    DynamicCast<OranCommandNr2NrHandover>(cmds.front());
                NS_TEST_ASSERT_MSG_NE(hoCmd, nullptr, "Command should be OranCommandNr2NrHandover");
                NS_TEST_ASSERT_MSG_EQ(hoCmd->GetTargetCellId(),
                                      20,
                                      "HO should target gNB2 (cellId 20)");
                NS_TEST_ASSERT_MSG_EQ(hoCmd->GetTargetRnti(), 5, "HO should target RNTI 5");
                NS_TEST_ASSERT_MSG_EQ(hoCmd->GetTargetE2NodeId(),
                                      1,
                                      "HO command should be sent to serving gNB (E2NodeId 1)");
            });

            Simulator::Stop(Seconds(0.2));
            Simulator::Run();
            Simulator::Destroy();
        }

        // --- Sub-case 4: the reports are older than MaxReportAge -> no HO ---
        // The gNB handover algorithm has no equivalent of this: it caches the
        // neighbour measurements for the whole simulation and never ages them.
        {
            Ptr<OranNearRtRic> ric = CreateNrTestRic("oran-test-nr-a2a4-lm-4.db");

            Ptr<OranLmNr2NrA2A4RsrpHandover> lm = CreateObject<OranLmNr2NrA2A4RsrpHandover>();
            lm->SetAttribute("NearRtRic", PointerValue(ric));
            lm->SetAttribute("NeighbourCellOffset", UintegerValue(1));
            lm->SetAttribute("MaxReportAge", TimeValue(Seconds(1)));
            lm->SetAttribute("Verbose", BooleanValue(true));

            // The same measurements as sub-case 3, which did trigger a handover.
            Simulator::Schedule(Seconds(0.1), [this, ric]() {
                SeedTopology(ric, Seconds(0.1));
                ric->Data()->SaveNrGnbMeasReport(1,
                                                 Seconds(0.1),
                                                 100,
                                                 5,
                                                 1,
                                                 OranReportNrGnbMeasReport::EVENT_A2,
                                                 10,
                                                 40,
                                                 20,
                                                 true,
                                                 true,
                                                 true);
                ric->Data()->SaveNrGnbMeasReport(1,
                                                 Seconds(0.1),
                                                 100,
                                                 5,
                                                 2,
                                                 OranReportNrGnbMeasReport::EVENT_A4,
                                                 20,
                                                 55,
                                                 24,
                                                 true,
                                                 true,
                                                 false);
            });

            // Run the Logic Module well after the reports have aged out.
            Simulator::Schedule(Seconds(3.0), [this, lm]() {
                lm->Activate();
                auto cmds = lm->Run();
                NS_TEST_ASSERT_MSG_EQ(cmds.size(),
                                      0,
                                      "Sub-case 4: reports older than MaxReportAge give no HO");
            });

            Simulator::Stop(Seconds(3.1));
            Simulator::Run();
            Simulator::Destroy();
        }
    }
};

/**
 * @ingroup oran
 *
 * Test suite for the O-RAN module
 */
class OranTestSuite : public TestSuite
{
  public:
    /**
     * Constructor for the test suite
     */
    OranTestSuite();
};

OranTestSuite::OranTestSuite()
    : TestSuite("oran", Type::UNIT)
{
    AddTestCase(new OranTestCaseMobility1, Duration::QUICK);
    AddTestCase(new OranTestCaseNrUeCellInfoReport, Duration::QUICK);
    AddTestCase(new OranTestCaseNrUeRsrpRsrqReport, Duration::QUICK);
    AddTestCase(new OranTestCaseNrHandoverCommand, Duration::QUICK);
    AddTestCase(new OranTestCaseNrRegistration, Duration::QUICK);
    AddTestCase(new OranTestCaseNrUeCellInfo, Duration::QUICK);
    AddTestCase(new OranTestCaseNrUeRsrpRsrq, Duration::QUICK);
    AddTestCase(new OranTestCaseNrReportDispatch, Duration::QUICK);
    AddTestCase(new OranTestCaseNrRsrpSinrLmDecision, Duration::QUICK);
    AddTestCase(new OranTestCaseNrRsrpLmMissingGnb, Duration::QUICK);
    AddTestCase(new OranTestCaseNrDistanceLmMissingGnb, Duration::QUICK);
    AddTestCase(new OranTestCaseNrDistanceHandover, Duration::QUICK);
    AddTestCase(new OranTestCaseNrA2A4LmDecision, Duration::QUICK);
}

static OranTestSuite soranTestSuite;
