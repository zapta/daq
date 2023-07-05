#!python

# A python program that communicates with the device over a serial link
# and Serial Packets protocol.

from __future__ import annotations

import sys
import os
import atexit
from typing import List, Dict, Any
from statistics import mean
import math
import pyformulas as pf
import matplotlib.pyplot as plt
import numpy as np
import argparse
import asyncio
import logging
from typing import Tuple, Optional
import scipy
import tomllib
from log_parser import LogPacketsParser, ChannelData, ParsedLogPacket
from sys_config import SysConfig, LoadCellChannelConfig

# For using the local version of serial_packet. Comment out if
# using serial_packets package installed by pip.
# sys.path.insert(0, "../../../../serial_packets_py/repo/src")

from serial_packets.client import SerialPacketsClient
from serial_packets.packets import PacketStatus, PacketsEvent, PacketData

# Pyplot documentation:
# https://matplotlib.org/stable/api/pyplot_summary.html


# ADC_TICKS_ZERO_OFFSET = 36000
# GRAMS_PER_ADC_TICK = 0.008871875

# Initialized by main.
sys_config: SysConfig = None

logging.basicConfig(
    level=logging.INFO,
    format="%(relativeCreated)07d %(levelname)-7s %(filename)-10s: %(message)s",
)
logger = logging.getLogger("main")

log_packets_parser = LogPacketsParser()

parser = argparse.ArgumentParser()
parser.add_argument("--port", dest="port", default="COM21", help="Serial port to use.")
args = parser.parse_args()

# Graphing example from https://stackoverflow.com/a/49594258/15038713

fig = None
canvas = None
screen = None


# def grams(adc_ticks: int) -> int:
#     return adc_ticks * GRAMS_PER_ADC_TICK


async def command_async_callback(endpoint: int, data: PacketData) -> Tuple[int, PacketData]:
    logger.info(f"Received command: [%d] %s", endpoint, data.hex_str(max_bytes=10))
    # In this example we don't expect incoming commands at the master side.
    return (PacketStatus.UNHANDLED.value, PacketData())


async def message_async_callback(endpoint: int, data: PacketData) -> Tuple[int, PacketData]:
    logger.info(f"Received message: [%d] %s", endpoint, data.hex_str(max_bytes=10))
    if endpoint == 10:
        handle_log_message(data)
        return


async def event_async_callback(event: PacketsEvent) -> None:
    logger.info("%s event", event)


window_width = 800
window_height = 600
DPI = 100


def init_graph():
    global fig, canvas, screen
    fig = plt.figure(figsize=(window_width / DPI, window_height / DPI), dpi=DPI)
    canvas = np.zeros((window_height, window_width))
    screen = pf.screen(canvas, 'Load cell ADC')


DISPLAY_NUM_POINTS = 501

display_points_grams = []
display_times_millis = []


def process_new_loadcell_points(chan_name: str, times_millis: List[int], adc_values: List[int]) -> None:
    global display_points_grams, display_times_millis
    assert len(times_millis) == len(adc_values)
    lc_chan_config = sys_config.get_load_cell_config(chan_name)
    # print(f"lc chan config: {lc_chan_config}", flush=True)
    # offset = lc_chan_config['offset']
    # scale = lc_chan_config['scale']
    # print(f"offset: {lc_chan_config.offset()}, scale: {lc_chan_config.scale()}", flush=True)
    # xxx = mean(new_points_ticks)
    # logger.info(f"*** mean adc tick: {m}")
    values_g = [lc_chan_config.adc_reading_to_grams(v) for v in adc_values]
    
    display_times_millis.extend(times_millis)
    display_times_millis = display_times_millis[-DISPLAY_NUM_POINTS:]
    # t0 = display_times_millis[0]
    # display_times_millis = [v - t0 for v in display_times_millis]
    
    # logger.info(f"Grams: {new_points_grams[0]}")
    display_points_grams.extend(values_g)
    display_points_grams = display_points_grams[-DISPLAY_NUM_POINTS:]
    
    # If we accomulated enough points then display the data.
    if len(display_points_grams) >= DISPLAY_NUM_POINTS:
      update_display( display_times_millis, display_points_grams)

fft_last_yf = None
slot = 0


