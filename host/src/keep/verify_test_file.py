#!python

# A python program that communicates with the device over a serial link
# and Serial Packets protocol.

from __future__ import annotations

import argparse
import logging

logging.basicConfig(
    level=logging.INFO,
    format="%(relativeCreated)07d %(levelname)-7s %(filename)-10s: %(message)s",
)
logger = logging.getLogger("main")

parser = argparse.ArgumentParser()
parser.add_argument("--input_file",
                    dest="input_file",
                    default="./TEST.bin",
                    help="Input file to test.")
# parser.add_argument("--output_dir",
#                     dest="output_dir",
#                     default=None,
#                     help="Output directory for generated files.")

args = parser.parse_args()


def main():

    logger.info(f"Input file:  {args.input_file}")
    assert args.input_file is not None
    in_f = open(args.input_file, "rb")
    bytes_verified = 0
    while (bfr := in_f.read(1000)):
        n = len(bfr)
        for i in range(0, n, 4):
            w = int.from_bytes(bfr[i:i + 4], byteorder='big', signed=False)
            assert w == bytes_verified
            bytes_verified += 4
    in_f.close()
    logger.info(f"Verified {bytes_verified} bytes")
    logger.info(f"All done.")


if __name__ == "__main__":
    main()
