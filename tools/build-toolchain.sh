#! /bin/bash
# N64 MIPS GCC toolchain build/install script.
# (c) 2012-2021 Shaun Taylor and libDragon Contributors.
# See the root folder for license information.


# This script may prompt for a password if it is not called with elevated privileges.
# Preferably it is fixed at the start!
#TODO: ensure elevated privileges! e.g.
#echo "$(whoami)"
#[ "$UID" -eq 0 ] || exec sudo "$0" "$@"

# Before calling this script, make sure you have all required dependency
# packages installed in your system.  On a Debian-based system this is
# achieved by typing the following commands:
#
# sudo apt-get update && sudo apt-get upgrade
# sudo apt-get install -yq wget bzip2 gcc g++ make file libmpfr-dev libmpc-dev zlib1g-dev texinfo git gcc-multilib

# Exit on error
set -e

# Check for cross compile script flag
if [ "$1" == "-xcw" ]; then # Windows cross compile flag is specified as a parameter.
  # This (probably) requires the toolchain to have already built and installed on the native (linux) system first!
  # This (may) also require the following (extra) package dependencies:
  # sudo apt-get install -yq mingw-w64 libgmp-dev bison

  echo "cross compiling for windows"
  # Use the script directory for the install path, as this is not for linux!
  INSTALL_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
  CROSS_COMPILE_FLAGS="--build=x86_64-linux-gnu --host=x86_64-w64-mingw32" # TODO: --build is probably not required...
  MAKE_V=4.3 #TODO: ensure this is working. V4.2.1 required a patch.

else # We are compiling for the native (linux) system.
  echo "building for linux"
  # Set N64_INST before calling the script to change the default installation directory path
  INSTALL_PATH="${N64_INST:-/usr/local}"
fi

# Set PATH for newlib to compile using GCC for MIPS N64 (pass 1)
export PATH="$PATH:$INSTALL_PATH/bin"

# Determine how many parallel Make jobs to run based on CPU count
JOBS="${JOBS:-`getconf _NPROCESSORS_ONLN`}"
JOBS="${JOBS:-1}" # If getconf returned nothing, default to 1

# Dependency source libs (Versions)
BINUTILS_V=2.36.1
GCC_V=10.2.0
NEWLIB_V=4.1.0

# Check if a command-line tool is available: status 0 means "yes"; status 1 means "no"
command_exists () {
  (command -v "$1" >/dev/null 2>&1)
  return $?
}

# Download the file URL using wget or curl (depending on which is installed)
download () {
  if   command_exists wget ; then wget -c  "$1"
  elif command_exists curl ; then curl -LO "$1"
  else
    echo "Install 'wget' or 'curl' to download toolchain sources" 1>&2
    return 1
  fi
}

# Dependency source: Download stage
test -f "binutils-$BINUTILS_V.tar.gz" || download "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_V.tar.gz"
test -f "gcc-$GCC_V.tar.gz"           || download "https://ftp.gnu.org/gnu/gcc/gcc-$GCC_V/gcc-$GCC_V.tar.gz"
test -f "newlib-$NEWLIB_V.tar.gz"     || download "https://sourceware.org/pub/newlib/newlib-$NEWLIB_V.tar.gz"
if [ "$MAKE_V" != "" ]; then
test -f "make-$MAKE_V.tar.gz"         || download "https://ftp.gnu.org/gnu/make/make-$MAKE_V.tar.gz"
fi

# Dependency source: Extract stage
test -d "binutils-$BINUTILS_V" || tar -xzf "binutils-$BINUTILS_V.tar.gz"
test -d "gcc-$GCC_V"           || tar -xzf "gcc-$GCC_V.tar.gz"
test -d "newlib-$NEWLIB_V"     || tar -xzf "newlib-$NEWLIB_V.tar.gz"
if [ "$MAKE_V" != "" ]; then
test -d "make-$MAKE_V"         || tar -xzf "make-$MAKE_V.tar.gz"
fi

# Compile binutils
cd "binutils-$BINUTILS_V"
./configure \
  --prefix="$INSTALL_PATH" \
  --target=mips64-elf \
  --with-cpu=mips64vr4300 \
  --disable-werror \
  $CROSS_COMPILE_FLAGS
make -j "$JOBS"
make install || sudo make install || su -c "make install"

# Compile GCC for MIPS N64 (pass 1) outside of the source tree
cd ..
rm -rf gcc_compile
mkdir gcc_compile
cd gcc_compile
../"gcc-$GCC_V"/configure \
  --prefix="$INSTALL_PATH" \
  --target=mips64-elf \
  --with-arch=vr4300 \
  --with-tune=vr4300 \
  --enable-languages=c \
  --without-headers \
  --with-newlib \
  --disable-libssp \
  --enable-multilib \
  --disable-shared \
  --with-gcc \
  --disable-threads \
  --disable-win32-registry \
  --disable-nls \
  --disable-werror \
  --with-system-zlib \
  $CROSS_COMPILE_FLAGS
make all-gcc -j "$JOBS"
make all-target-libgcc -j "$JOBS"
make install-gcc || sudo make install-gcc || su -c "make install-gcc"
make install-target-libgcc || sudo make install-target-libgcc || su -c "make install-target-libgcc"

# Compile newlib
cd ../"newlib-$NEWLIB_V"
CFLAGS_FOR_TARGET="-DHAVE_ASSERT_FUNC" ./configure \
  --target=mips64-elf \
  --prefix="$INSTALL_PATH" \
  --with-cpu=mips64vr4300 \
  --disable-threads \
  --disable-libssp \
  --disable-werror \
  $CROSS_COMPILE_FLAGS
make -j "$JOBS"
make install || sudo env PATH="$PATH" make install || su -c "env PATH=\"$PATH\" make install"

# Compile GCC for MIPS N64 (pass 2) outside of the source tree
cd ..
rm -rf gcc_compile
mkdir gcc_compile
cd gcc_compile
CFLAGS_FOR_TARGET="-G0 -O2" CXXFLAGS_FOR_TARGET="-G0 -O2" ../"gcc-$GCC_V"/configure \
  --prefix="$INSTALL_PATH" \
  --target=mips64-elf \
  --with-arch=vr4300 \
  --with-tune=vr4300 \
  --enable-languages=c,c++ \
  --with-newlib \
  --disable-libssp \
  --enable-multilib \
  --disable-shared \
  --with-gcc \
  --disable-threads \
  --disable-win32-registry \
  --disable-nls \
  --with-system-zlib \
  $CROSS_COMPILE_FLAGS
make -j "$JOBS"
make install || sudo make install || su -c "make install"

if [ "$MAKE_V" != "" ]; then
# Compile make
cd ..
cd "make-$MAKE_V"
  ./configure \
    --prefix="$INSTALL_PATH" \
    --disable-largefile \
    --disable-nls \
    --disable-rpath \
    $CROSS_COMPILE_FLAGS
make -j "$JOBS"
make install || sudo make install || su -c "make install"
fi

if [ "$CROSS_COMPILE_FLAGS" != "" ]; then
 echo "Cross compile successful"
else
echo "Native compile successful"
fi
