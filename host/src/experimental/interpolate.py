
import math
import matplotlib.pyplot as plt
import numpy as np
from scipy.interpolate import CubicSpline

# CR_TABLE = [(25, 100000), (40, 50900), (50, 33450), (60, 22480), (80, 10800), (85, 9094),
#            (100, 5569), (120, 3058), (140, 1770), (160, 1074), (180, 679.3), (200, 445.2),
#            (220, 301.6), (240, 210.4), (260, 150.7), (280, 110.5), (300, 82.78)]

# CR_TABLE = [
#             (200, 445.2),
#             (220, 301.6), (240, 210.4), (260, 150.7), (280, 110.5), (300, 82.78)]

CR_TABLE = [
            (200, 445.2),
            (220, 301.6), (240, 210.4), (260, 150.7), (280, 110.5)]

def select_x():
  result = []
  for item in CR_TABLE:
    result.append(item[1])
  result.reverse()
  return result

def select_y():
  result = []
  for item in CR_TABLE:
    result.append(item[0])
  result.reverse()
  return result

x = select_x()
y = select_y()

xnew = np.linspace(x[0], x[-1], num=10000)
# ynew = np.interp(xnew, x, y)

spl = CubicSpline(x, y)


plt.plot(xnew, spl(xnew), '-')
plt.plot(x, y, 'o', label='data')
plt.show()





