from typing import  Any, Dict
import tomllib
import logging

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

    def __init__(self, chan_name: str, short_reading: int, open_reading: int, r_series: float):
        self.__chan_name = chan_name
        self.__short_reading = short_reading
        self.__open_reading = open_reading
        self.__r_series = r_series

    def __str__(self):
        return f"Thermistor {self.__chan_name} config: short=0x{self.__short_reading:x}, open=0x{self.__open_reading}, r_series{self.__r_series}"

    def adc_reading_to_ohms(self, adc_reading: int) -> float:
        ratio = (adc_reading - self.__short_reading) / (self.__open_reading - self.__short_reading)
        if ratio >= 0.99:
          resistance = 999999
        else:
          resistance = (ratio * self.__r_series) / (1 - ratio)
        logger.info(f"Therm: {adc_reading:.1f}, {ratio:.7f}, {resistance:.2f}")
        return resistance



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
        return ThermistorChannelConfig(chan_name, short_reading, open_reading, r_series)

    def get_data_link_port(self) -> str:
        return self.__toml_dict["data_link"]["port"]
