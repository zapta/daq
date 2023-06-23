from typing import Optional, List, Tuple, Literal
import sys

# For using the local version of serial_packet. Comment out if
# using serial_packets package installed by pip.
# sys.path.insert(0, "../../../../serial_packets_py/repo/src")

from serial_packets.packets import PacketData

# from pandas import Interval


class BaseGroup:

    def time_interval_millis(self) -> tuple[int, int]:
        raise NotImplementedError(
            f"Subclass {type(self).__name__}  should implement time_interval_millis().")


class LoadCellGroup(BaseGroup):

    def __init__(self, chan: int, start_time: int, __step_time_millis: int, values: List[int]):
        """Constructor."""
        self.__chan = chan
        self.__start_time_millis: int = start_time
        self.__step_time_millis: int = __step_time_millis
        self.__values: List[int] = values

    def __str__(self):
        return f"Group(load_cell[{self.__chan}]: {self.time_interval_millis()}/{self.__step_time_millis}, {self.size()} points"

    def chan(self):
      return self.__chan
    
    def size(self):
        return len(self.__values)

    def time_interval_millis(self) -> Optional[tuple[int, int]]:
        if not self.__values:
            return None
        return (self.__start_time_millis,
                self.__start_time_millis + (self.size() * self.__step_time_millis))

    def __iter__(self):
        return LoadCellGroup.__Iter(self.__start_time_millis, self.__step_time_millis,
                                    self.__values)

    class __Iter:
        """Iterator class for load cell group (time_millis:int, value:int)"""

        def __init__(self, start_time_millis, step_time_millis, values):
            self.__start_time_millis = start_time_millis
            self.__step_time_millis = step_time_millis
            self.__values = values
            self.__size = len(values)
            self.__next_index: int = 0

        def __next__(self):
            if self.__next_index >= self.__size:
                raise StopIteration
            value = self.__values[self.__next_index]
            time = self.__start_time_millis + (self.__next_index * self.__step_time_millis)
            self.__next_index += 1
            return (time, value)


class ParsedLogPacket:

    def __init__(self, groups: List[BaseGroup]):
        """Constructor."""
        self.__groups = groups

    def groups(self) -> List[BaseGroup]:
        return self.__groups

    def size(self):
        return len(self.__groups)

    def is_empty(self):
        return not self.__groups

    def time_interval_millis(self) -> Optional[Tuple[int, int]]:
        if len(self.__groups) is None:
            return None
        start, end = self.__groups[0].time_interval_millis()
        for group in self.__groups:
            next_start, next_end = group.time_interval_millis()
            start = min(start, next_start)
            end = max(end, next_end)
        return (start, end)


class LogPacketsParser:

    def __init__(self):
        """Constructor."""
        pass

    def _parse_load_cell_group(self, packet_start_time_millis: int, chan: int,
                               data: PacketData) -> LoadCellGroup:
        rel_start_time = data.read_uint8()
        step_interval = data.read_uint8()
        num_values = data.read_uint8()
        assert not data.read_error()
        values = []
        for i in range(num_values):
            values.append(data.read_int24())
        assert not data.read_error()
        result = LoadCellGroup(chan, packet_start_time_millis + rel_start_time, step_interval,
                               values)
        return result

    def parse_next_packet(self, data: PacketData) -> ParsedLogPacket:
        data.reset_read_location()
        version = data.read_uint8()
        assert version == 1, f"Unexpected log packet version: {version}"
        packet_start_sys_time_millis = data.read_uint32()
        groups = []
        while not data.read_error() and not data.all_read():
            group_id = data.read_uint8()
            assert not data.read_error()
            if group_id >= 4 and group_id <= 7:
                chan = group_id % 4
                group = self._parse_load_cell_group(packet_start_sys_time_millis, chan, data)
                groups.append(group)
            else:
                raise ValueError("Unexpected log group id: {group_id}")
        assert data.all_read_ok()
        result = ParsedLogPacket(groups)
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
