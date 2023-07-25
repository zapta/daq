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
import numpy as np
import statistics

from pyqtgraph.Qt import QtWidgets, QtCore, QtGui
from pyqtgraph import LabelItem
from typing import Tuple, Optional, List, Dict
from lib.log_parser import LogPacketsParser, ChannelData, ParsedLogPacket
from lib.sys_config import SysConfig, LoadCellChannelConfig, ThermistorChannelConfig
from lib.display_series import DisplaySeries

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


class LoadCellChannel:

    def __init__(self, chan_name: str, lc_config: LoadCellChannelConfig):
        self.chan_name = chan_name
        self.lc_config = lc_config
        self.display_series = DisplaySeries(1000)


class ThermistorChannel:

    def __init__(self, chan_name: str, therm_config: ThermistorChannelConfig):
        self.chan_name = chan_name
        self.therm_config = therm_config
        self.display_series = DisplaySeries(1000)


# Initialized later.
sys_config: SysConfig = None
serial_port: str = None
serial_packets_client: SerialPacketsClient = None
serial_reconnection_task = None

# Initialized later. Keys are channel names.
lc_channels: Dict[str, LoadCellChannel] = None
therm_channels: Dict[str, ThermistorChannel] = None

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

# Initialized with channel_name -> channel_config, for load
# cells and thermistors respectively.
lc_configs = {}
therm_configs = {}


async def message_async_callback(endpoint: int, data: PacketData) -> Tuple[int, PacketData]:
    """Callback from the serial packets clients for incoming messages."""
    global lc_channels, therm_channels
    logger.debug(f"Received message: [%d] %s", endpoint, data.hex_str(max_bytes=10))
    if endpoint == 10:
        parsed_log_packet: ParsedLogPacket = log_packets_parser.parse_next_packet(data)
        # Process load cell channels
        for lc_ch_name in lc_channels.keys():
            lc_data: ChannelData = parsed_log_packet.channel(lc_ch_name)
            lc_chan: LoadCellChannel = lc_channels[lc_ch_name]
            # @@@ channel
            # lc_chan_config: LoadCellChannelConfig = sys_config.get_load_cell_config(lc_ch_name)
            times_secs = []
            values_g = []
            # logger.info(lc1_data.timed_values())
            for time_millis, adc_value in lc_data.timed_values():
                times_secs.append(time_millis / 1000)
                values_g.append(lc_chan.lc_config.adc_reading_to_grams(adc_value))
            lc_chan.display_series.extend(times_secs, values_g)
        # Process thermistor channels
        for thrm_ch_name in therm_channels.keys():
            # Process thermistor. We compute the average of the readings
            # in this packet.
            therm1_data: ChannelData = parsed_log_packet.channel(thrm_ch_name)
            times_millis = []
            adc_values = []
            for time_millis, adc_value in therm1_data.timed_values():
                times_millis.append(time_millis)
                adc_values.append(adc_value)
            avg_times_millis = np.mean(times_millis)
            avg_adc_value = np.mean(adc_values)
            therm_chan: ThermistorChannel = therm_channels[thrm_ch_name]
            # therm_chan_config: ThermistorChannelConfig = sys_config.get_thermistor_config(
            #     thrm_ch_name)
            # logger.info(f"{therm1_chan_config}")
            therm_chan.display_series.extend(
                [avg_times_millis / 1000], [therm_chan.therm_config.adc_reading_to_c(avg_adc_value)])
        # All done. Update the display
        update_display()


def set_display_status_line(msg: str) -> None:
    global status_label
    status_label.setText("  " + msg)


def update_display():
    global lc_channels, therm_channels, plot1, plot2, sys_config
    # Update plot 1 with load_cells
    plot1.clear()
    for ch_name, lc_chan in sorted(lc_channels.items()):
        x = lc_chan.display_series.retro_times()
        y = lc_chan.display_series.values()
        color = lc_chan.lc_config.color()
        plot1.plot(x, y, pen=pg.mkPen(color=color, width=2), name=ch_name, antialias=True)
        logger.info(f"{ch_name}: {lc_chan.display_series.mean_value():.3f}")

    # Update plot 2 with thermistors
    plot2.clear()
    for ch_name, therm_chan in sorted(therm_channels.items()):
        x = therm_chan.display_series.retro_times()
        y = therm_chan.display_series.values()
        color = therm_chan.therm_config.color()
        plot2.plot(x, y, pen=pg.mkPen(color=color, width=2), name=ch_name, antialias=True)


