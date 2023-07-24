from typing import Any, Dict
import tomllib
import logging
import math

logger = logging.getLogger("sys_config")


class LoadCellChannelConfig:
    """Configuration of a load cell channel."""

    def __init__(self, chan_name: str, offset: int, scale: float):
        self.__chan_name = chan_name
        self.__offset = offset
        self.__scale = scale

    def __str__(self):
        return f"Load cell {self.__chan_name} config: offset={self.__offset}, scale={self.__scale}"

    def adc_reading_to_grams(self, adc_reading: int) -> float:
        return (adc_reading - self.__offset) * self.__scale


class ThermistorChannelConfig:
    """Configuration of a thermistor channel."""

    def __init__(self, chan_name: str, short_reading: int, open_reading: int, r_series: int,
                 r_25c: int, beta: int, c: float):
        self.__chan_name = chan_name
        self.__short_reading = short_reading
        self.__open_reading = open_reading
        self.__r_series = r_series
        self.__r_25c = r_25c
        self.__beta = beta
        # coef_A/B/C are the coefficients of the Steinhart-Hart equation.
        # https://en.wikipedia.org/wiki/Steinhart%E2%80%93Hart_equation
        self.__coef_C = c
        self.__coef_B = 1.0 / self.__beta
        ln_R25 = math.log(self.__r_25c)
        ln_R25_cube = ln_R25 * ln_R25 * ln_R25
        # We compute A by solving for T = 25C.
        self.__coef_A = 1.0 / (
            25.0 + 273.15) - self.__coef_B * ln_R25 - self.__coef_C * ln_R25_cube

    def __str__(self):
        return f"Thermistor {self.__chan_name}: range=[{self.__short_reading}, {self.__open_reading}], r_ser={self.__r_series}, r_25c={self.__r_25c}, beta={self.__beta}, c={self.__c}"

    def adc_reading_to_ohms(self, adc_reading: int) -> float:
        ratio = (adc_reading - self.__short_reading) / (self.__open_reading - self.__short_reading)
        if ratio >= 0.99:
            resistance = 1e20
        else:
            resistance = (ratio * self.__r_series) / (1 - ratio)
        return resistance

    def resistance_to_c(self, r: float) -> float:
        ln_R = math.log(r)
        # The Steinhart-Hart equation.
        reciprocal_T = self.__coef_A + (self.__coef_B * ln_R) + (self.__coef_C * ln_R * ln_R * ln_R)
        if reciprocal_T <= 0:
            return -273.15
        return (1 / reciprocal_T) - 273.15

    def adc_reading_to_c(self, adc_reading: int) -> float:
        r = self.adc_reading_to_ohms(adc_reading)
        c = self.resistance_to_c(r)
        logger.info(f"Therm: {adc_reading:.1f}, {r:.7f}, {c:.2f}")
        return c


class SysConfig:
    """System configuration from a TOML config file."""

    def __init__(self):
        """Constructor."""
        self.__toml_dict: Dict[str, Any] = None
        self.__channels: Dict[str, Any] = None

    def __str__(self):
        return f"SysConfig: {self.__toml_dict}"

    def load_from_file(self, file_path: str) -> None:
        with open(file_path, "rb") as f:
            self.__toml_dict = tomllib.load(f)
        self.__channels = self.__toml_dict["channel"]

    def get_load_cell_config(self, chan_name: str) -> LoadCellChannelConfig:
        channel = self.__channels[chan_name]
        offset = channel["offset"]
        scale = channel["scale"]
        return LoadCellChannelConfig(chan_name, offset, scale)

    def get_thermistor_config(self, chan_name: str) -> ThermistorChannelConfig:
        channel = self.__channels[chan_name]
        short_reading = channel["short_reading"]
        open_reading = channel["open_reading"]
        r_series = channel["r_series"]
        r_25c = channel["r_25c"]
        beta = channel["beta"]
        c = channel["c"]
        return ThermistorChannelConfig(chan_name, short_reading, open_reading, r_series, r_25c,
                                       beta, c)

    def get_data_link_port(self) -> str:
        return self.__toml_dict["data_link"]["port"]
