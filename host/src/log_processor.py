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
from log_parser import  LogPacketsParser, ParsedLogPacket, LoadCellGroup

# For using the local version of serial_packet. Comment out if
# using serial_packets package installed by pip.
# sys.path.insert(0, "../../../../serial_packets_py/repo/src")

from serial_packets.client import SerialPacketsClient
from serial_packets.packets import PacketStatus, PacketsEvent, PacketData
from serial_packets.packet_decoder import PacketDecoder,  DecodedLogPacket

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


def grams(adc_ticks: int) -> int:
    return int(adc_ticks * 0.0167)

# TODO: Make this per loadcell and per channel.
ZERO_OFFSET = 16925

# TODO: Add sampling interval information to the packet.
# SAMPLING_INTERVAL_MILLIS = 2

session_start_time_millis = None

byte_count = 0
packet_count = 0
group_count = 0
point_count = 0

last_point_rel_time = None

log_packets_parser = LogPacketsParser()

output_files_dict = {}

def get_output_file(chan_id: str, header: str):
   """Header is used only first call per file"""
   if chan_id in output_files_dict:
     return output_files_dict[chan_id]
   path = os.path.join(args.output_dir, f"_channel_{chan_id}.csv")
   logger.info(f"Creating output file: {path}")
   f = open(path , "w")
   f.write(header + "\n")
   output_files_dict[chan_id] = f
   return f
     

def process_packet(packet: DecodedLogPacket):
    global log_packets_parser, session_start_time_millis, point_count, group_count
    assert isinstance(packet, DecodedLogPacket), f"Unexpected packet type: {type(packet)}"
    parsed_log_packet: ParsedLogPacket = log_packets_parser.parse_next_packet(packet.data)
    if session_start_time_millis is None:
      session_start_time_millis = parsed_log_packet.time_interval_millis()[0]
    for group in parsed_log_packet.groups():
      group_count += 1
      # For now we have only one kind of group.
      assert isinstance(group, LoadCellGroup)
      chan_index = group.chan()
      chan_id_str = f"lc{chan_index+1}"
      f = get_output_file(f"{chan_id_str}", f"T[ms],{chan_id_str.upper()}[g]", )
      for time, value in group:
         millis_in_session = time - session_start_time_millis     
         g = grams(value - ZERO_OFFSET)
         point_count += 1
         f.write(f"{millis_in_session},{g}\n")
        



def report_status():
    global byte_count, packet_count, group_count, point_count
    logger.info(f"Bytes: {byte_count:,}, packets: {packet_count:,}, groups: {group_count}, values: {point_count:,}")


def main():
    global byte_count, packet_count, point_count
    last_report_time = time.time()
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
    # out_f = open(args.output_file, "w")
    # out_f.write("time[ms], chan, value\n")
    while (bfr := in_f.read(1000)):
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
    # out_f.write("xxxxxxx\n")
    in_f.close()
    report_status()
    for chan_id, file in output_files_dict.items():
      logger.info(f"Closing output file: {file.name}")
      file.close()
    logger.info(f"All done.")


if __name__ == "__main__":
    main()
# asyncio.run(async_main(), debug=True)
