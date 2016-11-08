#!/bin/sh

if [ -z "$SAT_TARGET_BUILD" ]; then
	echo "SAT_TARGET_BUILD environment variable not set for signing"
fi

if [ -z "$SAT_TARGET_SOURCE" ]; then
	SAT_TARGET_SOURCE=$SAT_TARGET_BUILD
fi

if [ -z "$KERNEL_SRC_DIR" ]; then
	if [ -z "$SAT_PATH_KERNEL" ]; then
		echo "ERROR: kernel path not found!"
		exit 2
	else
		KERNEL_SRC_DIR=$SAT_PATH_KERNEL
	fi
fi

if [ $# -eq 0 ]; then
	VMLINUX=$KERNEL_SRC_DIR/vmlinux
else
	VMLINUX=$2
fi

rm -f sat.ko

make $USE_SPARSECC -C $KERNEL_SRC_DIR M=`pwd` modules

if [ -x "$SAT_TARGET_DEV/scripts/sign-file" ]; then
    SIGN_FILE="$SAT_TARGET_DEV/scripts/sign-file"
elif [ -x "$SAT_TARGET_SOURCE/linux/kernel/scripts/sign-file" ]; then
	SIGN_FILE="$SAT_TARGET_SOURCE/linux/kernel/scripts/sign-file"
elif [ -x "$SAT_TARGET_SOURCE/linux-3.10/scripts/sign-file" ]; then
	SIGN_FILE="$SAT_TARGET_SOURCE/linux-3.10/scripts/sign-file"
elif [ -x "$SAT_TARGET_SOURCE/kernel/gmin/scripts/sign-file" ]; then
	SIGN_FILE="$SAT_TARGET_SOURCE/kernel/gmin/scripts/sign-file"
fi

if [ -x $SIGN_FILE ] &&
   [ -e $KERNEL_SRC_DIR/signing_key.priv ] &&
   [ -e $KERNEL_SRC_DIR/signing_key.x509 ]; then
	$SIGN_FILE sha256 $KERNEL_SRC_DIR/signing_key.priv $KERNEL_SRC_DIR/signing_key.x509 ./sat.ko
else
	echo "WARNING: SAT kernel module not signed!"
	exit 2
fi
