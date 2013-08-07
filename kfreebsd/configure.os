#!/bin/sh

OS=`uname -s`
ARCH=`uname -m`

case "$ARCH" in
amd64)
  PWD=`pwd`
  CFLAGS="-O2 -DCAML_NAME_SPACE -DNATIVE_CODE -DSYS_bsd_elf \
    -DTARGET_amd64 -Werror -D_KERNEL -DKLD_MODULE -nostdinc \
    -I${PWD}/runtime/include -I${PWD}/runtime/kernel/@ \
    -I${PWD}/runtime/kernel/@/contrib/altq -I${PWD}/runtime/kernel \
    -finline-limit=8000 --param inline-unit-growth=100 \
    --param large-function-growth=1000 -fno-common \
    -fno-omit-frame-pointer -mcmodel=kernel -mno-red-zone -mno-mmx \
    -msoft-float -fno-asynchronous-unwind-tables -ffreestanding \
    -fstack-protector -std=iso9899:1999 -fstack-protector -Wall \
    -Wredundant-decls -Wnested-externs -Wstrict-prototypes \
    -Wmissing-prototypes -Wpointer-arith -Winline -Wcast-qual \
    -Wundef -Wno-pointer-sign -fformat-extensions \
    -Wmissing-include-dirs -fdiagnostics-show-option -mno-sse \
    -DKDTRACE_HOOKS"
  ;;
*)
  echo $ARCH is not supported.
  exit 1
esac

case "$OS" in
FreeBSD)
  ;;
*)
  echo $OS is not supported.
  exit 1
esac
