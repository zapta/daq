#!python

# To collect the test snippets.

from __future__ import annotations

import argparse
import logging
from typing import Tuple, Optional, List, Dict
import os
import glob
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

def load_test_ranges():
  pass
  


def main():
    global sys_config
    sys_config = SysConfig()
    sys_config.load_from_file("sys_config.toml")
    logger.info("Test collector started.")
    logger.info(f"Input dir : [{args.input_dir}]")
    logger.info(f"Output dir: [{args.output_dir}]")

    logger.info(f"All done.")


if __name__ == "__main__":
    main()
