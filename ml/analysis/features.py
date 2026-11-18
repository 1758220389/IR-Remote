"""Variable-length, timestamp-aware IMU features in physical units."""
import numpy as np


def extract(t_ms, accel_g, gyro_dps, bias_dps, duration_ms):
    t = np.asarray(t_ms, dtype=np.int64)
    dt = ((t[1:] - t[:-1]) % 2**32) / 1000
    a = np.asarray(accel_g)
    g = np.asarray(gyro_dps) - bias_dps

    def integral(v):
        return np.sum((v[:-1] + v[1:]) * (dt[:, None] / 2), axis=0)

    energy = integral(g**2)
    result = dict(duration_ms=duration_ms, sample_span_ms=int(round(dt.sum() * 1000)),
                  sample_count=len(t), gyro_z_energy_ratio=energy[2] / energy.sum() if energy.sum() else 0,
                  accel_norm_range_g=np.ptp(np.linalg.norm(a, axis=1)))
    for stem, values, unit in (
        ("gyro_integral", integral(g), "deg"),
        ("gyro_peak_abs", np.max(np.abs(g), axis=0), "dps"),
        ("gyro_abs_integral", integral(np.abs(g)), "deg"),
        ("gyro_energy", energy, "deg2_per_s"),
        ("accel_range", np.ptp(a, axis=0), "g"),
        ("accel_total_variation", np.abs(np.diff(a, axis=0)).sum(axis=0), "g"),
    ):
        result.update({f"{stem}_{axis}_{unit}": value for axis, value in zip("xyz", values)})
    return result
