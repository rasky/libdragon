#! /bin/bash
# N64 MIPS GCC toolchain build/install script (runs under UNIX systems).
# (c) 2012-2021 Shaun Taylor and libDragon Contributors.
# See the root folder for license information.


# Before calling this script, make sure you have all required
# dependency packages installed in your system.  On a Debian-based systems
# this is achieved by typing the following commands:
#
# sudo apt-get update && sudo apt-get upgrade
# sudo apt-get install -yq wget bzip2 gcc g++ make file libmpfr-dev libmpc-dev zlib1g-dev texinfo git gcc-multilib

# # Bash strict mode http://redsymbol.net/articles/unofficial-bash-strict-mode/
# set -euo pipefail
# IFS=$'\n\t'

# Exit script on error
set -e

# Ensure you set 'N64_INST' before calling the script to change the default installation directory path
  # by default it will presume 'usr/local/n64_toolchain'
  INSTALL_PATH="${N64_INST:-/usr/local/n64_toolchain}"

# Check for cross compile script flag
# TODO: detect the flag `--host`
# TODO: does not reach bash strict as $1 could be null!
if [ "$1" == "x86_64-w64-mingw32" ]; then # Windows cross compile (host) flag is specified as a parameter.
  # This (may) also require the following (extra) package dependencies:
  # sudo apt-get install -yq mingw-w64 libgmp-dev bison libz-mingw-w64-dev autoconf

  echo "Cross compiling for different host"
  # Use the current-directory/binaries for the install path, as this is not for linux!
  rm -rf binaries && mkdir binaries # always ensure the folder is clean (if rebuilding)
  THIS_SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
  FOREIGN_INSTALL_PATH="$THIS_SCRIPT_PATH/binaries"

  # This will require the extra flags (under certain libs)
  BUILD= # Probably never required but can use: --build=x86_64-linux-gnu
  HOST="--host=$1" 

  # Dependency source libs (Versions)
  # This will have to build Make and download FP libs
  GMP_V=6.2.0
  MPC_V=1.2.1
  MPFR_V=4.1.0
  MAKE_V=4.2.1

else # We are compiling for the native system.
  echo "building for native system"
  # Only define optional build and host if required (default empty).
  BUILD=
  HOST=

  # Only define versions of optional dependencies if required (default empty).
  GMP_V=
  MPC_V=
  MPFR_V=
  MAKE_V=

fi

BINUTILS_V=2.36.1 # linux works fine with 2.37 (but is it worth the effort?)
GCC_V=11.2.0
NEWLIB_V=4.1.0


# Determine how many parallel Make jobs to run based on CPU count
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN)}"
JOBS="${JOBS:-1}" # If getconf returned nothing, default to 1


# Check if a command-line tool is available: status 0 means "yes"; status 1 means "no"
command_exists () {
  (command -v "$1" >/dev/null 2>&1)
  return $?
}

# Download the file URL using wget or curl (depending on which is installed)
download () {
  if   command_exists wget ; then wget --no-check-certificate -c  "$1"
  elif command_exists curl ; then curl -LO "$1"
  else
    echo "Install 'wget' or 'curl' to download toolchain sources" 1>&2
    return 1
  fi
}

echo "Stage: Download and extract dependencies"
test -f "binutils-$BINUTILS_V.tar.gz" || download "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_V.tar.gz"
test -d "binutils-$BINUTILS_V"        || tar -xzf "binutils-$BINUTILS_V.tar.gz"

test -f "gcc-$GCC_V.tar.gz"           || download "https://ftp.gnu.org/gnu/gcc/gcc-$GCC_V/gcc-$GCC_V.tar.gz"
test -d "gcc-$GCC_V"                  || tar -xzf "gcc-$GCC_V.tar.gz" --checkpoint=.100 # TODO: there must be a better way of showing progress (given such a large file)!

test -f "newlib-$NEWLIB_V.tar.gz"     || download "https://sourceware.org/pub/newlib/newlib-$NEWLIB_V.tar.gz"
test -d "newlib-$NEWLIB_V"            || tar -xzf "newlib-$NEWLIB_V.tar.gz"

