set -e
ROOT_DIR=$(pwd)
[ -e ci-build ] && rm -r ci-build
mkdir ci-build && cd ci-build


mkdir [ -e build ] && rm -r build
mkdir bin && cd bin

make -C .. release