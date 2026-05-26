#
# Machine dependent definitions for the loongarch64 architecture.
#

# Long double is quad precision
GDTOASRCS+=strtorQ.c
SRCS+=machdep_ldisQ.c
SYM_MAPS+=${LIBC_SRCTOP}/${LIBC_ARCH}/Symbol.map
