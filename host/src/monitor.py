#!python

# A python program to monitor and control the data acquisition.


import argparse
import asyncio
import logging
import signal
import sys
import time
import pyqtgraph as pg

from PyQt6 import QtWidgets, QtCore
from pyqtgraph import LabelItem
from typing import Optional, List, Dict, Any
from dataclasses import dataclass, field
from serial_packets.client import SerialPacketsClient
from serial_packets.packets import PacketStatus, PacketData

# Local imports
sys.path.insert(0, "..")
from lib.log_parser import (
    LogPacketsParser,
    ChannelData,
    ParsedLogPacket,
    LcChannelValue,
    PwChannelValue,
    TmChannelValue,
    ExternalReportChannelValue,
    TimeMarkChannelValue,
)
from lib.sys_config import (
    SysConfig,
    ExternalReportConfig,
    LoadCellChannelConfig,
    PowerChannelConfig,
    TemperatureChannelConfig,
)
from lib.display_series import DisplaySeries


# Allows to stop the program by typing ctrl-c.
signal.signal(signal.SIGINT, lambda number, frame: sys.exit())

parser = argparse.ArgumentParser()
parser.add_argument(
    "--sys_config",
    dest="sys_config",
    default="sys_config.toml",
    help="Path to system configuration file.",
)
parser.add_argument(
    "--calibration",
    dest="calibration",
    default=False,
    action=argparse.BooleanOptionalAction,
    help="If on, dump also calibration info.",
)
parser.add_argument(
    "--refresh_rate",
    dest="refresh_rate",
    type=int,
    default=3,
    help="Graphs refreshes per sec.",
)
parser.add_argument(
    "--buffering_time",
    dest="buffering_time",
    type=float,
    default=0.3,
    help="Show only data points that are older than this time in secs, to avoid flickering.",
)
parser.add_argument(
    "--dry_run",
    dest="dry_run",
    default=False,
    action=argparse.BooleanOptionalAction,
    help="If true, perform sanity check and exit.",
)

args = parser.parse_args()

# X spans, in secs, of the the plots.
# TODO: Make these command line flags.
# TODO: Add a flag to perform down sampling of the LC channel, for display only.
plot1_x_span = 5.5
plot2_x_span = 65.0
plot3_x_span = 65.0
max_plot_x_span = max(plot1_x_span, plot2_x_span, plot3_x_span)

logging.basicConfig(
    level=logging.INFO,
    format="%(relativeCreated)07d %(levelname)-7s %(filename)-10s: %(message)s",
)
logger = logging.getLogger("main")

if args.dry_run:
    logger.info("Running nn Dry Run mode. Sanity checks passed. Terminating.")
    sys.exit(0)

# We use a single event loop for all asyncio operations.
main_event_loop = asyncio.get_event_loop()

# TODO: Move to a common place.
CONTROL_ENDPOINT = 0x01


# System time of last display update.
last_display_update_time: float = None


@dataclass(frozen=True)
class MarkerEntry:
    """A single marker entry in the marker history list."""

    marker_time: float
    marker_str: str
    marker_pen: Any


@dataclass(frozen=True)
class MarkerHistory:
    """A class that tracks the recent markers."""

    markers: List[MarkerEntry] = field(default_factory=list)

    def clear(self):
        self.markers.clear()

    def append(self, marker_time: float, marker_str: str, pen: Any) -> None:
        # Make sure time is monotonic. Time is in secs.
        if self.markers:
            assert self.markers[-1].marker_time <= marker_time
        self.markers.append(MarkerEntry(marker_time, marker_str, pen))

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
    chan_id: str
    lc_config: LoadCellChannelConfig
    display_series: DisplaySeries

    def clear(self):
        self.display_series.clear()


@dataclass(frozen=True)
class PowerChannel:
    chan_id: str
    pw_config: PowerChannelConfig
    display_series: DisplaySeries

    def clear(self):
        self.display_series.clear()


@dataclass(frozen=True)
class TemperatureSensorChannel:
    chan_id: str
    temperature_config: TemperatureChannelConfig
    display_series: DisplaySeries

    def clear(self):
        self.display_series.clear()


@dataclass(frozen=True)
class ExternalReportChannel:
    chan_id: str
    ex_report_config: ExternalReportConfig
    display_series: DisplaySeries

    def clear(self):
        self.display_series.clear()


# Initialized later.
sys_config: Optional[SysConfig] = None
serial_port: Optional[str] = None
serial_packets_client: Optional[SerialPacketsClient] = None
log_packets_parser = None

