#!python

# A python program that communicates with the device over a serial link
# and Serial Packets protocol.

from __future__ import annotations

import argparse
import logging
from typing import Optional, Dict, Any
import time
import sys
import os
import glob
from dataclasses import dataclass

# Local imports.
sys.path.insert(0, "..")
from lib.log_parser import LogPacketsParser, ParsedLogPacket
from lib.sys_config import SysConfig

# For using the local version of serial_packet. Comment out if
# using serial_packets package installed by pip.
# sys.path.insert(0, "../../../../serial_packets_py/repo/src")
# print(f"sys.path: {sys.path}", flush=True)

from serial_packets.packet_decoder import PacketDecoder, DecodedLogPacket

# Initialized by main().
sys_config: Optional[SysConfig] = None

logging.basicConfig(
    level=logging.INFO,
    format="%(relativeCreated)07d %(levelname)-7s %(filename)-10s: %(message)s",
)
logger = logging.getLogger("main")

# Command line args
parser = argparse.ArgumentParser()
parser.add_argument("--sys_config",
                    dest="sys_config",
                    default="sys_config.toml",
                    help="Path to system configuration file.")
parser.add_argument("--input_file",
                    dest="input_file",
                    default=".",
                    help="Input log file to process.")
parser.add_argument("--output_dir",
                    dest="output_dir",
                    default=".",
                    help="Output directory for generated files.")
args = parser.parse_args()

# For tracking time range
earliest_packet_start_time: Optional[int] = None
latest_packet_end_time_millis: Optional[int] = None

# For progress report.
byte_count = 0
packet_count = 0
chan_data_count = 0
point_count = 0

last_point_rel_time = None

log_packets_parser = LogPacketsParser()


@dataclass(frozen=False)
class OutputCsvFile:
    """Represents a single output csv file."""
    file_id: str
    file_path: str
    file_handle: Any
    row_count: int


# Maps output file ids to their information.
output_csv_files_dict: Dict[str, OutputCsvFile] = {}


def session_span_secs() -> float:
    """Return the timestamp duration of the log records processes so far."""
    if earliest_packet_start_time is None or latest_packet_end_time_millis is None:
        return 0
    return (latest_packet_end_time_millis - earliest_packet_start_time) / 1000


@dataclass(frozen=True)
class PendingTestStartMarker:
    """Partial information test, based on its start marker. Used to track test time ranges."""
    test_name: str
    test_start_time_ms: int


# If we are waiting for a test end marker, this contains the information
# from the start marker.
pending_test_start_marker: Optional[PendingTestStartMarker] = None


def process_packet_load_cell_channels(parsed_log_packet: ParsedLogPacket):
    """Process the load cell(s) data of a log packet."""
    global point_count, sys_config, output_csv_files_dict, earliest_packet_start_time
    for chan_name, lc_config in sys_config.load_cells_configs().items():
        chan_data = parsed_log_packet.channel(chan_name)
        if not chan_data:
            continue
        chan = output_csv_files_dict[chan_name]
        f = chan.file_handle
        for time, marker_name in chan_data.values():
            millis_in_session = time - earliest_packet_start_time
            g = lc_config.adc_reading_to_grams(marker_name)
            f.write(f"{millis_in_session},{marker_name},{g:.3f}\n")
        point_count += chan_data.size()
        chan.row_count += chan_data.size()


def process_packet_temperature_channels(parsed_log_packet: ParsedLogPacket):
    """Process the temperature sensor(s) data of a log packet."""
    global point_count, sys_config, output_csv_files_dict, earliest_packet_start_time
    for chan_name, temperature_config in sys_config.temperature_configs().items():
        chan_data = parsed_log_packet.channel(chan_name)
        if not chan_data:
            continue
        chan = output_csv_files_dict[chan_name]
        f = chan.file_handle
        for time, marker_name in chan_data.values():
            millis_in_session = time - earliest_packet_start_time
            r = temperature_config.adc_reading_to_ohms(marker_name)
            c = temperature_config.resistance_to_c(r)
            f.write(f"{millis_in_session},{marker_name},{r:.2f},{c:.3f}\n")
        point_count += chan_data.size()
        chan.row_count += chan_data.size()


def process_packet_markers(parsed_log_packet: ParsedLogPacket):
    """Process the markers data of a log packet."""
    global point_count, sys_config, output_csv_files_dict, earliest_packet_start_time
    global pending_test_start_marker
    chan_name = "mrk"
    chan = output_csv_files_dict["markers"]
    f = chan.file_handle
    chan_data = parsed_log_packet.channel(chan_name)
    markers_config = sys_config.markers_config()
    if chan_data:
        for time, marker_name in chan_data.values():
            millis_in_session = time - earliest_packet_start_time
            point_count += 1
            marker_type, marker_value = markers_config.classify_marker(marker_name)
            f.write(f"{millis_in_session},{marker_name},{marker_type},{marker_value}\n")
            # Process test range extraction
            if marker_type == "test_begin":
                assert pending_test_start_marker is None, f"No end marker for test {pending_test_start_marker.test_name}"
                assert marker_value, f"test_being marker has no test name"
                pending_test_start_marker = PendingTestStartMarker(marker_value, millis_in_session)
            elif marker_type == "test_end":
                assert pending_test_start_marker, f"Test end marker with no pending start marker: {marker_value}"
                assert marker_value == pending_test_start_marker.test_name, f"Test end name {marker_value} doesn't match start name {pending_test_start_marker.test_name}"
                tests_output_csv_file = output_csv_files_dict["tests"]
                tests_output_csv_file.file_handle.write(
                    f"{pending_test_start_marker.test_name},{pending_test_start_marker.test_start_time_ms},{millis_in_session}\n"
                )
                tests_output_csv_file.row_count += 1
                pending_test_start_marker = None

        point_count += chan_data.size()
        chan.row_count += chan_data.size()


