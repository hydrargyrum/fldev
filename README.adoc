FLDEV(1)
========

NAME
----
fldev - Mount a disk image to access the contained partitions

SYNOPSIS
--------

*fldev* [-o 'FUSEOPTS'] 'DISKIMAGE' 'MOUNTPOINT'

DESCRIPTION
-----------

FLDev mounts a disk image file 'DISKIMAGE' at 'MOUNTPOINT' to present the partitions contained in 'DISKIMAGE'.
'DISKIMAGE' must contain a partition table.

The partitions are exposed in the same fashion Linux would expose in /dev.

For each partition, fldev will assign a number 'X', and show a file called "'hdaX'" under 'MOUNTPOINT'. For MSDOS format partition tables, primary partitions will be numbered from 1 to 4, and logical partitions will start from 5.

Any read/write access to the partitions files accessible under 'MOUNTPOINT' will be mapped to the corresponding region in the 'DISKIMAGE' file.

File permissions of 'DISKIMAGE' are respected but permissions changes after mouting may not be reflected.
File permissions under 'MOUNTPOINT' can be changed with 'chmod'(1) and will be respected.

OPTIONS
-------

FLDev supports all the standard FUSE options:

*-f*::
  Run in foreground, do not daemonize.

*-d*::
  Run in foreground and print debug information.

*-s*::
  Run single-threaded.

*-o* 'OPTION1'='VALUE1','...'::
  Pass additional options.

FUSE supports a number of parameters to *-o*, see 'fuse'(8) for a description of them.

Additionally, FLDev supports these parameters to *-o*:

*prefix*='NAME'::
  Name the exposed partitions with the prefix 'NAME' instead of "'hda'".


EXAMPLE
-------

Mount the file image.raw' in a 'my_partitions' directory::
  fldev -o prefix=sdc myimage.raw my_partitions

Then, listing the 'my_partitions' dir could give files 'sdc1', 'sdc2'.

LICENSE
-------
Both FLDev and this man page are licensed under the WTFPL.

SEE ALSO
--------
'parted'(1), 'fusermount'(1), 'fuse'(8)
