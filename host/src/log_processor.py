#!python

# A python program that communicates with the device over a serial link
# and Serial Packets protocol.

from __future__ import annotations

import argparse
import logging
from typing import Tuple, Optional, List
import time
import sys
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
parser.add_argument("--output_file",
                    dest="output_file",
                    default=None,
                    help="Output file for extracted data.")

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

# def process_adc_report_message(data: PacketData, out_f):
#     global point_count, last_point_rel_time
#     # out_f.write("ccc\n")
#     global session_start_time_millis
#     message_format_version = data.read_uint8()
#     assert message_format_version == 1
#     # Time of last point
#     end_time_millis = data.read_uint32()
#     # Num of points
#     n = data.read_uint16()
#     points = []
#     for i in range(n):
#         val = data.read_int24()
#         points.append(val)
#     # Make sure all reads where OK.
#     assert not data.read_error(), "Errors reading a packet"
#     assert data.all_read(), "Unexpected extra bytes in packet"
#     if session_start_time_millis is None:
#         session_start_time_millis = end_time_millis - (n * SAMPLING_INTERVAL_MILLIS)
#     for i in range(n):
#         point_count += 1
#         rel_time_millis = (end_time_millis -
#                            session_start_time_millis) - (n - i) * SAMPLING_INTERVAL_MILLIS
#         if last_point_rel_time is not None:
#             dt = rel_time_millis - last_point_rel_time
#             if dt != SAMPLING_INTERVAL_MILLIS:
#                 logger.warning(f"Gap {dt} ms @ {point_count}")
#         last_point_rel_time = rel_time_millis
#         g = grams(points[i] - ZERO_OFFSET)
#         out_f.write(f"{rel_time_millis:09d}, {g:5d}\n")


def process_packet(packet: DecodedLogPacket, out_f):
    global log_packets_parser, session_start_time_millis, point_count, group_count
    assert isinstance(packet, DecodedLogPacket), f"Unexpected packet type: {type(packet)}"
    parsed_log_packet: ParsedLogPacket = log_packets_parser.parse_next_packet(packet.data)
    if session_start_time_millis is None:
      session_start_time_millis = parsed_log_packet.time_interval_millis()[0]
    for group in parsed_log_packet.groups():
      group_count += 1
      # For now we have only one kind of group.
      assert isinstance(group, LoadCellGroup)
      chan = group.chan()
      for time, value in group:
         millis_in_session = time - session_start_time_millis     
         g = grams(value - ZERO_OFFSET)
         point_count += 1
         out_f.write(f"{millis_in_session:09d}, lc{chan}, {g:5d}\n")
        



def report_status():
    global byte_count, packet_count, group_count, point_count
    logger.info(f"Bytes: {byte_count:,}, packets: {packet_count:,}, groups: {group_count}, values: {point_count:,}")


def main():
    global byte_count, packet_count, point_count
    last_report_time = time.time()
    logger.info("Log processor started.")
    logger.info(f"Input file:  {args.input_file}")
    logger.info(f"Output file: {args.output_file}")
    assert args.input_file is not None
    decoder = PacketDecoder()
    in_f = open(args.input_file, "rb")
    out_f = open(args.output_file, "w")
    out_f.write("time[ms], chan, value\n")
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
                process_packet(packet, out_f)
                #  logger.info(f"Got packet: {packet}")
    # out_f.write("xxxxxxx\n")
    in_f.close()
    out_f.close()
    report_status()
    logger.info(f"Closing output file {args.output_file}")
    logger.info(f"All done.")


if __name__ == "__main__":
    main()
# asyncio.run(async_main(), debug=True)
