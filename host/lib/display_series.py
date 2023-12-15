import logging
import numpy as np

from typing import List, Tuple

logger = logging.getLogger("display_series")


class DisplaySeries:
    """A class that tracks <time, value> in last T seconds"""

    def __init__(self, label: str, color: str):
        # Times are device timestamps time in secs.
        self.__label: str = label
        self.__times: List[float] = []
        self.__values: List[float] = []
        self.__color = color

    def size(self) -> int:
        return len(self.__times)

    def color(self) -> str:
        return self.__color

    def label(self) -> str:
        return self.__label

    def clear(self) -> None:
        self.__times.clear()
        self.__values.clear()

    def mean_value(self) -> float:
        return np.mean(self.__values) if self.__values else None

    def delete_older_than(self, time_limit: float):
        """Delete items that are older than"""
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
        assert len(new_times) == len(
            new_values
        ), f"{len(new_times)} vs {len(new_values)}"
        assert len(self.__times) == len(
            self.__values
        ), f"{len(self.__times)} vs {len(self.__values)}"
        assert len(self.__times) <= 1000000  # Detect leaks

        self.__times.extend(new_times)
        self.__values.extend(new_values)
        assert len(self.__times) == len(
            self.__values
        ), f"{self.__times} vs {self.__values}"

    def get_display_xy(self, reference_time: float) -> Tuple[List[float], List[float]]:
        assert len(self.__times) == len(
            self.__values
        ), f"{len(self.__times)} vs {len(self.__values)}"
        x = []
        for t in self.__times:
            t1 = t - reference_time
            if t1 > 0:
                # The data points from here on are newer than the buffering time so
                # we drop them to avoid flickering on the right end of the graphs.
                break
            x.append(t1)
        y = self.__values[: len(x)]
        assert len(x) == len(y), f"{len(x)} vs {len(y)}"
        return (x, y)
