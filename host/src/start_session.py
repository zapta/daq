#!python

from __future__ import annotations

import sys
import argparse
import asyncio
import logging
import time
from sys_config import SysConfig, LoadCellChannelConfig

# For using the local version of serial_packet. Comment out if
# using serial_packets package installed by pip.
# sys.path.insert(0, "../../../../serial_packets_py/repo/src")

from serial_packets.client import SerialPacketsClient
from serial_packets.packets import PacketStatus, PacketData

logging.basicConfig(
    level=logging.INFO,
    format="%(relativeCreated)07d %(levelname)-7s %(filename)-10s: %(message)s",
)
logger = logging.getLogger("main")

parser = argparse.ArgumentParser()
parser.add_argument("--sys_config",
                    dest="sys_config",
                    default="sys_config.toml",
                    help="Path to system configuration file.")
args = parser.parse_args()

# TODO: Move to a common place.
CONTROL_ENDPOINT = 0x01

async def async_main():
    global sys_config
    logger.info("Program started.")
    asyncio.get_event_loop().slow_callback_duration = 1.0

    # Load config
    sys_config = SysConfig()
    sys_config.load_from_file(args.sys_config)

    # Open and connect data link serial client.
    port = sys_config.get_data_link_port()
    logger.info(f"Data link at port {port}")
    logger.info("Connecting...")
    client = SerialPacketsClient(port, baudrate=576000)
    await client.connect()
    for i in range(10):
      await asyncio.sleep(0.2)
      if client.is_connected():
        break
    if not client.is_connected():
      logger.error(f"Fail to connect to {port}")
      sys.exit(1)
    logger.info(f"Connected: {client.is_connected()}")

    # Send command and get response
    session_name = time.strftime("session-%Y%m%d-%H%M%S")
    logger.info(f"Session name: {session_name}")
    session_name_bytes = session_name.encode()
    # TODO: Support longer names in SD file system.
    assert len(session_name_bytes) <= 30
    cmd = PacketData()
    cmd.add_uint8(0x02)  # Start command
    cmd.add_uint8(len(session_name_bytes))  # str len
    cmd.add_bytes(session_name_bytes)
    logger.info(f"Command: {cmd.hex_str(max_bytes=5)}")
    status, response = await client.send_command_blocking(CONTROL_ENDPOINT, cmd)
    logger.info(f"Response status: {status}")
    logger.info(f"Response data: {response.hex_str(max_bytes=20)}")
    if status != PacketStatus.OK.value:
      logger.error(f"Command failed. Status: {status}")
      sys.exit(1)
    had_session = response.read_uint8()
    if not response.all_read_ok():
      logger.error(f"Unexpected response content.")
      sys.exit(1)
    if had_session:
      logger.info("Previous session stopped.")
    else:
      logger.info("No previous running session.")
    logger.info(f"{session_name} started OK.")


asyncio.run(async_main(), debug=True)
