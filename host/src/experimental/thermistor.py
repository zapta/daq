import math
import matplotlib.pyplot as plt
from typing import List, Optional
import numpy as np
from scipy.interpolate import CubicSpline

# (Temp C, R Ohms) pairs, increasing in temp.
DATA = [(25, 100000), (40, 50900), (50, 33450), (60, 22480), (80, 10800), (85, 9094), (100, 5569),
        (120, 3058), (140, 1770), (160, 1074), (180, 679.3), (200, 445.2), (220, 301.6),
        (240, 210.4), (260, 150.7), (280, 110.5), (300, 82.78)]


class SHFunc:

    def __init__(self, beta, c, t0, r0):
        """t0, r0 are the calibration point, e.g. at 25C."""
        self.C = c
        self.B = 1.0 / beta
        ln_r0 = math.log(r0)
        ln_r0_cube = ln_r0 * ln_r0 * ln_r0
        self.A = 1.0 / (t0 + 273.15) - self.B * ln_r0 - self.C * ln_r0_cube

    def eval_scalar(self, r):
        ln_r = math.log(r)
        ln_r_cube = ln_r * ln_r * ln_r
        reciprocal_r = self.A + (self.B * ln_r) + (self.C * ln_r_cube)
        return 1 / reciprocal_r - 273.15

    def eval_vect(self, r_vect):
        result = []
        for r in r_vect:
            result.append(self.eval_scalar(r))
        return result


def get_data_xy() -> List[List[int]]:
    r_vect = []
    t_vect = []
    for item in DATA:
        r_vect.append(item[1])
        t_vect.append(item[0])
    r_vect.reverse()
    t_vect.reverse()
    return (r_vect, t_vect)


def find_xlim(xdense, spl, min_y, max_y):
    """Find the subrange x for y range of interest."""
    x_min = None
    x_max = None
    for x in xdense:
        y = spl(x)
        if y >= min_y and y <= max_y:
            if x_min is None or x < x_min:
                x_min = x
            if x_max is None or x > x_max:
                x_max = x
    return [x_min, x_max]


x, y = get_data_xy()

sh25 = SHFunc(4725, 7.06e-8, 25, 100000)
sh220 = SHFunc(4725, 7.06e-8, 220, 301.6)
spl = CubicSpline(x, y)

# Interpolated (dense) x.
xdense = np.linspace(x[0], x[-1], num=1000000)

ylim = [200, 245]
# ylim = [225, 227]

xlim = find_xlim(xdense, spl, ylim[0], ylim[1])

print(f"xlim: {xlim}")

plt.plot(xdense, spl(xdense), '-', label="spline")
plt.plot(xdense, sh25.eval_vect(xdense), '-', label="sh25")
plt.plot(xdense, sh220.eval_vect(xdense), '-', label="sh220")
plt.plot(x, y, 'o', label='data')
plt.legend()
plt.xlim(xlim)
plt.ylim(ylim)
plt.show()

for d in DATA:
    R = d[1]
    T_data = d[0]
    T_sh25 = sh25.eval_scalar(R)
    T_sh220 = sh220.eval_scalar(R)
    T_spl = spl(R)

    print(f"{R:8.1f}  {T_data:6.2f}  {T_sh25-T_data: 5.2f} {T_sh220-T_data:5.2f} {T_spl-T_data:5.2f}")

#     y_data
