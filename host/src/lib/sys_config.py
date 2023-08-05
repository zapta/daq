from typing import Any, Dict
import tomllib
import logging
import math

logger = logging.getLogger("sys_config")


class LoadCellChannelConfig:
    """Configuration of a load cell channel."""

    def __init__(self, chan_name: str, color: str, offset: int, scale: float):
        self.__chan_name = chan_name
        self.__color = color
        self.__offset = offset
        self.__scale = scale

    def __str__(self):
        return f"Load cell {self.__chan_name} ({self.__color}): offset={self.__offset}, scale={self.__scale}"

    def adc_reading_to_grams(self, adc_reading: int) -> float:
        return (adc_reading - self.__offset) * self.__scale

    def dump_lc_calibration(self, adc_reading: int) -> None:
        grams = self.adc_reading_to_grams(adc_reading)
        logger.info(f"{self.__chan_name:5s} adc {adc_reading:7.0f} -> {grams:7.1f} grams")

    def color(self) -> str:
        return self.__color
      
class TemperatureChannelConfig:
    """Configuration of a temperature channel. Should be subclassed for each sensor type.."""

    def __init__(self, chan_name: str, color: str, adc_open: int, adc_short: int, adc_calib: int,
                 adc_calib_r: float):
        # Config parameters from sys config.
        self.__chan_name = chan_name
        self.__color = color
        self.__adc_open = adc_open
        self.__adc_short = adc_short

        # Affective series resistor. As of July 2023, around 2k ohms.
        self.__r_series = (adc_calib_r * (adc_open - adc_calib)) / (adc_calib - adc_short)

        # self.__r_25c = r_25c
        # self.__beta = beta
        # coef_A/B/C are the coefficients of the Steinhart-Hart equation.
        # https://en.wikipedia.org/wiki/Steinhart%E2%80%93Hart_equation
        # self.__coef_C = therm_c
        # self.__coef_B = 1.0 / therm_beta
        # ln_Rref = math.log(therm_ref_r)
        # ln_Rref_cube = ln_Rref * ln_Rref * ln_Rref
        # # We compute A by solving for T = therm_ref_c.
        # self.__coef_A = 1.0 / (therm_ref_c +
        #                        273.15) - self.__coef_B * ln_Rref - self.__coef_C * ln_Rref_cube

    # def __str__(self):
    #     return f"{self.__chan_name} ({self.__color}): range=[{self.__adc_short}, {self.__adc_open}], r_ser={self.__r_series}, ref_c={self.__therm_ref_c}, ref_r={self.__therm_ref_r}, beta={self.__beta}, c={self.__c}"

    def color(self) -> str:
        return self.__color

    def adc_reading_to_ohms(self, adc_reading: int) -> float:
        ratio = (adc_reading - self.__adc_short) / (self.__adc_open - self.__adc_short)
        if ratio <= 0.0:
            resistance = 0
        elif ratio >= 0.99:
            resistance = 1e8 - 1
        else:
            resistance = (ratio * self.__r_series) / (1 - ratio)
        return resistance

    def resistance_to_c(self, r: float) -> float:
        raise NotImplemented("Should be implemented by subclass")

    def adc_reading_to_c(self, adc_reading: int) -> float:
        r = self.adc_reading_to_ohms(adc_reading)
        c = self.resistance_to_c(r)
        return c

    def dump_temperature_calibration(self, adc_reading: int) -> None:
        r = self.adc_reading_to_ohms(adc_reading)
        c = self.resistance_to_c(r)
        logger.info(f"{self.__chan_name:5s} adc {adc_reading:7.0f} -> {r:10.3f} ohms -> {c:6.2f} C")
  