def process_packet(packet: DecodedLogPacket):
    """Process next decoded log packet from the recording file."""
    global log_packets_parser, earliest_packet_start_time, latest_packet_end_time_millis
    global point_count, chan_data_count, pending_test_start_marker
    assert isinstance(packet, DecodedLogPacket), f"Unexpected packet type: {type(packet)}"

    # Parse the log packet bytes data into Python structures.
    parsed_log_packet: ParsedLogPacket = log_packets_parser.parse_next_packet(packet.data)

    # Track the earliest packet start time and the latest packet end time.
    packet_start_time_millis = parsed_log_packet.start_time_millis()
    packet_end_time_millis = parsed_log_packet.end_time_millis()
    if earliest_packet_start_time is None or packet_start_time_millis < earliest_packet_start_time:
        earliest_packet_start_time = packet_start_time_millis
        logger.info(f"Session base time millis: {earliest_packet_start_time}")
    if latest_packet_end_time_millis is None or packet_end_time_millis > latest_packet_end_time_millis:
        latest_packet_end_time_millis = packet_end_time_millis

    # Process load cells.
    process_packet_load_cell_channels(parsed_log_packet)

    # Process temperature channels.
    process_packet_temperature_channels(parsed_log_packet)

    # Process markers channel
    process_packet_markers(parsed_log_packet)


def report_status():
    global byte_count, packet_count, chan_data_count, point_count
    logger.info(
        f"Bytes: {byte_count:,}, packets: {packet_count:,}, chan_datas: {chan_data_count}, "
        f"values: {point_count:,}, span: {session_span_secs():.3f} secs"
    )


def init_output_csv_file(file_id: str, csv_header: str, file_base_name) -> None:
    """Initialize a single csv output file."""
    path = os.path.join(args.output_dir, f"{file_base_name}.csv")
    logger.info(f"Creating output file: {path}")
    f = open(path, "w")
    f.write(csv_header + "\n")
    output_csv_files_dict[file_id] = OutputCsvFile(file_id, path, f, 0)


def write_channels_file() -> None:
    """Write to the channels output file the information of the sensor channels."""
    channels_file = output_csv_files_dict["channels"]
    for chan_name in sys_config.load_cells_configs():
        chan_file = output_csv_files_dict[chan_name]
        channels_file.row_count += 1
        channels_file.file_handle.write(f"{chan_name},load_cell,Value[g],{chan_file.row_count},"
                                        f"{os.path.basename(chan_file.file_path)}\n")
    for chan_name in sys_config.temperature_configs():
        chan_file = output_csv_files_dict[chan_name]
        channels_file.row_count += 1
        channels_file.file_handle.write(f"{chan_name},temperature,Value[C],{chan_file.row_count},"
                                        f"{os.path.basename(chan_file.file_path)}\n")


def main():
    global byte_count, packet_count, point_count, sys_config
    # Process configuration and command line flags.
    sys_config = SysConfig()
    sys_config.load_from_file(args.sys_config)
    logger.info("Log processor started.")
    logger.info(f"Input file:  {args.input_file}")
    logger.info(f"Output directory: {args.output_dir}")
    assert args.input_file is not None
    assert args.output_dir is not None
    assert os.path.isdir(args.output_dir), f"No such dir {args.output_dir}"
    existing_files = glob.glob(os.path.join(args.output_dir, "channel_*"))
    assert not existing_files, f"Found in output dir preexisting files with prefix 'channel_': {existing_files[0]}"

    # Open input file.
    in_f = open(args.input_file, "rb")
    last_progress_report_time = time.time()

    # Initialized output files.
    # Load cell sensors files.
    for chan_name in sys_config.load_cells_configs():
        init_output_csv_file(chan_name, f"T[ms],Value[adc],Value[g]", f"_channel_{chan_name}")
    # Power sensors files.
    for chan_name in sys_config.power_configs():
        init_output_csv_file(chan_name, f"T[ms],Voltage[adc],Current[adc],Voltage[V],Current[A],Power[W]", f"_channel_{chan_name}")
    # Temperature sensors files.
    for chan_name in sys_config.temperature_configs():
        init_output_csv_file(chan_name, f"T[ms],Value[adc],Value[R],Value[C]",
                             f"_channel_{chan_name}")
    # Markers file
    init_output_csv_file("markers", f"T[ms],Name,Type,Value", "_markers")
    # Tests list file
    init_output_csv_file("tests", f"Test,Start[ms],End[ms]", "_tests")
    # Channels list file.
    init_output_csv_file("channels", f"Name,Type,Field,Values,File", "_channels")

    packet_decoder = PacketDecoder()
    while bfr := in_f.read(1000):
        # Report progress every 2 secs.
        if time.time() - last_progress_report_time > 2.0:
            last_progress_report_time = time.time()
            report_status()
        for b in bfr:
            byte_count += 1
            packet = packet_decoder.receive_byte(b)
            if packet:
                packet_count += 1
                process_packet(packet)
    in_f.close()
    report_status()
    write_channels_file()
    for _, output_file in output_csv_files_dict.items():
        output_file.file_handle.close()
    for file_id, output_file in output_csv_files_dict.items():
        logger.info(
            f"* {file_id:10s} {output_file.row_count:8d} rows  {output_file.file_path}")
    logger.info(f"Time span: {session_span_secs():.3f} secs")
    logger.info(f"All done.")


if __name__ == "__main__":
    main()
