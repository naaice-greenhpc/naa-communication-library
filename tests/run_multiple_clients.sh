#!/bin/bash

for i in {1..10}
do
    echo "Starting server $i"
    ./build/examples/naaice_client 10.3.10.42 3 "10 10 10" &
done
