#!/usr/bin/env python3
"""Train and export the RSRP-difference lag handover classifier used by
vienna-ho-onnx-lm.cc.

Features, per 0.1 s sample:

    diff        = rsrp_neighbor_dbm - rsrp_serving_dbm
    diff_lag_1  = diff at t - 0.1 s
    ...
    diff_lag_5  = diff at t - 0.5 s

Target: ho_command_issued.

WHY THE LAGS CARRY THE MODEL. The label fires when the RIC dispatches the
handover, which is 256 ms of time-to-trigger plus 155-305 ms of RIC pipeline
after the A3 condition first holds - four to six samples. A classifier looking
only at the instantaneous diff therefore cannot separate the positive from the
four to six negatives just before it, because their diff is nearly identical.
The lag window is what makes the two distinguishable: it lets the model see
that diff has been above the margin for long enough.

That also means the exported model has the TTT and the RIC latency baked into
it. It is not a pure "is the neighbour stronger" classifier; it reproduces the
whole pipeline delay of the scenario it was trained on.

The ONNX graph is exported WITHOUT ZipMap so that the probability output is a
plain [N, 2] float tensor rather than a sequence of maps, which is what
OranLmNr2NrRsrpLagOnnxHandover reads to apply its DecisionThreshold.

The dataset is not kept under version control; regenerate it from a simulation
database with extract_db_to_csv.py before training. The exported model is
written next to this script, in examples/, alongside the other committed ONNX
models, so it is not left in the ns-3 root.

Usage:
    python3 contrib/oran/examples/vienna_ho_rf_lag.py \
        --csv vienna-fitted-dataset.csv
"""
import argparse
import os

import numpy as np
import pandas as pd
from skl2onnx import to_onnx
from skl2onnx.common.data_types import FloatTensorType
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import average_precision_score, classification_report, confusion_matrix
from sklearn.model_selection import train_test_split

NUM_LAGS = 5
TARGET = "ho_command_issued"

# Default output sits next to this script rather than in the caller's working
# directory, which for `./ns3 run` is the ns-3 root.
DEFAULT_OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "vienna_ho_rf_lag.onnx")


def build_features(df, num_lags=NUM_LAGS):
    """Return (X, y, feature_names) with the lag columns added."""
    out = df[["sim_time_s", "rsrp_serving_dbm", "rsrp_neighbor_dbm", TARGET]].copy()
    out["diff"] = out["rsrp_neighbor_dbm"] - out["rsrp_serving_dbm"]
    for i in range(1, num_lags + 1):
        out[f"diff_lag_{i}"] = out["diff"].shift(i)
    out = out.dropna().copy()
    cols = ["diff"] + [f"diff_lag_{i}" for i in range(1, num_lags + 1)]
    return out[cols], out[TARGET], cols


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="vienna-fitted-dataset.csv")
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--trees", type=int, default=100)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--lags", type=int, default=NUM_LAGS,
                    help="Number of lagged differences. The window this spans must reach back "
                         "to the instant the A3 condition became true, which is 4 to 6 samples "
                         "before the label, or the crossing point is outside the features.")
    args = ap.parse_args()

    df = pd.read_csv(args.csv)
    X, y, cols = build_features(df, args.lags)
    print(f"{args.csv}: {len(X)} usable rows, {int(y.sum())} positives "
          f"({100.0 * y.mean():.2f} %)")
    print(f"features: {cols}")

    def fit(Xtr, ytr):
        m = RandomForestClassifier(n_estimators=args.trees,
                                   random_state=args.seed,
                                   class_weight="balanced")
        m.fit(Xtr, ytr)
        return m

    # --- Evaluation 1: random split, as in the notebook -------------------
    # Rows are consecutive samples of one continuous trajectory and each row
    # shares five of its six features with its neighbour, so a shuffled split
    # puts near-duplicates of the test rows into training. The score below is
    # therefore optimistic and is reported only for comparison.
    Xtr, Xte, ytr, yte = train_test_split(X, y, test_size=0.4,
                                          stratify=y, random_state=args.seed)
    m_rand = fit(Xtr, ytr)
    p_rand = m_rand.predict_proba(Xte)[:, 1]
    print("\n--- random split (leaky, notebook setting) ---")
    print(confusion_matrix(yte, m_rand.predict(Xte)))
    print(f"PR-AUC: {average_precision_score(yte, p_rand):.4f}")

    # --- Evaluation 2: chronological split, no leakage --------------------
    cut = int(len(X) * 0.6)
    Xtr2, Xte2 = X.iloc[:cut], X.iloc[cut:]
    ytr2, yte2 = y.iloc[:cut], y.iloc[cut:]
    m_chrono = fit(Xtr2, ytr2)
    p_chrono = m_chrono.predict_proba(Xte2)[:, 1]
    print("\n--- chronological split (honest) ---")
    print(confusion_matrix(yte2, m_chrono.predict(Xte2)))
    print(classification_report(yte2, m_chrono.predict(Xte2), zero_division=0))
    print(f"PR-AUC: {average_precision_score(yte2, p_chrono):.4f}")

    # --- Export: refit on everything, which is what gets deployed ---------
    model = fit(X, y)
    onx = to_onnx(model,
                  X.to_numpy().astype(np.float32),
                  initial_types=[("input", FloatTensorType([None, len(cols)]))],
                  options={id(model): {"zipmap": False}},
                  target_opset=15)
    with open(args.out, "wb") as f:
        f.write(onx.SerializeToString())
    print(f"\nwrote {args.out} ({len(onx.SerializeToString())} bytes), "
          f"trained on all {len(X)} rows")

    # Sanity check the exported graph against sklearn on the training data.
    import onnxruntime as ort

    sess = ort.InferenceSession(args.out, providers=["CPUExecutionProvider"])
    names = [o.name for o in sess.get_outputs()]
    print(f"onnx outputs: {names}")
    onx_prob = sess.run(None, {"input": X.to_numpy().astype(np.float32)})[1][:, 1]
    skl_prob = model.predict_proba(X)[:, 1]
    print(f"max |onnx - sklearn| probability difference: "
          f"{np.abs(onx_prob - skl_prob).max():.3e}")
    fires = int((onx_prob > 0.5).sum())
    print(f"onnx fires on {fires} of {len(X)} training rows "
          f"(labels: {int(y.sum())})")


if __name__ == "__main__":
    main()
