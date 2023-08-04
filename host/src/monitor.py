#!python

# A python program to monitor and control the data acquisition.

import argparse
import asyncio
import logging
import signal
import sys
import os
import time
import pyqtgraph as pg
import numpy as np
import re

# TODO: clean up device reset and reconnect logic.

from PyQt6 import QtWidgets, QtCore
from pyqtgraph import PlotWidget, plot, LabelItem
from typing import Tuple, Optional, List, Dict
from lib.log_parser import LogPacketsParser, ChannelData, ParsedLogPacket
from lib.sys_config import SysConfig, LoadCellChannelConfig, ThermistorChannelConfig
from lib.display_series import DisplaySeries

# A workaround to avoid auto formatting.
# For using the local version of serial_packet. Comment out if
# using serial_packets package installed by pip.
# sys.path.insert(0, "../../../../serial_packets_py/repo/src")

from serial_packets.client import SerialPacketsClient
from serial_packets.packets import PacketStatus, PacketData

# Allows to stop the program by typing ctrl-c.
signal.signal(signal.SIGINT, lambda number, frame: sys.exit())

parser = argparse.ArgumentParser()
parser.add_argument("--sys_config",
                    dest="sys_config",
                    default="sys_config.toml",
                    help="Path to system configuration file.")
parser.add_argument('--calibration',
                    dest="calibration",
                    default=False,
                    action=argparse.BooleanOptionalAction,
                    help="If on, dump also calibration info.")

args = parser.parse_args()

# X spans, in secs, of the two plots.
# TODO: Make these command line flags.
# TODO: Add a control of LC channel down sampling for display.
plot1_x_span = 2.0
plot2_x_span = 200.0
max_plot_x_span = max(plot1_x_span, plot2_x_span)

logging.basicConfig(
    level=logging.INFO,
    format="%(relativeCreated)07d %(levelname)-7s %(filename)-10s: %(message)s",
)
logger = logging.getLogger("main")

# We use a single event loop for all asyncio operatios.
main_event_loop = asyncio.get_event_loop()

# TODO: Move to a common place.
CONTROL_ENDPOINT = 0x01

# Add more color regex as needed.
markers_color_table = [[re.compile("stop"), "red"], [re.compile("start"), "green"]]


class MarkerEntry:
    """A single marker entry in the marker history list."""

    def __init__(self, marker_time: float, marker_name: str):
        # Time is device time in seconds.
        self.time = marker_time
        self.name = marker_name
        self.color = self.assign_marker_color(marker_name)
        self.pen = pg.mkPen(color=self.color, width=1, style=QtCore.Qt.PenStyle.DashLine)

    def assign_marker_color(self, marker_name: str) -> str:
        lower_marker_name = marker_name.lower()
        for entry in markers_color_table:
            if entry[0].match(lower_marker_name):
                return entry[1]
        return "gray"


class MarkerHistory:
    """A class that tracks the recent markers."""

    def __init__(self):
        # Oldest first.
        self.markers: List[MarkerEntry] = []

    def clear(self):
        self.markers.clear()

    def append(self, time: float, name: str):
        # Make sure time is monotonic. Time is in secs.
        if self.markers:
            assert self.markers[-1].time <= time
        self.markers.append(MarkerEntry(time, name))

    def prune_older_than(self, min_time: float):
        """Delete prefix of marker older than min_time"""
        items_to_delete = len(self.markers)
        for i in range(len(self.markers)):
            if self.markers[i].time >= min_time:
                items_to_delete = i
                break
        if items_to_delete:
            self.markers = self.markers[items_to_delete:]

    def keep_at_most(self, max_len: int):
        n = len(self.markers)
        if max_len <= 0:
            self.markers = []
        elif max_len > n:
            self.markers = self.markers[-max_len:]


class LoadCellChannel:

    def __init__(self, chan_name: str, lc_config: LoadCellChannelConfig):
        self.chan_name = chan_name
        self.lc_config = lc_config
        self.display_series = DisplaySeries()


class ThermistorChannel:

    def __init__(self, chan_name: str, therm_config: ThermistorChannelConfig):
        self.chan_name = chan_name
        self.therm_config = therm_config
        self.display_series = DisplaySeries()


# Initialized later.
sys_config: SysConfig = None
serial_port: str = None
serial_packets_client: SerialPacketsClient = None
# serial_reconnection_task = None

