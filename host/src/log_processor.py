#!python

# A python program that communicates with the device over a serial link
# and Serial Packets protocol.

from __future__ import annotations

import argparse
import logging
from typing import Tuple, Optional, List
import time
import sys
import os
import glob
from log_parser import LogPacketsParser, ParsedLogPacket, ChannelData
from sys_config import SysConfig, LoadCellChannelConfig

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

# def grams(adc_ticks: int) -> float:
#     """Converts ADC ticks to grams"""
#     return (adc_ticks - ADC_TICKS_ZERO_OFFSET) * GRAMS_PER_ADC_TICK

# TODO: Make this per loadcell and per channel.
ZERO_OFFSET = 16925

# TODO: Add sampling interval information to the packet.
# SAMPLING_INTERVAL_MILLIS = 2

first_packet_start_time = None
last_packet_end_time_millis = None

byte_count = 0
packet_count = 0
chan_data_count = 0
point_count = 0

last_point_rel_time = None

log_packets_parser = LogPacketsParser()

output_files_dict = {}


def session_span_secs() -> float:
    if first_packet_start_time is None or last_packet_end_time_millis is None:
        return 0
    return (last_packet_end_time_millis - first_packet_start_time) / 1000


def get_output_file(chan_name: str, header: str):
    """Header is used only first call per file"""
    if chan_name in output_files_dict:
        return output_files_dict[chan_name]
    path = os.path.join(args.output_dir, f"_channel_{chan_name.lower()}.csv")
    # logger.info(f"Creating output file: {path}")
    f = open(path, "w")
    f.write(header + "\n")
    output_files_dict[chan_name] = f
    return f


def process_packet(packet: DecodedLogPacket):
    global log_packets_parser, first_packet_start_time, last_packet_end_time_millis
    global point_count, chan_data_count
    assert isinstance(packet, DecodedLogPacket), f"Unexpected packet type: {type(packet)}"
    parsed_log_packet: ParsedLogPacket = log_packets_parser.parse_next_packet(packet.data)
    if first_packet_start_time is None:
        first_packet_start_time = parsed_log_packet.start_time_millis()
        logger.info(f"Session base time millis: {first_packet_start_time}")
    last_packet_end_time_millis = parsed_log_packet.end_time_millis()
    for chan_data in parsed_log_packet.channels():
        chan_data_count += 1
        # For now we have only one kind of group.
        # assert isinstance(group, LoadCellGroup)
        # chan_index = group.chan()
        chan_name = chan_data.chan_name()
        # logger.info(f"chan_name = {chan_name}")
        if chan_name.startswith("LC"):
            f = get_output_file(f"{chan_name}", f"T[s],{chan_name}[adc],{chan_name}[g]")
            # f_raw = get_output_file(f"{chan_name}", f"T[ms],{chan_name}[tick]", raw=True)
            load_cell_chan_config = sys_config.get_load_cell_config(chan_name)
            for time, value in chan_data.timed_values():
                millis_in_session = time - first_packet_start_time
                g = load_cell_chan_config.adc_reading_to_grams(value)
                point_count += 1
                f.write(f"{millis_in_session/1000:.3f},{value},{g:.3f}\n")
                # f_raw.write(f"{millis_in_session},{value}\n")
        elif chan_name.startswith("THRM"):
            f = get_output_file(f"{chan_name}", f"T[s],{chan_name}[adc],{chan_name}[R],{chan_name}[C]")
            # f_raw = get_output_file(f"{chan_name}", f"T[s],{chan_name}[C]", raw=True)
            thermistor_chan_config = sys_config.get_thermistor_config(chan_name)
            for time, value in chan_data.timed_values():
                millis_in_session = time - first_packet_start_time
                r = thermistor_chan_config.adc_reading_to_ohms(value)
                c = value  # TODO: Convert to C
                point_count += 1
                f.write(f"{millis_in_session/1000:.3f},{value},{r:.2f},{c:.3f}\n")
                # f_raw.write(f"{millis_in_session},{value}\n")
        else:
            raise RuntimeError(f"Unknown channel: {chan_name}")


def report_status():
    global byte_count, packet_count, chan_data_count, point_count
    logger.info(
        f"Bytes: {byte_count:,}, packets: {packet_count:,}, chan_datas: {chan_data_count}, values: {point_count:,}, span: {session_span_secs():.3f} secs"
    )


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
    last_report_time = time.time()
    while (bfr := in_f.read(1000)):
        # Report progress every 2 secs.
        if time.time() - last_report_time > 2.0:
            last_report_time = time.time()
            report_status()
            # logger.info(f"Processed {byte_count:,} bytes and {packet_count:,} packets")
        for b in bfr:
            byte_count += 1
            packet = decoder.receive_byte(b)
            if packet:
                packet_count += 1
                process_packet(packet)
                #  logger.info(f"Got packet: {packet}")
    in_f.close()
    report_status()
    for chan_id, file in output_files_dict.items():
        logger.info(f"Closing output file: {file.name}")
        file.close()
    logger.info(f"Time span: {session_span_secs():.3f} secs")
    logger.info(f"All done.")


if __name__ == "__main__":
    main()
# asyncio.run(async_main(), debug=True)
