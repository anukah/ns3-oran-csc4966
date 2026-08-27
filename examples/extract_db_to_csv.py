"""Extract 0.1 s metrics from an O-RAN SQLite DB into the vienna-ho dataset CSV format.

Output columns:
  sim_time_s,serving_pci,rsrp_serving_dbm,neighbor_pci,rsrp_neighbor_dbm,
  sinr_serving_db,ue_x,ue_y,dist_serving_m,dist_neighbor_m,ho_command_issued

sim_time_s is relative time = absolute_sim_time - T_OFFSET (matches the live
metrics logger, which starts at Seconds(g_tOffset)).
"""
import argparse
import math
import sqlite3

# Simulation parameters - must match the ns-3 scenario that produced the DB
T_OFFSET = 3.0          # Start time offset (g_tOffset in the example scenario)
UE_NODEID = 3           # NS-3 node ID of the UE we're extracting data for
UE_Z = 1.5              # UE antenna height in meters (for 3D distance calc)
STEP_NS = 100_000_000   # Default sampling interval in nanoseconds (0.1 s)

# Map from DB cell ID to (PCI, gNB position (x, y, z))
# These must match the gNB configuration in the simulation script
CELLS = {
    1: (331, (0.0, 0.0, 36.0)),
    2: (286, (592.62, -431.55, 58.0)),
}


def dist3d(x, y, gnb):
    """Euclidean 3D distance from UE at (x, y, UE_Z) to a gNB at (gx, gy, gz)."""
    return math.sqrt((x - gnb[0]) ** 2 + (y - gnb[1]) ** 2 + (UE_Z - gnb[2]) ** 2)


def nearest_le(rows, t):
    """Find the last row with timestamp <= t (sample-and-hold lookup).

    Rows must be sorted by time. Falls back to the first row if no
    row has time <= t, or None if the list is empty.
    """
    best = None
    for r in rows:
        if r[0] <= t:
            best = r
        else:
            break  # rows are sorted, so no later row can match
    return best if best is not None else (rows[0] if rows else None)