# Optional dependency handling
# Copies the FP libs into GCC sources so they are compiled as part of it
if [ "$GMP_V" != "" ]; then
  test -f "gmp-$GMP_V.tar.xz"         || download "https://ftp.gnu.org/gnu/gmp/gmp-$GMP_V.tar.xz"
  test -d "gmp-$GMP_V"                || tar -xf "gmp-$GMP_V.tar.xz" # note no .gz download file currently available
  cd "gcc-$GCC_V"
  ln -sf ../"gmp-$GMP_V" "gmp"
  cd ..
fi
if [ "$MPC_V" != "" ]; then
  test -f "mpc-$MPC_V.tar.gz"         || download "https://ftp.gnu.org/gnu/mpc/mpc-$MPC_V.tar.gz"
  test -d "mpc-$MPC_V"                || tar -xzf "mpc-$MPC_V.tar.gz"
  cd "gcc-$GCC_V"
  ln -sf ../"mpc-$MPC_V" "mpc"
  cd ..
fi
if [ "$MPFR_V" != "" ]; then
  test -f "mpfr-$MPFR_V.tar.gz"       || download "https://ftp.gnu.org/gnu/mpfr/mpfr-$MPFR_V.tar.gz"
  test -d "mpfr-$MPFR_V"              || tar -xzf "mpfr-$MPFR_V.tar.gz"
  cd "gcc-$GCC_V"
  ln -sf ../"mpfr-$MPFR_V" "mpfr"
  cd ..
fi
# Certain platforms might require Makefile cross compiling
if [ "$MAKE_V" != "" ]; then
  test -f "make-$MAKE_V.tar.gz"       || download "https://ftp.gnu.org/gnu/make/make-$MAKE_V.tar.gz"
  test -d "make-$MAKE_V"              || tar -xzf "make-$MAKE_V.tar.gz"
fi

if [ "$BUILD" != "$HOST" ]; then
  echo "Stage: Patch step"
  
  if [[ "$GCC_V" = "11.2.0" || "$GCC_V" = "11.1.0" ]]; then
    # GCC 11.x fails on canadian cross
    # see: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=100017
    # see: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80196
    echo "Apply patch for GCC using SED:"
    sed -z 's/RAW_CXX_FOR_TARGET="$CXX_FOR_TARGET"/RAW_CXX_FOR_TARGET="$CXX_FOR_TARGET -nostdinc++"/' ./"gcc-$GCC_V"/configure
  fi

  if ["$BINUTILS_V" = "2.37"]; then
    # BINUTILS 2.37 fails on canadian cross
    # See: https://lists.gnu.org/archive/html/bug-binutils/2021-07/msg00133.html
    # Also seems to involve more indepth patches... https://gcc.gnu.org/bugzilla/attachment.cgi?id=50777
    # BUT can try my changes...
    echo "Apply patch for BINUTILS 2.37 using SED:"
    # Add something like: #define uint unsigned int
    #nl=$'\n'
    #sed -z 's/uint recursion;/#define uint unsigned int'"\\${nl}"'uint recursion;/' ./"binutils-$BINUTILS_V"/libiberty/rust-demangle.c
    #sed -z 's/uint recursion;/unsigned int recursion;/' ./"binutils-$BINUTILS_V"/libiberty/rust-demangle.c
    #sed -z 's/#define RUST_NO_RECURSION_LIMIT   ((uint) -1)/#define RUST_NO_RECURSION_LIMIT   ((unsigned int) -1)/' ./"binutils-$BINUTILS_V"/libiberty/rust-demangle.c
  fi

fi

echo "Stage: Compile toolchain"

echo "Compiling binutils-$BINUTILS_V"
cd "binutils-$BINUTILS_V"
./configure \
  --prefix="$INSTALL_PATH" \
  --target=mips64-elf \
  --with-cpu=mips64vr4300 \
  --disable-werror
make -j "$JOBS"
make install-strip || sudo make install-strip || su -c "make install-strip"
make distclean # Ensure we can build it again
echo "Finished Compiling binutils-$BINUTILS_V"

