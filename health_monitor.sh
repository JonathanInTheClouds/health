#!/usr/bin/env bash
# =============================================================================
#  health_monitor.sh — Container A / B Health & Status Dashboard
#  Compatible with: RHEL UBI8 | Runtime: podman or docker
#  Dependencies: bash, ss (iproute2), tput — all standard on UBI8
#
#  Keys: q=Quit  r=Refresh  +=Faster  -=Slower  h=Help
# =============================================================================

# =============================================================================
#  CONFIGURATION — Edit these to match your environment
# =============================================================================
CONTAINER_RUNTIME="podman"       # "podman" or "docker"
TCP_PORT="8080"                   # TCP port Container A listens on
REFRESH_INTERVAL=5                # Seconds between auto-refresh (1–60)

# Set interactively at startup — do not edit
CONTAINER_A_NAME=""
CONTAINER_B_NAME=""
# =============================================================================


# --- Internal Constants -------------------------------------------------------
readonly MIN_INTERVAL=1
readonly MAX_INTERVAL=60
readonly VERSION="1.0.0"

# --- ANSI Colors & Styles -----------------------------------------------------
readonly RST="\033[0m"
readonly BOLD="\033[1m"
readonly DIM="\033[2m"

readonly FG_BLACK="\033[30m"
readonly FG_RED="\033[31m"
readonly FG_GREEN="\033[32m"
readonly FG_YELLOW="\033[33m"
readonly FG_BLUE="\033[34m"
readonly FG_CYAN="\033[36m"
readonly FG_WHITE="\033[37m"
readonly FG_BWHITE="\033[97m"

readonly BG_BLACK="\033[40m"
readonly BG_BLUE="\033[44m"
readonly BG_DKGRAY="\033[100m"

# Box-drawing chars (UTF-8)
readonly H="─"; readonly V="│"; readonly TL="╭"; readonly TR="╮"
readonly BL="╰"; readonly BR="╯"; readonly LT="├"; readonly RT="┤"
readonly TT="┬"; readonly BT="┴"; readonly XX="┼"


# =============================================================================
#  TERMINAL UTILITIES
# =============================================================================
term_cols()  { tput cols  2>/dev/null || echo 80; }
term_rows()  { tput lines 2>/dev/null || echo 24; }
cursor_hide(){ tput civis 2>/dev/null; }
cursor_show(){ tput cnorm 2>/dev/null; }
goto()       { tput cup "$(($1-1))" "$(($2-1))" 2>/dev/null; }   # goto row col (1-based)
clear_screen(){ tput clear 2>/dev/null || printf "\033[2J\033[H"; }

# Print a horizontal rule of char $1, length $2, optional prefix/suffix
hline() {
    local char="$1" len="$2"
    printf '%0.s'"$char" $(seq 1 "$len")
}

# Pad string $1 to width $2 (truncate if longer)
pad() {
    local str="$1" width="$2"
    printf "%-${width}s" "${str:0:$width}"
}

# Right-align string $1 in field of width $2
rpad() {
    local str="$1" width="$2"
    printf "%${width}s" "${str:0:$width}"
}

# Print centered text in a field of $1 width
center() {
    local width="$1"; shift
    local text="$*"
    local tlen=${#text}
    local lpad=$(( (width - tlen) / 2 ))
    local rpad=$(( width - tlen - lpad ))
    printf "%${lpad}s%s%${rpad}s" "" "$text" ""
}


# =============================================================================
#  DATA COLLECTION
# =============================================================================

# Returns "running", "exited", "paused", or "unknown"
container_status() {
    local name="$1"
    local status
    status=$("$CONTAINER_RUNTIME" inspect --format '{{.State.Status}}' "$name" 2>/dev/null)
    case "$status" in
        running) echo "running" ;;
        exited|stopped) echo "exited" ;;
        paused)  echo "paused"  ;;
        *)       echo "unknown" ;;
    esac
}

