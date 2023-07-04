from typing import Optional, List, Tuple, Literal, Any, Dict
import sys
from collections import OrderedDict


# For using the local version of serial_packet. Comment out if
# using serial_packets package installed by pip.

# sys.path.insert(0, "../../../../serial_packets_py/repo/src")

from serial_packets.packets import PacketData




# class ValueSequence:

#     def __init__(self, start_time: int, step_time_millis: Optional[int], values: List[Any]):
#         # print(f"*** len(values) = {len(values)}", flush=True)
#         assert len(values) >= 1
#         assert (step_time_millis is None) == (len(values) <= 1)
#         self.__start_time_millis: int = start_time
#         self.__step_time_millis: Optional[int] = step_time_millis
#         self.__values: List[int] = values

#     def __str__(self) -> str:
#         return f"{self.size()} values, {self.time_range_millis()}/{self.__step_time_millis}, {self.size()} points"

#     def chan_name(self) -> str:
#         return self.__chan_name

#     def size(self) -> int:
#         return len(self.__values)

#     def start_time_millis(self)-> int:
#         return self.__start_time_millis
      
#     def end_time_millis(self) -> int: 
#       if self.size() < 2:
#         return self.__start_time_millis
#       else:
#         return self.__start_time_millis + (self.size() * self.__step_time_millis)

#     def __iter__(self):
#         return ValueSequence.__Iter(self.__start_time_millis, self.__step_time_millis, self.__values)

#     class __Iter:
#         """Iterator class (time, value) of ValueSequence"""
#         def __init__(self, start_time_millis, step_time_millis, values):
#             self.__start_time_millis = start_time_millis
#             self.__step_time_millis = step_time_millis
#             self.__values = values
#             self.__size = len(values)
#             self.__next_index: int = 0

#         def __next__(self) -> Tuple[int, Any]:
#             if self.__next_index >= self.__size:
#                 raise StopIteration
#             value = self.__values[self.__next_index]
#             if self.__next_index == 0:
#               # Special case since __step_time_millis may be None in case this
#               # is the only value.
#               time_millis = self.__start_time_millis
#             else:
#               time_millis = self.__start_time_millis + (self.__next_index * self.__step_time_millis)
#             self.__next_index += 1
#             return (time_millis, value)

class ChannelData:

    def __init__(self, chan_name: str):
        self.__chan_name = chan_name
        # List of (time_millis, value)
        self.__timed_values: List[Tuple[int, Any]] = []

    def __str__(self)-> str:
        return f"Chan {self.__chan_name}: {self.size()} values"
      
    def chan_name(self) -> str:
      return self.__chan_name
      
    def timed_values(self)->Tuple[int, Any]:
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
      
    def append_timed_values(self, time_values: Tuple[int, any])-> None:
      assert len(time_values) > 0
      if not self.is_empty():
        # print(f"*** {type(time_values)}, {time_values}", flush=True)
        # print(f"*** {type(time_values[1])}, {time_values[-1]}", flush=True)
        # print(f"*** {type(time_values[1][0])}, {time_values[-1][0]}", flush=True)
        assert time_values[-1][0] >= self.end_time_millis()
      self.__timed_values.extend(time_values)
      
    # def __iter__(self):
    #     return ChannelData.__Iter(self.__sequences)

    # class __Iter:
    #     """Iterator class (time, value) of ChannelData"""
    #     def __init__(self, sequences):
    #         self.__sequence_iterator = iter(sequences)
    #         self.__value_iterator = None

    #     def __next__(self) -> Tuple[int, Any]:
    #       while True:
    #         if self.__value_iterator is None:
    #           # Advance to next sequence
    #           next_sequence = next(self.__sequence_iterator, None)
    #           if not next_sequence:
    #             raise StopIteration
    #           self.__value_iterator = iter(next_sequence)
    #         next_value = next(self.__value_iterator, None)
    #         if next_value:
    #           return next_value
    #         self.__value_iterator = None

