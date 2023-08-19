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

# References:
#   https://community.platformio.org/t/specifying-test-port-breaks-the-test/35344
#   https://docs.platformio.org/en/latest/scripting/actions.html
def after_upload(source, target, env):
    # NOTE: Run 'pio test -vv' to make the print() messages visible in testing.
    print(f"(extra_scripts.py): after_reload() called.")
    build_type = env.GetProjectOption("build_type")
    print(f"(extra_scripts.py): build_type = [{build_type}].")
    if build_type == "debug":
      print(f"(extra_scripts.py): Build type is debug, delaying...")
      time.sleep(1)
    else:
      print(f"(extra_scripts.py): Build type is not debug.")

    
env.AddPostAction("upload", after_upload)

