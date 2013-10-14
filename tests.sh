#!/bin/sh

# build image
dd if=/dev/zero of=test.dev bs=1024 count=10240
/sbin/parted test.dev mklabel msdos
/sbin/parted test.dev mkpart primary 1MiB 2MiB
/sbin/parted test.dev mkpart extended 2MiB 100%
/sbin/parted test.dev mkpart logical 3MiB 4MiB
mkdir mp

error=0
compare () {
	if [ "$1" != "$2" ]
	then
		error=1
		echo "error: $3" >&2
	fi
}
# mount and test
FLDEV=./fldev
$FLDEV test.dev mp
compare "`ls mp | wc -l`" 3 "There should be 3 files"
compare "`md5sum mp/hda1 | awk '{print $1}'`" b6d81b360a5672d80c27430f39153e2c "hda1 has different content"
compare "`md5sum mp/hda5 | awk '{print $1}'`" b6d81b360a5672d80c27430f39153e2c "hda5 has different content"
fusermount -u mp

# cleanup
rmdir mp
rm test.dev

# epilogue
if [ $error -ne 0 ]
then
	echo Failed.
	exit 1
fi
