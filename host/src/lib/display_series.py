from typing import List
import logging
import numpy as np

logger = logging.getLogger("display_series")


class DisplaySeries:
    """A class that tracks last N <time, value>"""
    def __init__(self, max_size: int):
        self.__max_size = max_size
        self.__times: List[float] = []
        self.__values: List[float] = []
        assert max_size > 0

    def size(self) -> int:
        return len(self.__times)
      
    def clear(self) -> None:
      self.__times.clear()
      self.__values.clear()
      
    def mean_value(self) -> float:
      return np.mean(self.__values)

    def extend(self, new_times: List[float], new_values: List[float]) -> None:
        assert len(new_times) > 0
        assert len(new_times) == len(new_values)
        # In case of going back in time due to device reset, clear
        # old data.
        if self.__times and self.__times[-1] > new_times[-1]:
          logger.warning("Time regression, clearing.")
          self.__times.clear()
          self.__values.clear()
        n1 = len(new_times)
        # Case 1: New series is longer than max
        if n1 >= self.__max_size:
            self.__times = new_times[-self.__max_size:]
            self.__values = new_values[-self.__max_size:]
            return
        # Case 2: Existing + new series is longer than max
        n0 = len(self.__times)
        if (n0 + n1) > self.__max_size:
            keep = self.__max_size - n1
            self.__times = self.__times[-keep:] + new_times
            self.__values = self.__values[-keep:] + new_values
            return
        # Case 3: Keep all
        self.__times = self.__times + new_times
        self.__values = self.__values + new_values

    def times(self) -> List[float]:
        """Caller is expected not to mutate the list."""
        return self.__times
      
    def retro_times(self) ->List[float]:
      tlast = self.__times[-1]
      return  [(v - tlast) for v in self.__times]

    def values(self) -> List[float]:
        """Caller is expected not to mutate the list."""
        return self.__values
