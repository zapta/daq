# A program that collect data from multiple tests.


import argparse
import logging
import os
import sys
import pandas as pd
import matplotlib.pyplot as plt
# noinspection PyUnresolvedReferences
import janitor


# Local imports
sys.path.insert(0, "..")
from lib.data_utils import load_tests_infos, load_channels_infos

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
    logger.info("Tests collector started.")

    # Load the tests information from the tests file.
    tests_file_path = os.path.join(args.input_dir, '_tests.csv')
    test_infos = load_tests_infos(tests_file_path)

    # Load channels infos from the tests file.
    channels_file_path = os.path.join(args.input_dir, '_channels.csv')
    channels_infos = load_channels_infos(channels_file_path)

    tests_data = []
    for channel_info in channels_infos:
        logger.info(f"Loading channel {channel_info.channel_name}[{channel_info.file_name}]")
        # Load the channel data
        channel_df = pd.read_csv(os.path.join(args.input_dir, channel_info.file_name), delimiter=',')
        # Select the timestamp and main value columns
        channel_df = channel_df[['T[ms]', channel_info.field_name]]

        # Add rows for missing ms timestamps. Missing values becomes None
        logger.info("Up sampling.")
        ms_min = round(channel_df['T[ms]'].min())
        ms_max = round(channel_df['T[ms]'].max())
        ms_range = range(ms_min, (ms_max + 1))
        channel_df = channel_df.complete({"T[ms]": ms_range}, sort=True)

        # Interpolate to fill in the missing values.
        logger.info("Interpolating.")
        channel_df.set_index('T[ms]', inplace=True)
        channel_df.interpolate(method='polynomial', order=2, inplace=True)
        channel_df.reset_index(inplace=True)

        # Here channel_df contains the T[ms] column with 1 ms increments
        # and missing values are interpolated.

        for test_info in test_infos:
            logger.info(f"Selectign test {test_info.test_name}")
            print(f"{test_info.test_name}-{channel_info.channel_name}")
            new_column_name = f"{channel_info.channel_name}/{test_info.test_name}"

            # df = extract_test_data(channel_df, test_info, channel_info.field_name, new_column_name)
            df = channel_df[channel_df['T[ms]'].between(test_info.start_ms, test_info.end_ms)].copy()
            # Normalize time to test start time.
            df['T[ms]'] -= test_info.start_ms
            # Rename the value column
            df.rename(columns={channel_info.field_name: new_column_name}, inplace=True)

            tests_data.append(df)

    # Join the tests data to a single data frame with common time column.
    logger.info("Joining tests.")
    merged_df = None
    for df in tests_data:
        if merged_df is None:
            merged_df = df
        else:
            merged_df = pd.merge(merged_df, df, on='T[ms]', how='outer')

    merged_df.sort_values(by=['T[ms]'], inplace=True)

    # Convert the time column from millis (ints) to secs (floats).
    # We do it after the join to avoid floating point irregularities in the time matching.
    merged_df['T[ms]'] /= 1000
    merged_df.rename(columns={'T[ms]': 'T[s]'}, inplace=True)
    # merged_df.round(3)

    # Write out the file
    csv_output_file = os.path.join(args.output_dir, "_tests_data.csv")
    logger.info(f"Writing to [{csv_output_file}]")
    merged_df.to_csv(csv_output_file, index=False, float_format="%.3f", header=True)

    # Plot for sanity check

    # Create a subsample for display. We pick only the 50th row.
    # TODO: Select automatically the optimal subsampling factor
    logger.info("Down sampling data.")
    sub_sample = merged_df.iloc[::50, :]
    logger.info("Plotting data.")
    sub_sample.plot(x="T[s]", xlabel="Seconds")
    plt.show()

    logger.info(f"All done.")


if __name__ == "__main__":
    main()
