# Per https://docs.platformio.org/en/latest/scripting/examples/extra_linker_flags.html

Import("env")

#
# Dump build environment (for debug)
# print(env.Dump())
#

# To support hardware FP also by the linker, we have to 
# pass -mfloat-abi=hard here in addition to platformio.ini 
env.Append(
  LINKFLAGS=[
    "--specs=nano.specs",
    "-mfpu=fpv5-d16",
    "-mfloat-abi=hard",
    "-mthumb"
  ]
)

# Cube IDE link command
# arm-none-eabi-g++ -o "cube_ide.elf" @"objects.list"   
# -mcpu=cortex-m7 
# -T"C:\Users\user\projects\daq\repo\controller\cube_ide\STM32H750VBTX_FLASH.ld" 
# --specs=nosys.specs 
# -Wl,-Map="cube_ide.map" 
# -Wl,--gc-sections 
# -static 
# --specs=nano.specs 
# -mfpu=fpv5-d16 
# -mfloat-abi=hard 
# -mthumb -Wl,
# --start-group -lc -lm -lstdc++ -lsupc++ -Wl,--end-group
