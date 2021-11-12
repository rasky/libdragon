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

# Exit script on error
set -e

# Ensure you set 'N64_INST' before calling the script to change the default installation directory path
# by default it will presume 'usr/local'
INSTALL_PATH="${N64_INST:-/usr/local}"

# Check for cross compile script flag
if [ "$1" == "-xcw" ]; then # Windows cross compile flag is specified as a parameter.
  # This (probably) requires the toolchain to have already built and installed on the native (linux) system first!
  # This (may) also require the following (extra) package dependencies:
  # sudo apt-get install -yq mingw-w64 libgmp-dev bison libz-mingw-w64-dev autoconf

  echo "Cross compiling for different host"
  # Use the current-directory/binaries for the install path, as this is not for linux!
  rm -rf binaries && mkdir binaries # always ensure the folder is clean (if rebuilding)
  THIS_SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
  FOREIGN_INSTALL_PATH="$THIS_SCRIPT_PATH/binaries"

  # This will require the extra flags (under certain libs)
  BUILD="--build=x86_64-linux-gnu"
  HOST="--host=x86_64-w64-mingw32" 

  # Dependency source libs (Versions)
  # This will have to build Make and download FP libs
  GMP_V=6.2.0
  MPC_V=1.2.1
  MPFR_V=4.1.0
  MAKE_V=4.2.1

  # These "should" be the same as linux, but may be out of sync (as need to ensure working natively first).
  # Binutils fails with 2.37 on canadian cross
  BINUTILS_V=2.36.1 # so we are stuck with 2.36.1 for the moment
  
  # GCC 11.x fails on canadian cross
  # see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=100017
  # see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80196
  GCC_V=10.3.0 # so we are stuck with the 10.x branch for the moment.

else # We are compiling for the native system.
  echo "building for native system"

  # Dependency source libs (Versions)
  BINUTILS_V=2.37
  GCC_V=11.2.0

fi

  NEWLIB_V=4.1.0


# Set PATH for newlib to compile using GCC for MIPS N64 (pass 1)
export PATH="$PATH:$INSTALL_PATH/bin" #TODO: why is this export?!

# Determine how many parallel Make jobs to run based on CPU count
JOBS="${JOBS:-`getconf _NPROCESSORS_ONLN`}"
JOBS="${JOBS:-1}" # If getconf returned nothing, default to 1


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

echo "Stage: Download and extract dependencies"
test -f "binutils-$BINUTILS_V.tar.gz" || download "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_V.tar.gz"
test -d "binutils-$BINUTILS_V"        || tar -xzf "binutils-$BINUTILS_V.tar.gz"

test -f "gcc-$GCC_V.tar.gz"           || download "https://ftp.gnu.org/gnu/gcc/gcc-$GCC_V/gcc-$GCC_V.tar.gz"
test -d "gcc-$GCC_V"                  || tar -xzf "gcc-$GCC_V.tar.gz"

test -f "newlib-$NEWLIB_V.tar.gz"     || download "https://sourceware.org/pub/newlib/newlib-$NEWLIB_V.tar.gz"
test -d "newlib-$NEWLIB_V"            || tar -xzf "newlib-$NEWLIB_V.tar.gz"

# Optional dependency handling
# Copies the FP libs into GCC sources so they are compiled as part of it
if [ "$GMP_V" != "" ]; then
  test -f "gmp-$GMP_V.tar.xz"         || download "https://ftp.gnu.org/gnu/gmp/gmp-$GMP_V.tar.xz"
  test -d "gmp-$GMP_V"                || tar -xf "gmp-$GMP_V.tar.xz" #note no .gz download currently available
  cp -R "gmp-$GMP_V" "gcc-$GCC_V"/gmp #TODO: should be a symbolic link `ln -s` rather than copy!
