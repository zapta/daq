#!python

# Run the update ../platformio with regenerated cube_ide files.

import shutil
import os


def copy_and_overwrite_file(from_path, to_path):
    print(f"Copying file {from_path} -> {to_path}")
    shutil.copyfile(from_path, to_path)


def copy_and_overwrite_dir(from_path, to_path):
    print(f"Copying directory {from_path} -> {to_path}")
    if os.path.exists(to_path):
        shutil.rmtree(to_path)
    shutil.copytree(from_path, to_path)


def patch_lines(lines, expected_count, line_text, repl_lines):
    count = 0
    patched_lines = []
    for l in lines:
        # print(f".*Line: {l}")
        if line_text == l:
            # print("MATCH***")
            patched_lines.extend(repl_lines)
            count += 1
        else:
            patched_lines.append(l)
    if count != expected_count:
        raise AssertionError(
            f"Excepted {expected_count} changes but had {count} changes")
    return patched_lines, count


def patch_file(file, expected_count, line_text, repl_lines):
    with open(file, "r") as infile:
        in_lines = infile.readlines()
    in_lines = [l.rstrip('\n\r') for l in in_lines]
    patched_lines, count = patch_lines(in_lines, expected_count, line_text,
                                       repl_lines)
    patched_text = "\n".join(patched_lines)
    with open(file, 'w') as outfile:
        outfile.write(patched_text)
        outfile.write("\n")
    return count


def patch_newlib_lock_glue(file):
    print(f"Patching newlib_lock_glue.c at {file}.")
    count = patch_file(
        file, 1,
        "#warning This makes malloc, env, and TZ calls thread-safe, not the entire newlib",
        [
            "// ### Auto patched.",
            "// ### #warning This makes malloc, env, and TZ calls thread-safe, not the entire newlib",
        ])
    print(f"{count} patches made to {file}")


def patch_main_c(file):
    print(f"Patching main.c at {file}.")
    count = patch_file(file, 1, "int main(void)", [
        "// ### Auto patched.", "// ### main() moved to application code.",
        "// ### Was: int main(void)", "int _ignored_main(void)"
    ])

    count += patch_file(file, 1, "void Error_Handler(void)", [
        "// ### Auto patched.",
        "// ### Error_Handler() moved to application code.",
        "// ### Was: void Error_Handler(void)",
        "void _ignored_Error_Handler(void)"
    ])
    print(f"{count} patches made to {file}")


def patch_fatfs_platform_c(file):
    print(f"Patching fatfs_platform.c at {file}.")
    count = patch_file(
        file, 1,
        "    if(HAL_GPIO_ReadPin(SD_DETECT_GPIO_PORT, SD_DETECT_PIN) != GPIO_PIN_RESET)",
        [
            "    // ### Auto patched.", "    // ### Changed '!=' to '=='.",
            "    if(HAL_GPIO_ReadPin(SD_DETECT_GPIO_PORT, SD_DETECT_PIN) == GPIO_PIN_RESET)"
        ])
    print(f"{count} patches made to {file}")


def patch_sd_diskio_c(file):
    print(f"Patching sd_diskio.c at {file}.")
    count = patch_file(file, 1, "  if(BSP_SD_Init() == MSD_OK)", [
        "  // ### Auto patched.",
        "  // ### Added Stat variable initialization.",
        "  Stat = STA_NOINIT | STA_NODISK;", "",
        "  if(BSP_SD_Init() == MSD_OK)"
    ])
    print(f"{count} patches made to {file}")


def patch_ff_c(file):
    print(f"Patching ff.c at {file}.")
    count = patch_file(
        file, 1,
        "static FATFS *FatFs[_VOLUMES];	/* Pointer to the file system objects (logical drives) */",
        [
            "",
            "// ### Auto patched.",
            "// ### Originally was static.",
            "FATFS *FatFs[_VOLUMES];	/* Pointer to the file system objects (logical drives) */",
            "",
        ])
    print(f"{count} patches made to {file}")


# FILE = "../controller/platformio/lib/cube_ide/Core/Src/main.c.ignored"
# patch_main_c(FILE)

DST = "../platformio"
DST_LIB = f"{DST}/lib/cube_ide"

# In case root lib dir doesn't exist.
print(f"\nCreating {DST_LIB} if necessary.")
os.makedirs(DST_LIB, exist_ok=True)

# Lib directories
copy_and_overwrite_dir("./Core", f"{DST_LIB}/Core")
copy_and_overwrite_dir("./Drivers", f"{DST_LIB}/Drivers")
copy_and_overwrite_dir("./Middlewares", f"{DST_LIB}/Middlewares")
copy_and_overwrite_dir("./USB_DEVICE", f"{DST_LIB}/USB_DEVICE")
copy_and_overwrite_dir("./FATFS", f"{DST_LIB}/FATFS")

# Copy individual  files.
copy_and_overwrite_file("STM32H750VBTX_FLASH.ld",
                        f"{DST}/STM32H750VBTX_FLASH.ld")
copy_and_overwrite_file("cube_ide.pdf", "../docs/cube_ide_report.pdf")

# Patch files.
patch_newlib_lock_glue(f"{DST_LIB}/Core/ThreadSafe/newlib_lock_glue.c")
patch_main_c(f"{DST_LIB}/Core/Src/main.c")
patch_fatfs_platform_c(f"{DST_LIB}/FATFS/Target/fatfs_platform.c")
patch_sd_diskio_c(f"{DST_LIB}/FATFS/Target/sd_diskio.c")
patch_ff_c(f"{DST_LIB}/Middlewares/Third_Party/FatFs/src/ff.c")

print("\nAll done ok.\n")
