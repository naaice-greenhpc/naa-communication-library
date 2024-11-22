set -e
ROOT_DIR=$(pwd)

[ -e bin ] && rm -r bin
mkdir bin && cd bin

make -C .. release