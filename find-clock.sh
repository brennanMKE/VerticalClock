#!/usr/bin/env zsh
#
# find-clock.sh — locate the VerticalClock on your WiFi via mDNS (Bonjour).
#
# The device advertises an _http._tcp service whose name is its mDNS hostname,
# e.g. "verticalclock-a1b2c3". This browses for it, resolves the IPv4
# address, and prints a ready-to-open URL.
#
# Usage:
#   ./find-clock.sh                 # look for hosts starting "verticalclock"
#   ./find-clock.sh myprefix        # look for a different name prefix
#   OPEN=1 ./find-clock.sh          # also open the first match in your browser
#
# Tunables (env): BROWSE_SECS (default 5), RESOLVE_SECS (default 3)

set -u

PREFIX="${1:-verticalclock}"
SERVICE="_http._tcp"
BROWSE_SECS="${BROWSE_SECS:-5}"
RESOLVE_SECS="${RESOLVE_SECS:-3}"

# dns-sd ships with macOS; bail clearly if somehow missing.
if ! command -v dns-sd >/dev/null 2>&1; then
  print -u2 "Error: dns-sd not found (it's built into macOS)."
  exit 1
fi

# Run a command for a fixed number of seconds, capturing stdout to a file.
# dns-sd never exits on its own, so we start it, wait, then kill it.
run_for() {
  local secs="$1" outfile="$2"; shift 2
  "$@" >"$outfile" 2>/dev/null &
  local pid=$!
  sleep "$secs"
  kill "$pid" 2>/dev/null
  wait "$pid" 2>/dev/null
}

print "Browsing ${SERVICE} for '${PREFIX}*' (${BROWSE_SECS}s)…"

browse_out=$(mktemp)
run_for "$BROWSE_SECS" "$browse_out" dns-sd -B "$SERVICE" local.

# dns-sd -B columns: Timestamp A/R Flags if Domain Service Instance…
# Keep "Add" rows whose instance name (field 7+) starts with our prefix.
typeset -a instances raw
raw=("${(@f)$(awk -v p="${PREFIX:l}" '
  $2 == "Add" {
    name = "";
    for (i = 7; i <= NF; i++) name = name (i > 7 ? " " : "") $i;
    if (index(tolower(name), p) == 1) print name;
  }' "$browse_out" | sort -u)}")
rm -f "$browse_out"

# Drop empty elements (zsh yields one empty string when awk found nothing).
for inst in $raw; do
  [[ -n "$inst" ]] && instances+=("$inst")
done

if (( ${#instances} == 0 )); then
  print ""
  print "No '${PREFIX}*' device found."
  print "Checks: is it powered on, past first-time WiFi setup, and on THIS same network?"
  exit 2
fi

first_url=""
for inst in $instances; do
  [[ -z "$inst" ]] && continue
  host="${inst}.local"

  # Resolve the IPv4 address (Address is the second-to-last column on Add rows).
  resolve_out=$(mktemp)
  run_for "$RESOLVE_SECS" "$resolve_out" dns-sd -G v4 "$host"
  ip=$(awk '$2 == "Add" { addr = $(NF-1) } END { print addr }' "$resolve_out")
  rm -f "$resolve_out"

  url="http://${host}/"
  [[ -z "$first_url" ]] && first_url="$url"

  print ""
  print "Found: ${inst}"
  print "  URL: ${url}"
  [[ -n "${ip:-}" ]] && print "  IP:  ${ip}"
done

# Optionally open the first match.
if [[ -n "$first_url" && "${OPEN:-}" == "1" ]]; then
  print ""
  print "Opening ${first_url} …"
  open "$first_url"
fi