class ThermistorChannelConfig(TemperatureChannelConfig):
    """Configuration of a temperature channel of type thermistor."""

    def __init__(self, chan_name: str, color: str, adc_open: int, adc_short: int, adc_calib: int,
                 adc_calib_r: float, thermistor_beta: int, thermistor_c: float, thermistor_ref_r: float,
                 thermistor_ref_c: int):
        super().__init__(chan_name, color, adc_open, adc_short, adc_calib,
                 adc_calib_r)
        # Config parameters from sys config.
        # self.__chan_name = chan_name
        # self.__color = color
        # self.__adc_open = adc_open
        # self.__adc_short = adc_short
        # self.__adc_calib = adc_calib
        # self.__adc_calib_r = adc_calib_r
        # self.__therm_beta = therm_beta
        # self.__therm_c = therm_c
        # self.__therm_ref_r = therm_ref_r
        # self.__therm_ref_c = therm_ref_c
        # Cached precomputed values.

        # Affective series resistor. As of July 2023, around 2k ohms.
        # self.__r_series = (adc_calib_r * (adc_open - adc_calib)) / (adc_calib - adc_short)

        # self.__r_25c = r_25c
        # self.__beta = beta
        # coef_A/B/C are the coefficients of the Steinhart-Hart equation.
        # https://en.wikipedia.org/wiki/Steinhart%E2%80%93Hart_equation
        self.__coef_C = thermistor_c
        self.__coef_B = 1.0 / thermistor_beta
        ln_Rref = math.log(thermistor_ref_r)
        ln_Rref_cube = ln_Rref * ln_Rref * ln_Rref
        # We compute A by solving for T = therm_ref_c.
        self.__coef_A = 1.0 / (thermistor_ref_c +
                               273.15) - self.__coef_B * ln_Rref - self.__coef_C * ln_Rref_cube

    def __str__(self):
        return f"Thermistor {self.__chan_name}"

    # def color(self) -> str:
    #     return self.__color

    # def adc_reading_to_ohms(self, adc_reading: int) -> float:
    #     ratio = (adc_reading - self.__adc_short) / (self.__adc_open - self.__adc_short)
    #     if ratio <= 0.0:
    #         resistance = 0
    #     elif ratio >= 0.99:
    #         resistance = 1e8 - 1
    #     else:
    #         resistance = (ratio * self.__r_series) / (1 - ratio)
    #     return resistance

    def resistance_to_c(self, r: float) -> float:
        """Thermistor specific resistance to temperature function."""
        if r >= 1e8 - 1:
            return -273.15
        if r <= 1.0:
          return 999
        ln_R = math.log(r)
        # The Steinhart-Hart equation.
        reciprocal_T = self.__coef_A + (self.__coef_B * ln_R) + (self.__coef_C * ln_R * ln_R * ln_R)
        T =  (1 / reciprocal_T) - 273.15
        if T > 9999:
          return 999
        return T

    # def adc_reading_to_c(self, adc_reading: int) -> float:
    #     r = self.adc_reading_to_ohms(adc_reading)
    #     c = self.resistance_to_c(r)
    #     # logger.info(f"{self.__chan_name} {adc_reading:.1f}, {r:.2f}, {c:.2f}")
    #     return c

    # def dump_therm_calibration(self, adc_reading: int) -> None:
    #     r = self.adc_reading_to_ohms(adc_reading)
    #     c = self.resistance_to_c(r)
    #     logger.info(f"{self.__chan_name:5s} adc {adc_reading:7.0f} -> {r:10.3f} ohms -> {c:6.2f} C")


class SysConfig:
    """System configuration from a TOML config file."""

    def __init__(self):
        """Constructor."""
        self.__toml_dict: Dict[str, Any] = None
        # self.__channels: Dict[str, Any] = None
        self.__lc_ch_configs: Dict[str, LoadCellChannelConfig] = None
        self.__tmp_ch_configs: Dict[str, TemperatureChannelConfig] = None

    def __parse_channels(self) -> None:
        """Assuming __toml_dict is populated. Populates __lc_ch_configs and __tmp_ch_configs."""
        self.__lc_ch_configs = {}
        self.__tmp_ch_configs = {}
        toml_channels = self.__toml_dict["channel"]
        for ch_name, ch_config in toml_channels.items():
            if ch_name.startswith("LDC"):
                assert ch_name not in self.__lc_ch_configs
                color = ch_config["color"]
                offset = ch_config["adc_offset"]
                scale = ch_config["scale"]
                self.__lc_ch_configs[ch_name] = LoadCellChannelConfig(ch_name, color, offset, scale)
            elif ch_name.startswith("TMP"):
                assert ch_name not in self.__tmp_ch_configs
                color = ch_config["color"]
                # Get ADC specific params.
                adc_config = ch_config["adc"]
                adc_open = adc_config["open"]
                adc_short = adc_config["short"]
                adc_calib = adc_config["calib"]
                adc_calib_r = adc_config["calib_r"]
                # For now we support only one kind of a temperature channel, thermistor.
                thermistor_config = ch_config["thermistor"]
                thermistor_beta = thermistor_config["beta"]
                thermistor_c = thermistor_config["c"]
                thermistor_ref_r = thermistor_config["ref_r"]
                thermistor_ref_c = thermistor_config["ref_c"]
                # Construct.
                self.__tmp_ch_configs[ch_name] = ThermistorChannelConfig(
                    ch_name, color, adc_open, adc_short, adc_calib, adc_calib_r, thermistor_beta,
                    thermistor_c, thermistor_ref_r, thermistor_ref_c)
            else:
                raise RuntimeError(f"Unexpected channel name in sys_config: {ch_name}")

    def __str__(self):
        return f"SysConfig: {self.__toml_dict}"

    def load_from_file(self, file_path: str) -> None:
        with open(file_path, "rb") as f:
            self.__toml_dict = tomllib.load(f)
        self.__parse_channels()

    def get_load_cell_config(self, chan_name: str) -> LoadCellChannelConfig:
        return self.__lc_ch_configs[chan_name]

    def get_temperature_config(self, chan_name: str) -> TemperatureChannelConfig:
        return self.__tmp_ch_configs[chan_name]

    def get_load_cells_configs(self) -> Dict[str, LoadCellChannelConfig]:
        return self.__lc_ch_configs

    def get_temperature_configs(self) -> Dict[str, TemperatureChannelConfig]:
        return self.__tmp_ch_configs

    def get_data_link_port(self) -> str:
        return self.__toml_dict["data_link"]["port"]
