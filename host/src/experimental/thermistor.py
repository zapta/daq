
import math

CR_TABLE = [(25, 100000), (40, 50900), (50, 33450), (60, 22480), (80, 10800), (85, 9094),
            (100, 5569), (120, 3058), (140, 1770), (160, 1074), (180, 679.3), (200, 445.2),
            (220, 301.6), (240, 210.4), (260, 150.7), (280, 110.5), (300, 82.78)]

R25 = 100000
LN_R25 = math.log(R25)
LN_R25_CUBE = LN_R25 * LN_R25 * LN_R25

def compute_ABC(beta, c):
    C = c
    B = 1.0 / beta
    A = 1.0 / (25.0 + 273.15) - B * LN_R25 - C * LN_R25_CUBE
    return (A, B, C)
     
def compute_T(A, B, C, R):   
    ln_R = math.log(R)
    ln_R_cube = ln_R * ln_R * ln_R 
    reciprocal_T = A + (B * ln_R) + (C * ln_R * ln_R * ln_R)
    return 1/reciprocal_T -  273.15

def compute_errors(beta, c):
    errors = []
    A, B, C = compute_ABC(beta, c)
    for item in CR_TABLE:
        T = item[0]
        R = item[1]
        computed_T = compute_T(A, B, C, R)
        errors.append([T, computed_T - T])
    return errors

def reduce_errors(errors_list):
    max_error = None
    max_error_T = None
    for item in errors_list:
        T = item[0]
        E = abs(item[1])
        if T < 200:
            continue
        if max_error is None or E > max_error:
            max_error = E
            max_error_T = T
    return (max_error_T, max_error)

min_error = None
min_error_T = None
for beta in range(4700, 4750):
    for ci in range(650, 750):
        c = ci * 10e-10
        errors = compute_errors(4725, 0.0000000706)
        T, error = reduce_errors(errors)
        if min_error is None or error < min_error:
          min_error = error
          min_error_T = T
print(T, min_error)