# Initialized later. Keys are channel names.
lc_channels: Dict[str, LoadCellChannel] = None
therm_channels: Dict[str, ThermistorChannel] = None

markers_history = MarkerHistory()

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

# Device time in secs. Represent the time of last
# known info.
latest_log_time = None


async def message_async_callback(endpoint: int, data: PacketData) -> Tuple[int, PacketData]:
    """Callback from the serial packets clients for incoming messages."""
    global lc_channels, therm_channels, latest_log_time
    logger.debug(f"Received message: [%d] %s", endpoint, data.hex_str(max_bytes=10))
    if endpoint == 10:
        parsed_log_packet: ParsedLogPacket = log_packets_parser.parse_next_packet(data)
        # Ignore packet if it's a left over from a previous device session (before reboot).
        if parsed_log_packet.session_id() != device_session_id:
          logger.warning(f"Session id mismatch ({parsed_log_packet.session_id():08x} vs {device_session_id:08x}), ignoring packet")
          return
        packet_end_time = parsed_log_packet.end_time()
        if latest_log_time is None or packet_end_time > latest_log_time:
            latest_log_time = packet_end_time
        # Process load cell channels
        for lc_ch_name in lc_channels.keys():
            lc_data: ChannelData = parsed_log_packet.channel(lc_ch_name)
            if not lc_data:
                # This packet has no data for this channel.
                break
            lc_chan: LoadCellChannel = lc_channels[lc_ch_name]
            times_secs = []
            values_g = []
            adc_values_sum = 0
            for time_millis, adc_value in lc_data.timed_values():
                times_secs.append(time_millis / 1000)
                values_g.append(lc_chan.lc_config.adc_reading_to_grams(adc_value))
                adc_values_sum += adc_value
            lc_chan.display_series.extend(times_secs, values_g)
            avg_adc_value = adc_values_sum / len(times_secs)
            if args.calibration:
                lc_chan.lc_config.dump_lc_calibration(avg_adc_value)
        # Process thermistor channels
        for thrm_ch_name in therm_channels.keys():
            # Process thermistor. We compute the average of the readings
            # in this packet.
            therm_data: ChannelData = parsed_log_packet.channel(thrm_ch_name)
            if not therm_data:
                # This packet has no data for this channel.
                break
            lc_chan: LoadCellChannel = lc_channels[lc_ch_name]
            therm_chan: ThermistorChannel = therm_channels[thrm_ch_name]
            times_millis = []
            adc_values = []
            for time_millis, adc_value in therm_data.timed_values():
                times_millis.append(time_millis)
                adc_values.append(adc_value)
            avg_times_millis = np.mean(times_millis)
            avg_adc_value = np.mean(adc_values)
            # therm_chan_config: ThermistorChannelConfig = sys_config.get_thermistor_config(
            #     thrm_ch_name)
            # logger.info(f"{therm1_chan_config}")
            therm_chan.display_series.extend(
                [avg_times_millis / 1000],
                [therm_chan.therm_config.adc_reading_to_c(avg_adc_value)])
            if args.calibration:
                therm_chan.therm_config.dump_therm_calibration(avg_adc_value)
        # Process marker channel.
        marker_data: ChannelData = parsed_log_packet.channel("MRKR")
        if marker_data:
            for time_millis, marker_name in marker_data.timed_values():
                time = time_millis / 1000
                logger.info(f"Marker: [{marker_name}] @ t={time:.3f}]")
                markers_history.append(time, marker_name)

        # All done. Update the display
        update_display()


def set_display_status_line(msg: str) -> None:
    global status_label
    status_label.setText("  " + msg)


