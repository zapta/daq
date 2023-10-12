# A thin wrapper around the stm32loader.

# Build on top of stm32loader
# https://pypi.org/project/stm32loader/
# https://github.com/florisla/stm32loader

import stm32loader.main as loader
import sys
import argparse
import os.path

# Local imports
sys.path.insert(0, "..")
from lib.sys_config import SysConfig


parser = argparse.ArgumentParser()
parser.add_argument(
    "--sys_config",
    dest="sys_config",
    default="sys_config.toml",
    help="Path to system configuration file.",
)
parser.add_argument(
    "--firmware",
    dest="firmware",
    default="controller_firmware.bin",
    help="Path to firmware .bin file.",
)

parser.add_argument(
    "--dry_run",
    dest="dry_run",
    default=False,
    action=argparse.BooleanOptionalAction,
    help="If true, ID the MCU and exit without flashing.",
)
args = parser.parse_args()


sys_config = SysConfig()
sys_config.load_from_file(args.sys_config)
serial_port = sys_config.data_link_port()


print(f"Dry run mode: {args.dry_run}", flush=True)
print(f"Sys config file: {args.sys_config}", flush=True)
print(f"Firmware file: {args.firmware}", flush=True)
print(f"Serial port: {serial_port}", flush=True)


# Construct the loader command line params
params = []
params.extend(["-p", serial_port])
params.extend(["-f", "H7"])

if not args.dry_run:
    params.extend(["-e"])
    params.extend(["-w"])
    params.extend(["-v"])
    if not os.path.exists(args.firmware):
        print(f"Firmware file {args.firmware} not found. Check the --firmware flag.")
        sys.exit(1)
    params.extend([args.firmware])
    
#print(f"Constructed params: {params}", flush=True)
print(f"Equivalent command: {'stm32loader ' + " ".join(params) }", flush=True)

# Call the loader
loader.main(*params)
