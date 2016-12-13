SQLite + The _Composite_ Component-Based OS
===========================================

This branch contains my incomplete port of SQLite to Composite.

All of my code is in the micro_booter test ([src/components/implementation/tests/micro_booter](https://github.com/mrdude/composite/tree/sqlite/src/components/implementation/tests/micro_booter)).

The port
--------

SQLite abstracts its accesses to the filesystem behind a virtual
filesystem layer. In order to port SQLite to Composite, it is
necessary to supply a suitable VFS implementation. It is also possible
to replace SQLite's memory allocation layer and mutexes.

My implementations for the VFS and memory allocation layers are
in ([micro_booter/os_composite.h](https://github.com/mrdude/composite/blob/sqlite/src/components/implementation/tests/micro_booter/os_composite.h))
and ([micro_booter/composite_sqlite.c](https://github.com/mrdude/composite/blob/sqlite/src/components/implementation/tests/micro_booter/composite_sqlite.c)).
I wrote stub code for a replacement mutex implementation, but I never
wrote the actual implementation; I decided to assume that SQLite would
be used in single-threaded mode.

In general, my implementations try to avoid depending on the C standard library.
If you compile without `-DSQLITE_COS_PROFILE_VFS`, `-DSQLITE_COS_PROFILE_MUTEX`, or `-DSQLITE_COS_PROFILE_MEMORY`,
this port will only use 2 functions from the C standard library (`malloc()`, `free()`).

The SQLite project prefers to distributes its releases as a single, large, "amalgamation" C file.
The code for SQLite release 3.15.2 is in (micro_booter/sqlite3.c).

The Debugging printf()'s
------------------------
Each subsystem implementation (VFS and memory) is instrumented with "printf() profiling" code.
When printf() profiling is enabled, every single API call to that implementation will print
it's parameters and return value.

It was great while I was debugging, but it creates a lot of spam to stdout so it's off by default.

You can enable profiling in the VFS, memory allocation, and mutex subsystems by compiling
with `-DSQLITE_COS_PROFILE_VFS=1`, `-DSQLITE_COS_PROFILE_MUTEX=1`, or `-DSQLITE_COS_PROFILE_MEMORY=1`.

Note that compiling with profiling will pull in quite a few `#include <>`'s.

My VFS Implementation
---------------------

A VFS implementation in SQLite is represented by a filled `sqlite3_vfs` struct.
The VFS implementation used by Composite is defined in
[composite_sqlite.c:1656](https://github.com/mrdude/composite/blob/sqlite/src/components/implementation/tests/micro_booter/composite_sqlite.c#L1656).

The functions used in this VFS implementation are defined starting at
[line 633](https://github.com/mrdude/composite/blob/sqlite/src/components/implementation/tests/micro_booter/composite_sqlite.c#L633).

My VFS implementation is a transient, in-memory filesystem. It uses the memory
allocation subsystem to make allocations.

Files in the filesystem are represented by an instance of `struct fs_file`. A
`fs_file` contains a pointer to the `char[]` holding its file data. This buffer
starts out as 4k in size, but is expanded in calls to `fs_write()` as needed. 

My Memory Allocation Implementation
-----------------------------------

A memory allocation implementation is represented by a filled `sqlite3_mem_methods` struct.
The implmentation used by Composite is defined in
[composite_sqlite.c:1697](https://github.com/mrdude/composite/blob/sqlite/src/components/implementation/tests/micro_booter/composite_sqlite.c#L1697).
The functions used in this implmentation are defined starting at
[line 8](https://github.com/mrdude/composite/blob/sqlite/src/components/implementation/tests/micro_booter/composite_sqlite.c#L8).

The memory implementation is basically just a wrapper around malloc() and free().
Because SQLite needs to be able to query the size of it's memory allocations
(and the C malloc() API annoyingly doesn't give this information), my implementation
adds extra 4 bytes to each allocation, which it uses to store the size of the allocation.

My memory allocation implementation contains the only `#include <>` in the entire port;
simply replacing the memory implementation with a pointer bump allocator (kind of like
the one included in Composite) would mean that this port doesn't rely on the C standard
library at all.

Licensing
---------

This code is licensed under the GPL version 2.0:

```
The Composite Component-Based OS
Copyright (C) 2009 Gabriel Parmer

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
```

This license is not set in stone, and we would be willing to negotiate
on a case-by-case basis for more business-friendly terms.  The license
should not prevent you from using this OS, as alternatives can be
arranged.  It _should_ prevent you from stealing the work and claiming
it as your own.

Support
-------

We'd like to sincerely thank our sponsors.  The _Composite_
Component-Based OS development effort has been supported by grants
from the National Science Foundation (NSF) under awards `CNS 1137973`,
`CNS 1149675`, and `CNS 1117243`.
