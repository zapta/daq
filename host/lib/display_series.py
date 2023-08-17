from typing import List
import logging
import numpy as np

logger = logging.getLogger("display_series")


class DisplaySeries:
    """A class that tracks <time, value> in last T seconds"""
    def __init__(self):
        self.__times: List[float] = []
        self.__values: List[float] = []

    def size(self) -> int:
        return len(self.__times)
      
    def clear(self) -> None:
      self.__times.clear()
      self.__values.clear()
      
    def mean_value(self) -> float:
      return np.mean(self.__values) if self.__values else None
    
    def delete_older_than(self, time_limit:float):
      """Delete items that are older than """
      items_to_delete = 0
      for time in self.__times:
        if time >= time_limit:
          break
        items_to_delete += 1
      if items_to_delete:
        self.__times = self.__times[items_to_delete:]
        self.__values = self.__values[items_to_delete:]

    def extend(self, new_times: List[float], new_values: List[float]) -> None:
        assert len(new_times) > 0
        assert len(new_times) == len(new_values)
        
        self.__times.extend(new_times)
        self.__values.extend(new_values)
        
      
    def relative_times(self, reference_time: float) ->List[float]:
      return  [(t - reference_time) for t in self.__times]

    def values(self) -> List[float]:
        """Caller is expected not to mutate the list."""
        return self.__values