# Initialized later. Keys are channel names.
lc_channels: Optional[Dict[str, LoadCellChannel]] = None
pw_channels: Optional[Dict[str, PowerChannel]] = None
tm_channels: Optional[Dict[str, TemperatureSensorChannel]] = None
ex_channels: Optional[Dict[str, ExternalReportChannel]] = None

plot1_display_series: List[DisplaySeries] = []
plot2_display_series: List[DisplaySeries] = []
plot3_display_series: List[DisplaySeries] = []

markers_history = MarkerHistory()

# Indicate pending button actions.
pending_start_button_click = False
pending_stop_button_click = False


# Initial window size in pixels.
initial_window_width = 800
initial_window_height = 500
window_title = "DAQ Monitor"

plot1: Optional[pg.PlotItem] = None
plot2: Optional[pg.PlotItem] = None
plot3: Optional[pg.PlotItem] = None
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
latest_log_arrival_time: Optional[float] = None


def pen_for_marker_type(marker_type: str) -> Any:
    if marker_type == "test_begin":
        return pg.mkPen(color="green", width=2)
    if marker_type == "test_end":
        return pg.mkPen(color="red", width=2)
    return pg.mkPen(color="gray", width=1, style=QtCore.Qt.PenStyle.DashLine)


async def message_async_callback(endpoint: int, data: PacketData) -> None:
    """Callback from the serial packets clients for incoming messages."""
    global lc_channels, pw_channels, tm_channels, latest_log_time, latest_log_arrival_time
    logger.debug(f"Received message: [%d] %s", endpoint, data.hex_str(max_bytes=10))
    if endpoint == 10:
        parsed_log_packet: ParsedLogPacket = log_packets_parser.parse_next_packet(data)
        # logger.info(f"New packet, channels: {parsed_log_packet.channel_keys()}")
        # Ignore packet if it's a left over from a previous device session (before reboot).
        if device_session_id is None:
            logger.warning(f"No session id yet, ignoring packet")
            return
        if parsed_log_packet.session_id() != device_session_id:
            logger.warning(
                f"Session id mismatch ({parsed_log_packet.session_id():08x} vs "
                f"{device_session_id:08x}), ignoring packet"
            )
            return
        packet_end_time = parsed_log_packet.end_time()
        if latest_log_time is None or packet_end_time > latest_log_time:
            latest_log_time = packet_end_time
            latest_log_arrival_time = time.time()

        # Process load cell channels
        for lc_chan_id in lc_channels.keys():
            lc_data: ChannelData = parsed_log_packet.channel(lc_chan_id)
            if not lc_data:
                # This packet has no data for this channel.
                break
            lc_chan: LoadCellChannel = lc_channels[lc_chan_id]
            adc_readings_sum = 0
            times_secs = []
            values_g = []
            # lc_value is a
            for lc_value in lc_data.values():
                assert isinstance(lc_value, LcChannelValue)
                times_secs.append(lc_value.time_millis / 1000)
                values_g.append(lc_value.value_grams)
                adc_readings_sum += lc_value.adc_reading
            lc_chan.display_series.extend(times_secs, values_g)
            if args.calibration:
                adc_readings_avg = round(adc_readings_sum / len(times_secs))
                lc_chan.lc_config.dump_lc_calibration(adc_readings_avg)

        # Process power channels.
        for pw_chan_id in pw_channels.keys():
            # logger.info(f"**** Looking for pw chan {pw_ch_name}")
            pw_data: ChannelData = parsed_log_packet.channel(pw_chan_id)
            if not pw_data:
                # This packet has no data for this channel.
                break
            pw_chan: PowerChannel = pw_channels[pw_chan_id]
            times_secs = []
            # values_v = []
            # values_a = []
            values_w = []
            adc_current_readings_sum = 0
            adc_voltage_readings_sum = 0
            for pw_value in pw_data.values():
                assert isinstance(pw_value, PwChannelValue)
                times_secs.append(pw_value.time_millis / 1000)
                # values_v.append(pw_value.value_volts)
                # values_a.append(pw_value.value_amps)
                values_w.append(pw_value.value_volts * pw_value.value_amps)
                adc_voltage_readings_sum += pw_value.adc_voltage_reading
                adc_current_readings_sum += pw_value.adc_current_reading
            pw_chan.display_series.extend(times_secs, values_w)
            if args.calibration:
                avg_adc_current_reading = round(
                    adc_current_readings_sum / len(times_secs)
                )
                avg_adc_voltage_reading = round(
                    adc_voltage_readings_sum / len(times_secs)
                )
                pw_chan.pw_config.dump_calibration(
                    avg_adc_current_reading, avg_adc_voltage_reading
                )

        # Process temperature channels
        for tm_chan_id in tm_channels.keys():
            # Process temperature channel. We compute the average of the readings
            # in this packet.
            tm_chan_data: ChannelData = parsed_log_packet.channel(tm_chan_id)
            if not tm_chan_data:
                # This packet has no data for this channel.
                break
            tm_chan: TemperatureSensorChannel = tm_channels[tm_chan_id]
            times_millis_sum = 0
            adc_readings_sum = 0
            temp_c_sum = 0
            n = 0
            for tm_value in tm_chan_data.values():
                assert isinstance(tm_value, TmChannelValue)
                times_millis_sum += tm_value.time_millis
                adc_readings_sum += tm_value.adc_reading
                temp_c_sum += tm_value.t_celsius
                n += 1
            # We compute average and add a single temp point.
            avg_times_secs = times_millis_sum / (n * 1000)
            avg_adc_reading = round(adc_readings_sum / n)
            avg_temp_c = temp_c_sum / n
            tm_chan.display_series.extend([avg_times_secs], [avg_temp_c])
            if args.calibration:
                tm_chan.temperature_config.dump_temperature_calibration(
                    round(avg_adc_reading)
                )

        # Process time marker channel.
        marker_data: ChannelData = parsed_log_packet.channel("mrk")
        if marker_data:
            # time_markers_config: TimeMarkersConfigs = sys_config.time_markers_configs()
            for mrk_value in marker_data.values():
                assert isinstance(mrk_value, TimeMarkChannelValue)
                marker_time_secs = mrk_value.time_millis / 1000
                # marker_type, marker_value = markers_config.classify_marker(marker_name)
                logger.info(
                    f"Marker: [{mrk_value.marker_str}] type=[{mrk_value.marker_type}] value[{mrk_value.marker_value}] time={marker_time_secs:.3f}]"
                )
                markers_history.append(
                    marker_time_secs,
                    mrk_value.marker_str,
                    pen_for_marker_type(mrk_value.marker_type),
                )

        # Process external reports channels
        for ex_chan_id in ex_channels.keys():
            ex_chan_data: ChannelData = parsed_log_packet.channel(ex_chan_id)
            if not ex_chan_data:
                # This packet has no data for this channel. It's normal.
                continue
            ex_chan: ExternalReportChannel = ex_channels[ex_chan_id]
            times_millis_sum = 0
            # As of Dec 2023 is send one marker per packet.
            assert len(ex_chan_data.values()) == 1
            data_point = ex_chan_data.values()[0]
            assert isinstance(data_point, ExternalReportChannelValue)
            # time_secs = data_point.time_millis / 1000
            ex_chan.display_series.extend(
                [data_point.time_millis / 1000.0], [data_point.float_value]
            )
            print
            if args.calibration:
                ex_chan.ex_report_config.dump_external_report_calibration(
                    data_point.str_value, data_point.float_value
                )


