# Common functionality for processing the data files.

from __future__ import annotations

import argparse
import logging
from typing import Tuple, Optional, List, Dict
import os
import pandas as pd
# from lib.sys_config import SysConfig
import matplotlib.pyplot as plt
from dataclasses import dataclass

# Initialized by main().
# sys_config: SysConfig = None

logger = logging.getLogger("main")

# parser = argparse.ArgumentParser()
# parser.add_argument("--input_dir",
#                     dest="input_dir",
#                     default="",
#                     help="Input directory with channels .csv files.")
# parser.add_argument("--output_dir",
#                     dest="output_dir",
#                     default="",
#                     help="Output directory for generated files.")

# args = parser.parse_args()


@dataclass
class TestRange:
    """Represent a single test time range"""
    test_name: str
    start_ms: int
    end_ms: int


# def input_file_path(basic_name: str) -> str:
#     if args.input_dir:
#         return os.join(args.input_dir, basic_name)
#     return basic_name


# def output_file_path(basic_name: str) -> str:
#     if args.output_dir:
#         return os.join(args.output_dir, basic_name)
#     return basic_name


def load_test_ranges(tests_file_path: str) -> List[TestRange]:
    """Extract the tests names and ranges from the markers file."""
    logger.info(f"Loading test ranges from tests file [{tests_file_path}]")
    df = pd.read_csv(tests_file_path, delimiter=',')
    result = []
    for i, row in df.iterrows():
        test_name = row["Test"]
        start_time_ms = row["Start[ms]"]
        end_time_ms = row["End[ms]"]
        result.append(TestRange(test_name, start_time_ms, end_time_ms))
    return result


def extract_test_data(channel_df: pd.DataFrame, test_range: TestRange, original_value_column: str,
                      new_value_column: str):
    """Extracts data of a single test range."""
    # logger.info(f"Extracting test range from file [{data_file_path}]")
    logger.info(f"Extracting: {test_range}")
    # Load original
    # df = pd.read_csv(data_file_path, delimiter=',')
    # Select columns of interest
    df = channel_df[['T[ms]', original_value_column]]
    # Extract rows in test range
    df = df[df['T[ms]'].between(test_range.start_ms, test_range.end_ms)]
    # Normalize time to test start time.
    df['T[ms]'] -= test_range.start_ms
    # Rename the value column
    if original_value_column != new_value_column:
      df.rename(columns={original_value_column: new_value_column}, inplace=True)
    # All done
    return df


