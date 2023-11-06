from typing import Any, Dict, Tuple, List, Optional
import tomllib
import logging
import math
import re
import pyqtgraph as pg
from PyQt6 import QtCore

logger = logging.getLogger("sys_config")

# A resistance const that represents an open circuit.
OPEN_CIRCUIT_OHMS = 999999

# A C temp const that represents an open circuit or unknown value.
OPEN_CIRCUIT_C = -99
    
class MarkerConfig:
    """Config of a single marker type."""

    def __init__(self, marker_type: str, marker_regex: str, regex_value_group: int,
                 marker_pen: Any):
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
        """Returns marker type value, both may be empty strings."""
        for marker_config in self.__marker_configs:
            match = marker_config.marker_regex.match(marker_name)
            if match:
                groups = match.groups("")
                n = marker_config.regex_value_group
                value = groups[n - 1] if n > 0 else ""
                return (marker_config.marker_type, value)
        # The marker doesn't match any of the markers in sys_config
        return ("", "")


class LoadCellChannelConfig:
    """Configuration of a load cell channel."""

    def __init__(self, chan_name: str, color: str, adc_offset: int, scale: float):
        self.__chan_name = chan_name
        self.__color = color
        self.__adc_offset = adc_offset
        self.__scale = scale

    def __str__(self):
        return f"Channel {self.__chan_name} ({self.__color}): offset={self.__adc_offset}, scale={self.__scale}"

    def adc_reading_to_grams(self, adc_reading: int) -> float:
        return (adc_reading - self.__adc_offset) * self.__scale

    def dump_lc_calibration(self, adc_reading: int) -> None:
        grams = self.adc_reading_to_grams(adc_reading)
        logger.info(f"{self.__chan_name:7s} adc {adc_reading:7d} -> {grams:7.1f} grams")

    def color(self) -> str:
        return self.__color
      
      
class PowerChannelConfig:
    """Configuration of a power (voltage and current) channel."""

    def __init__(self, chan_name: str, color: str, adc_current_offset: int, adc_current_scale: float, adc_voltage_offset: int, adc_voltage_scale: float):
        self.__chan_name = chan_name
        self.__color = color
        self.__adc_current_offset = adc_current_offset
        self.__adc_current_scale = adc_current_scale
        self.__adc_voltage_offset = adc_voltage_offset
        self.__adc_voltage_scale = adc_voltage_scale

    def __str__(self):
        return f"Channel {self.__chan_name} ({self.__color}): current=({self.__adc_current_offset}, {self.__adc_current_scale}), voltage=({self.__adc_voltage_offset}, {self.__adc_voltage_offset})"

    def adc_current_reading_to_amps(self, adc_current_reading: int) -> float:
        amps =  (adc_current_reading - self.__adc_current_offset) * self.__adc_current_scale
        # Avoid negative zero per https://stackoverflow.com/a/74383961
        return round(amps, 3) + 0.
      
    def adc_voltage_reading_to_volts(self, adc_voltage_reading: int) -> float:
        volts = (adc_voltage_reading - self.__adc_voltage_offset) * self.__adc_voltage_scale
        # Avoid negative zero per https://stackoverflow.com/a/74383961
        return round(volts, 3) + 0.

    def dump_calibration(self, adc_current_reading: int, adc_voltage_reading: int) -> None:
        amps = self.adc_current_reading_to_amps(adc_current_reading)
        volts = self.adc_voltage_reading_to_volts(adc_voltage_reading)
        watts = amps * volts
        logger.info(f"{self.__chan_name + ".v":7s} adc {adc_voltage_reading:7d} -> {volts:7.3f} V")
        logger.info(f"{self.__chan_name + ".a":7s} adc {adc_current_reading:7d} -> {amps:7.3f} A")
        logger.info(f"{self.__chan_name + ".w":7s} computed:   -> {watts:7.3f} W")

    def color(self) -> str:
        return self.__color


