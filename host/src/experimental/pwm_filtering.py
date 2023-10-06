import pandas as pd
import numpy as np
import scipy
import matplotlib.pyplot as plt

# from IPython.core.interactiveshell import InteractiveShell

# InteractiveShell.ast_node_interactivity = "all"

# pd.options.display.max_rows = 30
# print(pd.options.display.max_rows)

# pd.set_option('display.height', 500)
pd.set_option('display.min_rows', 30)
# pd.set_option('display.max_rows', 30)

PWM_F = 500
PWM_FRACTION = 0.3
SAMPLING_F = 390000
PWM_CYCLES_MEAN = 50
TIME_RANGE = 10
FILTER_F = 10

PWM_T = 1.0 / PWM_F
SAMPLING_T = 1.0 / SAMPLING_F
MEAN_N = int(PWM_CYCLES_MEAN * SAMPLING_F / PWM_F)

# def PWM(time_in_cycle, pwm_fraction) -> int:
#     fraction_in_cycle = (time_in_value / PWM_T)
#     return 1 if fraction_in_cycle < pwm_fraction else 0

sampling = []
t = 0.0
while t <= TIME_RANGE:
    # time_in_ramp_cycle = t % 2
    time_in_pwm_cycle = t % PWM_T
    fraction_in_pwm_cycle = time_in_pwm_cycle / PWM_T
    # if time_in_ramp_cycle <= 1.0:
    v = 1 if fraction_in_pwm_cycle <= PWM_FRACTION else 0
    # sampling.append((t, desired_fraction, v ))
    sampling.append([t, v])
    t += SAMPLING_T

df = pd.DataFrame(sampling, columns=['t', 'Raw'])

df['Avg'] = df["Raw"].rolling(MEAN_N, center=True).mean().round(2)

b, a = scipy.signal.butter(2, [FILTER_F / (SAMPLING_F / 2)], 'low')

df['LP'] = scipy.signal.filtfilt(b, a, df['Raw'], axis=0)

print(f"Raw mean: {df['Raw'].mean()}")
# print(df)

# plt.plot(df['v'].rolling(590).mean(),label= 'Average')
# df.plot(x="t", y="Raw")
# df.plot(x="t", y="Avg")
# df.plot(x="t", y="Avg")
df.plot(x="t", y=["Avg", "LP"])

plt.show()