class ParsedLogPacket:

    def __init__(self, packet_base_time_millis):
        """Constructor."""
        self.__packet_base_time_millis: int = packet_base_time_millis
        self.__channels: Dict[str, ChannelData] = OrderedDict()

    def base_time_millis(self)-> int:
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

    def is_empty(self):
        return not self.__channels
      
    def append_timed_values(self, chan_name: str, timed_values: List[Tuple[int, Any]]) -> None:
      # print(f"@@@ {type(timed_values)},  {chan_name}",flush=True)
      if not chan_name in self.__channels:
        self.__channels[chan_name] = ChannelData(chan_name)
      self.__channels[chan_name].append_timed_values(timed_values)
      # print(f"Appending: {chan_name}, {timed_values[0]}, {timed_values[1]}, {timed_values[2]}...", flush=True)

    # def time_span_millis(self) -> Optional[Tuple[int, int]]:
    #     """Time interval in millis of all data points from all channels."""
    #     if self.is_empty():
    #         return None
    #     start = None
    #     end = None
    #     for channelData in self.__channels.values():
    #         next_start, next_end = channelData.time_range_millis()
    #         if (start is None) or next_start < start:
    #             start = next_start
    #         if (end is None) or next_end > end:
    #             end = next_end
    #     return (start, end)


class LogPacketsParser:

    def __init__(self):
        """Constructor."""
        pass

    def _parse_int24_sequence(self, packet_start_time_millis: int,
                             data: PacketData) -> Tuple[int, Any]:
        """Returns a list of time values (time_millis, value)"""
        first_value_rel_time = data.read_uint16()
        # print(f"*** Rel start time: {first_value_rel_time}", flush=True)
        num_values = data.read_uint16()
        # print(f"*** Num values: {num_values}", flush=True)
        step_interval_millis = data.read_uint16()
        # print(f"*** Step interval: {step_interval_millis}", flush=True)

        assert not data.read_error()
        timed_values = []
        t_millis = packet_start_time_millis + first_value_rel_time
        for i in range(num_values):
            timed_values.append((t_millis, data.read_int24()))
            t_millis += step_interval_millis
        assert not data.read_error()
        return timed_values
      
    

    # def _parse_thermistor_group(self, packet_start_time_millis: int, therm_chan: int,
    #                             data: PacketData) -> LoadCellGroup:
    #     rel_start_time = data.read_uint8()
    #     step_interval = data.read_uint8()
    #     num_values = data.read_uint8()
    #     assert not data.read_error()
    #     values = []
    #     for i in range(num_values):
    #         values.append(data.read_int24())
    #     assert not data.read_error()
    #     result = LoadCellGroup(therm_chan, packet_start_time_millis + rel_start_time, step_interval,
    #                            values)
    #     return result

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


