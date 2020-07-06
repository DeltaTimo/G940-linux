#!/bin/sh

if test "x$1" = "x"; then
	echo "Target not specified, use 'modules', 'modules_install' or 'clean'"
	exit 1
fi

KVERSION=`uname -r`

make -C /lib/modules/${KVERSION}/build M=`pwd` $1
