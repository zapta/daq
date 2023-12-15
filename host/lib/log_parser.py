import logging
import sys
import re

from typing import Optional, List, Tuple, Any, Dict
from collections import OrderedDict
from dataclasses import dataclass, field
from serial_packets.packets import PacketData

# Local imports
sys.path.insert(0, "..")
from lib.sys_config import (
    SysConfig,
    LoadCellChannelConfig,
    PowerChannelConfig,
    TemperatureChannelConfig,
)


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
class TimeMarkChannelValue:
    """A single value of the time markers channel."""

    time_millis: int
    marker_str: str
    marker_type: str
    marker_value: str


@dataclass(frozen=True)
class ExternalReportChannelValue:
    """A single value of an external report channel."""

    time_millis: int
    report_str: str
    str_value: str
    float_value: float


class ChannelData:
    def __init__(self, chan_id: str):
        self.__chan_id = chan_id
        # List of (time_millis, value)
        self.__values: List[Any] = []

    def __str__(self) -> str:
        return f"Chan {self.__chan_id}: {self.size()} values"

    def chan_id(self) -> str:
        return self.__chan_id

    def values(self) -> List[Any]:
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

    def append_values(self, values: List[Any]) -> None:
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

    def channel(self, chan_id: str) -> Optional[ChannelData]:
        return self.__channels.get(chan_id, None)

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

    def append_values(
        self,
        chan_id: str,
        values: List[LcChannelValue] | List[PwChannelValue] | List[TmChannelValue],
    ) -> None:
        # logger.info(f"{chan_name}: {len(values)} new values")
        if not chan_id in self.__channels:
            self.__channels[chan_id] = ChannelData(chan_id)
        self.__channels[chan_id].append_values(values)


