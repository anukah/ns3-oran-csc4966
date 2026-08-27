# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

This is the `oran` module, a `contrib/` module of ns-3. The ns-3 root
(`../..` from here) has its own `CLAUDE.md` and `AGENTS.md` covering the ns-3
build system, coding style, Doxygen conventions, and linting - those apply here
and are not repeated. This file covers what is specific to the `oran` module.

## What This Module Is

A fork of [usnistgov/ns3-oran](https://github.com/usnistgov/ns3-oran) (NIST)
that adds full 5G NR support on top of the original LTE-only module. It models
an O-RAN Near-RT RIC: E2 node terminators attached to simulation nodes report
metrics to a central RIC, Logic Modules (xApps) run periodically over the
collected data and emit Commands, a Conflict Mitigation Module filters those
Commands, and the RIC's E2 terminator dispatches them back to the nodes.

The NR pipeline mirrors the LTE pipeline 1:1. Most classes exist in `Lte*` and
`Nr*` variants (`OranReportLteUeCellInfo` / `OranReportNrUeCellInfo`,
`OranE2NodeTerminatorLteEnb` / `OranE2NodeTerminatorNrGnb`, and so on). When
adding a feature to one RAT, check whether the mirror class needs it too.

## Commands

All commands run from the **ns-3 root** (`cd ../..`), not from this directory.

```bash
# Configure. SQLite is mandatory - CMakeLists.txt bails out with
# "ORAN requires sqlite3 library" and the module silently does not build
# without it.
./ns3 configure --enable-examples --enable-tests

# Build only what this module needs
./ns3 configure --enable-modules=oran,nr,lte,mobility,internet,applications,point-to-point --enable-examples --enable-tests

./ns3 build oran
./test.py -s oran -v
./ns3 run 'test-runner --suite=oran --verbose'   # single suite, full output, honors NS_LOG
./ns3 run "oran-nr-2-nr-rsrp-handover-lm-example --verbose"
```

Optional ML backends are discovered at configure time via environment
variables; if not found, the corresponding sources are dropped from the build
entirely (see `CMakeLists.txt`):

```bash
export LIBONNXPATH=/path/to/onnxruntime-linux-x64-1.14.1   # gates the *-onnx-handover LMs
export LIBTORCHPATH=/path/to/libtorch                      # gates oran-lm-lte-2-lte-torch-handover
./ns3 configure   # look for "find_external_library: OnnxRuntime was found"
```

Trained models live in `examples/` and are committed alongside the scripts that
produce them. Examples load them by a path relative to the working directory,
which is the ns-3 root under `./ns3 run`. The older examples default to a bare
filename and so need the model copied to the root first (e.g.
`cp contrib/oran/examples/nr_rsrp_sinr_logistic.onnx .`); `vienna-ho-onnx-lm`
defaults to the full `contrib/oran/examples/...` path and needs no copy.

Generated data is not committed. `vienna_ho_rf_lag.py` trains from a CSV that
`extract_db_to_csv.py` regenerates from a simulation database, and `*.csv` is
in `.gitignore`.

Module docs are Sphinx and build separately from the ns-3 doc targets:

```bash
cd contrib/oran/doc && make html   # also singlehtml, latexpdf
```

Simulations write a SQLite database to the working directory; inspecting it is
the normal way to check what a scenario did:

```bash
sqlite3 oran-repository.db "SELECT * FROM cmmaction ORDER BY time;"
python3 contrib/oran/examples/extract_db_to_csv.py <db> -o out.csv
```

## Architecture

### Control loop

`OranNearRtRic` (`model/oran-near-rt-ric.h`) owns everything: the data
repository, exactly one default Logic Module (mandatory, replaceable but never
removable), zero or more named additional Logic Modules, exactly one Conflict
Mitigation Module, and the RIC-side E2 terminator. `Activate()`/`Deactivate()`
propagate to all components; `Start()`/`Stop()` control the periodic LM query.

Report path: a `OranReporter` on a node produces `OranReport` objects when its
`OranReportTrigger` fires -> `OranE2NodeTerminator::StoreReport` batches them ->
sent to `OranNearRtRicE2Terminator::ReceiveReport`, which dispatches on
`report->GetInstanceTypeId()` through an if/else chain and calls the matching
`OranDataRepository::Save*` method.

Command path: `OranNearRtRic::QueryLms()` fires every `LmQueryInterval` (or
early, if an `OranQueryTrigger` decides a newly arrived Report warrants it).
Each LM's `Run()` reads only from the data repository and returns
`Ptr<OranCommand>`s. LMs may have a processing delay, so results arrive
asynchronously via `NotifyLmFinished`; commands later than `LmQueryMaxWaitTime`
are dropped or deferred per `LmQueryLateCommandPolicy`. Surviving commands go
through `OranCmm::Filter`, then the RIC E2 terminator delivers them to the
target node's terminator, which executes them (e.g.
`OranE2NodeTerminatorNrGnb::ReceiveCommand` calls `NrGnbRrc::SendHandoverRequest`).

