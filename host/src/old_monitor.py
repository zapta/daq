#!python

# A python program that communicates with the device over a serial link
# and Serial Packets protocol.

from __future__ import annotations

import sys
from typing import List, Dict, Any
from statistics import mean
import math
import pyformulas as pf
import matplotlib.pyplot as plt
from matplotlib.widgets import Button
import numpy as np
import argparse
import asyncio
import logging
from typing import Tuple, Optional
import scipy
from log_parser import LogPacketsParser, ChannelData, ParsedLogPacket
from sys_config import SysConfig, LoadCellChannelConfig

# For using the local version of serial_packet. Comment out if
# using serial_packets package installed by pip.
# sys.path.insert(0, "../../../../serial_packets_py/repo/src")

from serial_packets.client import SerialPacketsClient
from serial_packets.packets import PacketStatus, PacketsEvent, PacketData

# Pyplot documentation:
# https://matplotlib.org/stable/api/pyplot_summary.html




# Initialized by main.
sys_config: SysConfig = None

logging.basicConfig(
    level=logging.INFO,
    format="%(relativeCreated)07d %(levelname)-7s %(filename)-10s: %(message)s",
)
logger = logging.getLogger("main")

log_packets_parser = LogPacketsParser()

parser = argparse.ArgumentParser()
parser.add_argument("--sys_config",
                    dest="sys_config",
                    default="sys_config.toml",
                    help="Path to system configuration file.")
args = parser.parse_args()

# Graphing example from https://stackoverflow.com/a/49594258/15038713

fig = None
canvas = None
screen = None

window_width = 800
window_height = 700
DPI = 100

button_start = None
button_stop = None





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


def on_button_start():
    logger.info("On START")
    
def on_button_stop():
    logger.info("On STOP")


def init_display():
    global fig, canvas, screen, button_start, button_stop
    fig = plt.figure(figsize=(window_width / DPI, window_height / DPI), dpi=DPI)
    canvas = np.zeros((window_height, window_width))
    screen = pf.screen(canvas, 'Load cell ADC')
    plt.subplots_adjust(left=0.1, bottom=0.25, right=0.95, top=0.95, wspace=0.4, hspace=0.4)
    button_axis = fig.add_axes([0.7, 0.05, 0.1, 0.05])
    button_stop = Button(button_axis, "STOP")
    button_stop.on_clicked(on_button_stop)
    button_axis = fig.add_axes([0.85, 0.05, 0.1, 0.05])
    button_start = Button(button_axis, "START")
    button_start.on_clicked(on_button_start)


DISPLAY_NUM_POINTS = 501

display_points_grams = []
display_times_millis = []


def process_new_loadcell_points(chan_name: str, times_millis: List[int], adc_values: List[int]) -> None:
    global display_points_grams, display_times_millis
    assert len(times_millis) == len(adc_values)
    lc_chan_config = sys_config.get_load_cell_config(chan_name)
    values_g = [lc_chan_config.adc_reading_to_grams(v) for v in adc_values]
    display_times_millis.extend(times_millis)
    display_times_millis = display_times_millis[-DISPLAY_NUM_POINTS:]
    display_points_grams.extend(values_g)
    display_points_grams = display_points_grams[-DISPLAY_NUM_POINTS:]
    # If we accumulated enough points then display the data.
    if len(display_points_grams) >= DISPLAY_NUM_POINTS:
      update_display( display_times_millis, display_points_grams)

# fft_last_yf = None
# Used to alternate display operation to limit asyncio loop time.
slot = 0


def update_display(times_millis: List[int], values_grams: List[int]):
    global slot, button_start, button_stop
    # Increment display slot number.
    slot += 1
    if slot > 12:
        slot = 0
        
        
    # button_stop.on_clicked(on_button_stop)
    # button_start.on_clicked(on_button_start)
        
    # plt.subplots_adjust(left=0.1, bottom=0.25, right=0.95, top=0.95, wspace=0.4, hspace=0.4)
    
    # if slot 
    # axprev = fig.add_axes([0.7, 0.05, 0.1, 0.075])
    # axnext = fig.add_axes([0.81, 0.05, 0.1, 0.075])
    # bnext = Button(axnext, 'Next')
    # bprev = Button(axprev, 'Previous')

    # Update force graph.
    if slot in [0, 2, 4, 6, 8, ]:
      ax = plt.subplot(311)
      ax.clear()
      t0 = times_millis[0]
      x = [(v - t0)/ 1000 for v in times_millis]
      y = values_grams
      plt.xlabel('Time [ms]')
      plt.ylabel('Force [g]')
      plt.xlim(min(x), max(x))
      plt.ylim(-100, 3000)
      plt.plot(x, y, color='blue')
      
 

    # Update noise graph
    if slot in [1, 5, 9]:
        m = mean(values_grams)
        normalized = [v - m for v in values_grams]
        ax = plt.subplot(312)
        ax.clear()
        t0 = times_millis[0]
        x = [(v - t0)/ 1000 for v in times_millis]
        y = normalized
        sum_squars = 0
        for v in normalized:
            sum_squars += v * v
        rms = math.sqrt(sum_squars / len(normalized))
        plt.text(0.01, -9, f"RMS: {rms:.2f},  p2p: {max(normalized) - min(normalized):.2f} ")
        plt.xlabel('Time [ms]')
        plt.ylabel('AC Force [g]')
        plt.xlim(min(x), max(x))
        plt.ylim(-10, 10)
        plt.plot(x, y, color='red')

    # Noise FFT graph
    if slot in [3, 7, 11]:
        m = mean(values_grams)
        normalized = [v - m for v in values_grams]
        ax = plt.subplot(313)
        # ax = plt.subplot(3, 7, 11)
        ax.clear()
        # Number of sample points
        N = len(normalized)
        T = 1.0 / 500.0
        T = ((times_millis[-1] - times_millis[0]) / (len(times_millis) - 1)) / 1000
        # Signal in.
        x = np.linspace(0.0, N * T, N)
        y = np.asarray(normalized)
        # FFT values
        yf = scipy.fftpack.fft(y)
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
     
    last_packet_base_time_millis = parsed_log_packet.base_time_millis()
    assert parsed_log_packet.num_channels() == 4
    load_cell_cata: ChannelData = parsed_log_packet.channel("LC1")
    times_millis = []
    adc_values = []
    for time_millis, value in load_cell_cata.timed_values():
        times_millis.append(time_millis)
        adc_values.append(value)
    process_new_loadcell_points(load_cell_cata.chan_name(), times_millis, adc_values)


async def async_main():
    global sys_config
    # Supress slow loop warnings for loops that are less than 0.5sec.
    # See https://stackoverflow.com/a/76521656/15038713
    asyncio.get_event_loop().slow_callback_duration = 0.5
    logger.info("Started.")
    sys_config = SysConfig()
    assert args.sys_config is not None
    sys_config.load_from_file(args.sys_config)
    port = sys_config.get_data_link_port()
    client = SerialPacketsClient(port, command_async_callback, message_async_callback,
                                 event_async_callback, baudrate=576000)

    init_display()

    while True:
        # Connect if needed.
        if not client.is_connected():
            if not await client.connect():
                await asyncio.sleep(2.0)
                continue
        await asyncio.sleep(0.5)


asyncio.run(async_main(), debug=True)
