#ifndef PHP_BTSTORE_H
#define PHP_BTSTORE_H

extern zend_module_entry btstore_module_entry;
#define phpext_btstore_ptr &btstore_module_entry

#ifdef PHP_WIN32
#define PHP_BTSTORE_API __declspec(dllexport)
#else
#define PHP_BTSTORE_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

PHP_MINIT_FUNCTION( btstore);
PHP_MSHUTDOWN_FUNCTION( btstore);
PHP_RINIT_FUNCTION( btstore);
PHP_RSHUTDOWN_FUNCTION( btstore);
PHP_MINFO_FUNCTION( btstore);

PHP_FUNCTION( btstore_get); /* For testing, remove later. */
PHP_FUNCTION( btstore_reload); /* For testing, remove later. */
PHP_METHOD( btstore_element, toArray);
/* 
 Declare any global variables you may need between the BEGIN
 and END macros here:

 ZEND_BEGIN_MODULE_GLOBALS(btstore)
 long  global_value;
 char *global_string;
 ZEND_END_MODULE_GLOBALS(btstore)
 */

/* In every utility function you add that needs to use variables 
 in php_btstore_globals, call TSRMLS_FETCH(); after declaring other
 variables used by that function, or better yet, pass in TSRMLS_CC
 after the last function argument and declare your utility function
 with TSRMLS_DC after the last declared argument.  Always refer to
 the globals in your function as BTSTORE_G(variable).  You are
 encouraged to rename these macros something shorter, see
 examples in any other php module directory.
 */

#ifdef ZTS
#define BTSTORE_G(v) TSRMG(btstore_globals_id, zend_btstore_globals *, v)
#else
#define BTSTORE_G(v) (btstore_globals.v)
#endif

#endif	/* PHP_BTSTORE_H */

