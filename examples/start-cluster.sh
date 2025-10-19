#!/bin/bash

# Script to quickly start a distributed LXC cluster for testing

echo "Starting Distributed LXC Test Cluster..."

# Check if binaries exist
if [ ! -f "bin/coordinator" ] || [ ! -f "bin/worker" ]; then
    echo "Binaries not found. Building..."
    make all || { echo "Build failed"; exit 1; }
fi

# Start coordinator in background
echo "Starting coordinator on port 8888..."
./bin/coordinator 8888 &
COORD_PID=$!

# Wait for coordinator to start
sleep 2

# Start worker nodes
echo "Starting worker nodes..."
for i in {1..3}; do
    echo "Starting worker node $i..."
    ./bin/worker 127.0.0.1 8888 &
    WORKER_PIDS[$i]=$!
    sleep 1
done

echo ""
echo "Cluster started successfully!"
echo "Coordinator PID: $COORD_PID"
echo "Worker PIDs: ${WORKER_PIDS[@]}"
echo ""
echo "You can now deploy containers using the coordinator CLI."
echo "Example: deploy examples/ubuntu-basic.yaml"
echo ""
echo "To stop the cluster, run: kill $COORD_PID ${WORKER_PIDS[@]}"

# Wait for coordinator to finish
wait $COORD_PID