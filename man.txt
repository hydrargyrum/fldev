FLDEV(1)
========

NAME
----
fldev - Present partitions from a disk image in a virtual file-system.

SYNOPSIS
--------

*fldev* [-o 'FUSEOPTS'] 'DISKIMAGE' 'MOUNTPOINT'

DESCRIPTION
-----------

FLDev loads a disk image named 'DISKIMAGE' (with a partition table inside)
and presents partitions found in it the same way Linux would do it under its /dev (at least for MSDOS format partition tables).
The 'MOUNTPOINT' has to be an empty directory, and the partitions found in the disk image will be "virtual" files in the 'MOUNTPOINT' directory.

For each partition, fldev will assign a number 'N', and show a file called "'hdaN'" under 'MOUNTPOINT'.
This file will be mapped (for reading and writing) to the corresponding part of 'DISKIMAGE'.

For MSDOS format partition tables, primary partitions will be numbered from 1 to 4, and logical partitions from 5 to above.

EXAMPLE
-------

Mount the file 'myimage' in a 'parts' directory::
  fldev myimage parts



LIMITATIONS
-----------
IMPORTANT: No testing has been made for non-MSDOS format partition tables.


LICENSE
-------
Both FLDev and this man page are licensed under the WTFPL.

SEE ALSO
--------
parted(1), fusermount(1)


/////
2008-05-01
2008-07-29
/////