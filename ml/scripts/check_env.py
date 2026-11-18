import struct
import sys

import matplotlib
import numpy as np
import pandas as pd
import serial
import sklearn
from sklearn.preprocessing import StandardScaler


def main():
    bits = struct.calcsize("P") * 8
    print(f"Python {sys.version.split()[0]} ({bits}-bit) at {sys.executable}")
    for module in (np, pd, matplotlib, sklearn, serial):
        print(f"{module.__name__} {module.__version__}")
    values = pd.DataFrame({"sample": np.array([1.0, 3.0])})
    np.testing.assert_allclose(StandardScaler().fit_transform(values).ravel(), [-1.0, 1.0])
    print("Environment OK")


if __name__ == "__main__":
    main()
