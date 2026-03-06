#!/usr/bin/env bash
# =============================================================================
#  health_monitor_simple.sh — Container Health & Status
#  Usage: ./health_monitor_simple.sh <container-a> <container-b> <port>
#  Compatible with: RHEL UBI8 | Runtime: podman or docker
# =============================================================================

CONTAINER_RUNTIME="podman"
REFRESH_INTERVAL=5

# --- Argument parsing --------------------------------------------------------
if [[ "$1" == "-h" || "$1" == "--help" || $# -ne 3 ]]; then
    echo "Usage: $(basename "$0") <container-a> <container-b> <port>"
    echo "  Example: $(basename "$0") container-a container-b 5000"
    exit 1
fi

CONTAINER_A="$1"
CONTAINER_B="$2"
TCP_PORT="$3"

if ! [[ "$TCP_PORT" =~ ^[0-9]+$ ]] || (( TCP_PORT < 1 || TCP_PORT > 65535 )); then
    echo "ERROR: '$TCP_PORT' is not a valid port (1-65535)."
    exit 1
fi

# --- Data functions ----------------------------------------------------------
get_status() {
    local name="$1"
    local s; s=$("$CONTAINER_RUNTIME" inspect --format '{{.State.Status}}' "$name" 2>/dev/null)
    case "$s" in
        running)        echo "RUNNING" ;;
        exited|stopped) echo "STOPPED" ;;
        paused)         echo "PAUSED"  ;;
        *)              echo "UNKNOWN" ;;
    esac
}

get_stats() {
    local name="$1"
    "$CONTAINER_RUNTIME" stats --no-stream --format \
        "CPU: {{.CPUPerc}}  MEM: {{.MemUsage}}" "$name" 2>/dev/null || echo "CPU: N/A  MEM: N/A"
}

is_listening() {
    "$CONTAINER_RUNTIME" exec "$CONTAINER_A" ss -tlnp 2>/dev/null \
        | grep -qE ":${TCP_PORT}\b" && echo "YES" || echo "NO"
}

count_established() {
    local count
    count=$("$CONTAINER_RUNTIME" exec "$CONTAINER_A" ss -tn 2>/dev/null \
        | grep "ESTAB" | grep -cE ":${TCP_PORT}\b")
    echo "${count:-0}"
}

get_connections() {
    {
        "$CONTAINER_RUNTIME" exec "$CONTAINER_A" ss -tn 2>/dev/null
        "$CONTAINER_RUNTIME" exec "$CONTAINER_B" ss -tn 2>/dev/null
    } | grep "ESTAB" | grep -E ":${TCP_PORT}\b" | sort -u
}

# --- Main loop ---------------------------------------------------------------
echo "Container Health Monitor  |  Runtime: $CONTAINER_RUNTIME  |  Port: $TCP_PORT"
echo "Press Ctrl+C to exit"
echo ""

while true; do
    echo "======================================== $(date '+%Y-%m-%d %H:%M:%S')"

    echo "  [$CONTAINER_A]"
    echo "    Status : $(get_status "$CONTAINER_A")"
    echo "    $(get_stats "$CONTAINER_A")"

    echo "  [$CONTAINER_B]"
    echo "    Status : $(get_status "$CONTAINER_B")"
    echo "    $(get_stats "$CONTAINER_B")"

    echo ""
    echo "  [TCP :$TCP_PORT]"
    echo "    Listening  : $(is_listening)"
    echo "    Established: $(count_established)"

    conns=$(get_connections)
    if [[ -n "$conns" ]]; then
        echo ""
        echo "    State       Local                      Peer"
        echo "$conns" | awk '{printf "    %-11s %-26s %s\n", $1, $4, $5}'
    fi

    echo ""
    sleep "$REFRESH_INTERVAL"
done