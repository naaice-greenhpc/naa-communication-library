#!/bin/bash

echo "Start client"

apt-get update
apt-get install -y librdmacm-dev libibverbs1 ibverbs-utils

MAX_RETRIES=10
RETRY_DELAY=5
ATTEMPT=1

while [ $ATTEMPT -le $MAX_RETRIES ]; do
    echo "Attempt $ATTEMPT of $MAX_RETRIES"
    
    ./build/examples/naaice_client 10.3.10.41 10.3.10.42 3 "10 10 10"
    EXIT_CODE=$?

    if [ $EXIT_CODE -ne 0 ]; then
        echo "Client failed with exit code $EXIT_CODE"
        if [ $ATTEMPT -lt $MAX_RETRIES ]; then
            echo "Retrying in $RETRY_DELAY seconds..."
            sleep $RETRY_DELAY
        fi
    else
        echo "Finished with exit code $EXIT_CODE"
        exit $EXIT_CODE
    fi

    ATTEMPT=$((ATTEMPT + 1))
done

echo "Finished with exit code $EXIT_CODE"
exit $EXIT_CODE