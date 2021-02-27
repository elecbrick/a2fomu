### libmemory.a  --  Library file

libmemory.a is a reduction of the glibc standard library retaining only the
memory copy, set, move and compare functions. This avoids the issue of linking
with libc and having unexpected routines pulled into the resulting image.

Function | Archive Member
-------- | --------------
memcpy   | lib_a-memcpy.o
memset   | lib_a-memset.o
memmov   | lib_a-memmove-stub.o
memcmp   | lib_a-memcmp.o