def set_display_status_line(msg: str) -> None:
    global status_label
    status_label.setText("  " + msg)


def update_display():
    global plot1, plot2, plot3, plot1_display_series, plot2_display_series, plot3_display_series
    global lc_channels, tm_channels, ex_channels, sys_config, latest_log_time, latest_log_arrival_time
    global last_display_update_time

    # logger.info(f"Update display(), latest_log_time = {latest_log_time}")
    plot1.clear()
    plot2.clear()
    plot3.clear()

    if latest_log_time is None:
        # logger.info("No log base time to update graphs")
        markers_history.clear()
        return

    last_display_update_time = time.time()

    # For smoother display scroll. We keep advancing the time since the last
    # log packet.
    adjusted_latest_log_time = (
        latest_log_time + (time.time() - latest_log_arrival_time) - args.buffering_time
    )

    # Remove markers that are beyond all plots.
    markers_history.prune_older_than(adjusted_latest_log_time - max_plot_x_span)

    # Update plot 1 with its display series
    plot1_cleanup_time = adjusted_latest_log_time - plot1_x_span
    for display_series in plot1_display_series:
        display_series.delete_older_than(plot1_cleanup_time)
        x, y = display_series.get_display_xy(adjusted_latest_log_time)
        plot1.plot(
            x,
            y,
            pen=pg.mkPen(color=display_series.color(), width=2),
            name=display_series.label(),
            antialias=True,
        )

    # Update plot 2 with its display series
    plot2_cleanup_time = adjusted_latest_log_time - plot2_x_span
    for display_series in plot2_display_series:
        display_series.delete_older_than(plot2_cleanup_time)
        x, y = display_series.get_display_xy(adjusted_latest_log_time)
        plot2.plot(
            x,
            y,
            pen=pg.mkPen(color=display_series.color(), width=2),
            name=display_series.label(),
            antialias=True,
        )

    # Update plot 3 with its display series.
    plot3_cleanup_time = adjusted_latest_log_time - plot3_x_span
    for display_series in plot3_display_series:
        display_series.delete_older_than(plot3_cleanup_time)
        x, y = display_series.get_display_xy(adjusted_latest_log_time)
        plot3.plot(
            x,
            y,
            pen=pg.mkPen(color=display_series.color(), width=2),
            name=display_series.label(),
            antialias=True,
        )

    # Draw the markers on plot1, plot2. plot3.
    # marker_pen = pg.mkPen(color="red", width=2)
    for marker in markers_history.markers:
        rel_time = marker.marker_time - adjusted_latest_log_time
        if rel_time > -plot1_x_span:
            plot1.addLine(x=rel_time, pen=marker.marker_pen)
        if rel_time > -plot2_x_span:
            plot2.addLine(x=rel_time, pen=marker.marker_pen)
        if rel_time > -plot3_x_span:
            plot3.addLine(x=rel_time, pen=marker.marker_pen)


