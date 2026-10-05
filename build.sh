#!/bin/bash -e
#
# Build for wine >= 8 (PE driver hantek.sys + Unix library hantek.so).
#
# The wine tree must live on a filesystem that supports symlinks: on a
# VirtualBox shared folder (vboxsf) configure falls back to "cp -pR" and
# makedep aborts with "Assertion `cmd->files.count == 1' failed".
# In that case copy the tree to a local disk and run build.sh from there.

WINE_LIBDIR=${WINE_LIBDIR:-/opt/wine-stable/lib/wine}

# The stub Makefile in this directory only exists once configure has seen
# dlls/hantek.sys, so (re)configure if it is missing.
if [ ! -f Makefile ]
then
  ( cd ../../ && ./configure --enable-win64 && make dlls/hantek.sys/all )
fi

make

set -x
sudo cp x86_64-windows/hantek.sys "$WINE_LIBDIR/x86_64-windows/"
sudo cp hantek.so "$WINE_LIBDIR/x86_64-unix/"
cp x86_64-windows/hantek.sys ~/.wine/drive_c/windows/system32/drivers/hantek.sys
#wine sc delete Hantek

#define SERVICE_BOOT_START   0x00000000
#define SERVICE_SYSTEM_START 0x00000001
#define SERVICE_AUTO_START   0x00000002
#define SERVICE_DEMAND_START 0x00000003
#define SERVICE_DISABLED     0x00000004

#define SERVICE_KERNEL_DRIVER      0x00000001
#define SERVICE_FILE_SYSTEM_DRIVER 0x00000002
#define SERVICE_ADAPTER            0x00000004
#define SERVICE_RECOGNIZER_DRIVER  0x00000008

# start: boot = 0 auto = 2  type: kernel = 1 filesys = 2

sleep 1
WINEDEBUG=trace+hantek wine sc create Hantek binPath= C:\\windows\\system32\\drivers\\hantek.sys type= kernel start= auto