class LogPacketsParser:
    def __init__(self, sys_config: SysConfig):
        """Constructor."""
        self.__sys_config = sys_config
        # Counts the number of values each each ignored channel.
        self.__ignored_channels: Dict[str, int] = {}

    def __report_ignored_channel(self, chan_id: str, num_values: int) -> None:
        old_value = self.__ignored_channels.get(chan_id, 0)
        self.__ignored_channels[chan_id] = old_value + num_values
        # logger.info(f"Channel [{chan_id} not in config, skipping {num_values} values (total {self.__skipped_channels[chan_id]})")

    def ignored_channels(self) -> Dict[str, int]:
        """Returns a list of ignored channel ids and their respective row counts."""
        return self.__ignored_channels

    def _parse_lc_ch_values(
        self,
        chan_id: str,
        packet_start_time_millis: int,
        packet_data: PacketData,
        output: ParsedLogPacket,
    ) -> None:
        """Parse an append a list of lc channel values. If chan is not in sys_config, parse and ignore values."""
        # Get channel config. If not in config, we read but ignore this channel.
        lc_ch_config: Optional[
            LoadCellChannelConfig
        ] = self.__sys_config.load_cell_config(chan_id)
        # Read header
        first_value_rel_time: int = packet_data.read_uint16()
        num_values = packet_data.read_uint16()
        assert num_values > 1, f"num_values: {num_values}"
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
            output.append_values(chan_id, values)
        else:
            self.__report_ignored_channel(chan_id, num_values)

    def _parse_pw_ch_values(
        self,
        chan_id: str,
        packet_start_time_millis: int,
        packet_data: PacketData,
        output: ParsedLogPacket,
    ) -> None:
        """Parse an append a list of power channel values. If chan is not in sys_config, parse and ignore values."""
        # Get channel config. If not in config, we read but ignore this channel.
        pw_ch_config: Optional[PowerChannelConfig] = self.__sys_config.power_config(
            chan_id
        )
        # Read header
        first_value_rel_time: int = packet_data.read_uint16()
        num_values = packet_data.read_uint16()
        assert num_values > 1, f"num_values: {num_values}"
        step_interval_millis = packet_data.read_uint16()
        assert not packet_data.read_error()
        # Read values
        values = []
        item_time_millis: int = packet_start_time_millis + first_value_rel_time
        for i in range(num_values):
            adc_voltage_reading: int = packet_data.read_int16()
            adc_current_reading: int = packet_data.read_int16()
            if pw_ch_config:
                value_volts = pw_ch_config.adc_voltage_reading_to_volts(
                    adc_voltage_reading
                )
                value_amps = pw_ch_config.adc_current_reading_to_amps(
                    adc_current_reading
                )
                values.append(
                    PwChannelValue(
                        item_time_millis,
                        adc_voltage_reading,
                        adc_current_reading,
                        value_volts,
                        value_amps,
                    )
                )
            item_time_millis += step_interval_millis
        assert not packet_data.read_error()
        # If channel is not ignored, add its values.
        if values:
            # logger.info(f"Adding {len(values)} values of chan {chan_name}")
            output.append_values(chan_id, values)
        else:
            self.__report_ignored_channel(chan_id, num_values)

    def _parse_tm_ch_values(
        self,
        chan_id: str,
        packet_start_time_millis: int,
        packet_data: PacketData,
        output: ParsedLogPacket,
    ) -> None:
        """Parse an append a list of temperature channel values. If chan is not in sys_config, parse and ignore values."""
        # Get channel config. If not in config, we read but ignore this channel.
        tm_ch_config: Optional[
            TemperatureChannelConfig
        ] = self.__sys_config.temperature_config(chan_id)
        # Read header
        first_value_rel_time: int = packet_data.read_uint16()
        num_values = packet_data.read_uint16()
        assert num_values > 1, f"num_values: {num_values}"
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
                values.append(
                    TmChannelValue(item_time_millis, adc_reading, r_ohms, t_celsius)
                )
            item_time_millis += step_interval_millis
        assert not packet_data.read_error()
        # If channel is not ignored, add its values.
        if tm_ch_config:
            output.append_values(chan_id, values)
        else:
            self.__report_ignored_channel(chan_id, num_values)

    def _parse_external_reports_values(
        self,
        packet_start_time_millis: int,
        packet_data: PacketData,
        output: ParsedLogPacket,
    ) -> None:
        """Parse external reports."""
        first_value_rel_time: int = packet_data.read_uint16()
        num_values = packet_data.read_uint16()
        # As of dec 2023, each external report is sent independently.
        assert num_values == 1, f"num_values: {num_values}"
        assert not packet_data.read_error()
        item_time_millis: int = packet_start_time_millis + first_value_rel_time
        report_str: str = packet_data.read_str()

        tokens = report_str.split(":")
        n = len(tokens)
        assert n >= 1, f"report_str: '{report_str}'"
        chan_id = tokens[0]

        # Special handling for time markers. This is a hard coded channel with special semantic.
        if chan_id == "mrk":
            marker_type = tokens[1]
            marker_value = tokens[2] if n > 2 else ""
            marker_chan_value = TimeMarkChannelValue(
                item_time_millis,
                report_str,
                marker_type,
                marker_value,
            )
            output.append_values(chan_id, [marker_chan_value])
            return

        # Handle a general case of an external report.
        assert n == 2, f"report_str: '{report_str}'"
        ext_chan_value = ExternalReportChannelValue(
            item_time_millis, report_str, tokens[1], float(tokens[1])
        )
        output.append_values(chan_id, [ext_chan_value])

    def parse_next_packet(self, packet_data: PacketData) -> ParsedLogPacket:
        packet_data.reset_read_location()
        version = packet_data.read_uint8()
        assert version == 1, f"Unexpected log packet version: {version}"
        session_id = packet_data.read_uint32()
        packet_start_time_millis = packet_data.read_uint32()
        result: ParsedLogPacket = ParsedLogPacket(session_id, packet_start_time_millis)
        chan_id_regex = re.compile("[a-z0-9]{3}")
        while not packet_data.read_error() and not packet_data.all_read():
            # Get channel id
            chan_id = packet_data.read_str()
            assert not packet_data.read_error()
            assert chan_id_regex.fullmatch(chan_id), f"chan_id: [{chan_id}]"

            # Parse a load cell channel.
            if chan_id.startswith("lc"):
                self._parse_lc_ch_values(
                    chan_id, packet_start_time_millis, packet_data, result
                )
                continue

            # Parse a power channel
            if chan_id.startswith("pw"):
                self._parse_pw_ch_values(
                    chan_id, packet_start_time_millis, packet_data, result
                )
                continue

            # Temperature channels.
            if chan_id.startswith("tm"):
                self._parse_tm_ch_values(
                    chan_id, packet_start_time_millis, packet_data, result
                )
                continue

            # Markers.
            if chan_id == "ext":
                self._parse_external_reports_values(
                    packet_start_time_millis, packet_data, result
                )
                continue

            # Unknown channel id
            raise ValueError(f"Unexpected log chan id: [{chan_id}]")
        assert packet_data.all_read_ok()
        return result