Node liveness is a keep-alive: terminators re-register periodically, and the RIC
marks a node inactive if no registration arrives within
`E2NodeInactivityThreshold`. Reports from inactive nodes are rejected.

### Key consequences for writing code

- **LMs never touch ns-3 models directly.** They read the data repository and
  emit commands. This is what makes them unit-testable without a radio stack.
- **The data repository is the only shared state.** `OranDataRepository`
  (`model/oran-data-repository.h`) is a pure-virtual API;
  `OranDataRepositorySqlite` is the only backend. Adding a new metric means:
  new `Save*`/`Get*` pure virtuals on the base, an enum entry + prepared
  statement + `CREATE TABLE` in the SQLite backend, and a dispatch branch in
  `OranNearRtRicE2Terminator::ReceiveReport`.
- **Reporters are wired to ns-3 traces by the scenario, not by the module.**
  Scenario code does `uePhy->TraceConnectWithoutContext("DlCtrlSinr",
  MakeCallback(&OranReporterNrUeSinr::ReportSinr, sinrReporter))`. A reporter
  method converts trace arguments into a Report; unit conversion belongs here
  (e.g. the SINR reporter converts linear to dB before storing). A reporter may
  also carry state across trace callbacks to derive a metric that no single
  trace provides - `OranReporterAppLoss` counts packets over both a Tx and an Rx
  trace and turns the pair into a loss ratio in `GenerateReports()`, resetting
  the counters each time. One callback may also produce several Reports:
  `OranReporterNrGnbMeasReport` flattens one RRC Measurement Report into one
  Report per measured cell, serving and neighbour. Note that the Reporter's
  `OranReportTrigger` drains the accumulated reports via `GenerateReports()`, so
  a unit test that flushes manually must configure a trigger interval longer
  than the run or it will race the trigger.
- **`OranHelper` is factory-based, not immediate.** `SetDefaultLogicModule`,
  `AddReporter`, `SetE2NodeTerminator` etc. only configure `ObjectFactory`s.
  Nothing is instantiated until `CreateNearRtRic()` / `DeployTerminators()`.
  `AddReporter`'s factory list is cleared after each `DeployTerminators` call,
  so it must be repopulated per node group.
- **Everything is configured through ns-3 attributes** (`SinrThresholdDb`,
  `HysteresisDb`, `LmQueryInterval`, ...), which is how examples and tests tune
  behavior without code changes.

### Adding files

`CMakeLists.txt` lists every source and header explicitly - a new model file is
invisible to the build and to `ns3/oran-module.h` until added to both
`SOURCE_FILES` and `HEADER_FILES`. ONNX/Torch-dependent sources go into the
`oran_onnxruntime_sources` / `oran_torch_sources` lists so they drop out when
the library is absent; examples depending on them must be wrapped in
`if(${OnnxRuntime_FOUND})` in `examples/CMakeLists.txt`.

The module defines `ENABLE_ORAN` as both a CMake-visible target definition and
a C++ macro, so downstream code can guard `#include <ns3/oran-module.h>` and
link optionally.

## Testing Conventions

`test/oran-test-suite.cc` holds the whole suite (`oran`, `Type::UNIT`, all
`Duration::QUICK`). The NR tests deliberately avoid standing up a 5G-LENA radio
stack:

- `CreateNrTestRic(dbFile)` builds a RIC with `OranLmNoop` + `OranCmmNoop` and a
  fresh SQLite file (the file is `std::remove`d first - each test uses its own
  db name).
- State is seeded by calling `ric->Data()->RegisterNodeNrGnb(...)`,
  `SaveNrUeSinr(...)` etc. directly, inside a `Simulator::Schedule` lambda, then
  `Simulator::Run()` / `Destroy()`.
- LM decision logic is tested by calling `lm->Run()` directly. Concrete LMs
  declare the `Run()` override **public** even though it is `protected` in
  `OranLm` - keep that when adding an LM you intend to unit test.
- End-to-end dispatch is tested by handing a hand-built Report to
  `ric->GetE2Terminator()->ReceiveReport(report)`.