async def connection_task():
    """A continues task that tries to reconnect if needed."""
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

    serial_port = sys_config.data_link_port()
    serial_packets_client = SerialPacketsClient(
        serial_port,
        command_async_callback=None,
        message_async_callback=message_async_callback,
        event_async_callback=None,
        baudrate=115200,
    )
    connected = await serial_packets_client.connect()
    assert connected, f"Could not open port {serial_port}"
    # We are good. Create a continuous task that will try to reconnect
    # if the serial connection disconnected (e.g. USB plug is removed).
    # noinspection PyUnusedLocal
    reconnection_task = asyncio.create_task(connection_task(), name="connection_task")


async def do_nothing():
    """A dummy async method."""
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
    global plot1, plot2, plot3, plot1_display_series, plot2_display_series, plot3_display_series
    global button1, button2, status_label, app, app_view, lc_channels, pw_channels, tm_channels, ex_channels

    lc_channels = {}
    for chan_id, lc_config in sys_config.load_cells_configs().items():
        display_series = DisplaySeries(lc_config.label(), lc_config.color())
        lc_channels[chan_id] = LoadCellChannel(chan_id, lc_config, display_series)
        plot1_display_series.append(display_series)

    tm_channels = {}
    for chan_id, tm_config in sys_config.temperature_configs().items():
        display_series = DisplaySeries(tm_config.label(), tm_config.color())
        tm_channels[chan_id] = TemperatureSensorChannel(
            chan_id, tm_config, display_series
        )
        plot2_display_series.append(display_series)

    ex_channels = {}
    for chan_id, ex_config in sys_config.external_reports_configs().items():
        assert isinstance(ex_config , ExternalReportConfig)
        display_series = DisplaySeries(ex_config.label, ex_config.color)
        ex_channels[chan_id] = ExternalReportChannel(chan_id, ex_config, display_series)
        plot2_display_series.append(display_series)

    pw_channels = {}
    for chan_id, pw_config in sys_config.power_configs().items():
        display_series = DisplaySeries(pw_config.label(), pw_config.color())
        pw_channels[chan_id] = PowerChannel(chan_id, pw_config, display_series)
        plot3_display_series.append(display_series)

    pg.setConfigOption("background", "w")
    pg.setConfigOption("foreground", "k")

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

    plot1 = layout.addPlot(title="Force", colspan=5)
    plot1.setLabel("left", "Force", "g")
    plot1.showGrid(False, True, 0.7)
    plot1.setMouseEnabled(x=False, y=True)
    plot1.setXRange(-(plot1_x_span * 0.95), 0)
    plot1.setYRange(-100, 6100)
    plot1.addLegend(offset=(5, 5), verSpacing=-7, brush="#eee", labelTextSize="7pt")

    layout.nextRow()
    plot2 = layout.addPlot(title="Temperature", colspan=5)
    plot2.setLabel("left", "Temp", "C")
    plot2.showGrid(False, True, 0.7)
    plot2.setMouseEnabled(x=False, y=True)
    plot2.setXRange(-(plot2_x_span * 0.95), 0)
    plot2.setYRange(0, 260)
    plot2.addLegend(offset=(5, 5), verSpacing=-7, brush="#eee", labelTextSize="7pt")

    layout.nextRow()
    plot3 = layout.addPlot(title="Power", colspan=5)
    plot3.setLabel("left", "Power", "W")
    plot3.showGrid(False, True, 0.7)
    plot3.setMouseEnabled(x=False, y=True)
    plot3.setXRange(-(plot3_x_span * 0.95), 0)
    plot3.setYRange(-0.1, 60)
    plot3.addLegend(offset=(5, 5), verSpacing=-7, brush="#eee", labelTextSize="7pt")

    # Add an empty row as spacing.
    # TODO: Is there a cleaner way to specify top margin?
    layout.nextRow()
    status_label = layout.addLabel("")

    layout.nextRow()
    status_label = layout.addLabel("(status line)", colspan=3, justify="left")

    # Button1 - STOP.
    button1_proxy = QtWidgets.QGraphicsProxyWidget()
    button1 = QtWidgets.QPushButton("STOP")
    button1_proxy.setWidget(button1)
    button1.clicked.connect(lambda: on_stop_button())
    layout.addItem(button1_proxy, colspan=1)

    # Button2 - Start
    button2_proxy = QtWidgets.QGraphicsProxyWidget()
    button2 = QtWidgets.QPushButton("START")
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
device_session_id = None


