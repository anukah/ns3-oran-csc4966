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

T_OFFSET = 3.0          # g_tOffset in the example
UE_NODEID = 3
UE_Z = 1.5
STEP_NS = 100_000_000   # 0.1 s

# cellid -> (PCI, gNB position x,y,z)
CELLS = {
    1: (331, (0.0, 0.0, 36.0)),
    2: (286, (592.62, -431.55, 58.0)),
}


def dist3d(x, y, gnb):
    return math.sqrt((x - gnb[0]) ** 2 + (y - gnb[1]) ** 2 + (UE_Z - gnb[2]) ** 2)


def nearest_le(rows, t):
    """Last (time,val...) row with time <= t; fall back to first row."""
    best = None
    for r in rows:
        if r[0] <= t:
            best = r
        else:
            break
    return best if best is not None else (rows[0] if rows else None)


def interp_pos(loc, t):
    """Linear interpolation of (x,y) at time t from sorted [(time,x,y)]."""
    if t <= loc[0][0]:
        return loc[0][1], loc[0][2]
    if t >= loc[-1][0]:
        return loc[-1][1], loc[-1][2]
    for i in range(1, len(loc)):
        if loc[i][0] >= t:
            t0, x0, y0 = loc[i - 1]
            t1, x1, y1 = loc[i]
            f = (t - t0) / (t1 - t0)
            return x0 + f * (x1 - x0), y0 + f * (y1 - y0)
    return loc[-1][1], loc[-1][2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("db")
    ap.add_argument("-o", "--out", default="vienna-ho-extracted-0.1s.csv")
    args = ap.parse_args()

    con = sqlite3.connect(args.db)
    cur = con.cursor()

    # RSRP/RSRQ: group by timestamp -> {cellid: (rsrp, serving)}
    rsrp_by_t = {}
    for t, cellid, rsrp, serving in cur.execute(
        "SELECT simulationtime,cellid,rsrp,serving FROM nruersrprsrq "
        f"WHERE nodeid={UE_NODEID} ORDER BY simulationtime"
    ):
        rsrp_by_t.setdefault(t, {})[cellid] = (rsrp, serving)
    rsrp_times = sorted(rsrp_by_t)

    # SINR per cell: {cellid: sorted [(time, sinr_db)]}
    sinr_by_cell = {}
    for t, cellid, sinr in cur.execute(
        "SELECT simulationtime,cellid,sinr FROM nruesinr "
        f"WHERE nodeid={UE_NODEID} ORDER BY simulationtime"
    ):
        sinr_by_cell.setdefault(cellid, []).append((t, sinr))

    # UE location
    loc = list(cur.execute(
        "SELECT simulationtime,x,y FROM nodelocation "
        f"WHERE nodeid={UE_NODEID} ORDER BY simulationtime"
    ))

    # Handover-command times (LM emitted a command), aligned to the 0.1 s grid
    ho_times = {t for (t,) in cur.execute(
        "SELECT DISTINCT simulationtime FROM lmcommand"
    )}

    # Grid spans the rows we have RSRP for, snapped to 0.1 s relative to T_OFFSET.
    off_ns = int(T_OFFSET * 1e9)
    t_start = max(off_ns, rsrp_times[0])
    t_end = min(rsrp_times[-1], loc[-1][0]) if loc else rsrp_times[-1]
    # First grid point >= t_start that is a multiple of 0.1 s after T_OFFSET
    n0 = math.ceil((t_start - off_ns) / STEP_NS)

    rows_out = []
    n = n0
    while True:
        t_ns = off_ns + n * STEP_NS
        if t_ns > t_end:
            break
        n += 1

        # --- RSRP / serving-neighbor from the nearest RSRP timestamp <= t_ns ---
        rt = max((x for x in rsrp_times if x <= t_ns), default=rsrp_times[0])
        cells = rsrp_by_t[rt]
        serving_cell = next((c for c, (_, s) in cells.items() if s), None)
        if serving_cell is None:
            serving_cell = min(cells, key=lambda c: -cells[c][0])  # highest rsrp
        neigh_cell = next((c for c in cells if c != serving_cell), None)

        serving_pci = CELLS[serving_cell][0]
        rsrp_serving = cells[serving_cell][0]
        neigh_pci = CELLS[neigh_cell][0] if neigh_cell else ""
        rsrp_neigh = cells[neigh_cell][0] if neigh_cell else ""

        # --- SINR of serving cell: last sample <= t_ns ---
        srows = sinr_by_cell.get(serving_cell, [])
        s = nearest_le(srows, t_ns)
        sinr_serving = s[1] if s else ""

        # --- position + distances ---
        ux, uy = interp_pos(loc, t_ns) if loc else ("", "")
        dser = dist3d(ux, uy, CELLS[serving_cell][1]) if loc else ""
        dnei = dist3d(ux, uy, CELLS[neigh_cell][1]) if (loc and neigh_cell) else ""

        ho = 1 if t_ns in ho_times else 0

        rows_out.append([
            round((t_ns - off_ns) / 1e9, 3),
            serving_pci, rsrp_serving, neigh_pci, rsrp_neigh,
            sinr_serving, ux, uy, dser, dnei, ho,
        ])

    con.close()

    # Collapse consecutive handover flags: a single HO is sometimes logged on
    # two adjacent 0.1 s ticks (the LM re-emits the command on the next query
    # before the HO completes). Keep the first, zero the second so the training
    # label marks one positive per HO event.
    HO = 10  # index of ho_command_issued within each row
    for i in range(1, len(rows_out)):
        if rows_out[i][HO] == 1 and rows_out[i - 1][HO] == 1:
            rows_out[i][HO] = 0

    header = ("sim_time_s,serving_pci,rsrp_serving_dbm,neighbor_pci,"
              "rsrp_neighbor_dbm,sinr_serving_db,ue_x,ue_y,"
              "dist_serving_m,dist_neighbor_m,ho_command_issued")
    with open(args.out, "w") as f:
        f.write(header + "\n")
        for r in rows_out:
            f.write(",".join(str(c) for c in r) + "\n")
    print(f"Wrote {len(rows_out)} rows to {args.out}")


if __name__ == "__main__":
    main()