# def main():
#     packet_bytes = [
#         0x01, 0x00, 0x00, 0x0a, 0x4d, 0x04, 0x00, 0x02, 0x64, 0x00, 0x44, 0x1e, 0x00, 0x44, 0xb7,
#         0x00, 0x45, 0xf9, 0x00, 0x45, 0xf7, 0x00, 0x46, 0x68, 0x00, 0x44, 0x83, 0x00, 0x44, 0x4f,
#         0x00, 0x44, 0xb7, 0x00, 0x45, 0x54, 0x00, 0x45, 0x50, 0x00, 0x45, 0xea, 0x00, 0x44, 0xed,
#         0x00, 0x47, 0x1f, 0x00, 0x44, 0xfb, 0x00, 0x44, 0xef, 0x00, 0x45, 0x01, 0x00, 0x44, 0xed,
#         0x00, 0x43, 0xdd, 0x00, 0x45, 0x98, 0x00, 0x45, 0xbd, 0x00, 0x45, 0xa0, 0x00, 0x47, 0x45,
#         0x00, 0x45, 0x62, 0x00, 0x43, 0xd1, 0x00, 0x43, 0xb0, 0x00, 0x44, 0x1a, 0x00, 0x44, 0x16,
#         0x00, 0x45, 0x78, 0x00, 0x46, 0x14, 0x00, 0x47, 0x2d, 0x00, 0x45, 0x69, 0x00, 0x44, 0xd6,
#         0x00, 0x44, 0xa3, 0x00, 0x45, 0x50, 0x00, 0x44, 0xef, 0x00, 0x43, 0x17, 0x00, 0x44, 0xf3,
#         0x00, 0x46, 0x5e, 0x00, 0x46, 0x8e, 0x00, 0x43, 0x6d, 0x00, 0x45, 0x16, 0x00, 0x45, 0x6d,
#         0x00, 0x44, 0x61, 0x00, 0x44, 0x9e, 0x00, 0x44, 0xcf, 0x00, 0x46, 0xf1, 0x00, 0x46, 0x32,
#         0x00, 0x44, 0x2e, 0x00, 0x43, 0xae, 0x00, 0x43, 0x8b, 0x00, 0x46, 0x0b, 0x00, 0x44, 0xfe,
#         0x00, 0x45, 0x00, 0x00, 0x45, 0xd1, 0x00, 0x46, 0x75, 0x00, 0x45, 0xe4, 0x00, 0x44, 0xa7,
#         0x00, 0x43, 0x64, 0x00, 0x45, 0x6d, 0x00, 0x45, 0x39, 0x00, 0x45, 0x5b, 0x00, 0x44, 0x98,
#         0x00, 0x47, 0x82, 0x00, 0x47, 0x33, 0x00, 0x44, 0xc6, 0x00, 0x44, 0x56, 0x00, 0x45, 0xaf,
#         0x00, 0x44, 0x5b, 0x00, 0x44, 0xfc, 0x00, 0x44, 0x4c, 0x00, 0x46, 0x81, 0x00, 0x47, 0x10,
#         0x00, 0x45, 0x08, 0x00, 0x43, 0x41, 0x00, 0x44, 0x8c, 0x00, 0x44, 0x39, 0x00, 0x45, 0x4b,
#         0x00, 0x44, 0xf8, 0x00, 0x46, 0x03, 0x00, 0x44, 0xd7, 0x00, 0x45, 0xbf, 0x00, 0x45, 0x4c,
#         0x00, 0x44, 0xa2, 0x00, 0x44, 0x62, 0x00, 0x45, 0x0a, 0x00, 0x45, 0x7b, 0x00, 0x45, 0xa0,
#         0x00, 0x46, 0xad, 0x00, 0x46, 0x27, 0x00, 0x44, 0xc2, 0x00, 0x44, 0x0c, 0x00, 0x44, 0x94,
#         0x00, 0x44, 0x69, 0x00, 0x46, 0x18, 0x00, 0x45, 0x06, 0x00, 0x46, 0xd2, 0x00, 0x46, 0x02,
#         0x00, 0x44, 0x4b, 0x00, 0x44, 0x19, 0x00, 0x45, 0x24
#     ]

#     packet_data = PacketData()
#     packet_data.add_bytes(packet_bytes)
#     parser = LogPacketsParser()
#     parsed_packet = parser.parse_next_packet(packet_data)
#     for group in parsed_packet.groups():
#         print(f"  {group}")

#     # group1 = LoadCellGroup(2, 100, 2, [11, 22, 33, 44])
#     # group2 = LoadCellGroup(3, 120, 1, [88, 99])
#     # print(f"interval: {group1.time_interval_millis()}")
#     # print(f"size: {group1.size()}")
#     # for item in group1:
#     #     print(f"  {item}")
#     # decoded_packet = ParsedLogPacket([group1, group2])
#     # print(f"Packet time interval: {decoded_packet.time_interval_millis()}")
#     # for group in decoded_packet.groups():
#     #   print(f"  Group: {group}")
#     packet_data = PacketData()
#     packet_data.add_bytes(bytearray([0x11, 0x22, 0x33]))
#     parser = LogParser()
#     parser.parse_next_packet(packet_data)

# main()
