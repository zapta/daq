from typing import Optional, List, Tuple, Literal, Any, Dict
import sys
from collections import OrderedDict

# For using the local version of serial_packet. Comment out if
# using serial_packets package installed by pip.
# sys.path.insert(0, "../../../../serial_packets_py/repo/src")

from serial_packets.packets import PacketData


class ChannelData:

    def __init__(self, chan_name: str):
        self.__chan_name = chan_name
        # List of (time_millis, value)
        self.__timed_values: List[Tuple[int, Any]] = []

    def __str__(self) -> str:
        return f"Chan {self.__chan_name}: {self.size()} values"

    def chan_name(self) -> str:
        return self.__chan_name

    def timed_values(self) -> Tuple[int, Any]:
        return self.__timed_values

    # def num_sequences(self) -> int:
    #   return len(self.__sequences)

    def size(self) -> int:
        return len(self.__timed_values)

    def is_empty(self) -> int:
        return len(self.__timed_values) < 1

    def start_time_millis(self) -> Optional[int]:
        if self.is_empty():
            return None
        return self.__timed_values[0][0]

    def end_time_millis(self) -> Optional[int]:
        if self.is_empty():
            return None
        return self.__timed_values[-1][0]

    def append_timed_values(self, time_values: Tuple[int, any]) -> None:
        assert len(time_values) > 0
        if not self.is_empty():
            assert time_values[-1][0] >= self.end_time_millis()
        self.__timed_values.extend(time_values)


class ParsedLogPacket:

    def __init__(self, packet_base_time_millis):
        """Constructor."""
        self.__packet_base_time_millis: int = packet_base_time_millis
        self.__channels: Dict[str, ChannelData] = OrderedDict()

    def base_time_millis(self) -> int:
        return self.__packet_base_time_millis

    # def channels(self) -> Dict[str, ChannelData]:
    #     return self.__channels
    def channels(self) -> Dict[str, ChannelData]:
        return self.__channels.values()

    def channel(self, chan_name: str) -> Optional[ChannelData]:
        return self.__channels.get(chan_name, None)

    def num_channels(self):
        return len(self.__channels)

    def start_time_millis(self) -> Optional[int]:
        result = None
        for chan in self.__channels.values():
            chan_start = chan.start_time_millis()
            if (result is None) or result > chan_start:
                result = chan_start
        return result

    def end_time_millis(self) -> Optional[int]:
        result = None
        for chan in self.__channels.values():
            chan_end = chan.end_time_millis()
            if (result is None) or result < chan_end:
                result = chan_end
        return result

    def is_empty(self):
        return not self.__channels

    def append_timed_values(self, chan_name: str, timed_values: List[Tuple[int, Any]]) -> None:
        if not chan_name in self.__channels:
            self.__channels[chan_name] = ChannelData(chan_name)
        self.__channels[chan_name].append_timed_values(timed_values)


class LogPacketsParser:

    def __init__(self):
        """Constructor."""
        pass

    def _parse_int24_sequence(self, packet_start_time_millis: int,
                              data: PacketData) -> Tuple[int, Any]:
        """Returns a list of time values (time_millis, value)"""
        first_value_rel_time = data.read_uint16()
        num_values = data.read_uint16()
        step_interval_millis = data.read_uint16()
        # print(f"*** {packet_start_time_millis}, {first_value_rel_time}, {num_values}, {step_interval_millis}", flush=True)
        assert not data.read_error()
        timed_values = []
        t_millis = packet_start_time_millis + first_value_rel_time
        for i in range(num_values):
            timed_values.append((t_millis, data.read_int24()))
            t_millis += step_interval_millis
        assert not data.read_error()
        return timed_values

    def parse_next_packet(self, data: PacketData) -> ParsedLogPacket:
        data.reset_read_location()
        version = data.read_uint8()
        assert version == 1, f"Unexpected log packet version: {version}"
        packet_start_sys_time_millis = data.read_uint32()
        # print(f"### {packet_start_sys_time_millis:07d}", flush=True)
        result: ParsedLogPacket = ParsedLogPacket(packet_start_sys_time_millis)
        while not data.read_error() and not data.all_read():
            chan_id = data.read_uint8()
            assert not data.read_error()
            if chan_id >= 0x11 and chan_id <= 0x14:
                chan_name = f"LC{chan_id - 0x11 + 1}"
                timed_values = self._parse_int24_sequence(packet_start_sys_time_millis, data)
                # print(f"+++1 {chan_name}, {type(timed_values)}")
                result.append_timed_values(chan_name, timed_values)
            elif chan_id >= 0x21 and chan_id <= 0x26:
                chan_name = f"THRM{chan_id - 0x21 + 1}"
                timed_values = self._parse_int24_sequence(packet_start_sys_time_millis, data)
                result.append_timed_values(chan_name, timed_values)
            else:
                raise ValueError("Unexpected log group id: {group_id}")
        assert data.all_read_ok()
        return result