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
                    default="./log.txt",
                    help="Input file with device's log.")
parser.add_argument("--output_file",
                    dest="output_file",
                    default="./log.bin",
                    help="Output file with binary log.")

args = parser.parse_args()


def main():
    # Read lines
    logger.info(f"Input file:  {args.input_file}")
    logger.info(f"Output file:  {args.output_file}")
    assert args.input_file is not None
    in_f = open(args.input_file, 'r')
    lines = in_f.readlines()
    in_f.close()
    
    
    # Process lines
    out_f = open(args.output_file, "wb")
    for line in lines:
      if not line.startswith("# "):
        continue
      print(line)
      line = line[1:]
      line = line.strip()
      tokens = line.split()
      i = 0
      for token in tokens:
        ba = bytearray.fromhex(token)
        assert len(ba) == 1
        b = ba[0]
        print(f"{i:03d} {b:3d} : {b:02x}", flush=True)
        out_f.write(ba)
        i+= 1
    out_f.close()
    logger.info(f"Closed output file {args.output_file}")
    logger.info(f"All done.")

if __name__ == "__main__":
    main()
