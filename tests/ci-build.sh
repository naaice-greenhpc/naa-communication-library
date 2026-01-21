set -e
ROOT_DIR=$(pwd)

[ -e build ] && rm -r build
mkdir build
cmake -S . -B build -D CMAKE_BUILD_TYPE=Release
cmake --build build

cd "$ROOT_DIR/build/examples/"
if [ ! -e naaice_client ]; then
    echo "Error: naaice_client is missing"
    exit 1  # Exit with a non-zero status to indicate failure
fi

if [ ! -e naaice_server ]; then
    echo "Error: naaice_server is missing"
    exit 1 
fi

if [ ! -e naaice_client_ap2 ]; then
    echo "Error: naaice_client_ap2 is missing"
    exit 1 
fi

cd "$ROOT_DIR/build/tests/"
if [ ! -e naaice_client_low_level_measurement ]; then
    echo "Error: naaice_client_measurement is missing"
    exit 1 
fi

if [ ! -e naaice_server_measurement ]; then
    echo "Error: naaice_server_measurement is missing"
    exit 1  
fi