class TemperatureChannelConfig:
    """Configuration of a temperature channel. Should be subclassed for each sensor type."""

    def __init__(self, chan_name: str, color: str, adc_open: int, adc_short: int, adc_calib: int,
                 adc_calib_r: float, wire_r: float):
        # Config parameters from sys config.
        self.__chan_name = chan_name
        self.__color = color
        self.__adc_open = adc_open
        self.__adc_short = adc_short
        # Affective series resistance. As of July 2023, nominal 2k ohms.
        # By Thévenin, parallel(serial(2k, 2k), serial(2k, 2k)) ,
        # With the calibration value, we can establish the actual series resistance.
        # As for wire_r, this is the resistance of the sensor's calibration and it does
        # not present in the calibration with the reference R, which is done with a minimal wire.
        self.__r_series = wire_r + ((adc_calib_r * (adc_open - adc_calib)) / (adc_calib - adc_short))

    def chan_name(self) ->  str:
        return self.__chan_name
      
    def color(self) -> str:
        return self.__color

    def adc_reading_to_ohms(self, adc_reading: int) -> float:
        ratio = (adc_reading - self.__adc_short) / (self.__adc_open - self.__adc_short)
        if ratio <= 0.0:
            resistance = 0
        elif ratio >= 0.99:
           # A special case to avoid divide by zero.
           resistance = OPEN_CIRCUIT_OHMS
        else:
            resistance = (ratio * self.__r_series) / (1 - ratio)
            resistance = min(resistance, OPEN_CIRCUIT_OHMS)
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
        logger.info(f"{self.__chan_name:7s} adc {adc_reading:7.0f} -> {r:10.3f} ohms -> {c:6.2f} C")


class ThermistorChannelConfig(TemperatureChannelConfig):
    """Configuration of a temperature channel of type thermistor."""

    def __init__(self, chan_name: str, color: str, adc_open: int, adc_short: int, adc_calib: int,
                 adc_calib_r: float, thermistor_beta: int, thermistor_c: float,
                 thermistor_ref_r: float, thermistor_ref_c: float, wire_r: float, thermistor_offset: float):
        super().__init__(chan_name, color, adc_open, adc_short, adc_calib, adc_calib_r, wire_r)
        # coef_A/B/C are the coefficients of the Steinhart-Hart equation.
        # https://en.wikipedia.org/wiki/Steinhart%E2%80%93Hart_equation
        self.__thermistor_offset = thermistor_offset
        self.__coef_C = thermistor_c
        self.__coef_B = 1.0 / thermistor_beta
        ln_rref = math.log(thermistor_ref_r)
        ln_rref_cube = ln_rref * ln_rref * ln_rref
        # We compute A by solving for T = therm_ref_c.
        self.__coef_A = 1.0 / (thermistor_ref_c +
                               273.15) - self.__coef_B * ln_rref - self.__coef_C * ln_rref_cube

    def __str__(self):
        # TODO: Dump also parent values.
        return f"Channel {self.chan_name()} ({self.color()}): thermistor offset={self.__thermistor_offset}, a={self.__coef_A},  b={self.__coef_B}, c={self.__coef_C}"

    def resistance_to_c(self, r: float) -> float:
        """Thermistor specific resistance to temperature function."""
        if r >= OPEN_CIRCUIT_OHMS:
            return OPEN_CIRCUIT_C
        if r <= 1.0:
            # We use same invalid value for short.
            return OPEN_CIRCUIT_C
        ln_r = math.log(r)
        # The Steinhart-Hart equation.
        reciprocal_t = self.__coef_A + (self.__coef_B * ln_r) + (self.__coef_C * ln_r * ln_r * ln_r)
        t = (1 / reciprocal_t) - 273.15 + self.__thermistor_offset
        return max(OPEN_CIRCUIT_C, t)


# PT1000 [T[C], R[Ohms]] data points.
#
PT1000_TABLE = [(-50, 803.1), (-40, 842.7), (-30, 882.2), (-20, 921.6), (-10, 960.9), (0, 1000.0),
                (10, 1039.0), (20, 1077.9), (30, 1116.7), (40, 1155.4), (50, 1194.0), (60, 1232.4),
                (70, 1270.8), (80, 1309.0), (90, 1347.1), (100, 1385.1), (110, 1422.9),
                (120, 1460.7), (130, 1498.3), (140, 1535.8), (150, 1573.3), (160, 1610.5),
                (170, 1647.7), (180, 1684.8), (190, 1721.7), (200, 1758.6), (210, 1795.3),
                (220, 1831.9), (230, 1868.4), (240, 1904.7), (250, 1941.0), (260, 1977.1),
                (270, 2013.1), (280, 2049.0), (290, 2084.8), (300, 2120.5), (310, 2156.1),
                (320, 2191.5), (330, 2226.8), (340, 2262.1), (350, 2297.2), (360, 2332.1),
                (370, 2367.0), (380, 2401.8), (390, 2436.4), (400, 2470.9), (410, 2505.3),
                (420, 2539.6), (430, 2573.8), (440, 2607.8), (450, 2641.8), (460, 2675.6),
                (470, 2709.3), (480, 2742.9), (490, 2776.4), (500, 2809.8)]

