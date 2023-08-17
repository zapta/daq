#!python

# TODO: Make this a unit test of the RTD conversion.


from __future__ import annotations
from lib.sys_config import SysConfig


def main():
    # global byte_count, packet_count, point_count, sys_config
    sys_config = SysConfig()
    sys_config.load_from_file("/Users/user/projects/daq/repo/host/src/sys_config.toml")
    temp_config = sys_config.temperature_config("TMP1")
    assert temp_config
    r = 1106.24
    t = temp_config.resistance_to_c(1106.24)
    print(f"{r} -> {t}")
    t = temp_config.resistance_to_c(1106.24)
    print(f"{r} -> {t}")


if __name__ == "__main__":
    main()
