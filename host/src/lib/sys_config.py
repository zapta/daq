from typing import Any, Dict, Tuple, Optional, List
import tomllib
import logging
import math
import re
import pyqtgraph as pg
from PyQt6 import QtCore

logger = logging.getLogger("sys_config")


class MarkerConfig:
    """Config of a single marker type."""

    def __init__(self, marker_type: str, marker_regex: str,regex_value_group: int, marker_pen: Any):
        self.marker_type = marker_type
        self.marker_regex = re.compile(marker_regex, re.IGNORECASE)
        self.regex_value_group = regex_value_group
        self.marker_pen = marker_pen


class MarkersConfig:
    """Configs of all marker types."""

    def __init__(self):
        self.__marker_configs: List[MarkerConfig] = []
        self.__default_marker_pen = pg.mkPen(color="gray",
                                             width=1,
                                             style=QtCore.Qt.PenStyle.DashLine)

    def append_marker(self, marker_config: MarkerConfig) -> None:
        self.__marker_configs.append(marker_config)

    def pen_for_marker(self, marker_name: str) -> Any:
        for marker_config in self.__marker_configs:
            if marker_config.marker_regex.match(marker_name):
                return marker_config.marker_pen
        return self.__default_marker_pen

    def classify_marker(self, marker_name: str) -> Tuple[str, str]:
        """Returns marker optional action and optional test name."""
        for marker_config in self.__marker_configs:
            match = marker_config.marker_regex.match(marker_name)
            if match:
                groups = match.groups("")
                n = marker_config.regex_value_group
                value = groups[n-1] if n > 0 else ""
                return (marker_config.marker_type, value)
        # The marker doesn't match any of the markers in sys_config
        return ("", "")


class LoadCellChannelConfig:
    """Configuration of a load cell channel."""

    def __init__(self, chan_name: str, color: str, offset: int, scale: float):
        self.__chan_name = chan_name
        self.__color = color
        self.__offset = offset
        self.__scale = scale

    def __str__(self):
        return f"Load cell {self.__chan_name} ({self.__color}): offset={self.__offset}, scale={self.__scale}"

    def adc_reading_to_grams(self, adc_reading: int) -> float:
        return (adc_reading - self.__offset) * self.__scale

    def dump_lc_calibration(self, adc_reading: int) -> None:
        grams = self.adc_reading_to_grams(adc_reading)
        logger.info(f"{self.__chan_name:5s} adc {adc_reading:7.0f} -> {grams:7.1f} grams")

    def color(self) -> str:
        return self.__color


class TemperatureChannelConfig:
    """Configuration of a temperature channel. Should be subclassed for each sensor type.."""

    def __init__(self, chan_name: str, color: str, adc_open: int, adc_short: int, adc_calib: int,
                 adc_calib_r: float):
        # Config parameters from sys config.
        self.__chan_name = chan_name
        self.__color = color
        self.__adc_open = adc_open
        self.__adc_short = adc_short
        # Affective series resistance. As of July 2023, nominal 2k ohms.
        # By Thévenin, parallel(serial(2k, 2k), serial(2k, 2k)) ,
        # With the calibration value, we can establish the actual series resistance.
        self.__r_series = (adc_calib_r * (adc_open - adc_calib)) / (adc_calib - adc_short)

    def color(self) -> str:
        return self.__color

    def adc_reading_to_ohms(self, adc_reading: int) -> float:
        ratio = (adc_reading - self.__adc_short) / (self.__adc_open - self.__adc_short)
        if ratio <= 0.0:
            resistance = 0
        elif ratio >= 0.99:
            resistance = 1e8 - 1
        else:
            resistance = (ratio * self.__r_series) / (1 - ratio)
        return resistance

    def resistance_to_c(self, r: float) -> float:
        raise NotImplemented("Should be implemented by subclass")

    def adc_reading_to_c(self, adc_reading: int) -> float:
        r = self.adc_reading_to_ohms(adc_reading)
        c = self.resistance_to_c(r)
        return c

    def dump_temperature_calibration(self, adc_reading: int) -> None:
        r = self.adc_reading_to_ohms(adc_reading)
        c = self.resistance_to_c(r)
        logger.info(f"{self.__chan_name:5s} adc {adc_reading:7.0f} -> {r:10.3f} ohms -> {c:6.2f} C")


class ThermistorChannelConfig(TemperatureChannelConfig):
    """Configuration of a temperature channel of type thermistor."""

    def __init__(self, chan_name: str, color: str, adc_open: int, adc_short: int, adc_calib: int,
                 adc_calib_r: float, thermistor_beta: int, thermistor_c: float,
                 thermistor_ref_r: float, thermistor_ref_c: int):
        super().__init__(chan_name, color, adc_open, adc_short, adc_calib, adc_calib_r)
        # coef_A/B/C are the coefficients of the Steinhart-Hart equation.
        # https://en.wikipedia.org/wiki/Steinhart%E2%80%93Hart_equation
        self.__coef_C = thermistor_c
        self.__coef_B = 1.0 / thermistor_beta
        ln_Rref = math.log(thermistor_ref_r)
        ln_Rref_cube = ln_Rref * ln_Rref * ln_Rref
        # We compute A by solving for T = therm_ref_c.
        self.__coef_A = 1.0 / (thermistor_ref_c +
                               273.15) - self.__coef_B * ln_Rref - self.__coef_C * ln_Rref_cube

    def __str__(self):
        return f"Thermistor {self.__chan_name}"

    def resistance_to_c(self, r: float) -> float:
        """Thermistor specific resistance to temperature function."""
        if r >= 1e8 - 1:
            return -273.15
        if r <= 1.0:
            return 999
        ln_R = math.log(r)
        # The Steinhart-Hart equation.
        reciprocal_T = self.__coef_A + (self.__coef_B * ln_R) + (self.__coef_C * ln_R * ln_R * ln_R)
        T = (1 / reciprocal_T) - 273.15
        if T > 9999:
            return 999
        return T


