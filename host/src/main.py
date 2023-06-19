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

parser = argparse.ArgumentParser()
parser.add_argument("--port", dest="port", default="COM21", help="Serial port to use.")
parser.add_argument("--output_file",
                    dest="output_file",
                    default="test.txt",
                    help="Name of output file.")
args = parser.parse_args()

output_file = None

# Graphing example from https://stackoverflow.com/a/49594258/15038713

fig = None
canvas = None
screen = None


def grams(adc_ticks: int) -> int:
    return int(adc_ticks * 0.01)


def atexit_handler():
    global output_file
    if output_file:
        logger.info(f"Flushing data file [{args.output_file}]")
        output_file.flush()
        output_file = None
    os._exit(0)


atexit.register(atexit_handler)


async def command_async_callback(endpoint: int, data: PacketData) -> Tuple[int, PacketData]:
    logger.info(f"Received command: [%d] %s", endpoint, data.hex_str(max_bytes=10))
    # In this example we don't expect incoming commands at the master side.
    return (PacketStatus.UNHANDLED.value, PacketData())


async def message_async_callback(endpoint: int, data: PacketData) -> Tuple[int, PacketData]:
    logger.info(f"Received message: [%d] %s", endpoint, data.hex_str(max_bytes=10))
    if endpoint == 10:
        handle_adc_report_message(data)
        return
    # v1 = data.read_uint32()
    # assert (v1 == 12345678)
    # assert (data.all_read_ok())


async def event_async_callback(event: PacketsEvent) -> None:
    logger.info("%s event", event)


window_width = 1000
window_height = 700
DPI = 100


def init_graph():
    global fig, canvas, screen
    fig = plt.figure(figsize=(window_width / DPI, window_height / DPI), dpi=DPI)
    canvas = np.zeros((window_height, window_width))
    screen = pf.screen(canvas, 'Load cell ADC')


ZERO_OFFSET = 27210.57
DISPLAY_NUM_POINTS = 501

display_points_grams = [0 for v in range(0, DISPLAY_NUM_POINTS)]


def process_new_points(new_points_ticks: List[int]) -> None:
    global ZERO_OFFSET, display_points_grams
    new_points_grams = [grams(v - ZERO_OFFSET) for v in new_points_ticks]
    display_points_grams.extend(new_points_grams)
    display_points_grams = display_points_grams[-DISPLAY_NUM_POINTS:]
    update_graph(display_points_grams)


def update_graph(gram_points: List[int]):
    fig.clear()

    plt.subplot(211)
    y = gram_points
    x = [v * 2 for v in range(0, len(gram_points))]
    plt.xlim(min(x), max(x))
    plt.ylim(-100, 3000)
    plt.plot(x, y, color='blue')

    plt.subplot(212)
    m = mean(gram_points)
    normalized = [v - m for v in gram_points]
    y = normalized
    x = [v * 2 for v in range(0, len(gram_points))]
    plt.xlabel('Time [ms]')
    plt.ylabel('Force [g]')
    plt.xlim(min(x), max(x))
    plt.ylim(-31, 31)
    plt.plot(x, y, color='red')

    fig.canvas.draw()
    image = np.fromstring(fig.canvas.tostring_rgb(), dtype=np.uint8, sep='')
    image = image.reshape(fig.canvas.get_width_height()[::-1] + (3,))
    screen.update(image)


def handle_adc_report_message(data: PacketData):
    global output_file
    # Get message metadata
    data.reset_read_location()
    message_format_version = data.read_uint8()
    assert (message_format_version == 1)
    isr_millis = data.read_uint32()
    points_expected = data.read_uint16()
    # Open file if first time
    if not output_file:
        logger.info(f"Opening output data file [{args.output_file}]")
        output_file = open(args.output_file, "w")
    # Write data to file.
    points_written = 0
    points = []
    #output_file.write(f"--- Packet {points_expected}, {isr_millis}\n")
    while not data.all_read():
        val = data.read_int24()
        if data.read_error():
            break
        output_file.write(f"{val}\n")
        points.append(val)
        points_written += 1
    # Report errors.
    if data.read_error() or points_written != points_expected:
        logger.error("Error while processing an incoming adc report message.")
    else:
        process_new_points(points)


async def async_main():
    logger.info("Started.")
    assert args.port is not None
    client = SerialPacketsClient(args.port, command_async_callback, message_async_callback,
                                 event_async_callback)

    init_graph()

    while True:
        # Connect if needed.
        if not client.is_connected():
            if not await client.connect():
                await asyncio.sleep(2.0)
                continue
        await asyncio.sleep(0.5)


asyncio.run(async_main(), debug=True)
