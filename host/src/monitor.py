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
import statistics
from PyQt6 import QtWidgets, QtCore
from pyqtgraph import PlotWidget, plot, LabelItem
from typing import Tuple, Optional, List, Dict, Any
from lib.log_parser import LogPacketsParser, ChannelData, ParsedLogPacket
from lib.sys_config import SysConfig, MarkersConfig, LoadCellChannelConfig, TemperatureChannelConfig
from lib.display_series import DisplaySeries
from dataclasses import dataclass, field

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
plot2_x_span = 70.0
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


@dataclass(frozen=True)
class MarkerEntry:
    """A single marker entry in the marker history list."""
    marker_time: float
    marker_name: str
    marker_pen: Any


@dataclass(frozen=True)
class MarkerHistory:
    """A class that tracks the recent markers."""
    markers: List[MarkerEntry] = field(default_factory=list)

    # def __init__(self):
    #     # Oldest first.
    #     self.markers: List[MarkerEntry] = []

    def clear(self):
        self.markers.clear()

    def append(self, time: float, name: str, pen: Any) -> None:
        # Make sure time is monotonic. Time is in secs.
        if self.markers:
            assert self.markers[-1].marker_time <= time
        self.markers.append(MarkerEntry(time, name, pen))

    def prune_older_than(self, min_time: float):
        """Delete prefix of marker older than min_time"""
        items_to_delete = len(self.markers)
        for i in range(len(self.markers)):
            if self.markers[i].marker_time >= min_time:
                items_to_delete = i
                break
        if items_to_delete:
            del self.markers[0:items_to_delete]


@dataclass(frozen=True)
class LoadCellChannel:
    chan_name: str
    lc_config: LoadCellChannelConfig
    display_series: DisplaySeries


@dataclass(frozen=True)
class TemperatureChannel:
    chan_name: str
    temperature_config: TemperatureChannelConfig
    display_series: DisplaySeries


# Initialized later.
sys_config: Optional[SysConfig] = None
serial_port: Optional[str] = None
serial_packets_client: Optional[SerialPacketsClient] = None
# serial_reconnection_task = None

# Initialized later. Keys are channel names.
lc_channels: Optional[Dict[str, LoadCellChannel]] = None
temperature_channels: Optional[Dict[str, TemperatureChannel]] = None

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

plot1: Optional[pg.PlotItem] = None
plot2: Optional[pg.PlotItem] = None
button1: Optional[QtWidgets.QPushButton] = None
button2: Optional[QtWidgets.QPushButton] = None
status_label: Optional[LabelItem] = None
app = None
app_view = None

# Initialized with channel_name -> channel_config, for load
# cells and temperature channels respectively.
lc_configs = {}
temperature_configs = {}

# Device time in secs. Represent the time of last
# known info.
latest_log_time: Optional[float] = None


