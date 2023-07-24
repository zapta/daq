from typing import Any, Dict
import tomllib
import logging
import math

logger = logging.getLogger("sys_config")


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

    def color(self) -> str:
        return self.__color


class ThermistorChannelConfig:
    """Configuration of a thermistor channel."""

    def __init__(self, chan_name: str, color: str, short_reading: int, open_reading: int,
                 r_series: int, r_25c: int, beta: int, c: float):
        self.__chan_name = chan_name
        self.__color = color
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
        self.__coef_A = 1.0 / (25.0 + 273.15) - self.__coef_B * ln_R25 - self.__coef_C * ln_R25_cube

    def __str__(self):
        return f"{self.__chan_name} ({self.__color}): range=[{self.__short_reading}, {self.__open_reading}], r_ser={self.__r_series}, r_25c={self.__r_25c}, beta={self.__beta}, c={self.__c}"

    def color(self) -> str:
        return self.__color

    def adc_reading_to_ohms(self, adc_reading: int) -> float:
        ratio = (adc_reading - self.__short_reading) / (self.__open_reading - self.__short_reading)
        if ratio >= 0.99:
            resistance = 1e8 - 1
        else:
            resistance = (ratio * self.__r_series) / (1 - ratio)
        return resistance

    def resistance_to_c(self, r: float) -> float:
        if r >= 1e8 - 1:
            return -273.15
        ln_R = math.log(r)
        # The Steinhart-Hart equation.
        reciprocal_T = self.__coef_A + (self.__coef_B * ln_R) + (self.__coef_C * ln_R * ln_R * ln_R)
        return (1 / reciprocal_T) - 273.15

    def adc_reading_to_c(self, adc_reading: int) -> float:
        r = self.adc_reading_to_ohms(adc_reading)
        c = self.resistance_to_c(r)
        logger.info(f"{self.__chan_name} {adc_reading:.1f}, {r:.2f}, {c:.2f}")
        return c


class SysConfig:
    """System configuration from a TOML config file."""

    def __init__(self):
        """Constructor."""
        self.__toml_dict: Dict[str, Any] = None
        # self.__channels: Dict[str, Any] = None
        self.__lc_ch_configs: Dict[str, LoadCellChannelConfig] = None
        self.__thrm_ch_configs: Dict[str, ThermistorChannelConfig] = None

    def __parse_channels(self) -> None:
        """Assuming __toml_dict is populated. Populates __lc_ch_configs and __thrm_ch_configs."""
        self.__lc_ch_configs = {}
        self.__thrm_ch_configs = {}
        toml_channels = self.__toml_dict["channel"]
        for ch_name, ch_config in toml_channels.items():
            if ch_name.startswith("LC"):
                assert ch_name not in self.__lc_ch_configs
                color = ch_config["color"]
                offset = ch_config["offset"]
                scale = ch_config["scale"]
                self.__lc_ch_configs[ch_name] = LoadCellChannelConfig(ch_name, color, offset, scale)
            elif ch_name.startswith("THRM"):
                assert ch_name not in self.__thrm_ch_configs
                color = ch_config["color"]
                short_reading = ch_config["short_reading"]
                open_reading = ch_config["open_reading"]
                r_series = ch_config["r_series"]
                r_25c = ch_config["r_25c"]
                beta = ch_config["beta"]
                c = ch_config["c"]
                self.__thrm_ch_configs[ch_name] = ThermistorChannelConfig(
                    ch_name, color, short_reading, open_reading, r_series, r_25c, beta, c)
            else:
                raise RuntimeError(f"Unexpected channel name in sys_config: {id}")

    def __str__(self):
        return f"SysConfig: {self.__toml_dict}"

    def lc_chan_names(self):
        return sorted(self.__lc_ch_configs.keys())

    def thrm_chan_names(self):
        return sorted(self.__thrm_ch_configs.keys())

    def load_from_file(self, file_path: str) -> None:
        with open(file_path, "rb") as f:
            self.__toml_dict = tomllib.load(f)
        self.__parse_channels()

    def get_load_cell_config(self, chan_name: str) -> LoadCellChannelConfig:
        return self.__lc_ch_configs[chan_name]

    def get_thermistor_config(self, chan_name: str) -> ThermistorChannelConfig:
        return self.__thrm_ch_configs[chan_name]

    def get_data_link_port(self) -> str:
        return self.__toml_dict["data_link"]["port"]
