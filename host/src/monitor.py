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

# from pyqtgraph.Qt.QtCore import QString
from pyqtgraph.Qt import QtWidgets
from pyqtgraph import mkPen, TextItem
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
initial_window_width = 700
initial_window_height = 500
window_title = "DAQ Monitor"

plot1: pg.PlotItem = None
plot2: pg.PlotItem = None
button1: QtWidgets.QPushButton = None
button2: QtWidgets.QPushButton = None
window = None


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
    global window, plot1, plot2, button1, button2

    ## Switch to using white background and black foreground
    pg.setConfigOption('background', 'w')
    pg.setConfigOption('foreground', 'k')

    window = pg.GraphicsLayoutWidget(show=True, size=[initial_window_width, initial_window_height])
    window.setWindowTitle(window_title)

    # Layout class doc: https://doc.qt.io/qt-5/qgraphicsgridlayout.html

    # We divide the window vertically into 5 equal columns
    # to have more control in positioning the buttons.
    #
    # TODO: Is there a cleaner way to have the buttons on the
    # right side?
    window.ci.layout.setColumnPreferredWidth(0, 180)
    window.ci.layout.setColumnPreferredWidth(1, 180)
    window.ci.layout.setColumnPreferredWidth(2, 180)
    window.ci.layout.setColumnPreferredWidth(3, 180)
    window.ci.layout.setColumnPreferredWidth(4, 180)

    # NOTE: See graph configuration params here
    # https://pyqtgraph.readthedocs.io/en/latest/api_reference/graphicsItems/viewbox.html

    # Graph 1 - Load cell grams vs secs.
    plot1 = window.addPlot(name="Plot1", colspan=5)
    plot1.setLabel('left', 'Force', "g")
    plot1.showGrid(False, True, 0.7)
    plot1.setXRange(-1.8, 0)
    plot1.setYRange(-100, 6100)

    # Graph 2 - Thermistor's degrees vs time.
    window.nextRow()
    plot2 = window.addPlot(name="Plot2", colspan=5)
    plot2.setLabel('left', 'Temp', "C")
    plot2.showGrid(False, True, 0.7)

    # Add a spacing row before the buttons.
    # TODO: Can we have a cleaner way to vertically align the buttons?
    window.nextRow()
    dummy_layout = window.addLayout(col=4, colspan=5)
    dummy_layout.layout.setRowFixedHeight(0, 10)

    # Add a row for the buttons
    window.nextRow()
    buttons_layout = window.addLayout(col=0, colspan=5)
    # buttons_layout.setSpacing(20)
    # buttons_layout.layout.setHorizontalSpacing(30)
    # buttons_layout.layout.rowMaximumHeight(200)
    buttons_layout.setBorder("red")
    
    label1 = buttons_layout.addLabel('Long Vertical Label', row=0, rowspan=1, col=0)
    
    # logger.info(f"{type(    buttons_layout.layout)}")
    # logger.info(f"{type(label1)}")

    # text1_proxy = QtWidgets.QGraphicsProxyWidget()
    # text1 = QtWidgets.QGraphicsTextItem ()
    # text1_proxy.setWidget(text1)
    # buttons_layout.addItem(text1, row=1, col=0)
    # text1.setPlainText("laskdf aklsef")

    # text_view_box = buttons_layout.addViewBox(col=0, colspan=4)
    # text = TextItem("xxxxxx")
    # text_view_box.addItem(text)
    
    # text_plot = buttons_layout.addPlot()
    # text_proxy = QtWidgets.QGraphicsProxyWidget()
    # text = QtWidgets.QGraphicsTextItem(QString("aaaa"))
    # # text = pg.TextItem(html='xyz')
    # text_proxy.setWidget(text)
    # buttons_layout.addItem(button1_proxy, row=0, col=1)

    # Button1 - Stop.
    button1_proxy = QtWidgets.QGraphicsProxyWidget()
    button1 = QtWidgets.QPushButton('STOP')
    button1_proxy.setWidget(button1)
    buttons_layout.addItem(button1_proxy, row=0, col=3)
    button1.clicked.connect(lambda: on_stop_button())

    # Button2 - Start
    button2_proxy = QtWidgets.QGraphicsProxyWidget()
    button2 = QtWidgets.QPushButton('START')
    button2_proxy.setWidget(button2)
    buttons_layout.addItem(button2_proxy, row=0, col=4)
    button2.clicked.connect(lambda: on_start_button())


command_start_future = None
command_stop_future = None


def timer_handler():
    global main_event_loop, serial_packets_client
    global pending_start_button_click, command_start_future
    global pending_stop_button_click, command_stop_future

    # Process any pending events of the serial packets client.
    main_event_loop.run_until_complete(do_nothing())

    if pending_start_button_click:
        session_name = time.strftime("session-%Y%m%d-%H%M%S")
        logger.info(f"Will start a recording session named {session_name}")
        session_name_bytes = session_name.encode()
        cmd = PacketData()
        cmd.add_uint8(0x02)  # Command = START
        cmd.add_uint8(len(session_name_bytes))  # str len
        cmd.add_bytes(session_name_bytes)
        logger.info(f"START command: {cmd.hex_str(max_bytes=5)}")
        command_start_future = serial_packets_client.send_command_future(CONTROL_ENDPOINT, cmd)
        pending_start_button_click = False

    if command_start_future and command_start_future.done():
        logger.info("Got response for START command")
        status, response_data = command_start_future.result()
        command_start_future = None
        logger.info(f"START command status: {status}")

    if pending_stop_button_click:
        # session_name = time.strftime("session-%Y%m%d-%H%M%S")
        logger.info(f"Will stop recording session, if any.")
        # session_name_bytes = session_name.encode()
        cmd = PacketData()
        cmd.add_uint8(0x03)  # Command = STOP
        # cmd.add_uint8(len(session_name_bytes))  # str len
        # cmd.add_bytes(session_name_bytes)
        logger.info(f"STOP command: {cmd.hex_str(max_bytes=5)}")
        command_stop_future = serial_packets_client.send_command_future(CONTROL_ENDPOINT, cmd)
        pending_stop_button_click = False

    if command_stop_future and command_stop_future.done():
        logger.info("Got response for STOP command")
        status, response_data = command_stop_future.result()
        command_stop_future = None
        logger.info(f"STOP command status: {status}")


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
