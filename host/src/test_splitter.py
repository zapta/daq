#!python

# To collect the test snippets.

from __future__ import annotations

import argparse
import logging
from typing import Tuple, Optional, List, Dict
import os
import pandas as pd
from lib.sys_config import SysConfig
import matplotlib.pyplot as plt

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
        self.test_name = test_name
        self.start_ms = start_ms
        self.end_ms = end_ms

    def __str__(self):
        return f"Test \"{self.test_name}\" {self.start_ms} -> {self.end_ms} ({self.end_ms - self.start_ms} ms)"

    def __repr__(self):
        return str(self)


def input_file_path(basic_name: str) -> str:
    if args.input_dir:
        return os.join(args.input_dir, basic_name)
    return basic_name


def output_file_path(basic_name: str) -> str:
    if args.output_dir:
        return os.join(args.output_dir, basic_name)
    return basic_name


def load_test_ranges(markers_file_path: str) -> List[TestRange]:
    """Extract the tests names and ranges from the markers file."""
    logger.info(f"Loading test ranges from marker file [{markers_file_path}]")
    df = pd.read_csv(markers_file_path, delimiter=',')
    result = []
    current_test_name = None
    current_test_start_ms = None
    for i, row in df.iterrows():
        marker_time_ms = row["T[ms]"]
        marker_type = row["MRKR[type]"]
        marker_value = row["MRKR[value]"]
        if marker_type == "test_begin":
            assert marker_value, "Begin marker has an empty test name"
            assert current_test_name is None, f"Missing end for test {current_test_name}"
            current_test_name = marker_value
            current_test_start_ms = marker_time_ms
        elif marker_type == "test_end":
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


def extract_test_data(data_file_path: str, test_range: TestRange, original_value_column: str,
                      new_value_column: str):
    """Returns a dataframe with test time and value columns, normalized to test range."""
    logger.info(f"Extracting test range from file [{data_file_path}]")
    logger.info(f"Test range: {test_range}")
    # Load original
    df = pd.read_csv(data_file_path, delimiter=',')
    # logger.info(f"\n{df}")
    # Select columns of interest
    df = df[['T[ms]', original_value_column]]
    # Extract rows in range
    df = df[df['T[ms]'].between(test_range.start_ms, test_range.end_ms)]
    # logger.info(f"\n{df}")
    # Substract test start time
    df['T[ms]'] -= test_range.start_ms
    # Rename the value column
    df.rename(columns={original_value_column: new_value_column}, inplace=True)
    # All done
    # logger.info(f"\n{df}")
    return df


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

    # Extract load cell data

    load_cell_file_path = input_file_path('_channel_ldc1.csv')
    test_datas = []
    for test_range in test_ranges:
        df = extract_test_data(load_cell_file_path, test_range, "LDC1[g]", test_range.test_name)
        test_datas.append(df)

    merged_df = None
    for df in test_datas:
        if merged_df is None:
            merged_df = df
        else:
            merged_df = pd.merge(merged_df, df, on='T[ms]', how='outer')
    # logger.info(f"\n{merged_df}")

    # Now that we merged, it's safe to switch to floating point
    # time in secs.
    merged_df['T[ms]'] /= 1000
    merged_df.rename(columns={'T[ms]': 'T[s]'}, inplace=True)
    # logger.info(f"\n{merged_df}")

    output_file = output_file_path("_tests_ldc1.csv")
    logger.info(f"Writing results to file [{output_file}]")
    merged_df.to_csv(output_file, index=False)
    merged_df.plot(x="T[s]", xlabel="Seconds", ylabel="Grams")
    plt.show()

    logger.info(f"All done.")


if __name__ == "__main__":
    main()
