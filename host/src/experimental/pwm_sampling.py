import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
# from IPython.core.interactiveshell import InteractiveShell

# InteractiveShell.ast_node_interactivity = "all"

# pd.options.display.max_rows = 30
# print(pd.options.display.max_rows)

# pd.set_option('display.height', 500)
pd.set_option('display.min_rows', 30)
# pd.set_option('display.max_rows', 30)

PWM_F = 1000
PWM_T = 1.0 / PWM_F

SAMPLING_F = 100000
SAMPLING_T = 1.0 / SAMPLING_F

# def PWM(time_in_cycle, pwm_fraction) -> int:
#     fraction_in_cycle = (time_in_value / PWM_T)
#     return 1 if fraction_in_cycle < pwm_fraction else 0

sampling = []
t = 0.0
while t <= 6.0:
    time_in_ramp_cycle = t % 2
    time_in_pwm_cycle = t % PWM_T
    fraction_in_pwm_cycle = time_in_pwm_cycle / PWM_T
    if time_in_ramp_cycle <= 1.0:
        desired_fraction = time_in_ramp_cycle
    else:
        desired_fraction = 2 - time_in_ramp_cycle
    v = 1 if fraction_in_pwm_cycle <= desired_fraction else 0
    # sampling.append((t, desired_fraction, v ))
    sampling.append(v)
    t += SAMPLING_T

df = pd.DataFrame(sampling, columns=['v'])

print(df)

plt.plot(df['v'].rolling(590).mean(),label= 'Average')

plt.show()

