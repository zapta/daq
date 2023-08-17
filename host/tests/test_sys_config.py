# Unit tests of sys_config.py
# Requires:
#   pip install pytest

# See PT100 table (Multiply R by x10 for PT1000):
# at https://web.mst.edu/~cottrell/ME240/Resources/Temperature/RTD%20table.pdf
#
# Semitec NT104 thermistor table:
# https://www.mouser.com/datasheet/2/362/P18_NT_Thermistor-1535133.pdf

import unittest
import sys

# Local imports
sys.path.insert(0, "..")
from lib.sys_config import SysConfig, RtdChannelConfig, ThermistorChannelConfig


class TestSysConfig(unittest.TestCase):

    def setUp(self):
        self.sys_config = SysConfig()
        self.sys_config.load_from_file("./test_data/sys_config.toml")
        assert self.sys_config

    def test_rtd_r2t_conversion(self):
        rtd_config = self.sys_config.temperature_config("TMP1")
        assert rtd_config
        self.assertIsInstance(rtd_config, RtdChannelConfig)
        self.assertAlmostEqual(0.0, rtd_config.resistance_to_c(1000.0), places=4)
        self.assertAlmostEqual(27.3041, rtd_config.resistance_to_c(1106.24), places=4)
        self.assertAlmostEqual(27.3041, rtd_config.resistance_to_c(1106.24), places=4)
        self.assertAlmostEqual(200.0, rtd_config.resistance_to_c(1758.6), places=4)

    def test_thermistor_r2t_conversion(self):
        thermistor_config = self.sys_config.temperature_config("TMP2")
        assert thermistor_config
        self.assertIsInstance(thermistor_config, ThermistorChannelConfig)
        self.assertAlmostEqual(25.0, thermistor_config.resistance_to_c(100000.0), places=4)
        self.assertAlmostEqual(199.3531, thermistor_config.resistance_to_c(445.2), places=4)


if __name__ == '__main__':
    unittest.main()