# Returns uptime string from container start time
container_uptime() {
    local name="$1"
    local started
    started=$("$CONTAINER_RUNTIME" inspect --format '{{.State.StartedAt}}' "$name" 2>/dev/null)
    [[ -z "$started" || "$started" == "0001-01-01T00:00:00Z" ]] && { echo "N/A"; return; }

    # Podman outputs: "2026-03-06 04:38:35.44078364 -0600 CST"
    # Strip only the sub-second fraction, preserve the timezone offset
    started=$(echo "$started" | sed 's/\.[0-9]*//')

    # Parse ISO8601 start time to epoch seconds
    local start_epoch
    start_epoch=$(date -d "$started" +%s 2>/dev/null) || { echo "N/A"; return; }
    local now_epoch; now_epoch=$(date +%s)
    local secs=$(( now_epoch - start_epoch ))

    local d=$(( secs / 86400 ))
    local h=$(( (secs % 86400) / 3600 ))
    local m=$(( (secs % 3600) / 60 ))
    local s=$(( secs % 60 ))

    if   (( d > 0 ));  then printf "%dd %02dh %02dm" "$d" "$h" "$m"
    elif (( h > 0 ));  then printf "%dh %02dm %02ds"  "$h" "$m" "$s"
    else                    printf "%dm %02ds"         "$m" "$s"
    fi
}

# Returns PID count of processes inside container
container_pids() {
    local name="$1"
    local status; status=$(container_status "$name")
    [[ "$status" != "running" ]] && { echo "N/A"; return; }

    # Count numeric dirs in /proc — one per process, no extra packages needed
    local count
    count=$("$CONTAINER_RUNTIME" exec "$name" \
        sh -c 'ls /proc | grep -cE "^[0-9]+$"' 2>/dev/null)
    echo "${count:-N/A}"
}

# Returns CPU% and MEM usage from stats (non-blocking)
container_stats() {
    local name="$1"
    local stats_out
    stats_out=$("$CONTAINER_RUNTIME" stats --no-stream --format \
        "{{.CPUPerc}} {{.MemUsage}}" "$name" 2>/dev/null)

    if [[ -z "$stats_out" ]]; then
        echo "N/A N/A"
    else
        echo "$stats_out"
    fi
}

# Run ss inside a container, return TCP connection lines
# $1 = container name, $2 = optional: "listen" or "estab"
container_ss() {
    local name="$1"
    local filter="${2:-all}"
    local ss_args="-tn"

    case "$filter" in
        listen) ss_args="-tlnp" ;;
        estab)  ss_args="-tn" ;;   # grep for ESTAB after; 'state established' drops State column
        *)      ss_args="-tn" ;;
    esac

    "$CONTAINER_RUNTIME" exec "$name" ss $ss_args 2>/dev/null | tail -n +2
}

# Check if Container A is listening on TCP_PORT
check_a_listening() {
    local raw
    raw=$(container_ss "$CONTAINER_A_NAME" "listen")
    echo "$raw" | grep -qE ":${TCP_PORT}\b" && echo "YES" || echo "NO"
}

# Get established connections involving TCP_PORT
get_established_conns() {
    local raw
    raw=$(container_ss "$CONTAINER_B_NAME" "estab")
    # Also pull from Container A for the full picture
    local raw_a
    raw_a=$(container_ss "$CONTAINER_A_NAME" "estab")

    # Merge and deduplicate, filter to our port
    { echo "$raw"; echo "$raw_a"; } | grep "ESTAB" | grep -E ":${TCP_PORT}\b" | sort -u
}

# Count established TCP connections on the given port (from Container A's perspective)
count_established() {
    local count
    count=$(container_ss "$CONTAINER_A_NAME" "estab" | grep "ESTAB" | grep -cE ":${TCP_PORT}\b" 2>/dev/null)
    echo "${count:-0}"
}


# =============================================================================
#  RENDERING
# =============================================================================
COLS=80  # updated each draw

# Color a status string
status_color() {
    case "$1" in
        running) printf "${FG_GREEN}${BOLD}● RUNNING${RST}" ;;
        exited)  printf "${FG_RED}${BOLD}● STOPPED${RST}" ;;
        paused)  printf "${FG_YELLOW}${BOLD}◌ PAUSED${RST}" ;;
        *)       printf "${FG_YELLOW}${BOLD}? UNKNOWN${RST}" ;;
    esac
}

