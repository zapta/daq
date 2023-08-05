# A program that collect data from multiple tests.

# TODO: Currently supports channel ldc1. Generalize using command line flags.

from __future__ import annotations

import argparse
import logging
from typing import Tuple, Optional, List, Dict
import os
import pandas as pd
from lib.data_utils import extract_test_data, load_test_ranges, TestRange
# from lib.sys_config import SysConfig
import matplotlib.pyplot as plt
from dataclasses import dataclass

# Initialized by main().
# sys_config: SysConfig = None

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


# @dataclass
# class TestRange:
#     """Represent a single test time range"""
#     test_name: str
#     start_ms: int
#     end_ms: int


def input_file_path(basic_name: str) -> str:
    if args.input_dir:
        return os.join(args.input_dir, basic_name)
    return basic_name


def output_file_path(basic_name: str) -> str:
    if args.output_dir:
        return os.join(args.output_dir, basic_name)
    return basic_name


# def load_test_ranges(tests_file_path: str) -> List[TestRange]:
#     """Extract the tests names and ranges from the markers file."""
#     logger.info(f"Loading test ranges from tests file [{tests_file_path}]")
#     df = pd.read_csv(tests_file_path, delimiter=',')
#     result = []
#     for i, row in df.iterrows():
#         test_name = row["Test"]
#         start_time_ms = row["Start[ms]"]
#         end_time_ms = row["End[ms]"]
#         result.append(TestRange(test_name, start_time_ms, end_time_ms))
#     return result


# def extract_test_data(data_file_path: str, test_range: TestRange, original_value_column: str,
#                       new_value_column: str):
#     """Extracts data of a single test range."""
#     logger.info(f"Extracting test range from file [{data_file_path}]")
#     logger.info(f"Test range: {test_range}")
#     # Load original
#     df = pd.read_csv(data_file_path, delimiter=',')
#     # Select columns of interest
#     df = df[['T[ms]', original_value_column]]
#     # Extract rows in test range
#     df = df[df['T[ms]'].between(test_range.start_ms, test_range.end_ms)]
#     # Normalize time to test start time.
#     df['T[ms]'] -= test_range.start_ms
#     # Rename the value column
#     df.rename(columns={original_value_column: new_value_column}, inplace=True)
#     # All done
#     return df


def main():
    global sys_config

    logger.info("Test Splitter started.")

    # Load test ranges from tests file.
    tests_file_path = input_file_path('_tests.csv')
    test_ranges = load_test_ranges(tests_file_path)

    # Extract the load cell data of each test.
    channel_file_path = input_file_path('_channel_ldc1.csv')
    logger.info(f"Using channel file [{channel_file_path}]")
    channel_df = pd.read_csv(channel_file_path, delimiter=',')

    tests_data = []
    for test_range in test_ranges:
        df = extract_test_data(channel_df, test_range, "Value[g]", test_range.test_name)
        tests_data.append(df)

    # Join the tests data to a single data frame with common time column.
    merged_df = None
    for df in tests_data:
        if merged_df is None:
            merged_df = df
        else:
            merged_df = pd.merge(merged_df, df, on='T[ms]', how='outer')

    # Convert the time from millis (ints) to secs (floats).
    # We do it after the join to avoid floating point irregularities in the time matching.
    merged_df['T[ms]'] /= 1000
    merged_df.rename(columns={'T[ms]': 'T[s]'}, inplace=True)

    output_file = output_file_path("_tests_ldc1.csv")
    logger.info(f"Writing results to file [{output_file}]")
    merged_df.to_csv(output_file, index=False)
    merged_df.plot(x="T[s]", xlabel="Seconds", ylabel="Grams")
    plt.show()

    logger.info(f"All done.")


if __name__ == "__main__":
    main()