class SysConfig:
    """System configuration from a TOML config file."""

    def __init__(self):
        """Constructor."""
        # self.__toml_dict: Dict[str, Any] = None
        # self.__channels: Dict[str, Any] = None
        self.__com_port: str = None
        self.__ldc_ch_configs: Dict[str, LoadCellChannelConfig] = None
        self.__tmp_ch_configs: Dict[str, TemperatureChannelConfig] = None
        self.__markers_config: MarkersConfig = None

    def __populate_com_port(self, toml: Dict[str, Any]) -> None:
        """Populates self.__com_port"""
        self.__com_port = toml["data_link"]["port"]

    def __populate_channels(self, toml: Dict[str, Any]) -> None:
        """Populates __ldc_ch_configs and __tmp_ch_configs from toml sys_config."""
        self.__ldc_ch_configs = {}
        self.__tmp_ch_configs = {}
        toml_channels = toml["channel"]
        for ch_name, ch_config in toml_channels.items():
            if ch_name.startswith("LDC"):
                assert ch_name not in self.__ldc_ch_configs
                color = ch_config["color"]
                offset = ch_config["adc_offset"]
                scale = ch_config["scale"]
                self.__ldc_ch_configs[ch_name] = LoadCellChannelConfig(
                    ch_name, color, offset, scale)
            elif ch_name.startswith("TMP"):
                assert ch_name not in self.__tmp_ch_configs
                color = ch_config["color"]
                # Get ADC specific params.
                adc_config = ch_config["adc"]
                adc_open = adc_config["open"]
                adc_short = adc_config["short"]
                adc_calib = adc_config["calib"]
                adc_calib_r = adc_config["calib_r"]
                # For now we support only one kind of a temperature channel, thermistor.
                thermistor_config = ch_config["thermistor"]
                thermistor_beta = thermistor_config["beta"]
                thermistor_c = thermistor_config["c"]
                thermistor_ref_r = thermistor_config["ref_r"]
                thermistor_ref_c = thermistor_config["ref_c"]
                # Construct.
                self.__tmp_ch_configs[ch_name] = ThermistorChannelConfig(
                    ch_name, color, adc_open, adc_short, adc_calib, adc_calib_r, thermistor_beta,
                    thermistor_c, thermistor_ref_r, thermistor_ref_c)
            else:
                raise RuntimeError(f"Unexpected channel name in sys_config: {ch_name}")

    @classmethod
    def __parse_toml_pen(cls, toml_pen: Dict[str, Any]) -> Any:
        """Parses and return a QT pen specification."""
        color = toml_pen["color"]
        logger.info(f"Pen parser: {color}")
        is_solid = toml_pen["solid"]
        width = toml_pen["width"]
        style = QtCore.Qt.PenStyle.DashLine if is_solid else QtCore.Qt.PenStyle.SolidLine
        return pg.mkPen(color=color, width=width, style=style)

    def __populate_markers(self, toml: Dict[str, Any]) -> None:
        """Populates self.__markers_config from a toml sys_config."""
        self.__markers_config = MarkersConfig()
        toml_markers = toml["marker"]
        for marker_type, marker_config in toml_markers.items():
            logger.info(f"Found marker config: {marker_type}")
            marker_regex = marker_config["regex"]
            regex_value_group = marker_config["value_group"]
            marker_pen = self.__parse_toml_pen(marker_config["pen"])
            marker_config = MarkerConfig(marker_type, marker_regex, regex_value_group, marker_pen)
            self.__markers_config.append_marker(marker_config)

    def __str__(self):
        return f"SysConfig: {self.__toml_dict}"

    def load_from_file(self, file_path: str) -> None:
        with open(file_path, "rb") as f:
            toml: Dict[str, Any] = tomllib.load(f)
            self.__populate_com_port(toml)
            self.__populate_channels(toml)
            self.__populate_markers(toml)

    def load_cell_config(self, chan_name: str) -> LoadCellChannelConfig:
        return self.__ldc_ch_configs[chan_name]

    def temperature_config(self, chan_name: str) -> TemperatureChannelConfig:
        return self.__tmp_ch_configs[chan_name]

    def load_cells_configs(self) -> Dict[str, LoadCellChannelConfig]:
        return self.__ldc_ch_configs

    def temperature_configs(self) -> Dict[str, TemperatureChannelConfig]:
        return self.__tmp_ch_configs

    def markers_config(self) -> MarkersConfig:
        return self.__markers_config

    def data_link_port(self) -> str:
        return self.__com_port
