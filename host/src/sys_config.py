from typing import Optional, List, Tuple, Literal, Any, Dict
import sys
import tomllib


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
      
    def get_data_link_port(self) -> str:
      return self.__toml_dict["data_link"]["port"]
