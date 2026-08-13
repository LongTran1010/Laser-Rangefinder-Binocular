import pandas as pd
from alphabeta_core import *

df = pd.DataFrame({
    "timestamp_ms": [0, 100, 200, 300, 400, 500],
    "raw_m":        [100, 101, 102, 103, 104, 105],
    "status":       [MEAS_OK]*6,
    "fps":          [10]*6
})

cfg = ABConfig(alpha=0.6, beta=0.2, gate_threshold_m=10.0)
out = run_tracker_on_dataframe(df, cfg)
print(out[["rawDistanceM", "filteredDistanceM", "rangeRateMps", "trackState"]])