def update_display(times_millis: List[int], values_grams: List[int]):
    global slot, fft_last_yf
    # Increment slot number
    slot += 1
    if slot > 10:
        slot = 1
        
    plt.subplots_adjust(left=0.1, bottom=0.1, right=0.95, top=0.95, wspace=0.4, hspace=0.4)

    # logger.info("Updating display")
    # print(f"*** {times_millis[0]}, {times_millis[100]}", flush=True)
    # Update force graph.
    ax = plt.subplot(311)
    ax.clear()
    # x = times_millis / 1000
    t0 = times_millis[0]
    x = [(v -t0)/ 1000 for v in times_millis]
    y = values_grams
    # x = [v * 2 for v in range(0, len(gram_points))]
    plt.xlabel('Time [ms]')
    plt.ylabel('Force [g]')
    plt.xlim(min(x), max(x))
    plt.ylim(-100, 3000)
    plt.plot(x, y, color='blue')

    # Update noise graph
    if slot in [3, 6, 9]:
        m = mean(values_grams)
        normalized = [v - m for v in values_grams]
        ax = plt.subplot(312)
        ax.clear()
        y = normalized
        sum_squars = 0
        for v in normalized:
            sum_squars += v * v
        rms = math.sqrt(sum_squars / len(normalized))
        plt.text(0.01, -9, f"RMS: {rms:.2f},  p2p: {max(normalized) - min(normalized):.2f} ")
        # x = [v * 2 for v in range(0, len(gram_points))]
        # x = times_millis / 1000
        plt.xlabel('Time [ms]')
        plt.ylabel('AC Force [g]')
        plt.xlim(min(x), max(x))
        plt.ylim(-10, 10)
        plt.plot(x, y, color='red')

    # Noise FFT graph
    if fft_last_yf is None or slot == 1:
        m = mean(values_grams)
        normalized = [v - m for v in values_grams]
        ax = plt.subplot(313)
        ax.clear()
        # logger.info("*** Computing fft")
        # Number of samplepoints
        N = len(normalized)
        # sample interval secs
        T = 1.0 / 500.0
        T = ((times_millis[-1] - times_millis[0]) / (len(times_millis) - 1)) / 1000
        # Signal in.
        x = np.linspace(0.0, N * T, N)
        y = np.asarray(normalized)
        # FFT values
        fft_last_yf = scipy.fftpack.fft(y)

        yf = fft_last_yf
        xf = np.linspace(0.0, 1.0 / (2.0 * T), N // 2)
        plt.xlabel('Frequency [Hz]')
        plt.ylabel('Rel level')
        plt.ylim(0, 2)
        plt.plot(xf, 2.0 / N * np.abs(yf[:N // 2]))

    fig.canvas.draw()
    image = np.frombuffer(fig.canvas.tostring_rgb(), dtype=np.uint8)
    image = image.reshape(fig.canvas.get_width_height()[::-1] + (3,))
    screen.update(image)

last_packet_base_time_millis = None

def handle_log_message(data: PacketData):
    global last_packet_base_time_millis
    parsed_log_packet: ParsedLogPacket = log_packets_parser.parse_next_packet(data)
    # We are expecting a single LC channel + three thermistor 
    # if last_packet_base_time_millis is not None:
    #   logger.info(f"Packet time diff: {parsed_log_packet.base_time_millis() - last_packet_base_time_millis}")
    
    last_packet_base_time_millis = parsed_log_packet.base_time_millis()
    # channels.
    assert parsed_log_packet.num_channels() == 4
    load_cell_cata: ChannelData = parsed_log_packet.channel("LC1")
    # assert isinstance(load_cell_group, LoadCellGroup)
    # Get message metadata
    # data.reset_read_location()
    # message_format_version = data.read_uint8()
    # assert (message_format_version == 1)
    # isr_millis = data.read_uint32()
    # points_expected = data.read_uint16()
    # Write data to file.
    times_millis = []
    adc_values = []
    #output_file.write(f"--- Packet {points_expected}, {isr_millis}\n")
    # i = 0
    for time_millis, value in load_cell_cata.timed_values():
        # i += 1
        # if i <= 10:
        #   print(f"** {i}: {time_millis, value}")
        # val = data.read_int24()
        # if data.read_error():
        #     break
        # #output_file.write(f"{val}\n")
        times_millis.append(time_millis)
        adc_values.append(value)
    # logger.info(f"points: {points[0]}, {points[1]}")
    # Report errors.
    # if data.read_error() or len(points) != points_expected:
    #     logger.error("Error while processing an incoming adc report message.")
    # else:
    process_new_loadcell_points(load_cell_cata.chan_name(), times_millis, adc_values)


async def async_main():
    global sys_config
    # Supress slow loop warnings for loops that are less than 0.5sec.
    # See https://stackoverflow.com/a/76521656/15038713
    asyncio.get_event_loop().slow_callback_duration = 0.5
    logger.info("Started.")
    sys_config = SysConfig()
    sys_config.load_from_file("sys_config.toml")
    #   sys_config = tomllib.load(f)
    # print(f"Sys config:\n{sys_config}", flush=True)
    # print(f"offset: {sys_config['channel']['LC1']['offset']}", flush=True)
    # print(f"type(offset): {type(sys_config['channel']['LC1']['offset'])}", flush=True)

    assert args.port is not None
    client = SerialPacketsClient(args.port, command_async_callback, message_async_callback,
                                 event_async_callback, baudrate=576000)

    init_graph()

    # init_level = 0
    while True:
        # Connect if needed.
        if not client.is_connected():
            # init_level = 0
            if not await client.connect():
                await asyncio.sleep(2.0)
                continue
            # init_level = 1

        await asyncio.sleep(0.5)


asyncio.run(async_main(), debug=True)
