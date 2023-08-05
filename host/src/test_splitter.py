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
                    default="",
                    help="Input directory with channels .csv files.")
parser.add_argument("--output_dir",
                    dest="output_dir",
                    default="",
                    help="Output directory for generated files.")

args = parser.parse_args()


class TestRange:

    def __init__(self, test_name, start_ms, end_ms):
        self.name = test_name
        self.start_ms = start_ms
        self.end_ms = end_ms

    def __str__(self):
        return f"Test \"{self.name}\" {self.start_ms} -> {self.end_ms} ({self.end_ms - self.start_ms} ms)"
      
    def __repr__(self):
       return str(self)
    
def input_file_path(basic_name: str) -> str:
  if args.input_dir:
    return os.join(args.input_dir, basic_name)
  return basic_name 
     
def load_test_ranges(markers_file_path: str) -> List[TestRange]:
    logger.info(f"Loading test ranges from marker file {markers_file_path}")
    rows = pd.read_csv(markers_file_path, delimiter=',')
    result = []
    current_test_name = None
    current_test_start_ms = None
    for i, row in rows.iterrows():
        marker_time_ms = row["T[ms]"]
        marker_type = row["MRKR[type]"]
        marker_value = row["MRKR[value]"]
        if marker_type == "begin":
            assert marker_value, "Begin marker has an empty test name"
            assert current_test_name is None, f"Missing end for test {current_test_name}"
            current_test_name = marker_value
            current_test_start_ms = marker_time_ms
        elif marker_type == "end":
            assert marker_value, "End marker has an empty test name"
            assert current_test_name, "Missing start marker for test {marker_value}"
            assert marker_value == current_test_name, "Test name mismatch"
            result.append(TestRange(current_test_name, current_test_start_ms, marker_time_ms))
            current_test_name = None
            current_test_start_ms = None
        else:
            # Ignore other trypes.
            continue

    assert current_test_name is None, "Missing test end"
    return result


def main():
    global sys_config
    
    logger.info("Test Splitter started.")

    sys_config = SysConfig()
    sys_config.load_from_file("sys_config.toml")
    # logger.info(f"Input dir : [{args.input_dir}]")
    # logger.info(f"Output dir: [{args.output_dir}]")

    # Load test ranges
    markers_file_path = input_file_path('_channel_mrkr.csv')
    test_ranges = load_test_ranges(markers_file_path)
    logger.info(f"Found {len(test_ranges)} test ranges.")
    for test_range in test_ranges:
      logger.info(f"- {test_range}")
      
      
    logger.info(f"All done.")


if __name__ == "__main__":
    main()