PT1000_MIN_R = PT1000_TABLE[0][1]
PT1000_MAX_R = PT1000_TABLE[-1][1]


class RtdChannelConfig(TemperatureChannelConfig):
    """Configuration of a temperature channel of type RTD (e.g. PT1000)."""

    def __init__(self, chan_name: str, color: str, adc_open: int, adc_short: int, adc_calib: int,
                 adc_calib_r: float, rtd_r0: float, rtd_wire_r: float, rtd_offset: float):
        super().__init__(chan_name, color, adc_open, adc_short, adc_calib, adc_calib_r, rtd_wire_r)
        self.__rtd_r0 = rtd_r0
        self.__rtd_offset = rtd_offset
        self.__last_index = 0

    def __str__(self):
        # TODO: Dump also parent values.
        return f"Channel {self.chan_name()} ({self.color()}): RTD r0={self.__rtd_r0}, offset={self.__rtd_offset}"

    def __interpolate__(self, i, pt1000_r):
        e0 = PT1000_TABLE[i]
        e1 = PT1000_TABLE[i + 1]
        assert e0[1] <= pt1000_r <= e1[1]
        dt = e1[0] - e0[0]
        dr = e1[1] - e0[1]
        fraction = (pt1000_r - e0[1]) / dr
        t = e0[0] + fraction * dt
        return self.__rtd_offset + t

    def resistance_to_c(self, r: float) -> float:
        """RTD specific resistance to temperature function."""
        # Normalize r for 1000 at 0C.
        pt1000_r = r * 1000 / self.__rtd_r0
        # First try the cached bucket, to avoid linear search.
        if PT1000_TABLE[self.__last_index][1] <= pt1000_r <= PT1000_TABLE[self.__last_index + 1][1]:
            return self.__interpolate__(self.__last_index, pt1000_r)
        # R is not in cached bucket. Compute from scratch.
        if pt1000_r < PT1000_MIN_R:
            return OPEN_CIRCUIT_C
        if pt1000_r > PT1000_MAX_R:
            return OPEN_CIRCUIT_C
        for i in range(len(PT1000_TABLE) - 1):
            if pt1000_r <= PT1000_TABLE[i + 1][1]:
                self.__last_index = i
                return self.__interpolate__(i, pt1000_r)
        # This should never happen.
        raise RuntimeError(f"Can't find temperature for r={r} ({pt1000_r})")


