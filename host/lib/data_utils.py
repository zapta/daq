# Common functionality for processing the data files.

from __future__ import annotations

import logging
import re
from typing import Tuple, Optional, List, Dict
import pandas as pd

from dataclasses import dataclass

logger = logging.getLogger("main")


@dataclass(frozen=True)
class TestInfo:
    """Contains the information of a single test as read from the tests file."""
    test_name: str
    start_ms: int
    end_ms: int


def load_tests_infos(tests_file_path: str, tests_selector: Optional[str]) -> List[TestInfo]:
    """Extract the tests information from the tests file."""
    logger.info(f"Loading tests infos from the tests file [{tests_file_path}]")
    logger.info(f"Tests selector is [{tests_selector}]")
    df = pd.read_csv(tests_file_path, delimiter=',')
    regex = re.compile(tests_selector) if tests_selector else re.compile(".*")
    result = []
    for i, row in df.iterrows():
        test_name = row["Test"]
        if not regex.match(test_name):
            logger.warning(f"Test [{test_name}] was filtered out per user request.")
            continue
        start_time_ms = row["Start[ms]"]
        end_time_ms = row["End[ms]"]
        result.append(TestInfo(test_name, start_time_ms, end_time_ms))
    return result


@dataclass(frozen=True)
class ChannelInfo:
    """Contains the information of a single channel as read from channels file."""
    channel_name: str
    channel_type: str
    field_name: str
    num_values: int
    file_name: str


def load_channels_infos(channels_file_path: str, channels_selector: Optional[str]) -> List[ChannelInfo]:
    """Extract the channels information from the channels file."""
    logger.info(f"Loading channels infos from the channels file [{channels_file_path}]")
    logger.info(f"Channels selector is [{channels_selector}]")
    df = pd.read_csv(channels_file_path, delimiter=',')
    regex = re.compile(channels_selector) if channels_selector else re.compile(".*")
    result = []
    for i, row in df.iterrows():
        channel_name = row["Name"]
        if not regex.match(channel_name):
            logger.warning(f"Channel [{channel_name}] was filtered out per user request.")
            continue
        channel_type = row["Type"]
        value_field = row["Field"]
        num_values = row["Values"]
        file_name = row["File"]
        result.append(ChannelInfo(channel_name, channel_type, value_field, num_values, file_name))
    return result
