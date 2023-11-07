from typing import Optional, List, Tuple, Any, Dict
from collections import OrderedDict
import logging
import sys
from dataclasses import dataclass, field

# Local imports
sys.path.insert(0, "..")
from lib.sys_config import SysConfig, LoadCellChannelConfig, PowerChannelConfig, TemperatureChannelConfig


# For using the local version of serial_packet. Comment out if
# using serial_packets package installed by pip.
# import sys
# sys.path.insert(0, "../../../../serial_packets_py/repo/src")

from serial_packets.packets import PacketData


logger = logging.getLogger("main")


# NOTE: All channel item value are assumed to have a time_millis:int field.

@dataclass(frozen=True)
class LcChannelValue:
    """A single value of a load cell channel."""
    time_millis: int
    adc_reading: int
    value_grams: float
    
@dataclass(frozen=True)
class PwChannelValue:
    """A single value of a power (volt/amp) channel."""
    time_millis: int
    adc_voltage_reading: int
    adc_current_reading: int
    value_volts: float
    value_amps: float
    
@dataclass(frozen=True)
class TmChannelValue:
    """A single value of a temperature channel."""
    time_millis: int
    adc_reading: int
    r_ohms: float
    t_celsius: float
    
@dataclass(frozen=True)
class MrkChannelValue:
    """A single value of the markers channel."""
    time_millis: int
    marker_name: str
    marker_type: str
    marker_value: str
    
class ChannelData:

    def __init__(self, chan_name: str):
        self.__chan_name = chan_name
        # List of (time_millis, value)
        self.__values: List[Tuple[int, Any]] = []

    def __str__(self) -> str:
        return f"Chan {self.__chan_name}: {self.size()} values"

    def chan_name(self) -> str:
        return self.__chan_name

    def values(self) -> List[Tuple[int, Any]]:
        return self.__values

    def size(self) -> int:
        return len(self.__values)

    def is_empty(self) -> int:
        return len(self.__values) < 1

    def start_time_millis(self) -> Optional[int]:
        if self.is_empty():
            return None
        return self.__values[0].time_millis

    def end_time_millis(self) -> Optional[int]:
        if self.is_empty():
            return None
        return self.__values[-1].time_millis

    def append_values(self, values: List[Tuple[int, any]]) -> None:
        assert len(values) > 0
        if not self.is_empty():
            assert values[-1].time_millis >= self.end_time_millis()
        self.__values.extend(values)


class ParsedLogPacket:

    def __init__(self, session_id: int, packet_base_time_millis: int):
        """Constructor."""
        self.__session_id: int = session_id
        self.__packet_base_time_millis: int = packet_base_time_millis
        self.__channels: Dict[str, ChannelData] = OrderedDict()

    def session_id(self) -> int:
        return self.__session_id

    def base_time_millis(self) -> int:
        return self.__packet_base_time_millis

    def base_time_secs(self) -> float:
        return self.__packet_base_time_millis / 1000.0

    def channels(self) -> Dict[str, ChannelData]:
        return self.__channels

    def channel(self, chan_name: str) -> Optional[ChannelData]:
        return self.__channels.get(chan_name, None)

    def channel_keys(self) -> List[str]:
        return list(self.__channels.keys())
      
    def num_channels(self):
        return len(self.__channels)

    def start_time_millis(self) -> Optional[int]:
        result = None
        for chan in self.__channels.values():
            chan_start = chan.start_time_millis()
            if (result is None) or result > chan_start:
                result = chan_start
        return result

    def end_time_millis(self) -> int:
        result = self.__packet_base_time_millis
        for chan in self.__channels.values():
            chan_end = chan.end_time_millis()
            if (result is None) or result < chan_end:
                result = chan_end
        return result

    def end_time(self) -> float:
        t_millis = self.end_time_millis()
        return t_millis / 1000.0 if t_millis else None

    def is_empty(self):
        return not self.__channels

    def append_values(self, chan_name: str, values: List[LcChannelValue]|List[PwChannelValue]|List[TmChannelValue]) -> None:
        # logger.info(f"{chan_name}: {len(values)} new values")
        if not chan_name in self.__channels:
            self.__channels[chan_name] = ChannelData(chan_name)
        self.__channels[chan_name].append_values(values)