# Resets the session. E.g. when the devices were detected
# to go through reset.
def reset_display():
    global latest_log_time, latest_log_arrival_time
    latest_log_time = None
    latest_log_arrival_time = None
    for chan in lc_channels.values():
        chan.clear()
    for chan in pw_channels.values():
        chan.clear()
    for chan in tm_channels.values():
        chan.clear()
    markers_history.clear()
    set_display_status_line("")
    # Force a display update
    last_display_update_time = None


def timer_handler():
    """Called repeatedly by the pyqtgraph framework."""
    global main_event_loop, serial_packets_client
    global pending_start_button_click, command_start_future
    global pending_stop_button_click, command_stop_future
    global last_command_status_time, command_status_future
    global device_session_id, last_display_update_time

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
        command_start_future = serial_packets_client.send_command_future(
            CONTROL_ENDPOINT, cmd
        )
        pending_start_button_click = False

    if command_start_future and command_start_future.done():
        logger.info("Got response for START command")
        status, response_data = command_start_future.result()
        command_start_future = None
        logger.info(
            f"START command status: {status}{' (Error)' if status != 0 else ''}"
        )
        # set_status_line(f"START command status: {status}")

    if pending_stop_button_click:
        cmd = PacketData()
        cmd.add_uint8(0x03)  # Command = STOP
        logger.info(f"STOP command: {cmd.hex_str(max_bytes=5)}")
        command_stop_future = serial_packets_client.send_command_future(
            CONTROL_ENDPOINT, cmd
        )
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
        command_status_future = serial_packets_client.send_command_future(
            CONTROL_ENDPOINT, cmd
        )
        last_command_status_time = time.time()

    if command_status_future and command_status_future.done():
        logger.debug("Got response for STATUS command")
        status, response_data = command_status_future.result()
        command_status_future = None
        if status != PacketStatus.OK.value:
            logger.error(f"STATUS command failed with status: {status}")
            msg = f"ERROR: Device not available (status {status})"
        else:
            # noinspection PyUnusedLocal
            version = response_data.read_uint8()
            session_id = response_data.read_uint32()
            # noinspection PyUnusedLocal
            device_time_millis = response_data.read_uint32()
            sd_card_inserted = response_data.read_uint8()
            recording_active = response_data.read_uint8()
            if recording_active:
                recording_millis = response_data.read_uint32()
                name = response_data.read_str()
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

    if (last_display_update_time is None) or (
        (time.time() - last_display_update_time) * args.refresh_rate >= 1
    ):
        update_display()


def main():
    global sys_config, log_packets_parser

    sys_config = SysConfig()
    sys_config.load_from_file(args.sys_config)
    log_packets_parser = LogPacketsParser(sys_config)

    init_display()
    main_event_loop.run_until_complete(init_serial_packets_client())

    timer = pg.QtCore.QTimer()
    timer.timeout.connect(timer_handler)

    # NOTE: The argument 1 is delay in millis between invocations.
    timer.start(1)
    pg.exec()


if __name__ == "__main__":
    main()
