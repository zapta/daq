#!python

# A python program to monitor and control the data acquisition.

import argparse
import asyncio
import logging
import platform
import signal
import sys
import os
import time
import pyqtgraph as pg
import statistics

from pyqtgraph.Qt import QtWidgets, QtCore, QtGui
from pyqtgraph import mkPen, TextItem, LabelItem
from typing import Tuple, Optional, List
from log_parser import LogPacketsParser, ChannelData, ParsedLogPacket
from sys_config import SysConfig, LoadCellChannelConfig, ThermistorChannelConfig

# A workaround to avoid auto formatting.
# For using the local version of serial_packet. Comment out if
# using serial_packets package installed by pip.
# sys.path.insert(0, "../../../../serial_packets_py/repo/src")

from serial_packets.client import SerialPacketsClient
from serial_packets.packets import PacketStatus, PacketsEvent, PacketData

# Allows to stop the program by typing ctrl-c.
signal.signal(signal.SIGINT, lambda number, frame: sys.exit())

parser = argparse.ArgumentParser()
parser.add_argument("--sys_config",
                    dest="sys_config",
                    default="sys_config.toml",
                    help="Path to system configuration file.")
args = parser.parse_args()

logging.basicConfig(
    level=logging.INFO,
    format="%(relativeCreated)07d %(levelname)-7s %(filename)-10s: %(message)s",
)
logger = logging.getLogger("main")

# We use a single event loop for all asyncio operatios.
main_event_loop = asyncio.get_event_loop()

# TODO: Move to a common place.
CONTROL_ENDPOINT = 0x01

# Initialized later.
sys_config: SysConfig = None
serial_port: str = None
serial_packets_client: SerialPacketsClient = None

# Two parallel lists with load cell data to display. They contains
# up to the MAX_LOAD_CELL_DISPLAY_POINTS last points.
MAX_LOAD_CELL_DISPLAY_POINTS = 1000
load_cell_display_points_grams = []
load_cell_display_times_secs = []

# Indicate pending button actions.
pending_start_button_click = False
pending_stop_button_click = False

# Used to parse the incoming data log messages from the device.
log_packets_parser = LogPacketsParser()

# Initial window size in pixels.
initial_window_width = 800
initial_window_height = 500
window_title = "DAQ Monitor"

plot1: pg.PlotItem = None
plot2: pg.PlotItem = None
button1: QtWidgets.QPushButton = None
button2: QtWidgets.QPushButton = None
status_label: LabelItem = None
app = None
app_view = None


async def message_async_callback(endpoint: int, data: PacketData) -> Tuple[int, PacketData]:
    """Callback from the serial packets clients for incoming messages."""
    logger.info(f"Received message: [%d] %s", endpoint, data.hex_str(max_bytes=10))
    if endpoint == 10:
        parsed_log_packet: ParsedLogPacket = log_packets_parser.parse_next_packet(data)
        assert parsed_log_packet.num_channels() == 4
        # Process the load cell channel
        load_cell_data: ChannelData = parsed_log_packet.channel("LC1")
        load_cell_chan_config: LoadCellChannelConfig = sys_config.get_load_cell_config("LC1")
        times_secs = []
        values_g = []
        for time_millis, adc_value in load_cell_data.timed_values():
            times_secs.append(time_millis / 1000)
            values_g.append(load_cell_chan_config.adc_reading_to_grams(adc_value))
        process_new_load_cell_points(load_cell_data.chan_name(), times_secs, values_g)
        update_display()

def set_status_line(msg: str) -> None:
  global status_label
  status_label.setText("  " + msg)
  
def process_new_load_cell_points(chan_name: str, times_secs: List[float],
                                 values_g: List[float]) -> None:
    """Called on arrival on new load cell data points."""
    global load_cell_display_points_grams, load_cell_display_times_secs
    assert len(times_secs) == len(values_g)
    load_cell_display_times_secs.extend(times_secs)
    load_cell_display_points_grams.extend(values_g)
    # Keep only the last MAX_LOAD_CELL_DISPLAY_POINTS points.
    load_cell_display_times_secs = load_cell_display_times_secs[-MAX_LOAD_CELL_DISPLAY_POINTS:]
    load_cell_display_points_grams = load_cell_display_points_grams[-MAX_LOAD_CELL_DISPLAY_POINTS:]


def update_display():
    global load_cell_display_points_grams, load_cell_display_times_secs, plot1

    # m = statistics.mean(load_cell_display_points_grams)
    # logger.info(f"Mean: {m} grams")

    # Update load cell graph.
    tn = load_cell_display_times_secs[-1]
    x = [(v - tn) for v in load_cell_display_times_secs]
    y = load_cell_display_points_grams
    plot1.clear()
    plot1.plot(x, y, pen=pg.mkPen("red", width=2), antialias=True)


async def init_client() -> None:
    global sys_config, serial_port, serial_packets_client
    sys_config = SysConfig()
    assert args.sys_config is not None
    sys_config.load_from_file(args.sys_config)
    serial_port = sys_config.get_data_link_port()
    serial_packets_client = SerialPacketsClient(serial_port,
                                                command_async_callback=None,
                                                message_async_callback=message_async_callback,
                                                event_async_callback=None,
                                                baudrate=576000)
    connected = await serial_packets_client.connect()
    assert connected, f"Could open port {serial_port}"


async def do_nothing():
    """ A dummy async method. """
    None


def on_start_button():
    global pending_start_button_click
    logger.info("START button pressed")
    pending_start_button_click = True


def on_stop_button():
    global pending_stop_button_click
    logger.info("STOP button pressed")
    pending_stop_button_click = True