fi
if [ "$MPC_V" != "" ]; then
  test -f "mpc-$MPC_V.tar.gz"         || download "https://ftp.gnu.org/gnu/mpc/mpc-$MPC_V.tar.gz"
  test -d "mpc-$MPC_V"                || tar -xzf "mpc-$MPC_V.tar.gz"
  cp -R "mpc-$MPC_V" "gcc-$GCC_V"/mpc #TODO: should be a symbolic link `ln -s` rather than copy!
fi
if [ "$MPFR_V" != "" ]; then
  test -f "mpfr-$MPFR_V.tar.gz"       || download "https://ftp.gnu.org/gnu/mpfr/mpfr-$MPFR_V.tar.gz"
  test -d "mpfr-$MPFR_V"              || tar -xzf "mpfr-$MPFR_V.tar.gz"
  cp -R "mpfr-$MPFR_V" "gcc-$GCC_V"/mpfr #TODO: should be a symbolic link `ln -s` rather than copy!
fi
# Certain platforms might require Makefile cross compiling
if [ "$MAKE_V" != "" ]; then
  test -f "make-$MAKE_V.tar.gz"       || download "https://ftp.gnu.org/gnu/make/make-$MAKE_V.tar.gz"
  test -d "make-$MAKE_V"              || tar -xzf "make-$MAKE_V.tar.gz"
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
make install || sudo make install || su -c "make install" # Perhaps use `checkinstall` instead?!
make distclean # Ensure we can build it again
echo "Finished Compiling binutils-$BINUTILS_V"

echo "Compiling native build of GCC-$GCC_V for MIPS N64 - (pass 1) outside of the source tree"
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
make install-gcc || sudo make install-gcc || su -c "make install-gcc"
make install-target-libgcc || sudo make install-target-libgcc || su -c "make install-target-libgcc"
echo "Finished Compiling GCC-$GCC_V for MIPS N64 - (pass 1) outside of the source tree"

echo "Compiling newlib-$NEWLIB_V"
cd ../"newlib-$NEWLIB_V"
CFLAGS_FOR_TARGET="-DHAVE_ASSERT_FUNC -O2" ./configure \
  --target=mips64-elf \
  --prefix="$INSTALL_PATH" \
  --with-cpu=mips64vr4300 \
  --disable-threads \
  --disable-libssp \
  --disable-werror
make -j "$JOBS"
make install || sudo env PATH="$PATH" make install || su -c "env PATH=\"$PATH\" make install" # Perhaps use `checkinstall` instead?!
make distclean # Ensure we can build it again
echo "Finished Compiling newlib-$NEWLIB_V"

if [ "$BUILD" != "$HOST" ]; then
  INSTALL_PATH="${FOREIGN_INSTALL_PATH}"
fi

if [ "$BUILD" != "$HOST" ]; then
  echo "Compiling newlib-$NEWLIB_V for foreign host"
  cd ../"newlib-$NEWLIB_V"
  CFLAGS_FOR_TARGET="-DHAVE_ASSERT_FUNC -O2" ./configure \
    --target=mips64-elf \
    --prefix="$INSTALL_PATH" \
    --with-cpu=mips64vr4300 \
    --disable-threads \
    --disable-libssp \
    --disable-werror \
    $BUILD \
    $HOST
  make -j "$JOBS"
  make install || sudo env PATH="$PATH" make install || su -c "env PATH=\"$PATH\" make install" # Perhaps use `checkinstall` instead?!
  make clean # Ensure we can build it again
  echo "Finished Compiling newlib-$NEWLIB_V"

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
  make install || sudo make install || su -c "make install"
  make distclean # Ensure we can build it again
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
  --with-newlib \
  --disable-libssp \
  --enable-multilib \
  --disable-shared \
  --with-gcc \
  --disable-threads \
  --disable-win32-registry \
  --disable-nls \
  --with-system-zlib \
  $BUILD \
  $HOST
make -j "$JOBS"
make install || sudo make install || su -c "make install"
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
make install || sudo make install || su -c "make install"
echo "Finished Compiling make-$MAKE_V"
fi

if [ "$BUILD" != "$HOST" ]; then
  echo "Cross compile successful"
else
  echo "Native compile successful"
fi
