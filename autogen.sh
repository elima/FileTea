#!/bin/sh

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PROJECT="FileTea"

test "$srcdir" = "." || {
        echo "You must run this script in the top-level directory"
        exit 1
}

AUTORECONF=`which autoreconf`
if test -z $AUTORECONF; then
        echo "*** No autoreconf found ***"
        exit 1
else
        ACLOCAL="${ACLOCAL-aclocal} $ACLOCAL_FLAGS" autoreconf -v --install || exit $?
fi

if test x$NOCONFIGURE = x; then
        ./configure "$@"
else
        echo Skipping configure process.
fi