async def connection_task():
    """Continuos task that tries to reconnect if needed."""
    global serial_packets_client
    while True:
        if not serial_packets_client.is_connected():
            connected = await serial_packets_client.connect()
            if connected:
                logger.info("Serial port reconnected")
            else:
                logger.error(f"Failed to reconnect to serial port {serial_port}")
            await asyncio.sleep(5)
        else:
            await asyncio.sleep(1)


async def init_serial_packets_client() -> None:
    global sys_config, serial_port, serial_packets_client, connection_task
    # sys_config = SysConfig()
    # assert args.sys_config is not None
    # sys_config.load_from_file(args.sys_config)
    serial_port = sys_config.get_data_link_port()
    serial_packets_client = SerialPacketsClient(serial_port,
                                                command_async_callback=None,
                                                message_async_callback=message_async_callback,
                                                event_async_callback=None,
                                                baudrate=576000)
    connected = await serial_packets_client.connect()
    assert connected, f"Could not open port {serial_port}"
    # We are good. Create a continuous task that will try to reconnect
    # if the serial connection disconnected (e.g. USB plug is removed).
    reconnection_task = asyncio.create_task(connection_task(), name="connection_task")


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
    global plot1, plot2, button1, button2, status_label, app, app_view, lc_channels, therm_channels

    lc_channels = {}
    for chan_name, lc_config in sys_config.get_load_cells_configs().items():
        lc_channels[chan_name] = LoadCellChannel(chan_name, lc_config)

    therm_channels = {}
    for chan_name, therm_config in sys_config.get_thermistors_configs().items():
        therm_channels[chan_name] = ThermistorChannel(chan_name, therm_config)

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

    plot1 = layout.addPlot(title="Load Cells", colspan=5)
    plot1.setLabel('left', 'Force', "g")
    plot1.showGrid(False, True, 0.7)
    plot1.setMouseEnabled(x=False, y=True)
    plot1.setXRange(-1.8, 0)
    plot1.setYRange(-100, 6100)
    plot1.addLegend(offset=(5, 5), verSpacing=-7, brush="#eee", labelTextSize='7pt')

    layout.nextRow()
    plot2 = layout.addPlot(title="Thermistors", colspan=5)
    plot2.setLabel('left', 'Temp', "C")
    plot2.showGrid(False, True, 0.7)
    plot2.setMouseEnabled(x=False, y=True)
    plot2.setXRange(-200, 0)
    plot2.setYRange(0, 260)
    plot2.addLegend(offset=(5, 5), verSpacing=-7, brush="#eee", labelTextSize='7pt')

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
    """Called repeatedly by the pyqtgraph framework."""
    global main_event_loop, serial_packets_client
    global pending_start_button_click, command_start_future
    global pending_stop_button_click, command_stop_future
    global last_command_status_time, command_status_future

    # Process any pending events of the serial packets client.
    # This calls indirectly display_update when new packets arrive.
    main_event_loop.run_until_complete(do_nothing())

    if pending_start_button_click:
        recording_name = time.strftime("%y%m%d-%H%M%S")
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
                writes_ok = response_data.read_uint32()
                write_failures = response_data.read_uint32()
                errors_note = f" ERRORS: {write_failures}" if write_failures else ""
                msg = f"  Recording [{name}] [{recording_millis/1000:.0f} secs] [{writes_ok} records]{errors_note}"
            else:
                msg = "  Recording is off"
            assert response_data.all_read_ok()
        set_display_status_line(msg)


sys_config = SysConfig()
sys_config.load_from_file(args.sys_config)
init_display()
main_event_loop.run_until_complete(init_serial_packets_client())

timer = pg.QtCore.QTimer()
timer.timeout.connect(timer_handler)

# NOTE: The argument 1 is delay in millis between invocations.
timer.start(1)

if __name__ == '__main__':
    pg.exec()
