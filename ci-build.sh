set -e
ROOT_DIR=$(pwd)

[ -e bin ] && rm -r bin
mkdir bin && cd bin

make -C .. release

if [ ! -e naaice_client ]; then
    echo "Error: naaice_client is missing"
    exit 1  # Exit with a non-zero status to indicate failure
fi

if [ ! -e naaice_server ]; then
    echo "Error: naaice_server is missing"
    exit 1  # Exit with a non-zero status to indicate failure
fi