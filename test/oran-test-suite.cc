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
#include "ns3/mobility-module.h"
#include "ns3/oran-module.h"
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
}

static OranTestSuite soranTestSuite;
