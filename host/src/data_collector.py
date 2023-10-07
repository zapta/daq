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
from lib.data_utils import load_tests_infos, load_channels_infos, TestInfo, down_sample

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
parser.add_argument("--csv_output_file",
                    dest="csv_output_file",
                    default="",
                    help="Name of the CSV output file.")
parser.add_argument("--channels_selector",
                    dest="channels_selector",
                    default=None,
                    help="Optional regex. If specifies, only channels with matching names are selected. E.g.")
parser.add_argument("--tests_selector",
                    dest="tests_selector",
                    default=None,
                    help="Optional regex. If specifies, only tests with matching names are selected.")
parser.add_argument('--group_by_test',
                    dest="group_by_test",
                    default=False,
                    action=argparse.BooleanOptionalAction,
                    help="If true, will collect tests side by side. Otherwise, ignores test markers.")

args = parser.parse_args()


def main():
    logger.info("Tests collector started.")

    # Determine the tests boundaries
    if args.group_by_test:
        tests_file_path = os.path.join(args.input_dir, '_tests.csv')
        test_infos = load_tests_infos(tests_file_path, args.tests_selector)
        if not test_infos:
            logger.fatal("No channels were found after tests filtering. "
                         "Check _tests.csv file and --tests_selector flag.")
            sys.exit(1)
    else:
        test_infos = [TestInfo("all", 0, 9999999999999)]

    # Load channels infos from the tests file.
    channels_file_path = os.path.join(args.input_dir, '_channels.csv')
    channels_infos = load_channels_infos(channels_file_path, args.channels_selector)
    if not channels_infos:
        logger.fatal("No channels were found after channel filtering. "
                     "Check _channels.csv file and --channels_selector flag.")
        sys.exit(1)

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
            logger.info(f"Selecting test {test_info.test_name}")
            print(f"{test_info.test_name}-{channel_info.channel_name}")
            if args.group_by_test:
                new_column_name = f"{channel_info.channel_name}/{test_info.test_name}"
            else:
                new_column_name = f"{channel_info.channel_name}"

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

    # Write the data to the output file.
    if args.csv_output_file:
      logger.info(f"Writing to [{args.csv_output_file}]")
      merged_df.to_csv(args.csv_output_file, index=False, float_format="%.3f", header=True)
    else:
      logger.info("csv_output_file flag not specified, skipping output file.")

    # Create data to plot. This is the merged data but down sampled if too large.
    original_num_rows = merged_df.shape[0]
    desire_num_rows = 3000
    down_sampling_factor = int(original_num_rows / desire_num_rows)
    logger.info(f"Plot size: original={original_num_rows} rows, desire={desire_num_rows} "
                f"rows, factor={down_sampling_factor}")
    if down_sampling_factor >= 2:
        logger.info("Down sampling for plot purposes only")
        display_df = down_sample(merged_df, down_sampling_factor)
        # display_df = merged_df.drop(merged_df[merged_df.index % down_sampling_factor != 0].index)
        # display_df.reset_index(inplace=True)
        # display_df.drop(['index'], axis=1, inplace=True)
        # logger.info(f"Display df after index reset:\n{display_df}")
    else:
        logger.info("No need to down sample plot data")
        display_df = merged_df
    logger.info(f"Plot data has {display_df.shape[0]} rows.")

    # Plot the data.
    logger.info("Plotting data.")
    display_df.plot(x="T[s]", xlabel="Seconds")
    plt.show()

    logger.info(f"All done.")


if __name__ == "__main__":
    main()
