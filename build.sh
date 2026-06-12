#!/usr/bin/env zsh
#
# build.sh — convenience wrapper around PlatformIO for Vertical Clock.
#
# Usage:
#   ./build.sh [clean] [build] [install] [monitor]
#
# Commands (run in the order given; defaults to "build" if none supplied):
#   clean     Remove build artifacts (pio run --target clean)
#   build     Compile the firmware (pio run)
#   install   Compile and flash to the connected board (pio run --target upload)
#   monitor   Open the serial monitor (pio device monitor)
#
# Examples:
#   ./build.sh                   # build
#   ./build.sh clean build       # clean then build
#   ./build.sh install monitor   # build + flash, then watch serial output
#
# Set PIO_ENV to target a specific environment (default: esp32-c3).

set -e
set -u
set -o pipefail

SCRIPT_NAME="${0:t}"

# Run from the project root (the directory this script lives in).
cd "${0:A:h}"

PIO_ENV="${PIO_ENV:-esp32-c3}"

# Locate the PlatformIO CLI.
if command -v pio >/dev/null 2>&1; then
    PIO=pio
elif command -v platformio >/dev/null 2>&1; then
    PIO=platformio
elif [[ -x "$HOME/.platformio/penv/bin/pio" ]]; then
    PIO="$HOME/.platformio/penv/bin/pio"
else
    print -u2 "error: PlatformIO CLI not found (looked for 'pio' and 'platformio')."
    print -u2 "       Install it from https://platformio.org/install/cli"
    exit 1
fi

usage() {
    print "Usage: $SCRIPT_NAME [clean] [build] [install] [monitor]"
    print "  clean    remove build artifacts"
    print "  build    compile the firmware (default)"
    print "  install  compile and flash to the board"
    print "  monitor  open the serial monitor"
    print "  PIO_ENV=<env> to override the target (current: $PIO_ENV)"
}

do_clean()   { print "==> clean ($PIO_ENV)";   "$PIO" run --environment "$PIO_ENV" --target clean; }
do_build()   { print "==> build ($PIO_ENV)";   "$PIO" run --environment "$PIO_ENV"; }
do_install() { print "==> install ($PIO_ENV)"; "$PIO" run --environment "$PIO_ENV" --target upload; }
do_monitor() { print "==> monitor ($PIO_ENV)"; "$PIO" device monitor --environment "$PIO_ENV"; }

# No arguments → build.
if (( $# == 0 )); then
    set -- build
fi

# Validate everything before running anything.
for cmd in "$@"; do
    case "$cmd" in
        clean|build|install|monitor) ;;
        -h|--help|help) usage; exit 0 ;;
        *) print -u2 "error: unknown command '$cmd'"; usage; exit 2 ;;
    esac
done

for cmd in "$@"; do
    case "$cmd" in
        clean)   do_clean ;;
        build)   do_build ;;
        install) do_install ;;
        monitor) do_monitor ;;
    esac
done

print "==> done"
