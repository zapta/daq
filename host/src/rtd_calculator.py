from UliEngineering.Physics.RTD import pt1000_temperature, ptx_temperature
import functools

converter =   functools.partial(ptx_temperature, 1000)
for r in [803.1, 1000, 1194.0, 1385.1, 1573.3, 1758.6, 1941.0]:
        print(f"{r} ohms -> {converter(r)} C")
