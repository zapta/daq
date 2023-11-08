#!python

# A python program that communicates with the device over a serial link
# and Serial Packets protocol.

from __future__ import annotations

import argparse
import logging
from typing import Optional, Dict, Any, List
import time
import sys
import os
import glob
from dataclasses import dataclass
from serial_packets.packet_decoder import PacketDecoder, DecodedLogPacket


# Local imports.
sys.path.insert(0, "..")
from lib.log_parser import LogPacketsParser, ParsedLogPacket, ChannelData,  LcChannelValue, PwChannelValue, TmChannelValue, MrkChannelValue
from lib.sys_config import SysConfig


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

last_point_rel_time = None

# Setup by main()
log_packets_parser: Optional[LogPacketsParser] = None


@dataclass(frozen=False)
class OutputCsvFile:
    """Represents a single output csv file."""
    file_id: str
    file_path: str
    file_handle: Any
    row_count: int


# Maps output file ids to their information.
output_csv_files_dict: Dict[str, OutputCsvFile] = {}

def total_output_rows()->int:
    """Returns the total number of output rows emitted so far."""
    n = 0
    for output_file in output_csv_files_dict.values():
        n + output_file.row_count
    return n
    

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
        
def process_lc_channel_data(ch_name: str, ch_data: ChannelData) -> None:
    global sys_config, output_csv_files_dict, earliest_packet_start_time
    chan = output_csv_files_dict[ch_name]
    f = chan.file_handle
    for lc_value in ch_data.values():
      assert isinstance(lc_value, LcChannelValue)
      millis_in_session = lc_value.time_millis - earliest_packet_start_time
      f.write(f"{millis_in_session},{lc_value.adc_reading},{lc_value.value_grams:.3f}\n")
    # point_count += ch_data.size()
    chan.row_count += ch_data.size()
    
def process_pw_channel_data(ch_name: str, ch_data: ChannelData) -> None:
    global sys_config, output_csv_files_dict, earliest_packet_start_time
    chan = output_csv_files_dict[ch_name]
    f = chan.file_handle
    for pw_value in ch_data.values():
      assert isinstance(pw_value, PwChannelValue)
      millis_in_session = pw_value.time_millis - earliest_packet_start_time
      watts = pw_value.value_volts * pw_value.value_amps
      f.write(f"{millis_in_session},{pw_value.adc_voltage_reading},{pw_value.adc_current_reading},{pw_value.value_volts:.3f},{pw_value.value_amps:.3f},{watts:.3f}\n")
    # point_count += ch_data.size()
    chan.row_count += ch_data.size()

def process_tm_channel_data(ch_name: str, ch_data: ChannelData) -> None:
    global sys_config, output_csv_files_dict, earliest_packet_start_time
    chan = output_csv_files_dict[ch_name]
    f = chan.file_handle
    for tm_value in ch_data.values():
      assert isinstance(tm_value, TmChannelValue)
      millis_in_session = tm_value.time_millis - earliest_packet_start_time
      f.write(f"{millis_in_session},{tm_value.adc_reading},{tm_value.r_ohms:.2f},{tm_value.t_celsius:.3f}\n")
    # point_count += ch_data.size()
    chan.row_count += ch_data.size()
        
def process_mrk_channel_data(ch_data: ChannelData) -> None:
    global sys_config, output_csv_files_dict, earliest_packet_start_time, pending_test_start_marker
    chan = output_csv_files_dict["markers"]
    f = chan.file_handle
    for mrk_value in ch_data.values():
      assert isinstance(mrk_value, MrkChannelValue)
      marker_name = mrk_value.marker_name
      marker_type = mrk_value.marker_type
      marker_value = mrk_value.marker_value
      millis_in_session = mrk_value.time_millis - earliest_packet_start_time
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
          
    # point_count += ch_data.size()
    chan.row_count += ch_data.size()



