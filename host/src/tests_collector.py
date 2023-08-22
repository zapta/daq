# A program that collect data from multiple tests.

# from __future__ import annotations

import argparse
import logging
import os
import sys
import pandas as pd
import matplotlib.pyplot as plt
from typing import Optional

# Local imports
sys.path.insert(0, "..")
from lib.data_utils import extract_test_data, load_tests_infos, load_channels_infos


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





def main():
    # global sys_config

    logger.info("Test Splitter started.")

    # Load the tests information from the tests file.
    tests_file_path = os.path.join(args.input_dir, '_tests.csv')
    test_infos = load_tests_infos(tests_file_path)

    # Load channels infos from the tests file.
    channels_file_path = os.path.join(args.input_dir, '_channels.csv')
    channels_infos = load_channels_infos(channels_file_path)
    # print(f"Channels infos: {channels_infos}")


    # TODO: Derive the input directory somewhere.
    tests_data = []
    for channel_info in channels_infos:
        channel_df = pd.read_csv(os.path.join(args.input_dir, channel_info.file_name), delimiter=',')
        for test_info in test_infos:
            print(f"{test_info.test_name}-{channel_info.channel_name}")
            new_column_name = f"{channel_info.channel_name}/{test_info.test_name}"
            df = extract_test_data(channel_df, test_info, channel_info.field_name, new_column_name)
            tests_data.append(df)



    # Join the tests data to a single data frame with common time column.
    merged_df = None
    for df in tests_data:
        if merged_df is None:
            merged_df = df
        else:
            merged_df = pd.merge(merged_df, df, on='T[ms]', how='outer')

    merged_df.sort_values(by=['T[ms]'], inplace=True)

    # Interpolate the missing values.
    merged_df.set_index('T[ms]', inplace=True)
    merged_df.interpolate(method='polynomial', order=2, inplace=True)
    # print(f"*** merged_df 1:\n{merged_df}")
    merged_df.reset_index(inplace=True)
    # print(f"*** merged_df 1:\n{merged_df}")



    # Convert the time column from millis (ints) to secs (floats).
    # We do it after the join to avoid floating point irregularities in the time matching.
    merged_df['T[ms]'] /= 1000
    merged_df.rename(columns={'T[ms]': 'T[s]'}, inplace=True)
    # merged_df.round(3)

    # merged_df.set_index('T[s]', inplace=True)

    # Write out the file
    output_file =   os.path.join(args.output_dir, "_combined_test_data.csv")
    logger.info(f"Writing results to file [{output_file}]")
    merged_df.to_csv(output_file, index=False, float_format="%.3f")

    # Plot for sanity check
    merged_df.plot(x="T[s]", xlabel="Seconds")
    plt.show()

    logger.info(f"All done.")


if __name__ == "__main__":
    main()