# Draw a full-width titled box line
box_title_line() {
    local title="$1"
    local total="$COLS"
    local tlen=${#title}
    local left=$(( (total - tlen - 4) / 2 ))
    local right=$(( total - tlen - 4 - left ))
    printf "${FG_CYAN}${TL}"
    hline "$H" "$left"
    printf "[ ${BOLD}${FG_BWHITE}%s${RST}${FG_CYAN} ]" "$title"
    hline "$H" "$right"
    printf "${TR}${RST}\n"
}

box_sep() {
    printf "${FG_CYAN}${LT}"
    hline "$H" $(( COLS - 2 ))
    printf "${RT}${RST}\n"
}

box_bottom() {
    printf "${FG_CYAN}${BL}"
    hline "$H" $(( COLS - 2 ))
    printf "${BR}${RST}\n"
}

box_row() {
    # $1 = left content (already formatted), $2 = right content
    # renders: │ <content padded to cols-2> │
    local content="$1"
    # Strip ANSI for length calc
    local plain; plain=$(echo -e "$content" | sed 's/\x1b\[[0-9;]*m//g')
    local plen=${#plain}
    local pad=$(( COLS - 2 - plen ))
    (( pad < 0 )) && pad=0
    printf "${FG_CYAN}${V}${RST} %b%${pad}s ${FG_CYAN}${V}${RST}\n" "$content" ""
}

# Split box: two columns divided at midpoint
box_row2() {
    local left="$1" right="$2"
    local mid=$(( (COLS - 3) / 2 ))
    local plain_l; plain_l=$(echo -e "$left"  | sed 's/\x1b\[[0-9;]*m//g')
    local plain_r; plain_r=$(echo -e "$right" | sed 's/\x1b\[[0-9;]*m//g')
    local ll=${#plain_l}; local rl=${#plain_r}
    local lpad=$(( mid - ll - 1 )); (( lpad < 0 )) && lpad=0
    local rpad=$(( COLS - mid - rl - 3 )); (( rpad < 0 )) && rpad=0
    printf "${FG_CYAN}${V}${RST} %b%${lpad}s ${FG_CYAN}${V}${RST} %b%${rpad}s ${FG_CYAN}${V}${RST}\n" \
        "$left" "" "$right" ""
}

box_mid_sep() {
    local mid=$(( (COLS - 3) / 2 ))
    printf "${FG_CYAN}${LT}"
    hline "$H" "$mid"
    printf "${XX}"
    hline "$H" $(( COLS - mid - 3 ))
    printf "${RT}${RST}\n"
}

box_top_split() {
    local mid=$(( (COLS - 3) / 2 ))
    printf "${FG_CYAN}${TL}"
    hline "$H" "$mid"
    printf "${TT}"
    hline "$H" $(( COLS - mid - 3 ))
    printf "${TR}${RST}\n"
}

# =============================================================================
#  MAIN DRAW FUNCTION
# =============================================================================
draw_dashboard() {
    COLS=$(term_cols)
    (( COLS < 60 )) && COLS=60  # enforce minimum width

    local now; now=$(date '+%Y-%m-%d  %H:%M:%S')

    # --- Collect data ---------------------------------------------------------
    local sta_a; sta_a=$(container_status  "$CONTAINER_A_NAME")
    local sta_b; sta_b=$(container_status  "$CONTAINER_B_NAME")

    local stats_a; stats_a=$(container_stats "$CONTAINER_A_NAME")
    local stats_b; stats_b=$(container_stats "$CONTAINER_B_NAME")
    local cpu_a;   cpu_a=$(echo "$stats_a" | awk '{print $1}')
    local mem_a;   mem_a=$(echo "$stats_a" | awk '{print $2}')
    local cpu_b;   cpu_b=$(echo "$stats_b" | awk '{print $1}')
    local mem_b;   mem_b=$(echo "$stats_b" | awk '{print $2}')

    local a_listening; a_listening=$(check_a_listening)
    local num_estab;   num_estab=$(count_established)
    local estab_lines; estab_lines=$(get_established_conns)

    # --- Draw -----------------------------------------------------------------
    clear_screen

    # Header bar
    printf "${BG_DKGRAY}${FG_BWHITE}${BOLD}"
    center "$COLS" " Container Health Monitor v${VERSION} "
    printf "${RST}\n"

    printf "${DIM}%s${RST}" "$(center "$COLS" "Runtime: ${CONTAINER_RUNTIME}   |   Port: ${TCP_PORT}   |   Refresh: ${REFRESH_INTERVAL}s   |   ${now}")"
    printf "\n"

    # Container panels (split columns)
    box_top_split
    local mid=$(( (COLS - 3) / 2 ))
    box_row2 "${BOLD}${FG_CYAN}  ${CONTAINER_A_NAME}${RST}" \
             "${BOLD}${FG_CYAN}  ${CONTAINER_B_NAME}${RST}"
    box_row2 "  Status : $(status_color "$sta_a")" \
             "  Status : $(status_color "$sta_b")"
    box_row2 "  CPU    : ${FG_GREEN}${cpu_a:-N/A}${RST}" \
             "  CPU    : ${FG_GREEN}${cpu_b:-N/A}${RST}"
    box_row2 "  Mem    : ${FG_GREEN}${mem_a:-N/A}${RST}" \
             "  Mem    : ${FG_GREEN}${mem_b:-N/A}${RST}"
    box_mid_sep

    # TCP section header
    local listen_color="${FG_RED}"
    [[ "$a_listening" == "YES" ]] && listen_color="${FG_GREEN}"
    local estab_color="${FG_YELLOW}"
    (( num_estab > 0 )) && estab_color="${FG_GREEN}"

    box_row "${BOLD}${FG_CYAN}  TCP STATUS${RST}  (port ${TCP_PORT})"
    box_row "  ${CONTAINER_A_NAME} listening on :${TCP_PORT}  →  ${listen_color}${BOLD}${a_listening}${RST}"
    box_row "  Established connections  →  ${estab_color}${BOLD}${num_estab}${RST}"

    box_sep

    # Column headers for connection table
    local ch_state ch_local ch_peer
    ch_state="${BOLD}${FG_BWHITE}$(pad "State"  11)${RST}"
    ch_local="${BOLD}${FG_BWHITE}$(pad "Local Address:Port" 26)${RST}"
    ch_peer="${BOLD}${FG_BWHITE}$(pad "Peer Address:Port" 26)${RST}"
    box_row "  ${ch_state}  ${ch_local}  ${ch_peer}"
    box_sep

    # Connection rows
    local row_count=0
    if [[ -n "$estab_lines" ]]; then
        while IFS= read -r line; do
            [[ -z "$line" ]] && continue
            local state local_ap peer_ap
            state=$(echo "$line" | awk '{print $1}')
            local_ap=$(echo "$line" | awk '{print $4}')
            peer_ap=$(echo "$line"  | awk '{print $5}')

            local color_state="${FG_GREEN}"
            [[ "$state" != "ESTAB" ]] && color_state="${FG_YELLOW}"

            local f_state f_local f_peer
            f_state=$(pad "$state"    11)
            f_local=$(pad "$local_ap" 26)
            f_peer=$(pad  "$peer_ap"  26)

            box_row "  ${color_state}${f_state}${RST}  ${FG_WHITE}${f_local}${RST}  ${FG_CYAN}${f_peer}${RST}"
            (( row_count++ ))
            (( row_count >= 8 )) && break   # cap rows for readability
        done <<< "$estab_lines"
    fi

    if (( row_count == 0 )); then
        box_row "  ${FG_YELLOW}${DIM}No established connections found on port ${TCP_PORT}${RST}"
    fi

    box_sep

    # Footer / keybinds
    local footer
    footer="  ${BOLD}q${RST}${DIM}:Quit  ${RST}${BOLD}r${RST}${DIM}:Refresh  ${RST}${BOLD}+${RST}${DIM}:Faster  ${RST}${BOLD}-${RST}${DIM}:Slower  ${RST}${BOLD}h${RST}${DIM}:Help${RST}"
    local rf_info="  ${DIM}Refresh in ${REFRESH_INTERVAL}s${RST}"
    box_row "${footer}${rf_info}"
    box_bottom
}


# =============================================================================
#  HELP OVERLAY
# =============================================================================
draw_help() {
    local COLS; COLS=$(term_cols)
    (( COLS < 60 )) && COLS=60
    clear_screen

    box_title_line "HELP"
    box_row ""
    box_row "  ${BOLD}${FG_CYAN}KEYBOARD SHORTCUTS${RST}"
    box_row "  ${BOLD}q${RST}  — Quit the monitor"
    box_row "  ${BOLD}r${RST}  — Force immediate refresh"
    box_row "  ${BOLD}+${RST}  — Decrease refresh interval (faster)"
    box_row "  ${BOLD}-${RST}  — Increase refresh interval (slower)"
    box_row "  ${BOLD}h${RST}  — Toggle this help screen"
    box_row ""
    box_sep
    box_row "  ${BOLD}${FG_CYAN}WHAT IS CHECKED${RST}"
    box_row "  • Container running state   (podman/docker inspect)"
    box_row "  • CPU & memory usage        (podman/docker stats)"
    box_row "  • TCP listen state on port  (ss -tlnp inside container A)"
    box_row "  • Established TCP sessions  (ss -tn inside containers)"
    box_row ""
    box_sep
    box_row "  ${BOLD}${FG_CYAN}CONFIGURATION${RST}  (edit top of script)"
    box_row "  TCP_PORT          — port Container A listens on"
    box_row "  CONTAINER_RUNTIME — 'podman' or 'docker'"
    box_row "  REFRESH_INTERVAL  — default seconds between auto-refresh"
    box_row "  Container A/B     — selected interactively at startup"
    box_row ""
    box_bottom

    printf "\n  ${DIM}Press any key to return...${RST}\n"
    read -r -s -n 1 2>/dev/null
}


# =============================================================================
#  CLEANUP
# =============================================================================
cleanup() {
    cursor_show
    tput rmcup 2>/dev/null
    printf "\n${FG_CYAN}health_monitor:${RST} Exited cleanly.\n"
    exit 0
}
trap cleanup INT TERM EXIT


# =============================================================================
#  ARGUMENT PARSING
# =============================================================================
usage() {
    printf "Usage: %s <container-a> <container-b> <port>\n\n" "$(basename "$0")"
    printf "  container-a   Name or ID of the TCP receiver container\n"
    printf "  container-b   Name or ID of the TCP sender container\n"
    printf "  port          TCP port container-a listens on\n\n"
    printf "Example:\n"
    printf "  %s container-a container-b 5000\n" "$(basename "$0")"
    exit 1
}

parse_args() {
    if [[ "$1" == "-h" || "$1" == "--help" ]]; then
        usage
    fi

    if (( $# != 3 )); then
        printf "${FG_RED}ERROR:${RST} Expected 3 arguments, got %d.\n\n" "$#"
        usage
    fi

    CONTAINER_A_NAME="$1"
    CONTAINER_B_NAME="$2"
    TCP_PORT="$3"

    if [[ "$CONTAINER_A_NAME" == "$CONTAINER_B_NAME" ]]; then
        printf "${FG_RED}ERROR:${RST} container-a and container-b cannot be the same.\n"
        exit 1
    fi

    if ! [[ "$TCP_PORT" =~ ^[0-9]+$ ]] || (( TCP_PORT < 1 || TCP_PORT > 65535 )); then
        printf "${FG_RED}ERROR:${RST} '%s' is not a valid port number (1-65535).\n" "$TCP_PORT"
        exit 1
    fi
}


# =============================================================================
#  STARTUP CHECKS
# =============================================================================
preflight() {
    local ok=1

    if ! command -v "$CONTAINER_RUNTIME" &>/dev/null; then
        printf "${FG_RED}ERROR:${RST} '%s' not found. Set CONTAINER_RUNTIME at top of script.\n" \
            "$CONTAINER_RUNTIME"
        ok=0
    fi

    if ! command -v ss &>/dev/null; then
        printf "${FG_YELLOW}WARN:${RST} 'ss' not found inside this shell. TCP checks will show N/A.\n"
        printf "      'ss' is provided by the 'iproute' package.\n"
        printf "      If dnf is available:  dnf install iproute -y\n"
        printf "      Otherwise check with your sysadmin or verify it is installed\n"
        printf "      inside your containers (TCP checks run via exec into them).\n"
        sleep 2
    fi

    if ! command -v tput &>/dev/null; then
        printf "${FG_YELLOW}WARN:${RST} 'tput' not found. Display may degrade.\n"
    fi

    (( ok == 0 )) && exit 1
}


# =============================================================================
#  MAIN LOOP
# =============================================================================
main() {
    parse_args "$@"
    preflight

    # Save terminal state, switch to alternate buffer
    tput smcup 2>/dev/null
    cursor_hide

    local in_help=0
    local elapsed=0
    local tick=1   # poll interval for keypress (seconds)

    while true; do
        if (( in_help )); then
            draw_help
            in_help=0
            elapsed=0
            continue
        fi

        # Redraw on first run or when refresh interval hit
        if (( elapsed == 0 )); then
            draw_dashboard
        fi

        # Non-blocking read with $tick second timeout
        local key=""
        IFS= read -r -s -n 1 -t "$tick" key 2>/dev/null || true

        case "$key" in
            q|Q) cleanup ;;
            r|R) elapsed=0 ;;
            h|H) in_help=1; elapsed=0 ;;
            "+") (( REFRESH_INTERVAL > MIN_INTERVAL )) && (( REFRESH_INTERVAL-- ))
                 elapsed=0 ;;
            "-") (( REFRESH_INTERVAL < MAX_INTERVAL )) && (( REFRESH_INTERVAL++ ))
                 elapsed=0 ;;
        esac

        (( elapsed += tick ))
        (( elapsed >= REFRESH_INTERVAL )) && elapsed=0
    done
}

main "$@"