class SysConfig:
    """System configuration from a TOML config file."""

    def __init__(self):
        """Constructor."""
        self.__com_port: Optional[str] = None
        self.__lc_ch_configs: Optional[Dict[str, LoadCellChannelConfig]] = None
        self.__pw_ch_configs: Optional[Dict[str, PowerChannelConfig]] = None
        self.__tm_ch_configs: Optional[Dict[str, TemperatureChannelConfig]] = None
        self.__markers_config: Optional[MarkersConfig] = None

    def __populate_com_port(self, toml: Dict[str, Any]) -> None:
        """Populates self.__com_port"""
        self.__com_port = toml["data_link"]["port"]

    def __populate_channels(self, toml: Dict[str, Any]) -> None:
        """Populates __ldc_ch_configs and __tmp_ch_configs from toml sys_config."""
        self.__lc_ch_configs = {}
        self.__pw_ch_configs = {}
        self.__tm_ch_configs = {}
        toml_channels = toml["channel"]
        for ch_name, ch_config in toml_channels.items():
            # Load cell channel config
            if ch_name.startswith("lc"):
                assert ch_name not in self.__lc_ch_configs
                color = ch_config["color"]
                offset = ch_config["adc_offset"]
                scale = ch_config["scale"]
                lc_ch_config =  LoadCellChannelConfig(
                    ch_name, color, offset, scale)
                logger.info(lc_ch_config)
                self.__lc_ch_configs[ch_name] = lc_ch_config
                
            # Power channel config.
            elif ch_name.startswith("pw"):
                assert ch_name not in self.__lc_ch_configs
                color = ch_config["color"]
                current_config = ch_config["current"]
                adc_current_offset = current_config["adc_offset"]
                adc_current_scale = current_config["scale"]
                voltage_config = ch_config["voltage"]
                adc_voltage_offset = voltage_config["adc_offset"]
                adc_voltage_scale = voltage_config["scale"]
                pw_ch_config = PowerChannelConfig(
                    ch_name, color, adc_current_offset, adc_current_scale, adc_voltage_offset, adc_voltage_scale)
                logger.info(pw_ch_config)
                self.__pw_ch_configs[ch_name] = pw_ch_config
                
                
            # Temperature channel config.  
            elif ch_name.startswith("tm"):
                assert ch_name not in self.__tm_ch_configs
                color = ch_config["color"]
                # Get ADC specific params.
                adc_config = ch_config["adc"]
                adc_open = adc_config["open"]
                adc_short = adc_config["short"]
                adc_calib = adc_config["calib"]
                adc_calib_r = adc_config["calib_r"]
                # We expect the channel to have exactly one of thermistor or RTD specification.
                rtd_config = ch_config.get("rtd", None)
                if rtd_config:
                    assert "thermistor" not in ch_config, "A channel cannot be both a thermistor and a RTD"
                    rtd_r0 = rtd_config["r0"]
                    rtd_wire_r = rtd_config["adjustment"]
                    rtd_adjustment = rtd_config["adjustment"]
                    # logger.info(f"RTD channel: {ch_name}, {rtd_r0}")
                    tm_ch_config = RtdChannelConfig(ch_name, color, adc_open,
                                                                      adc_short, adc_calib,
                                                                      adc_calib_r, rtd_r0, rtd_wire_r,
                                                                      rtd_adjustment)
                    logger.info(tm_ch_config)
                    self.__tm_ch_configs[ch_name] = tm_ch_config
                else:
                    thermistor_config = ch_config["thermistor"]
                    thermistor_beta = thermistor_config["beta"]
                    thermistor_c = thermistor_config["c"]
                    thermistor_ref_r = thermistor_config["ref_r"]
                    thermistor_ref_c = thermistor_config["ref_c"]
                    thermistor_wire_r = thermistor_config["wire_r"]
                    thermistor_adjustment = thermistor_config["adjustment"]
                    # logger.info(
                    #     f"Thermistor channel: {ch_name}, {thermistor_beta},  {thermistor_c},"
                    #     f" {thermistor_ref_r}, {thermistor_ref_c}"
                    # )
                    tm_ch_config = ThermistorChannelConfig(
                        ch_name, color, adc_open, adc_short, adc_calib, adc_calib_r,
                        thermistor_beta, thermistor_c, thermistor_ref_r, thermistor_ref_c, thermistor_wire_r,
                        thermistor_adjustment)
                    logger.info(tm_ch_config)
                    self.__tm_ch_configs[ch_name] = tm_ch_config
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

    def load_from_file(self, file_path: str) -> None:
        with open(file_path, "rb") as f:
            toml: Dict[str, Any] = tomllib.load(f)
            self.__populate_com_port(toml)
            self.__populate_channels(toml)
            self.__populate_markers(toml)

    # Individual channel getters.
    def load_cell_config(self, chan_name: str) -> Optional[LoadCellChannelConfig]:
        return self.__lc_ch_configs.get(chan_name, None)
      
    def power_config(self, chan_name: str) -> Optional[PowerChannelConfig]:
        return self.__pw_ch_configs.get(chan_name, None)

    def temperature_config(self, chan_name: str) -> Optional[TemperatureChannelConfig]:
        return self.__tm_ch_configs.get(chan_name, None)
      
    # Channel list getters
    def load_cells_configs(self) -> Dict[str, LoadCellChannelConfig]:
        return self.__lc_ch_configs
      
    def power_configs(self) -> Dict[str, PowerChannelConfig]:
        return self.__pw_ch_configs

    def temperature_configs(self) -> Dict[str, TemperatureChannelConfig]:
        return self.__tm_ch_configs
      
    # Other getters
    def markers_config(self) -> MarkersConfig:
        return self.__markers_config

    def data_link_port(self) -> str:
        return self.__com_port
