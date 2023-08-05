#!python

# A python program that communicates with the device over a serial link
# and Serial Packets protocol.

from __future__ import annotations

import argparse
import logging
from typing import Tuple, Optional, List, Dict, Any
import time
import sys
import os
import glob
from dataclasses import dataclass
from lib.log_parser import LogPacketsParser, ParsedLogPacket
from lib.sys_config import SysConfig

# For using the local version of serial_packet. Comment out if
# using serial_packets package installed by pip.
# sys.path.insert(0, "../../../../serial_packets_py/repo/src")

# print(f"sys.path: {sys.path}", flush=True)

from serial_packets.client import SerialPacketsClient
from serial_packets.packets import PacketStatus, PacketsEvent, PacketData
from serial_packets.packet_decoder import PacketDecoder, DecodedLogPacket

# ADC_TICKS_ZERO_OFFSET = 37127
# GRAMS_PER_ADC_TICK = 0.008871875

# Initialized by main().
sys_config: SysConfig = None

logging.basicConfig(
    level=logging.INFO,
    format="%(relativeCreated)07d %(levelname)-7s %(filename)-10s: %(message)s",
)
logger = logging.getLogger("main")

parser = argparse.ArgumentParser()
parser.add_argument("--input_file",
                    dest="input_file",
                    default=None,
                    help="Input log file to process.")
parser.add_argument("--output_dir",
                    dest="output_dir",
                    default=None,
                    help="Output directory for generated files.")

args = parser.parse_args()

first_packet_start_time = None
last_packet_end_time_millis = None

# For progress report.
byte_count = 0
packet_count = 0
chan_data_count = 0
point_count = 0

last_point_rel_time = None

log_packets_parser = LogPacketsParser()

chan_dict: Dict[str, Channel] = {}


@dataclass
class Channel:
    """Represents a single output channel (an output .csv file)."""
    chan_name: str
    file_path: str
    file: Any
    value_count: int


def session_span_secs() -> float:
    if first_packet_start_time is None or last_packet_end_time_millis is None:
        return 0
    return (last_packet_end_time_millis - first_packet_start_time) / 1000


# Used to extract test ranges.
@dataclass
class PendingTest:
    test_name: str
    test_start_time_ms: int


pending_test: PendingTest = None


def process_packet(packet: DecodedLogPacket):
    global log_packets_parser, first_packet_start_time, last_packet_end_time_millis
    global point_count, chan_data_count, pending_test
    assert isinstance(packet, DecodedLogPacket), f"Unexpected packet type: {type(packet)}"
    parsed_log_packet: ParsedLogPacket = log_packets_parser.parse_next_packet(packet.data)
    if first_packet_start_time is None:
        first_packet_start_time = parsed_log_packet.start_time_millis()
        logger.info(f"Session base time millis: {first_packet_start_time}")
    # Track the log time.
    packet_end_time_millis = parsed_log_packet.end_time_millis()
    if last_packet_end_time_millis is None or packet_end_time_millis > last_packet_end_time_millis:
        last_packet_end_time_millis = packet_end_time_millis
    # Process load cells.
    for chan_name, lc_config in sys_config.load_cells_configs().items():
        chan_data = parsed_log_packet.channel(chan_name)
        if not chan_data:
            continue
        chan = chan_dict[chan_name]
        f = chan.file
        for time, marker_name in chan_data.timed_values():
            millis_in_session = time - first_packet_start_time
            g = lc_config.adc_reading_to_grams(marker_name)
            f.write(f"{millis_in_session},{marker_name},{g:.3f}\n")
        point_count += chan_data.size()
        chan.value_count += chan_data.size()

    # Process temperature channels.
    for chan_name, temperature_config in sys_config.temperature_configs().items():
        chan_data = parsed_log_packet.channel(chan_name)
        if not chan_data:
            continue
        chan = chan_dict[chan_name]
        f = chan.file
        for time, marker_name in chan_data.timed_values():
            millis_in_session = time - first_packet_start_time
            r = temperature_config.adc_reading_to_ohms(marker_name)
            c = temperature_config.resistance_to_c(r)
            f.write(f"{millis_in_session},{marker_name},{r:.2f},{c:.3f}\n")
        point_count += chan_data.size()
        chan.value_count += chan_data.size()

    # Process markers channel
    chan_name = "MRKR"
    chan = chan_dict[chan_name]
    f = chan.file
    chan_data = parsed_log_packet.channel(chan_name)
    markers_config = sys_config.markers_config()
    if chan_data:
        for time, marker_name in chan_data.timed_values():
            millis_in_session = time - first_packet_start_time
            point_count += 1
            marker_type, marker_value = markers_config.classify_marker(marker_name)
            f.write(f"{millis_in_session},{marker_name},{marker_type},{marker_value}\n")
            # Process test range extraction
            if marker_type == "test_begin":
                assert pending_test is None, f"No end marker for test {pending_test.test_name}"
                assert marker_value, f"test_being marker has no test name"
                pending_test = PendingTest(marker_value, millis_in_session)
            elif marker_type == "test_end":
                assert pending_test, f"Test end marker with no pending start marker: {marker_value}"
                assert marker_value == pending_test.test_name, f"Test end name {marker_value} doesn't match start name {pending_test.test_name}"
                tests_chan = chan_dict["TEST"]
                tests_chan.file.write(
                    f"{pending_test.test_name},{pending_test.test_start_time_ms},{millis_in_session}\n"
                )
                tests_chan.value_count += 1
                pending_test = None

        point_count += chan_data.size()
        chan.value_count += chan_data.size()


