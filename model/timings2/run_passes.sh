#!/bin/bash
# the three follow-up passes over the same case set
cd "$(dirname "$0")"
BRACKET=0BFA ./run.sh brk 14 && ./run.sh frzreg 28 && ./run.sh frzfpu 29
