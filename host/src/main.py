#!python

# A python program that communicates with the device over a serial link
# and Serial Packets protocol.

from __future__ import annotations

import sys

# For using the local version of serial_packet. Comment out if
# using serial_packets package installed by pip.
sys.path.insert(0, "../../../../serial_packets_py/repo/src")

import argparse
import asyncio
import logging
from typing import Tuple, Optional
from serial_packets.client import SerialPacketsClient
from serial_packets.packets import PacketStatus, PacketsEvent, PacketData

logging.basicConfig(
    level=logging.INFO,
    format="%(relativeCreated)07d %(levelname)-7s %(filename)-10s: %(message)s",
)
logger = logging.getLogger("main")

parser = argparse.ArgumentParser()
parser.add_argument("--port", dest="port", default=None, help="Serial port to use.")
args = parser.parse_args()


async def command_async_callback(
    endpoint: int, data: PacketData
) -> Tuple[int, PacketData]:
    logger.info(f"Received command: [%d] %s", endpoint, data.hex_str(max_bytes=10))
    # In this example we don't expect incoming commands at the master side.
    return (PacketStatus.UNHANDLED.value, PacketData())


async def message_async_callback(
    endpoint: int, data: PacketData
) -> Tuple[int, PacketData]:
    logger.info(f"Received message: [%d] %s", endpoint, data.hex_str(max_bytes=10))
    if endpoint == 10:
        handle_adc_report_message(data)
        return
    # v1 = data.read_uint32()
    # assert (v1 == 12345678)
    # assert (data.all_read_ok())


async def event_async_callback(event: PacketsEvent) -> None:
    logger.info("%s event", event)


def handle_adc_report_message(data: PacketData):
    data.reset_read_location()
    with open("test.txt", "a") as myfile:
      while not data.all_read():
          val = data.read_int24()
          if data.read_error():
            break
          myfile.write(f"{val}\n")
    if data.read_error():
      print(f"ERROR: Error while parsing an adc report message.", flush=True)

async def async_main():
    logger.info("Started.")
    assert args.port is not None
    client = SerialPacketsClient(
        args.port, command_async_callback, message_async_callback, event_async_callback
    )
    while True:
        # Connect if needed.
        if not client.is_connected():
            if not await client.connect():
                await asyncio.sleep(2.0)
                continue
        # Here connected. Send a command every 500 ms.
        await asyncio.sleep(0.5)
        # endpoint = 20
        # cmd_data = PacketData().add_uint8(200).add_uint32(1234)
        # logger.info("Sending command: [%d], %s", endpoint, cmd_data.hex_str())
        # status, response_data = await client.send_command_blocking(endpoint, cmd_data, timeout=0.2)
        # logger.info(f"Command result: [%d], %s", status, response_data.hex_str())


asyncio.run(async_main(), debug=True)