def report_status():
    global byte_count, packet_count, chan_data_count, point_count
    logger.info(
        f"Bytes: {byte_count:,}, packets: {packet_count:,}, chan_datas: {chan_data_count}, values: {point_count:,}, span: {session_span_secs():.3f} secs"
    )


def init_channel(chan_name: str, csv_header: str) -> None:
    path = os.path.join(args.output_dir, f"_channel_{chan_name.lower()}.csv")
    # logger.info(f"Creating output file: {path}")
    f = open(path, "w")
    f.write(csv_header + "\n")
    chan_dict[chan_name] = Channel(chan_name, path, f, 0)


def main():
    global byte_count, packet_count, point_count, sys_config
    sys_config = SysConfig()
    sys_config.load_from_file("sys_config.toml")
    logger.info("Log processor started.")
    logger.info(f"Input file:  {args.input_file}")
    logger.info(f"Output directory: {args.output_dir}")
    assert args.input_file is not None
    assert args.output_dir is not None
    assert os.path.isdir(args.output_dir), f"No such dir {args.output_dir}"
    existing_files = glob.glob(os.path.join(args.output_dir, "channel_*"))
    assert not existing_files, f"Found in output dir preexisting files with prefix 'channel_': {existing_files[0]}"
    decoder = PacketDecoder()
    in_f = open(args.input_file, "rb")
    last_progress_report_time = time.time()

    # Initialized channels and their output files.
    for chan_name in sys_config.load_cells_configs():
        init_channel(chan_name, f"T[ms],{chan_name}[adc],{chan_name}[g]")
    for chan_name in sys_config.temperature_configs():
        init_channel(chan_name, f"T[ms],{chan_name}[adc],{chan_name}[R],{chan_name}[C]")
    chan_name = "MRKR"
    init_channel(chan_name, f"T[ms],{chan_name}[name],{chan_name}[type],{chan_name}[value]")
    # A pseudo channel with tests start/end times extracted from the markers.
    chan_name = "TEST"
    init_channel(chan_name, f"Test,Start[ms],End[ms]")

    while (bfr := in_f.read(1000)):
        # Report progress every 2 secs.
        if time.time() - last_progress_report_time > 2.0:
            last_progress_report_time = time.time()
            report_status()
        for b in bfr:
            byte_count += 1
            packet = decoder.receive_byte(b)
            if packet:
                packet_count += 1
                process_packet(packet)
    in_f.close()
    report_status()
    for chan_name, chan in chan_dict.items():
        chan.file.close()
    for chan_name, chan in chan_dict.items():
        logger.info(
            f"* {chan_name:5s} {chan.value_count:8d} rows  {os.path.basename(chan.file_path):18s}")
    logger.info(f"Time span: {session_span_secs():.3f} secs")
    logger.info(f"All done.")


if __name__ == "__main__":
    main()
