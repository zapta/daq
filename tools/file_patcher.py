#!python3

import shutil
# import re


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
    print(f"{count} patches made")
    if count != expected_count:
        raise AssertionError(
            f"Excepted {expected_count} changes but had {count} changes")
    return patched_lines


def patch_file(file, expected_count, line_text, repl_lines):
    with open(file, "r") as infile:
        in_lines = infile.readlines()
    in_lines = [l.rstrip('\n\r') for l in in_lines]
    patched_lines = patch_lines(
        in_lines, expected_count, line_text, repl_lines)
    patched_text = "\n".join(patched_lines)
    with open(file, 'w') as outfile:
        outfile.write(patched_text)
        outfile.write("\n")



def patch_main_c(file):
    patch_file(file, 1, "int main(void)", [
        "// ### Auto patched.",
        "// ### main() moved to application code.",
        "// ### Was: int main(void)",
        "int _ignored_main(void)"
    ])

    patch_file(file, 1, "void Error_Handler(void)", [
        "// ### Auto patched.",
        "// ### Error_Handler() moved to application code.",
        "// ### Was: void Error_Handler(void)",
        "void _ignored_Error_Handler(void)"
    ])

FILE = "../controller/platformio/lib/cube_ide/Core/Src/main.c.ignored"
patch_main_c(FILE);