def init_display():
    global plot1, plot2, button1, button2, status_label, app, app_view

    pg.setConfigOption('background', 'w')
    pg.setConfigOption('foreground', 'k')

    app = pg.mkQApp("DAQ Monitor")
    app_view = pg.GraphicsView()
    layout = pg.GraphicsLayout()

    layout.layout.setColumnPreferredWidth(0, 20)
    layout.layout.setColumnPreferredWidth(1, 20)
    layout.layout.setColumnPreferredWidth(2, 20)
    layout.layout.setColumnPreferredWidth(3, 20)
    layout.layout.setColumnPreferredWidth(4, 20)

    app_view.setCentralItem(layout)
    app_view.show()
    app_view.setWindowTitle("DAQ Monitor")
    app_view.resize(initial_window_width, initial_window_height)

    plot1 = layout.addPlot(title="Load Cell", colspan=5)
    plot1.setLabel('left', 'Force', "g")
    plot1.showGrid(False, True, 0.7)
    plot1.setMouseEnabled(x=False, y=True)
    plot1.setXRange(-1.8, 0)
    plot1.setYRange(-100, 6100)

    layout.nextRow()
    plot2 = layout.addPlot(title="Thermistors", colspan=5)
    plot2.setLabel('left', 'Temp', "C")
    plot2.showGrid(False, True, 0.7)
    plot1.setMouseEnabled(x=False, y=True)

    # Add an empty row as spacing.
    # TODO: Is there a cleaner way to specify top margin?
    layout.nextRow()
    status_label = layout.addLabel("")

    layout.nextRow()
    status_label = layout.addLabel("(status line)", colspan=3, justify='left')

    # Button1 - STOP.
    button1_proxy = QtWidgets.QGraphicsProxyWidget()
    button1 = QtWidgets.QPushButton('STOP')
    button1_proxy.setWidget(button1)
    button1.clicked.connect(lambda: on_stop_button())
    layout.addItem(button1_proxy, colspan=1)

    # Button2 - Start
    button2_proxy = QtWidgets.QGraphicsProxyWidget()
    button2 = QtWidgets.QPushButton('START')
    button2_proxy.setWidget(button2)
    button2.clicked.connect(lambda: on_start_button())
    layout.addItem(button2_proxy, colspan=1)


command_start_future = None
command_stop_future = None
command_status_future = None

last_command_status_time = time.time() - 10


def timer_handler():
    global main_event_loop, serial_packets_client
    global pending_start_button_click, command_start_future
    global pending_stop_button_click, command_stop_future
    global  last_command_status_time, command_status_future

    # Process any pending events of the serial packets client.
    # This calls indirectly display_update when new packets arrive.
    main_event_loop.run_until_complete(do_nothing())

    if pending_start_button_click:
        recording_name = time.strftime("%Y%m%d-%H%M%S")
        logger.info(f"Will start a recording named {recording_name}")
        recording_name_bytes = recording_name.encode()
        cmd = PacketData()
        cmd.add_uint8(0x02)  # Command = START
        cmd.add_uint8(len(recording_name_bytes))  # str len
        cmd.add_bytes(recording_name_bytes)
        logger.info(f"START command: {cmd.hex_str(max_bytes=5)}")
        command_start_future = serial_packets_client.send_command_future(CONTROL_ENDPOINT, cmd)
        pending_start_button_click = False

    if command_start_future and command_start_future.done():
        logger.info("Got response for START command")
        status, response_data = command_start_future.result()
        command_start_future = None
        logger.info(f"START command status: {status}")
        # set_status_line(f"START command status: {status}")

    if pending_stop_button_click:
        logger.info(f"Will stop recording, if any.")
        cmd = PacketData()
        cmd.add_uint8(0x03)  # Command = STOP
        logger.info(f"STOP command: {cmd.hex_str(max_bytes=5)}")
        command_stop_future = serial_packets_client.send_command_future(CONTROL_ENDPOINT, cmd)
        pending_stop_button_click = False

    if command_stop_future and command_stop_future.done():
        logger.info("Got response for STOP command")
        status, response_data = command_stop_future.result()
        command_stop_future = None
        logger.info(f"STOP command status: {status}")
        # set_status_line(f"STOP command status: {status}")

    status_elapsed_secs = time.time() - last_command_status_time
    if (command_status_future is None) and status_elapsed_secs >= 1.0:
        cmd = PacketData()
        cmd.add_uint8(0x04)  # Command = STATUS
        logger.debug(f"STATUS command: {cmd.hex_str(max_bytes=5)}")
        command_status_future = serial_packets_client.send_command_future(CONTROL_ENDPOINT, cmd)
        last_command_status_time = time.time()

    if command_status_future and command_status_future.done():
        logger.debug("Got response for STATUS command")
        status, response_data = command_status_future.result()
        command_status_future = None
        if (status != PacketStatus.OK.value):
          logger.error(f"STATUS command failed with status: {status}")
          msg = f"ERROR: Device not available (status {status})"
        else:
          version = response_data.read_uint8()
          recording_active = response_data.read_uint8()
          if recording_active:
            recording_millis = response_data.read_uint32()
            name_len = response_data.read_uint8()
            name_bytes = response_data.read_bytes(name_len)
            name = name_bytes.decode("utf-8")
            msg = f"  Recording [{name}]   {recording_millis/1000:.0f} secs"
          else:
            msg = "  Recording off"
          assert response_data.all_read_ok()
        set_status_line(msg)
        


init_display()

main_event_loop.run_until_complete(init_client())
timer = pg.QtCore.QTimer()
timer.timeout.connect(timer_handler)

# The argument of start() is delay in millis between timer
# The timer period is this value + the execution time of the
# timer handler. We try to keep the delay to a reasonable minimum.
timer.start(1)

if __name__ == '__main__':
    pg.exec()
