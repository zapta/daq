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


@dataclass(frozen=True)
class TestInfo:
    """Contains the information of a single test as read from the tests file."""
    test_name: str
    start_ms: int
    end_ms: int





def load_tests_infos(tests_file_path: str) -> List[TestInfo]:
    """Extract the tests information from the tests file."""
    logger.info(f"Loading tests infos from the tests file [{tests_file_path}]")
    df = pd.read_csv(tests_file_path, delimiter=',')
    result = []
    for i, row in df.iterrows():
        test_name = row["Test"]
        start_time_ms = row["Start[ms]"]
        end_time_ms = row["End[ms]"]
        result.append(TestInfo(test_name, start_time_ms, end_time_ms))
    return result


@dataclass(frozen=True)
class ChannelInfo:
    """Contains the information of a single channel as read from channels file."""
    channel_name: str
    channel_type: str
    field_name: str
    num_values: int
    file_name: str


def load_channels_infos(channels_file_path: str) -> List[ChannelInfo]:
    """Extract the channels information from the channels file."""
    logger.info(f"Loading channels infos from the channels file [{channels_file_path}]")
    df = pd.read_csv(channels_file_path, delimiter=',')
    result = []
    for i, row in df.iterrows():
        channel_name = row["Name"]
        channel_type = row["Type"]
        value_field = row["Field"]
        num_values = row["Values"]
        file_name = row["File"]
        result.append(ChannelInfo(channel_name, channel_type, value_field, num_values,  file_name))
    return result


def extract_test_data(channel_df: pd.DataFrame, test_info: TestInfo, original_value_column: str,
                      new_value_column: str):
    """Extracts data of a single test."""
    logger.info(f"Extracting: {test_info}")
    # Load original
    # df = pd.read_csv(data_file_path, delimiter=',')
    # Select columns of interest
    df = channel_df[['T[ms]', original_value_column]]
    # Extract rows in the test time range
    df = df[df['T[ms]'].between(test_info.start_ms, test_info.end_ms)]
    # Normalize time to test start time.
    df['T[ms]'] -= test_info.start_ms
    # Rename the value column
    if original_value_column != new_value_column:
      df.rename(columns={original_value_column: new_value_column}, inplace=True)
    # All done
    return df