def update_display():

    global lc_channels, therm_channels, plot1, plot2, sys_config, latest_log_time
    
    # logger.info(f"Update display(), latest_log_time = {latest_log_time}")
    plot1.clear()
    plot2.clear()

    if not latest_log_time:
        logger.info("No log base time to update graphs")
        markers_history.clear()
        return

    # Remove markers that are beyond all plots.
    markers_history.prune_older_than(latest_log_time - max_plot_x_span)

    # Update plot 1 with load_cells
    for ch_name, lc_chan in sorted(lc_channels.items()):
        lc_chan.display_series.delete_older_than(latest_log_time - plot1_x_span)
        x = lc_chan.display_series.relative_times(latest_log_time)
        y = lc_chan.display_series.values()
        color = lc_chan.lc_config.color()
        plot1.plot(x, y, pen=pg.mkPen(color=color, width=2), name=ch_name, antialias=True)
        # logger.info(f"{ch_name}: {lc_chan.display_series.mean_value():.3f}")

    # Update plot 2 with thermistors
    for ch_name, therm_chan in sorted(therm_channels.items()):
        lc_chan.display_series.delete_older_than(latest_log_time - plot2_x_span)
        x = therm_chan.display_series.relative_times(latest_log_time)
        y = therm_chan.display_series.values()
        color = therm_chan.therm_config.color()
        plot2.plot(x, y, pen=pg.mkPen(color=color, width=2), name=ch_name, antialias=True)

    # Draw the markers on plot1, plot2.
    # marker_pen = pg.mkPen(color="red", width=2)
    for marker in markers_history.markers:
        rel_time = marker.time - latest_log_time
        if rel_time > -plot1_x_span:
            plot1.addLine(x=rel_time, pen=marker.pen)
        if rel_time > -plot2_x_span:
            plot2.addLine(x=rel_time, pen=marker.pen)


async def connection_task():
    """Continuos task that tries to reconnect if needed."""
    global serial_packets_client
    while True:
        if not serial_packets_client.is_connected():
            connected = await serial_packets_client.connect()
            if connected:
                logger.info("Serial port reconnected")
                reset_display()
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
                                                baudrate=115200)
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
    plot1.setXRange(-(plot1_x_span * 0.95), 0)
    plot1.setYRange(-100, 6100)
    plot1.addLegend(offset=(5, 5), verSpacing=-7, brush="#eee", labelTextSize='7pt')

    layout.nextRow()
    plot2 = layout.addPlot(title="Thermistors", colspan=5)
    plot2.setLabel('left', 'Temp', "C")
    plot2.showGrid(False, True, 0.7)
    plot2.setMouseEnabled(x=False, y=True)
    plot2.setXRange(-(plot2_x_span * 0.95), 0)
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


# Variables to handle execution of outgoing commands.
command_start_future = None
command_stop_future = None
command_status_future = None

# Last (host) time we sent a command to fetch device status.
last_command_status_time = time.time() - 10

# When the device starts it picks a random uint32 as a session
# id. We use it to detect device restarts. 0 is an 
# invalid session id.
device_session_id = 0


# Resets the session. E.g. when the devices was detected
# to go through reset.
def reset_display():
    global latest_log_time
    latest_log_time = None
    for chan in lc_channels.values():
        chan.display_series.clear()
    for chan in therm_channels.values():
        chan.display_series.clear()
    markers_history.clear()
    set_display_status_line("")
    update_display()


def timer_handler():
    """Called repeatedly by the pyqtgraph framework."""
    global main_event_loop, serial_packets_client
    global pending_start_button_click, command_start_future
    global pending_stop_button_click, command_stop_future
    global last_command_status_time, command_status_future
    global device_session_id

    # Process any pending events of the serial packets client.
    # This calls indirectly display_update when new packets arrive.
    main_event_loop.run_until_complete(do_nothing())

    if pending_start_button_click:
        recording_name = time.strftime("%y%m%d-%H%M%S")
        logger.info(f"Will start a recording {recording_name}")
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
            # print(response_data.hex_str(), flush=True)
            version = response_data.read_uint8()
            # assert(version == 1)
            session_id = response_data.read_uint32()
            # logger.info(f"Session id: {session_id:08x}")
            device_time_millis = response_data.read_uint32()
            recording_active = response_data.read_uint8()
            if recording_active:
                recording_millis = response_data.read_uint32()
                name = response_data.read_str()
                # name_bytes = response_data.read_bytes(name_len)
                # logger.info(f"Name bytes: [{name_bytes}]")
                # name = name_bytes.decode("utf-8")
                writes_ok = response_data.read_uint32()
                write_failures = response_data.read_uint32()
                errors_note = f" ERRORS: {write_failures}" if write_failures else ""
                msg = f"  Recording [{name}] [{recording_millis/1000:.0f} secs] [{writes_ok} records]{errors_note}"
            else:
                msg = "  Recording is off"
            # logger.info(f"*** Available: {response_data.bytes_left_to_read()}")
            # assert not response_data.read_error()
            assert response_data.all_read_ok()
            if session_id != device_session_id:
                logger.info("Device reset detected, clearing display.")
                device_session_id = session_id 
                logger.info(f"New session id: {device_session_id:08x}")
                reset_display()
                msg = "Device reset"  
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
