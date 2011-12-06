dnl $Id$
dnl config.m4 for extension btstore

AC_ARG_WITH(btstore-shm, 
	[  --with-btstore-shm           Enable btstore share memory support],
	[  btstore_shm=yes],
	[  btstore_shm=no]
)

PHP_ARG_ENABLE(btstore, whether to enable btstore support,
	[  --enable-btstore           Enable btstore support])

if test "$PHP_BTSTORE" != "no"; then
	PHP_NEW_EXTENSION(btstore, btstore.c, $ext_shared)
	if test "$btstore_shm" = "yes"; then
		AC_DEFINE(BTSTORE_SHM, 1, [Define if you like to use share memory])
	fi
fi
