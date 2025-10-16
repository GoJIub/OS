#!/bin/bash

# Check arguments
if [ $# -lt 1 ]; then
    echo "Usage: $0 <executable> [arguments...]"
    exit 1
fi

EXEC="$1"
shift # remove the first argument, leave the rest for the program

# Run the program in the background and get its PID
"$EXEC" "$@" &
PID=$!

echo "Program started. PID = $PID"

# Give the program 2 seconds to create threads
sleep 2

# Display thread information
echo "List of threads for process $PID (TID):"
for TID in /proc/$PID/task/*; do
    TID_NUM=$(basename "$TID")
    STATUS=$(grep "^State:" "$TID/status" | awk '{print $2,$3,$4}')
    echo "TID: $TID_NUM  |  State: $STATUS"
done

NUM_THREADS=$(ls -1 /proc/$PID/task | wc -l)
echo "Total number of threads: $NUM_THREADS"

# Wait for the program to finish
wait $PID