class LogPacketsParser:

    def __init__(self, sys_config: SysConfig):
        """Constructor."""
        self.__sys_config = sys_config

    def _parse_lc_ch_values(self, chan_name: str, packet_start_time_millis: int,
                           packet_data: PacketData, output: ParsedLogPacket) -> None:
        """Parse an append a list of lc channel values. If chan is not in sys_config, parse and ignore values."""
        # Get channel config. If not in config, we read but ignore this channel.
        lc_ch_config: Optional[LoadCellChannelConfig] = self.__sys_config.load_cell_config(chan_name)
        # Read header 
        first_value_rel_time: int = packet_data.read_uint16()
        num_values = packet_data.read_uint16()
        assert num_values > 1
        step_interval_millis = packet_data.read_uint16()
        assert not packet_data.read_error()
        # Read values
        values = [] 
        item_time_millis: int = packet_start_time_millis + first_value_rel_time
        for i in range(num_values):
            adc_reading: int = packet_data.read_int24()
            if lc_ch_config:
                grams = lc_ch_config.adc_reading_to_grams(adc_reading)
                values.append(LcChannelValue(item_time_millis, adc_reading, grams))
            item_time_millis += step_interval_millis
        assert not packet_data.read_error()
        # If channel is not ignored, add its values.
        if values:
          # logger.info(f"Adding {len(values)} values of chan {chan_name}")
          output.append_values(chan_name, values)
        else:
          logger.info(f"Channel [{chan_name} not in config, skipping {num_values}values")
          
          
    def _parse_pw_ch_values(self, chan_name: str, packet_start_time_millis: int,
                           packet_data: PacketData, output: ParsedLogPacket) -> None:
        """Parse an append a list of power channel values. If chan is not in sys_config, parse and ignore values."""
         # Get channel config. If not in config, we read but ignore this channel.
        pw_ch_config: Optional[PowerChannelConfig] = self.__sys_config.power_config(chan_name)
        # Read header 
        first_value_rel_time: int = packet_data.read_uint16()
        num_values = packet_data.read_uint16()
        assert num_values > 1
        step_interval_millis = packet_data.read_uint16()
        assert not packet_data.read_error()
        # Read values
        values = [] 
        item_time_millis: int = packet_start_time_millis + first_value_rel_time
        for i in range(num_values):
            adc_voltage_reading: int = packet_data.read_int16()
            adc_current_reading: int = packet_data.read_int16()
            if pw_ch_config:
                value_volts =  pw_ch_config.adc_voltage_reading_to_volts(adc_voltage_reading)
                value_amps =  pw_ch_config.adc_current_reading_to_amps(adc_current_reading)
                values.append(PwChannelValue(item_time_millis, adc_voltage_reading, adc_current_reading, value_volts, value_amps))
            item_time_millis += step_interval_millis
        assert not packet_data.read_error()
        # If channel is not ignored, add its values.
        if values:
          # logger.info(f"Adding {len(values)} values of chan {chan_name}")
          output.append_values(chan_name, values)
        else:
          logger.info(f"Channel [{chan_name} not in config, skipping {num_values}values")
        
    def _parse_tm_ch_values(self, chan_name: str, packet_start_time_millis: int,
                           packet_data: PacketData, output: ParsedLogPacket) -> None:
        """Parse an append a list of temperature channel values. If chan is not in sys_config, parse and ignore values."""
        # Get channel config. If not in config, we read but ignore this channel.
        tm_ch_config: Optional[TemperatureChannelConfig] = self.__sys_config.temperature_config(chan_name)
        # Read header 
        first_value_rel_time: int = packet_data.read_uint16()
        num_values = packet_data.read_uint16()
        assert num_values > 1
        step_interval_millis = packet_data.read_uint16()
        assert not packet_data.read_error()
        # Read values
        values = [] 
        item_time_millis: int = packet_start_time_millis + first_value_rel_time
        for i in range(num_values):
            adc_reading: int = packet_data.read_int24()
            if tm_ch_config:
                r_ohms = tm_ch_config.adc_reading_to_ohms(adc_reading)
                t_celsius = tm_ch_config.resistance_to_c(r_ohms)
                values.append(TmChannelValue(item_time_millis, adc_reading, r_ohms, t_celsius))
            item_time_millis += step_interval_millis
        assert not packet_data.read_error()
        # If channel is not ignored, add its values.
        if tm_ch_config:
          output.append_values(chan_name, values)
        else:
          logger.info(f"Channel [{chan_name} not in config, skipping {num_values}values")
      
    def _parse_mrk_ch_values(self, chan_name: str, packet_start_time_millis: int,
                           packet_data: PacketData, output: ParsedLogPacket) -> None:
        """Parse an append a list of marker values."""
        first_value_rel_time: int = packet_data.read_uint16()
        num_values = packet_data.read_uint16()
        assert num_values == 1
        assert not packet_data.read_error()
        item_time_millis: int = packet_start_time_millis + first_value_rel_time
        marker_name: str =  packet_data.read_str()
        marker_type, marker_value =  self.__sys_config.markers_config().classify_marker(marker_name)
        output.append_values(chan_name, [MrkChannelValue(item_time_millis, marker_name, marker_type, marker_value)])
        assert not packet_data.read_error()

    def parse_next_packet(self, packet_data: PacketData) -> ParsedLogPacket:
        packet_data.reset_read_location()
        version = packet_data.read_uint8()
        assert version == 1, f"Unexpected log packet version: {version}"
        session_id = packet_data.read_uint32()
        packet_start_time_millis = packet_data.read_uint32()
        result: ParsedLogPacket = ParsedLogPacket(session_id, packet_start_time_millis)
        while not packet_data.read_error() and not packet_data.all_read():
            chan_id = packet_data.read_uint8()
            assert not packet_data.read_error()
            # Parse a load cell channel.
            if 0x11 <= chan_id <= 0x14:
                chan_name = f"lc{chan_id - 0x11 + 1}"
                # None if not found. In this case we read but ignore the channel.
                # lc_ch_config: Optional[LoadCellChannelConfig] = self.__sys_config.load_cell_config(chan_name)
                self._parse_lc_ch_values(chan_name, packet_start_time_millis, packet_data, result)
                # if lc_ch_config:
                #   logger.info(f"Adding {len(values)} values of chan {chan_name}")
                #   result.append_values(chan_name, values)
                # else:
                #   logger.info(f"Channel [{chan_name} not in config, skipping values")
                continue
              
            # Parse a power channel
            if 0x30 <= chan_id <= 0x32:
                chan_name = f"pw{chan_id - 0x30 + 1}"
                self._parse_pw_ch_values(chan_name, packet_start_time_millis, packet_data, result)
                continue
                #
                # # None if not found. In this case we read but ignore the channel.
                # pw_ch_config: Optional[PowerChannelConfig] = self.__sys_config.power_config(chan_name)
                # values: Optional[List[PwChannelValue]] = self._parse_pw_sequence(packet_start_time_millis, packet_data, pw_ch_config)
                # if pw_ch_config:
                #   logger.info(f"Adding {len(values)} value of chan {chan_name}")
                #   result.append_values(chan_name, values)
                # else:
                #   logger.info(f"Channel [{chan_name} not in config, skipping values")
                # continue
              
            # Temperature channels.
            if 0x21 <= chan_id <= 0x26:
                chan_name = f"tm{chan_id - 0x21 + 1}"
                self._parse_tm_ch_values(chan_name, packet_start_time_millis, packet_data, result)
                # timed_values = self._parse_int24_sequence(packet_start_time_millis, packet_data)
                # result.append_values(chan_name, timed_values)
                continue
              
            # Markers.
            if chan_id == 0x07:
                chan_name = "mrk"
                self._parse_mrk_ch_values(chan_name, packet_start_time_millis, packet_data, result)
                # timed_values = self._parse_str_sequence(packet_start_time_millis, packet_data)
                # logger.info(f"Marker: {timed_values}")
                # result.append_values(chan_name, timed_values)
                continue
              
            # Unknown channel id
            raise ValueError(f"Unexpected log chan id: 0x{chan_id:02x}")
        assert packet_data.all_read_ok()
        return result
