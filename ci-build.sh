set -e
ROOT_DIR=$(pwd)

[ -e build ] && rm -r build
mkdir build
cmake -S . -B build
cmake --build build

cd /build/src/
if [ ! -e naaice_client ]; then
    echo "Error: naaice_client is missing"
    exit 1  # Exit with a non-zero status to indicate failure
fi

if [ ! -e naaice_server ]; then
    echo "Error: naaice_server is missing"
    exit 1  # Exit with a non-zero status to indicate failure
fi