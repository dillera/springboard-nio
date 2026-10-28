#!/bin/sh
# Start, stop or restart fujinet-nio on the Springboard NIO link port.
# NIO does not notice the RP2350 re-enumerating, so restart it after a flash.
#   host/nio.sh start|stop|restart|status
# Log: host/nio-run/nio.log
HOST=$(cd "$(dirname "$0")" && pwd)
RUN=$HOST/nio-run
PIDFILE=$RUN/nio.pid
mkdir -p "$RUN"

running() { [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; }

stop() {
    if running; then
        kill "$(cat "$PIDFILE")" 2>/dev/null
        sleep 0.5
    fi
    rm -f "$PIDFILE"
}

start() {
    running && { echo "fujinet-nio already running (pid $(cat "$PIDFILE"))"; return; }
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        PORT=$(python3 "$HOST/sbcon.py" --port "NIO link" 2>/dev/null) && break
        sleep 0.5
    done
    [ -n "$PORT" ] || { echo "no Springboard NIO link port" >&2; exit 1; }
    FN_SERIAL_PORT=$PORT nohup "$HOST/run-nio.sh" >"$RUN/nio.log" 2>&1 &
    echo $! >"$PIDFILE"
    echo "fujinet-nio started on $PORT (pid $!)"
}

# Keep fujinet-nio bound to the board: restart it whenever the NIO link port
# appears, changes or NIO dies (the RP2350 re-enumerates on every reset).
supervise() {
    while :; do
        PORT=$(python3 "$HOST/sbcon.py" --port "NIO link" 2>/dev/null)
        SESSION=$(python3 "$HOST/sbcon.py" --session 2>/dev/null)
        if [ -n "$PORT" ] && { ! running || [ "$PORT $SESSION" != "$(cat "$RUN/port" 2>/dev/null)" ]; }; then
            stop
            sleep 1
            start && echo "$PORT $SESSION" >"$RUN/port"
        elif [ -z "$PORT" ] && running; then
            stop
        fi
        sleep 2
    done
}

case "$1" in
start) start ;;
supervise) supervise ;;
stop) stop ;;
restart) stop; start ;;
status) running && echo "running (pid $(cat "$PIDFILE"))" || echo "stopped" ;;
*) echo "usage: $0 start|stop|restart|status|supervise" >&2; exit 2 ;;
esac
