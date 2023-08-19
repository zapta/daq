# Per https://docs.platformio.org/en/latest/scripting/examples/extra_linker_flags.html

import time

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

# Per https://community.platformio.org/t/specifying-test-port-breaks-the-test/35344
# The print messages are available only if running pio with -vv
build_type = env.GetBuildType()
print(f"** (extra_script.py): build type = [{build_type}]")
if "test" in build_type:
    print(f"** (extra_script.py): adding a post upload delay.")
    env.AddPostAction("upload", lambda *_, **__: time.sleep(2))
else:
    print(f"** (extra_script.py): Not adding a post upload delay.")
    