async def message_async_callback(endpoint: int, data: PacketData) -> None:
    """Callback from the serial packets clients for incoming messages."""
    global lc_channels, temperature_channels, latest_log_time
    logger.debug(f"Received message: [%d] %s", endpoint, data.hex_str(max_bytes=10))
    if endpoint == 10:
        parsed_log_packet: ParsedLogPacket = log_packets_parser.parse_next_packet(data)
        # Ignore packet if it's a left over from a previous device session (before reboot).
        if parsed_log_packet.session_id() != device_session_id:
            logger.warning(
                f"Session id mismatch ({parsed_log_packet.session_id():08x} vs {device_session_id:08x}), ignoring packet"
            )
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
                lc_chan.lc_config.dump_lc_calibration(round(avg_adc_value))
        # Process temperature channels
        for temperature_chan_name in temperature_channels.keys():
            # Process temperature channel. We compute the average of the readings
            # in this packet.
            temperature_data: ChannelData = parsed_log_packet.channel(temperature_chan_name)
            if not temperature_data:
                # This packet has no data for this channel.
                break
            temperature_chan: TemperatureChannel = temperature_channels[temperature_chan_name]
            times_millis = []
            adc_values = []
            for time_millis, adc_value in temperature_data.timed_values():
                times_millis.append(time_millis)
                adc_values.append(adc_value)
            avg_times_millis = statistics.mean(times_millis)
            avg_adc_value = statistics.mean(adc_values)
            temperature_chan.display_series.extend(
                [avg_times_millis / 1000],
                [temperature_chan.temperature_config.adc_reading_to_c(round(avg_adc_value))])
            if args.calibration:
                temperature_chan.temperature_config.dump_temperature_calibration(round(avg_adc_value))
        # Process marker channel.
        marker_data: ChannelData = parsed_log_packet.channel("MRKR")
        markers_config: MarkersConfig = sys_config.markers_config()
        if marker_data:
            for time_millis, marker_name in marker_data.timed_values():
                time = time_millis / 1000
                marker_type, marker_value = markers_config.classify_marker(marker_name)
                logger.info(
                    f"Marker: [{marker_name}] type=[{marker_type}] value[{marker_value}] time={time:.3f}]"
                )
                markers_history.append(time, marker_name,
                                       markers_config.pen_for_marker(marker_name))

        # All done. Update the display
        update_display()


def set_display_status_line(msg: str) -> None:
    global status_label
    status_label.setText("  " + msg)


def update_display():
    global lc_channels, temperature_channels, plot1, plot2, sys_config, latest_log_time

    # logger.info(f"Update display(), latest_log_time = {latest_log_time}")
    plot1.clear()
    plot2.clear()

    if latest_log_time is None:
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

    # Update plot 2 with temperature channels
    for ch_name, temperature_chan in sorted(temperature_channels.items()):
        temperature_chan.display_series.delete_older_than(latest_log_time - plot2_x_span)
        x = temperature_chan.display_series.relative_times(latest_log_time)
        y = temperature_chan.display_series.values()
        color = temperature_chan.temperature_config.color()
        plot2.plot(x, y, pen=pg.mkPen(color=color, width=2), name=ch_name, antialias=True)

    # Draw the markers on plot1, plot2.
    # marker_pen = pg.mkPen(color="red", width=2)
    for marker in markers_history.markers:
        rel_time = marker.marker_time - latest_log_time
        if rel_time > -plot1_x_span:
            plot1.addLine(x=rel_time, pen=marker.marker_pen)
        if rel_time > -plot2_x_span:
            plot2.addLine(x=rel_time, pen=marker.marker_pen)


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
    serial_port = sys_config.data_link_port()
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
    pass


def on_start_button():
    global pending_start_button_click
    logger.info("START button pressed")
    pending_start_button_click = True


def on_stop_button():
    global pending_stop_button_click
    logger.info("STOP button pressed")
    pending_stop_button_click = True


def init_display():
    global plot1, plot2, button1, button2, status_label, app, app_view, lc_channels, temperature_channels

    lc_channels = {}
    for chan_name, lc_config in sys_config.load_cells_configs().items():
        lc_channels[chan_name] = LoadCellChannel(chan_name, lc_config, DisplaySeries())

    temperature_channels = {}
    for chan_name, temperature_config in sys_config.temperature_configs().items():
        temperature_channels[chan_name] = TemperatureChannel(chan_name, temperature_config,
                                                             DisplaySeries())

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
    plot2 = layout.addPlot(title="Temperatures", colspan=5)
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
    for chan in temperature_channels.values():
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
        recording_name_bytes = bytearray(recording_name.encode())
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
        logger.info(f"START command status: {status}{' (Error)' if status != 0 else ''}")
        # set_status_line(f"START command status: {status}")

    if pending_stop_button_click:
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
            sd_card_inserted = response_data.read_uint8()
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
                msg = f"  Recording [{name}] [{recording_millis / 1000:.0f} secs] [{writes_ok} records]{errors_note}"
            else:
                msg = "  Recording is off" if sd_card_inserted else "  No SD card"
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