echo "Compiling native build of GCC-$GCC_V for MIPS N64 - (pass 1) outside of the source tree"
# TODO why do we bother if we already have a good (compatible) compiler installed?! 
# e.g. we could use ` whereis` ?! it does not need to be up-to-date as we have a second pass?!
cd ..
rm -rf gcc_compile
mkdir gcc_compile
cd gcc_compile
../"gcc-$GCC_V"/configure \
  --prefix="$INSTALL_PATH" \
  --target=mips64-elf \
  --with-arch=vr4300 \
  --with-tune=vr4300 \
  --enable-languages=c,c++ \
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
  --with-system-zlib
make all-gcc -j "$JOBS"
make all-target-libgcc -j "$JOBS"
make install-strip-gcc || sudo make install-strip-gcc || su -c "make install-strip-gcc"
make install-target-libgcc || sudo make install-target-libgcc || su -c "make install-target-libgcc"
echo "Finished Compiling GCC-$GCC_V for MIPS N64 - (pass 1) outside of the source tree"


echo "Compiling newlib-$NEWLIB_V"
cd ../"newlib-$NEWLIB_V"

# Set PATH for newlib to compile using GCC for MIPS N64 (pass 1)
export PATH="$PATH:$INSTALL_PATH/bin" #TODO: why is this export?!
CFLAGS_FOR_TARGET="-DHAVE_ASSERT_FUNC -O2" ./configure \
  --target=mips64-elf \
  --prefix="$INSTALL_PATH" \
  --with-cpu=mips64vr4300 \
  --disable-threads \
  --disable-libssp \
  --disable-werror
make -j "$JOBS"
make install || sudo env PATH="$PATH" make install || su -c "env PATH=\"$PATH\" make install"
echo "Finished Compiling newlib-$NEWLIB_V"


if [ "$BUILD" != "$HOST" ]; then
  INSTALL_PATH="${FOREIGN_INSTALL_PATH}"

  echo "Installing newlib-$NEWLIB_V for foreign host"
  # make install || sudo env PATH="$FOREIGN_INSTALL_PATH/bin" make install || su -c "env PATH=\"$FOREIGN_INSTALL_PATH/bin\" make install"
  make install DESTDIR="$FOREIGN_INSTALL_PATH/mips64-elf" || sudo make install  DESTDIR="$FOREIGN_INSTALL_PATH/mips64-elf" || su -c "make install DESTDIR=\"$FOREIGN_INSTALL_PATH/mips64-elf\""
  make clean


  echo "Compiling binutils-$BINUTILS_V for foreign host"
  cd ../"binutils-$BINUTILS_V"
  ./configure \
    --prefix="$INSTALL_PATH" \
    --target=mips64-elf \
    --with-cpu=mips64vr4300 \
      --disable-werror \
    $BUILD \
    $HOST
  make -j "$JOBS"
  make install-strip || sudo make install-strip || su -c "make install-strip"
  make distclean # Ensure we can build it again (distclean is used as we may use it again for a native target).
  echo "Finished Compiling foreign binutils-$BINUTILS_V"
fi

echo "Compiling gcc-$GCC_V for MIPS N64 for host - (pass 2) outside of the source tree"
cd ..
rm -rf gcc_compile
mkdir gcc_compile
cd gcc_compile
CFLAGS_FOR_TARGET="-O2" CXXFLAGS_FOR_TARGET=" -O2" ../"gcc-$GCC_V"/configure \
  --prefix="$INSTALL_PATH" \
  --target=mips64-elf \
  --with-arch=vr4300 \
  --with-tune=vr4300 \
  --enable-languages=c,c++ \
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
  $BUILD \
  $HOST
make -j "$JOBS"
make install-strip || sudo make install-strip || su -c "make install-strip"
echo "Finished Compiling gcc-$GCC_V for MIPS N64 - (pass 2) outside of the source tree"

if [ "$MAKE_V" != "" ]; then
echo "Compiling make-$MAKE_V" # As make is otherwise not available on Windows
cd ../"make-$MAKE_V"
  ./configure \
    --prefix="$INSTALL_PATH" \
    --disable-largefile \
    --disable-nls \
    --disable-rpath \
    $BUILD \
    $HOST
make -j "$JOBS"
make install-strip || sudo make install-strip || su -c "make install-strip"
make clean
echo "Finished Compiling make-$MAKE_V"
fi

if [ "$BUILD" != "$HOST" ]; then
  echo "Cross compile successful"
else
  echo "Native compile successful"
fi
