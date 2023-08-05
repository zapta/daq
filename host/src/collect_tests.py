#!python

# To collect the test snippets.

from __future__ import annotations

import argparse
import logging
from typing import Tuple, Optional, List, Dict
import os
import glob
import pandas as pd
from lib.sys_config import SysConfig

# Initialized by main().
sys_config: SysConfig = None

logging.basicConfig(
    level=logging.INFO,
    format="%(relativeCreated)07d %(levelname)-7s %(filename)-10s: %(message)s",
)
logger = logging.getLogger("main")

parser = argparse.ArgumentParser()
parser.add_argument("--input_dir",
                    dest="input_dir",
                    default=".",
                    help="Input directory with channels .csv files.")
parser.add_argument("--output_dir",
                    dest="output_dir",
                    default=".",
                    help="Output directory for generated files.")

args = parser.parse_args()


class TestRange:
  
  def __init__(self, test_name, start_ms, end_ms):
    self.name = test_name
    self.start_ms = start_ms
    self.end_ms = end_ms
    
    


def main():
    global sys_config
    sys_config = SysConfig()
    sys_config.load_from_file("sys_config.toml")
    logger.info("Test collector started.")
    logger.info(f"Input dir : [{args.input_dir}]")
    logger.info(f"Output dir: [{args.output_dir}]")

    rows = pd.read_csv('_channel_mrkr.csv', delimiter=',')
    current_test_name = None
    current_test_start_ms = None
    for i, row in rows.iterrows():
        # Note: access row with ['col']
        marker_time_ms = row["T[ms]"]
        marker_type = row["MRKR[type]"]
        marker_value = row["MRKR[value]"]
        # print(f"Row [{i}]: type [{marker_type}], value [{marker_value}]")
        if marker_type == "begin":
          assert current_test_name is None, "Missing test end"
          assert marker_value, "Begin marker has a empty test name"
          current_test_name = marker_value
          current_test_start_ms = marker_time_ms
        elif marker_type == "end":
          assert current_test_name, "Missing test start"
          assert marker_value == current_test_name, "Test name mismatch"
          print(f"Test [{current_test_name}] {current_test_start_ms} -> {marker_time_ms}  ({marker_time_ms - current_test_start_ms })")
          current_test_name = None
          current_test_start_ms = None
        else:
          # Ignore other trypes.
          continue
        
    assert current_test_name is None, "Missing test end"
    logger.info(f"All done.")


if __name__ == "__main__":
    main()
