#!python

# A python program that communicates with the device over a serial link
# and Serial Packets protocol.

from __future__ import annotations

import sys
import os
import atexit
from typing import List
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
from log_parser import LogPacketsParser, LoadCellGroup, ParsedLogPacket

# For using the local version of serial_packet. Comment out if
# using serial_packets package installed by pip.
# sys.path.insert(0, "../../../../serial_packets_py/repo/src")

from serial_packets.client import SerialPacketsClient
from serial_packets.packets import PacketStatus, PacketsEvent, PacketData

# Pyplot documentation:
# https://matplotlib.org/stable/api/pyplot_summary.html

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


def grams(adc_ticks: int) -> int:
    return int(adc_ticks * 0.0167)


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


ZERO_OFFSET = 16925
DISPLAY_NUM_POINTS = 501

display_points_grams = [0 for v in range(0, DISPLAY_NUM_POINTS)]


def process_new_points(new_points_ticks: List[int]) -> None:
    global ZERO_OFFSET, display_points_grams
    m = mean(new_points_ticks)
    # logger.info(f"*** mean adc tick: {m}")
    new_points_grams = [grams(v - ZERO_OFFSET) for v in new_points_ticks]
    display_points_grams.extend(new_points_grams)
    display_points_grams = display_points_grams[-DISPLAY_NUM_POINTS:]
    update_graph(display_points_grams)


fft_last_yf = None
slot = 0


def update_graph(gram_points: List[int]):
    global slot, fft_last_yf
    # Increment slot number
    slot += 1
    if slot > 10:
        slot = 1

    plt.subplots_adjust(left=0.1, bottom=0.1, right=0.95, top=0.95, wspace=0.4, hspace=0.4)

    # Force graph.
    ax = plt.subplot(311)
    ax.clear()
    y = gram_points
    x = [v * 2 for v in range(0, len(gram_points))]
    plt.xlabel('Time [ms]')
    plt.ylabel('Force [g]')
    plt.xlim(min(x), max(x))
    plt.ylim(-100, 3000)
    plt.plot(x, y, color='blue')

    # Noise graph
    if slot in [3, 6, 9]:
        m = mean(gram_points)
        normalized = [v - m for v in gram_points]
        ax = plt.subplot(312)
        ax.clear()
        y = normalized
        sum_squars = 0
        for v in normalized:
            sum_squars += v * v
        rms = math.sqrt(sum_squars / len(normalized))
        plt.text(10, -18, f"RMS: {rms:.2f},  p2p: {max(normalized) - min(normalized):.2f} ")
        x = [v * 2 for v in range(0, len(gram_points))]
        plt.xlabel('Time [ms]')
        plt.ylabel('AC Force [g]')
        plt.xlim(min(x), max(x))
        plt.ylim(-20, 20)
        plt.plot(x, y, color='red')

    # Noise FFT graph
    if fft_last_yf is None or slot == 1:
        m = mean(gram_points)
        normalized = [v - m for v in gram_points]
        ax = plt.subplot(313)
        ax.clear()
        # logger.info("*** Computing fft")
        # Number of samplepoints
        N = len(normalized)
        # sample interval
        T = 1.0 / 500.0
        # Signal in.
        x = np.linspace(0.0, N * T, N)
        y = np.asarray(normalized)
        # FFT values
        fft_last_yf = scipy.fftpack.fft(y)

        yf = fft_last_yf
        xf = np.linspace(0.0, 1.0 / (2.0 * T), N // 2)
        plt.xlabel('Frequency [Hz]')
        plt.ylabel('Rel level')
        plt.ylim(0, 10)
        plt.plot(xf, 2.0 / N * np.abs(yf[:N // 2]))

    fig.canvas.draw()
    image = np.frombuffer(fig.canvas.tostring_rgb(), dtype=np.uint8)
    image = image.reshape(fig.canvas.get_width_height()[::-1] + (3,))
    screen.update(image)


def handle_log_message(data: PacketData):
    parsed_log_packet: ParsedLogPacket = log_packets_parser.parse_next_packet(data)
    # For now, we expect a single group with load cell data.
    assert parsed_log_packet.size() == 1
    load_cell_group = parsed_log_packet.groups()[0]
    assert isinstance(load_cell_group, LoadCellGroup)
    # Get message metadata
    # data.reset_read_location()
    # message_format_version = data.read_uint8()
    # assert (message_format_version == 1)
    # isr_millis = data.read_uint32()
    # points_expected = data.read_uint16()
    # Write data to file.
    points = []
    #output_file.write(f"--- Packet {points_expected}, {isr_millis}\n")
    for _, val in load_cell_group:
        # val = data.read_int24()
        # if data.read_error():
        #     break
        # #output_file.write(f"{val}\n")
        points.append(val)
    # Report errors.
    # if data.read_error() or len(points) != points_expected:
    #     logger.error("Error while processing an incoming adc report message.")
    # else:
    process_new_points(points)


async def async_main():
    # Supress slow loop warnings for loops that are less than 0.5sec.
    # See https://stackoverflow.com/a/76521656/15038713
    asyncio.get_event_loop().slow_callback_duration = 0.5
    logger.info("Started.")
    assert args.port is not None
    client = SerialPacketsClient(args.port, command_async_callback, message_async_callback,
                                 event_async_callback)

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
