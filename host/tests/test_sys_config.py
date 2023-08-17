# Unit tests of sys_config.py
# Requires:
#   pip install pytest

import unittest
import sys

# See PT100 table. Multiply R by x10 for PT1000.
# at https://web.mst.edu/~cottrell/ME240/Resources/Temperature/RTD%20table.pdf


# Assuming VSCode project opened at repo/host
sys.path.insert(0, "..")
# sys.path.insert(0, "../src/lib")

from src.lib.sys_config import SysConfig


# from sys_config import SysConfig


class TestSysConfig(unittest.TestCase):

    def setUp(self):
        self.sys_config = SysConfig()
        self.sys_config.load_from_file("./test_sys_config.toml")
        assert self.sys_config

    def test_rtd_r2t_conversion(self):
        rtd_config = self.sys_config.temperature_config("TMP1")
        assert rtd_config
        self.assertAlmostEqual(0.0, rtd_config.resistance_to_c(1000.0), places=4)
        self.assertAlmostEqual(27.3041, rtd_config.resistance_to_c(1106.24), places=4)
        self.assertAlmostEqual(27.3041, rtd_config.resistance_to_c(1106.24), places=4)
        self.assertAlmostEqual(200.0, rtd_config.resistance_to_c(1758.6), places=4)


if __name__ == '__main__':
    unittest.main()
