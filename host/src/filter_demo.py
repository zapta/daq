
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import scipy
import sys

# Based on https://youtu.be/juYqcck_GfU

input_file: str = "_channel_lc1.csv"

print(f"Reading file {input_file}")
# We extract one sec of signal + 500 samples before and after for the end conditions.
data: pd.DataFrame = pd.read_csv(input_file, nrows = 500+501+500, dtype= {"T[ms]": np.int32, "LC1[g]": np.float32})
print(f"File read ok")
# print(data.count())
print("\nData types:")
print(data.dtypes)
print("\nData:")
print(data)

t = (data[["T[ms]"]] / 1000) - 1.0
t = np.array(t)

s = data[["LC1[g]"]]
s = np.array(s)
s = s - s.mean()


t0 = t[0]
tn = t[-1]
ts = ((tn - t0) / (len(t)-1))
fs = 1/ ts
print(f"ts: {ts}, fs: {fs}")
# fs = 1 / ()

fnyq = fs / 2
order = 2

plt.plot(t[500:1001], s[500:1001], color="lightgrey")
for fcut in [100, 25, 5]:
  b, a = scipy.signal.butter(order, [fcut/ fnyq], 'lowpass', analog=False)
  y = scipy.signal.filtfilt(b, a, s, axis=0)
  plt.plot(t[500:1001], y[500:1001])


sys.stdout.flush()
plt.show()