def interp_pos(loc, t):
    """Linearly interpolate UE (x, y) position at time t.

    loc is a sorted list of (time_ns, x, y) tuples from the nodelocation
    table. Clamps to the first/last known position if t is outside the range.
    """
    if t <= loc[0][0]:
        return loc[0][1], loc[0][2]
    if t >= loc[-1][0]:
        return loc[-1][1], loc[-1][2]
    for i in range(1, len(loc)):
        if loc[i][0] >= t:
            t0, x0, y0 = loc[i - 1]
            t1, x1, y1 = loc[i]
            f = (t - t0) / (t1 - t0)  # interpolation factor
            return x0 + f * (x1 - x0), y0 + f * (y1 - y0)
    return loc[-1][1], loc[-1][2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("db")
    ap.add_argument("-o", "--out", default="vienna-ho-extracted-0.1s.csv")
    ap.add_argument("--step", type=float, default=STEP_NS / 1e9,
                    help="sampling interval in seconds (default 0.1)")
    ap.add_argument("--no-sinr", action="store_true",
                    help="omit the sinr_serving_db column")
    ap.add_argument("--segment", default=None,
                    help="value for an extra leading 'segment' column, so several runs "
                         "can be concatenated and split by segment rather than by row")
    args = ap.parse_args()

    step_ns = int(round(args.step * 1e9))

    con = sqlite3.connect(args.db)
    cur = con.cursor()

    # Load RSRP/RSRQ measurements
    # Build a dict: timestamp -> {cellid: (rsrp_dbm, is_serving_bool)}
    # This tells us which cell the UE is attached to and the signal strength
    # from each cell at every measurement instant.
    rsrp_by_t = {}
    for t, cellid, rsrp, serving in cur.execute(
        "SELECT simulationtime,cellid,rsrp,serving FROM nruersrprsrq "
        f"WHERE nodeid={UE_NODEID} ORDER BY simulationtime"
    ):
        rsrp_by_t.setdefault(t, {})[cellid] = (rsrp, serving)
    rsrp_times = sorted(rsrp_by_t)

    # Load SINR measurements 
    # Build a dict: cellid -> sorted list of (timestamp, sinr_db)
    # Used for sample-and-hold lookup of the serving cell's SINR.
    sinr_by_cell = {}
    for t, cellid, sinr in cur.execute(
        "SELECT simulationtime,cellid,sinr FROM nruesinr "
        f"WHERE nodeid={UE_NODEID} ORDER BY simulationtime"
    ):
        sinr_by_cell.setdefault(cellid, []).append((t, sinr))

    # Load UE positions
    # List of (timestamp, x, y) for linear interpolation at each grid tick.
    loc = list(cur.execute(
        "SELECT simulationtime,x,y FROM nodelocation "
        f"WHERE nodeid={UE_NODEID} ORDER BY simulationtime"
    ))

    # Load handover command timestamps
    # Set of timestamps where a command was actually DISPATCHED to a node, used
    # to label the ho_command_issued column (ML training target).
    #
    # This reads terminatorcommand, not lmcommand. The Logic Module re-proposes
    # the same handover on every query cycle until the UE is recorded on the new
    # cell, so lmcommand holds ~3 rows per handover and the Conflict Mitigation
    # Module drops all but the first ("Excluding a pending command" in
    # cmmaction). Labelling from lmcommand therefore marks several ticks per
    # event, including ticks AFTER the handover has already completed, where the
    # features already show the new serving cell - i.e. guaranteed false
    # positives. terminatorcommand holds exactly the commands that reached a
    # node, one per handover.
    ho_times = {t for (t,) in cur.execute(
        "SELECT DISTINCT simulationtime FROM terminatorcommand"
    )}

    # Build the 0.1 s sampling grid
    # All DB timestamps are in nanoseconds. We build a regular grid at
    # STEP_NS intervals, starting from T_OFFSET, covering the range where
    # both RSRP data and location data are available.
    off_ns = int(T_OFFSET * 1e9)
    t_start = max(off_ns, rsrp_times[0])
    t_end = min(rsrp_times[-1], loc[-1][0]) if loc else rsrp_times[-1]
    n0 = math.ceil((t_start - off_ns) / step_ns)  # first grid index >= t_start

    # Snap each dispatch to the grid tick at or before it. Dispatches are offset
    # from the query grid by the Logic Module's processing delay (10 ms in the
    # vienna examples), so they do not fall on a tick and cannot be matched
    # exactly. Rounding DOWN labels the last tick whose features precede the
    # command, which is the tick a predictor would have to fire on.
    ho_ticks = {off_ns + ((t - off_ns) // step_ns) * step_ns
                for t in ho_times if t >= off_ns}

    rows_out = []
    n = n0
    while True:
        t_ns = off_ns + n * step_ns  # current grid tick in nanoseconds
        if t_ns > t_end:
            break
        n += 1

        # Determine serving and neighbor cells from nearest RSRP snapshot
        rt = max((x for x in rsrp_times if x <= t_ns), default=rsrp_times[0])
        cells = rsrp_by_t[rt]
        # The DB marks the serving cell with a boolean flag
        serving_cell = next((c for c, (_, s) in cells.items() if s), None)
        if serving_cell is None:
            # Fallback: pick the cell with the strongest RSRP
            serving_cell = min(cells, key=lambda c: -cells[c][0])
        # The other cell is the neighbor (works for the 2-cell scenario)
        neigh_cell = next((c for c in cells if c != serving_cell), None)

        # Look up PCI and RSRP for both cells
        serving_pci = CELLS[serving_cell][0]
        rsrp_serving = cells[serving_cell][0]
        neigh_pci = CELLS[neigh_cell][0] if neigh_cell else ""
        rsrp_neigh = cells[neigh_cell][0] if neigh_cell else ""

        # SINR of serving cell (sample-and-hold: last value <= t_ns)
        srows = sinr_by_cell.get(serving_cell, [])
        s = nearest_le(srows, t_ns)
        sinr_serving = s[1] if s else ""

        # UE position (interpolated) and 3D distances to each gNB
        ux, uy = interp_pos(loc, t_ns) if loc else ("", "")
        dser = dist3d(ux, uy, CELLS[serving_cell][1]) if loc else ""
        dnei = dist3d(ux, uy, CELLS[neigh_cell][1]) if (loc and neigh_cell) else ""

        # Handover label: 1 if a command was dispatched during this tick
        ho = 1 if t_ns in ho_ticks else 0

        row = [
            round((t_ns - off_ns) / 1e9, 3),  # relative sim time in seconds
            serving_pci, rsrp_serving, neigh_pci, rsrp_neigh,
        ]
        if not args.no_sinr:
            row.append(sinr_serving)
        row += [ux, uy, dser, dnei, ho]
        if args.segment is not None:
            row.insert(0, args.segment)
        rows_out.append(row)

    con.close()

    # Deduplicate consecutive handover labels
    # Kept as a safety net. Labelling from terminatorcommand already yields one
    # positive per handover, but a step finer than the dispatch spacing, or a
    # scenario where two commands land in adjacent ticks, could still produce a
    # run. Keep only the first so each event maps to exactly one positive label
    # (important for balanced ML training). Note this collapses only ADJACENT
    # pairs, so a run of three would leave the first and the third.
    HO = len(rows_out[0]) - 1 if rows_out else 10  # ho_command_issued is the last column
    for i in range(1, len(rows_out)):
        if rows_out[i][HO] == 1 and rows_out[i - 1][HO] == 1:
            rows_out[i][HO] = 0

    # Write output CSV
    cols = ["sim_time_s", "serving_pci", "rsrp_serving_dbm",
            "neighbor_pci", "rsrp_neighbor_dbm"]
    if not args.no_sinr:
        cols.append("sinr_serving_db")
    cols += ["ue_x", "ue_y", "dist_serving_m", "dist_neighbor_m", "ho_command_issued"]
    if args.segment is not None:
        cols.insert(0, "segment")
    header = ",".join(cols)
    with open(args.out, "w") as f:
        f.write(header + "\n")
        for r in rows_out:
            f.write(",".join(str(c) for c in r) + "\n")
    print(f"Wrote {len(rows_out)} rows to {args.out}")


if __name__ == "__main__":
    main()