def process_packet(packet: DecodedLogPacket):
    """Process next decoded log packet from the recording file."""
    global log_packets_parser, earliest_packet_start_time, latest_packet_end_time_millis
    global chan_data_count, pending_test_start_marker
    assert isinstance(packet, DecodedLogPacket), f"Unexpected packet type: {type(packet)}"

    # Parse the log packet bytes data into Python structures.
    parsed_log_packet: ParsedLogPacket = log_packets_parser.parse_next_packet(packet.data)

    # Track the earliest packet start time and the latest packet end time.
    packet_start_time_millis = parsed_log_packet.start_time_millis()
    packet_end_time_millis = parsed_log_packet.end_time_millis()
    if earliest_packet_start_time is None or packet_start_time_millis < earliest_packet_start_time:
        earliest_packet_start_time = packet_start_time_millis
        logger.info(f"Recording base time millis: {earliest_packet_start_time}")
    if latest_packet_end_time_millis is None or packet_end_time_millis > latest_packet_end_time_millis:
        latest_packet_end_time_millis = packet_end_time_millis


    for ch_name, ch_data in parsed_log_packet.channels().items():
      assert ch_name == ch_data.chan_id()
      if ch_name.startswith("lc"):
         process_lc_channel_data(ch_name, ch_data)
         continue
       
      if ch_name.startswith("pw"):
         process_pw_channel_data(ch_name, ch_data)
         continue
       
      if ch_name.startswith("tm"):
         process_tm_channel_data(ch_name, ch_data)
         continue
       
      if ch_name == ("mrk"):
         process_mrk_channel_data(ch_data)
         continue
    

def report_status():
    global byte_count, packet_count, chan_data_count
    logger.info(
        f"Bytes: {byte_count:,}, packets: {packet_count:,}, rows: {total_output_rows()}, "
        f"time_span: {session_span_secs():.3f} secs"
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
    for chan_id in sys_config.load_cells_configs():
        chan_file = output_csv_files_dict[chan_id]
        channels_file.row_count += 1
        channels_file.file_handle.write(f"{chan_id},load_cell,Grams,{chan_file.row_count},"
                                        f"{os.path.basename(chan_file.file_path)}\n")
    for chan_id in sys_config.power_configs():
        chan_file = output_csv_files_dict[chan_id]
        channels_file.row_count += 1
        channels_file.file_handle.write(f"{chan_id},power,Watts,{chan_file.row_count},"
                                        f"{os.path.basename(chan_file.file_path)}\n")
    for chan_id in sys_config.temperature_configs():
        chan_file = output_csv_files_dict[chan_id]
        channels_file.row_count += 1
        channels_file.file_handle.write(f"{chan_id},temperature,Celsius,{chan_file.row_count},"
                                        f"{os.path.basename(chan_file.file_path)}\n")


def main():
    global byte_count, packet_count, sys_config, log_packets_parser
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
    
    # Initialized the packet log parser
    log_packets_parser = LogPacketsParser(sys_config)

    # Open input file.
    in_f = open(args.input_file, "rb")
    last_progress_report_time = time.time()

    # Initialized output files.
    # Load cell sensors files.
    for chan_id in sys_config.load_cells_configs():
        init_output_csv_file(chan_id, f"T[ms],ADC,Grams", f"_channel_{chan_id}")
    # Power sensors files.
    for chan_id in sys_config.power_configs():
        init_output_csv_file(chan_id, f"T[ms],ADC1,ADC2,Volts,Amps,Watts", f"_channel_{chan_id}")
    # Temperature sensors files.
    for chan_id in sys_config.temperature_configs():
        init_output_csv_file(chan_id, f"T[ms],ADC,Ohms,Celsius",
                             f"_channel_{chan_id}")
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
    ignored_channels = list(log_packets_parser.ignored_channels().items())
    ignored_channels.sort()
    if ignored_channels:
      logger.info("Ignored channels:")
      for chan_id, row_count in log_packets_parser.ignored_channels().items():
          logger.info(f"* {chan_id:10s} {row_count:8d} rows.")  
        
    for _, output_file in output_csv_files_dict.items():
        output_file.file_handle.close()
        
    logger.info("Active channels:")
    for file_id, output_file in output_csv_files_dict.items():
        logger.info(
            f"* {file_id:10s} {output_file.row_count:8d} rows  {output_file.file_path}")
    logger.info(f"Time span: {session_span_secs():.3f} secs")
    logger.info(f"All done.")


if __name__ == "__main__":
    main()
