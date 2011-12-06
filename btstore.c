#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "zend.h"
#include "ext/standard/info.h"
#include "ext/standard/php_var.h"
#include "php_btstore.h"
#include <sys/ipc.h>
#include <sys/shm.h>
#include <SAPI.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <Zend/zend_interfaces.h>

/* If you declare any globals in php_btstore.h uncomment this:
 ZEND_DECLARE_MODULE_GLOBALS(btstore)
 */

/* True global resources - no need for thread safety here */
static HashTable* gData;
static char* gKey;
static int gShmid;
static void* gStart;
static int gOffset;

#define PROJ_KEY 138
#define START_ADDRESS ((const void*)0x2c0000000000)

/* {{{ btstore_functions[]
 *
 * Every user visible function must have an entry in btstore_functions[].
 */
zend_function_entry btstore_functions[] = { //函数定义
        PHP_FE(btstore_get, NULL) /* For testing, remove later. */
PHP_FE        (btstore_reload, NULL) /* 重新加载内存 */
        {	NULL, NULL, NULL} /* Must be the last line in btstore_functions[] */
    };
/* }}} */

/* {{{ btstore_module_entry
 */
zend_module_entry btstore_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
        STANDARD_MODULE_HEADER,
#endif
"btstore"        , /* 模块名 */
        btstore_functions, /* 所有自己定义的函数 */
        PHP_MINIT(btstore), /* 模块启动函数 */
        PHP_MSHUTDOWN(btstore), /* 模块关闭函数 */
        PHP_RINIT(btstore), /* Replace with NULL if there's nothing to do at request start */
        PHP_RSHUTDOWN(btstore), /* Replace with NULL if there's nothing to do at request end */
        PHP_MINFO(btstore),
#if ZEND_MODULE_API_NO >= 20010901
        "0.1", /* Replace with version number for your extension */
#endif
        STANDARD_MODULE_PROPERTIES};
/* }}} */

#ifdef COMPILE_DL_BTSTORE
ZEND_GET_MODULE(btstore)
#endif

/* {{{ PHP_INI
 */
PHP_INI_BEGIN() PHP_INI_ENTRY("btstore.dir", "/home/hoping/rpcfw/data", PHP_INI_ALL, NULL)
#ifdef BTSTORE_SHM
        PHP_INI_ENTRY("btstore.size", "102400", PHP_INI_ALL, NULL)
        PHP_INI_ENTRY("btstore.file", "/tmp/test", PHP_INI_ALL, NULL)
#endif
PHP_INI_END        ()

/* }}} */

/* {{{ php_btstore_init_globals
 */
/* Uncomment this function if you have INI entries
 static void php_btstore_init_globals(zend_btstore_globals *btstore_globals)
 {
 btstore_globals->global_value = 0;
 btstore_globals->global_string = NULL;
 }
 */
/* }}} */

#ifdef BTSTORE_SHM
#define PALLOC_ZVAL(value) (value = (zval*)btstore_malloc(sizeof(zval)))
#else
#define btstore_malloc(size) pecalloc(1, (size), 1)
#define PALLOC_ZVAL(value) (value = (zval*)btstore_malloc(sizeof(zval)))
#endif

#define CONNECT_TO_BUCKET_DLLIST(element, list_head)        \
    (element)->pNext = (list_head);                         \
    (element)->pLast = NULL;                                \
    if ((element)->pNext) {                                 \
        (element)->pNext->pLast = (element);                \
    }

#define CONNECT_TO_GLOBAL_DLLIST(element, ht)               \
    (element)->pListLast = (ht)->pListTail;                 \
    (ht)->pListTail = (element);                            \
    (element)->pListNext = NULL;                            \
    if ((element)->pListLast != NULL) {                     \
        (element)->pListLast->pListNext = (element);        \
    }                                                       \
    if (!(ht)->pListHead) {                                 \
        (ht)->pListHead = (element);                        \
    }                                                       \
    if ((ht)->pInternalPointer == NULL) {                   \
        (ht)->pInternalPointer = (element);                 \
    }

#define INIT_DATA(ht, p, pData, nDataSize);                             \
    if (nDataSize == sizeof(void*)) {                                   \
        memcpy(&(p)->pDataPtr, pData, sizeof(void *));                  \
        (p)->pData = &(p)->pDataPtr;                                    \
    } else {                                                            \
        (p)->pData = (void *) btstore_malloc(nDataSize);                \
        if (!(p)->pData) {                                              \
            return FAILURE;                                             \
        }                                                               \
        memcpy((p)->pData, pData, nDataSize);                           \
        (p)->pDataPtr=NULL;                                             \
    }

#ifdef BTSTORE_SHM
static int btstore_init_shm(int shmflag)
{

	gKey = INI_STR("btstore.file");
	FILE * file = fopen(gKey, "a+");
	if (NULL == file)
	{
		zend_error(E_ERROR, "open file %s failed", gKey);
		fclose(file);
		return FAILURE;
	}
	fclose(file);

	key_t key = ftok(gKey, PROJ_KEY);
	if (-1 == key)
	{
		zend_error(E_ERROR, "ftok key failed for file:%s, key:%d", gKey, PROJ_KEY);
		return FAILURE;
	}

	gShmid = shmget(key, INI_INT("btstore.size"), shmflag);
	if (gShmid == -1)
	{
		zend_error(E_ERROR, "shmget failed:%s, file:%s", strerror(errno), gKey);
		return FAILURE;
	}

	if(shmflag & S_IWUSR)
	{
		gStart = shmat(gShmid, START_ADDRESS, 0);
	}
	else
	{
		gStart = shmat(gShmid, START_ADDRESS, SHM_RDONLY);
	}

	if (((void*) -1) == gStart)
	{
		zend_error(E_ERROR, "shmat failed:%s", strerror(errno));
		return FAILURE;
	}

	if (shmflag & IPC_CREAT)
	{
		memset(gStart, 0, INI_INT("btstore.size"));
	}
	return SUCCESS;
}

static void* btstore_malloc(int size)
{
	if (gOffset + size > INI_INT("btstore.size"))
	{
		zend_error(E_ERROR, "btstore malloc size:%d, expected %d", gOffset, size);
		return NULL;
	}
	void* ret = gStart + gOffset;
	gOffset += size;
	return ret;
}
#endif

static int btstore_hash_init(HashTable *ht, uint nSize, hash_func_t pHashFunction,
        dtor_func_t pDestructor)
{
	uint i = 3;
	Bucket **tmp;

	if (nSize >= 0x80000000)
	{
		/* prevent overflow */
		ht->nTableSize = 0x80000000;
	}
	else
	{
		while ((1U << i) < nSize)
		{
			i++;
		}
		ht->nTableSize = 1 << i;
	}

	ht->nTableMask = ht->nTableSize - 1;
	ht->pDestructor = pDestructor;
	ht->arBuckets = NULL;
	ht->pListHead = NULL;
	ht->pListTail = NULL;
	ht->nNumOfElements = 0;
	ht->nNextFreeElement = 0;
	ht->pInternalPointer = NULL;
	ht->persistent = 1;
	ht->nApplyCount = 0;
#if Z_DEBUG
	ht->inconsistent = HT_OK;
#endif

	tmp = (Bucket **) btstore_malloc(ht->nTableSize * sizeof(Bucket *));
	if (!tmp)
	{
		return FAILURE;
	}
	ht->arBuckets = tmp;

	return SUCCESS;
}

static int btstore_hash_index_insert(HashTable *ht, ulong h, void *pData, uint nDataSize,
        void **pDest)
{
	uint nIndex;
	Bucket *p;

	nIndex = h & ht->nTableMask;

	p = ht->arBuckets[nIndex];
	while (p != NULL)
	{
		if ((p->nKeyLength == 0) && (p->h == h))
		{
			zend_error(E_ERROR, "index already exists");
			return FAILURE;
		}
		p = p->pNext;
	}
	p = (Bucket *) btstore_malloc(sizeof(Bucket) - 1);
	if (!p)
	{
		return FAILURE;
	}
	p->nKeyLength = 0; /* Numeric indices are marked by making the nKeyLength == 0 */
	p->h = h;
	INIT_DATA(ht, p, pData, nDataSize);
	if (pDest)
	{
		*pDest = p->pData;
	}

	CONNECT_TO_BUCKET_DLLIST(p, ht->arBuckets[nIndex]);

	HANDLE_BLOCK_INTERRUPTIONS();
	ht->arBuckets[nIndex] = p;
	CONNECT_TO_GLOBAL_DLLIST(p, ht);
	HANDLE_UNBLOCK_INTERRUPTIONS();
	if ((long) h >= (long) ht->nNextFreeElement)
	{
		ht->nNextFreeElement = h < LONG_MAX ? h + 1 : LONG_MAX;
	}
	ht->nNumOfElements++;
	if (ht->nNumOfElements > ht->nTableSize)
	{
		zend_error(E_ERROR, "more elements, need resize");
		return FAILURE;
	}
	return SUCCESS;
}

static int btstore_hash_quick_add(HashTable *ht, char *arKey, uint nKeyLength, ulong h,
        void *pData, uint nDataSize, void **pDest)
{
	uint nIndex;
	Bucket *p;

	if (nKeyLength == 0)
	{
		return btstore_hash_index_insert(ht, h, pData, nDataSize, pDest);
	}

	nIndex = h & ht->nTableMask;

	p = ht->arBuckets[nIndex];
	while (p != NULL)
	{
		if ((p->h == h) && (p->nKeyLength == nKeyLength))
		{
			if (!memcmp(p->arKey, arKey, nKeyLength))
			{
				zend_error(E_ERROR, "key:%s already exists", arKey);
				return FAILURE;
			}
		}
		p = p->pNext;
	}

	p = (Bucket *) btstore_malloc(sizeof(Bucket) - 1 + nKeyLength);
	if (!p)
	{
		return FAILURE;
	}

	memcpy(p->arKey, arKey, nKeyLength);
	p->nKeyLength = nKeyLength;
	INIT_DATA(ht, p, pData, nDataSize);
	p->h = h;
	CONNECT_TO_BUCKET_DLLIST(p, ht->arBuckets[nIndex]);

	if (pDest)
	{
		*pDest = p->pData;
	}

	HANDLE_BLOCK_INTERRUPTIONS();
	ht->arBuckets[nIndex] = p;
	CONNECT_TO_GLOBAL_DLLIST(p, ht);
	HANDLE_UNBLOCK_INTERRUPTIONS();

	ht->nNumOfElements++;
	if (ht->nNumOfElements > ht->nTableSize)
	{
		zend_error(E_ERROR, "more elements, need resize");
		return FAILURE;
	}
	return SUCCESS;
}

typedef int (*btstore_copy_func_t)(void*);

static int btstore_hash_copy(HashTable *target, HashTable *source,
        btstore_copy_func_t pCopyConstructor, void *tmp, uint size)
{
	Bucket *p;
	void *new_entry;
	zend_bool setTargetPointer;

	setTargetPointer = !target->pInternalPointer;
	p = source->pListHead;
	while (p)
	{
		if (setTargetPointer && source->pInternalPointer == p)
		{
			target->pInternalPointer = NULL;
		}
		if (p->nKeyLength)
		{
			if (FAILURE == btstore_hash_quick_add(target, p->arKey, p->nKeyLength, p->h, p->pData,
			        size, &new_entry))
			{
				return FAILURE;
			}
		}
		else
		{
			if (FAILURE == btstore_hash_index_insert(target, p->h, p->pData, size, &new_entry))
			{
				return FAILURE;
			}
		}
		if (pCopyConstructor)
		{
			if (FAILURE == pCopyConstructor(new_entry))
			{
				return FAILURE;
			}
		}
		p = p->pListNext;
	}
	if (!target->pInternalPointer)
	{
		target->pInternalPointer = target->pListHead;
	}
	return SUCCESS;
}

static int btstore_deep_copy(zval** p)
{
	zval* orig = *p;
	switch (Z_TYPE_P(orig))
	{
		case IS_STRING:
			PALLOC_ZVAL(*p);
			**p = *orig;
			Z_SET_REFCOUNT_PP(p, 0);
			Z_UNSET_ISREF_PP(p);
			(*p)->value.str.len = orig->value.str.len;
			(*p)->value.str.val = (char*) btstore_malloc(orig->value.str.len + 1);
			memcpy((*p)->value.str.val, orig->value.str.val, orig->value.str.len);
			(*p)->value.str.val[orig->value.str.len] = 0;
			break;
		case IS_ARRAY:
			PALLOC_ZVAL(*p);
			**p = *orig;
			Z_SET_REFCOUNT_PP(p, 0);
			Z_UNSET_ISREF_PP(p);
			(*p)->value.ht = (HashTable*) btstore_malloc(sizeof(HashTable));
			btstore_hash_init((*p)->value.ht, orig->value.ht->nTableSize, NULL, NULL);
			btstore_hash_copy((*p)->value.ht, orig->value.ht,
			        (btstore_copy_func_t) btstore_deep_copy, NULL, sizeof(zval*));
			break;
		case IS_DOUBLE:
		case IS_LONG:
		case IS_NULL:
		case IS_BOOL:
			PALLOC_ZVAL(*p);
			**p = *orig;
			Z_SET_REFCOUNT_PP(p, 0);
			Z_UNSET_ISREF_PP(p);
			break;
		default:
			zend_error(E_ERROR, "unsupported type:%d", Z_TYPE_P(orig));
			return FAILURE;
	}
	return SUCCESS;
}
static zval* btstore_unserialize(const char* filename)
{

	struct stat statBuf;
	if (stat(filename, &statBuf))
	{
		zend_error(E_ERROR, "can't stat file %s", filename);
		return NULL;
	}

	FILE* file = fopen(filename, "r");
	if (NULL == file)
	{
		zend_error(E_ERROR, "can't open file %s", filename);
		return NULL;
	}

	char* buffer = (char*) malloc(statBuf.st_size);
	if (NULL == buffer)
	{
		zend_error(E_ERROR, "not enough memory for file buffer, alloc failed");
		fclose(file);
		return NULL;
	}

	int length = fread(buffer, 1, statBuf.st_size, file);
	if (length != statBuf.st_size)
	{
		zend_error(E_ERROR, "read file %s failed, expected %d, read %d", filename, statBuf.st_size,
		        length);
		free(buffer);
		return NULL;
	}

	fclose(file);

	zval* src;
	MAKE_STD_ZVAL(src);
	php_unserialize_data_t data;
	PHP_VAR_UNSERIALIZE_INIT(data);
	const unsigned char* p = (const unsigned char*) buffer;
	if (!php_var_unserialize(&src, &p, p + statBuf.st_size, &data TSRMLS_CC))
	{
		zend_error(E_ERROR, "unserialize file %s failed", filename);
		zend_error(E_ERROR, "Error at offset %ld of %d bytes", (long) ((char*) p - buffer),
		        statBuf.st_size);
		free(buffer);
		zval_ptr_dtor(&src);
		PHP_VAR_UNSERIALIZE_DESTROY(data);
		return NULL;
	}
	PHP_VAR_UNSERIALIZE_DESTROY(data);
	free(buffer);
	return src;
}

static int php_btstore_init_file(const char* dirname, const char* aFilename, HashTable* gData)
{
	char filename[256];
	memcpy(filename, dirname, strlen(dirname));
	filename[strlen(dirname)] = '/';
	memcpy(filename + strlen(dirname) + 1, aFilename, strlen(aFilename) + 1);

	struct stat dir_stat;
	if (0 != stat(filename, &dir_stat))
	{
		zend_error(E_ERROR, "stat file:%s failed", filename);
		return FAILURE;
	}

	if (!S_ISREG(dir_stat.st_mode))
	{
		//zend_error(E_USER_NOTICE, "file:%s is not a regular file", filename);
		return SUCCESS;
	}

	zval* src = btstore_unserialize(filename);
	if (NULL == src)
	{
		return FAILURE;
	}

	if (src->type != IS_ARRAY)
	{
		zend_error(E_ERROR, "top element must be an array");
		zval_ptr_dtor(&src);
		return FAILURE;
	}

	zval* dst;
	PALLOC_ZVAL(dst);
	INIT_PZVAL(dst);
	dst->type = IS_ARRAY;
	dst->value.ht = (HashTable*) btstore_malloc(sizeof(HashTable));
	Z_SET_REFCOUNT_P(dst, 0);
	Z_UNSET_ISREF_P(dst);
	if (NULL == dst->value.ht)
	{
		zend_error(E_ERROR, "not enough memory, for dst ht, alloc failed");
		zval_ptr_dtor(&src);
		zval_ptr_dtor(&dst);
		return FAILURE;
	}

	if (FAILURE == btstore_hash_init(dst->value.ht, src->value.ht->nTableSize, NULL, NULL))
	{
		zval_ptr_dtor(&src);
		zval_ptr_dtor(&dst);
		return FAILURE;
	}

	if (FAILURE == btstore_hash_copy(dst->value.ht, src->value.ht,
	        (btstore_copy_func_t) btstore_deep_copy, NULL, sizeof(zval*)))
	{
		zval_ptr_dtor(&src);
		zval_ptr_dtor(&dst);
		return FAILURE;
	}
	zval_ptr_dtor(&src);

	ulong h = zend_inline_hash_func((char*) aFilename, strlen(aFilename) + 1);
	return btstore_hash_quick_add(gData, (char*) aFilename, strlen(aFilename) + 1, h,
	        (void *) &dst, sizeof(zval *), NULL);
}

static int get_file_num(const char* dirname)
{
	DIR* dir = opendir(dirname);
	if (NULL == dir)
	{
		zend_error(E_ERROR, "can't open dir %s", dirname);
		return FAILURE;
	}
	int num = 0;
	struct dirent* file;
	unsigned int fd = dirfd(dir);
	while (file = readdir(dir))
	{
		if (!strcmp(file->d_name, ".") || !strcmp(file->d_name, ".."))
		{
			continue;
		}
		num++;
	}
	return num;
}

static int php_btstore_init(const char* dirname)
{


#ifdef BTSTORE_SHM
	if (FAILURE == btstore_init_shm(IPC_CREAT | S_IRUSR | S_IWUSR))
	{
		return FAILURE;
	}
#endif

	DIR* dir = opendir(dirname);
	if (NULL == dir)
	{
		zend_error(E_ERROR, "can't open dir %s", dirname);
		return FAILURE;
	}

	gData = (HashTable*) btstore_malloc(sizeof(HashTable));
	if (NULL == gData)
	{
		zend_error(E_ERROR, "not enough memory for gData");
		return FAILURE;
	}

	if (FAILURE == btstore_hash_init(gData, get_file_num(dirname), NULL, NULL))
	{
		return FAILURE;
	}

	struct dirent* file;
	unsigned int fd = dirfd(dir);
	while (file = readdir(dir))
	{
		if (!strcmp(file->d_name, ".") || !strcmp(file->d_name, ".."))
		{
			continue;
		}
		if (FAILURE == php_btstore_init_file(dirname, file->d_name, gData))
		{
			closedir(dir);
			return FAILURE;
		}
	}

	return SUCCESS;
}

zend_class_entry* btstore_ce;

static zend_object_handlers btstore_handlers;

static zend_function_entry btstore_class_functions[] = {
        PHP_ME(btstore_element, toArray, NULL, ZEND_ACC_PUBLIC)//将当前的object拷贝成array
{        NULL, NULL, NULL}};

typedef struct _btstore_object
{
	zend_object std;
	HashTable* value;
} btstore_object;

static btstore_object* btstore_object_init(zend_class_entry* ce)
{
	btstore_object *intern;
	intern = ecalloc(1, sizeof(btstore_object));
	zend_object_std_init(&(intern->std), ce TSRMLS_CC);
	zend_hash_copy(intern->std.properties, &ce->default_properties,
	        (copy_ctor_func_t) zval_add_ref, NULL, sizeof(zval *));
	return intern;
}

static void btstore_object_dtor(void *object, zend_object_handle handle TSRMLS_DC)
{
	btstore_object *intern = (btstore_object *) object;
	zend_object_std_dtor(&(intern->std) TSRMLS_CC);
	efree(object);
}
static zend_object_value btstore_object_register(btstore_object* intern)
{
	zend_object_value retval;
	retval.handle = zend_objects_store_put(intern, btstore_object_dtor, NULL, NULL TSRMLS_CC);
	retval.handlers = &btstore_handlers;
	return retval;
}

static zend_object_value btstore_object_new(zend_class_entry *ce TSRMLS_DC)
{
	btstore_object *intern = btstore_object_init(ce);
	return btstore_object_register(intern);
}

static zval* btstore_zval_copy(zval* return_value, zval* zvalue)
{
	btstore_object* intern;
	return_value->type = zvalue->type;
	switch (zvalue->type)
	{
		case IS_NULL:
		case IS_LONG:
		case IS_DOUBLE:
		case IS_BOOL:
			return_value->value = zvalue->value;
			break;
		case IS_STRING:
			return_value->value.str.val = estrndup(zvalue->value.str.val, zvalue->value.str.len);
			return_value->value.str.len = zvalue->value.str.len;
			break;
		case IS_ARRAY:
			intern = btstore_object_init(btstore_ce);
			intern->value = zvalue->value.ht;
			return_value->value.obj = btstore_object_register(intern);
			return_value->type = IS_OBJECT;
			break;
		default:
			zend_error(E_ERROR, "unsupported type %d in BtstoreElement", zvalue->type);
	}
}
static zval* btstore_object_read_property(zval *object, zval *member, int type TSRMLS_DC)
{
	if (member->type != IS_STRING)
	{
		zend_error(E_ERROR, "invalid type:%d, only string allowed", member->type);
		return member;
	}
	btstore_object* intern = (btstore_object*) zend_object_store_get_object(object TSRMLS_CC);
	zval *return_value, **zvalue;
	MAKE_STD_ZVAL(return_value);
	ZVAL_NULL(return_value);
	if (FAILURE == zend_symtable_find(intern->value, member->value.str.val, member->value.str.len
	        + 1, (void**) &zvalue))
	{
		zend_error(E_NOTICE, "undefined key:%s, size:%d in BtstoreElement", member->value.str.val,
		        member->value.str.len);
		RETURN_NULL()
	}
	else
	{
		btstore_zval_copy(return_value, *zvalue);
		Z_SET_REFCOUNT_P(return_value, 0);
		Z_UNSET_ISREF_P(return_value);
		return return_value;
	}
}

static zval* btstore_object_read_dimension(zval *object, zval *offset, int type TSRMLS_DC)
{
	long index = 0;
	switch (offset->type)
	{
		case IS_STRING:
			return btstore_object_read_property(object, offset, type TSRMLS_CC);
		case IS_LONG:
			index = offset->value.lval;
			break;
		case IS_DOUBLE:
			index = (long) offset->value.dval;
			break;
		default:
			zend_error(E_ERROR, "unsupported key type:%d", offset->type);
			return offset;
	}
	btstore_object* intern = (btstore_object*) zend_object_store_get_object(object TSRMLS_CC);
	zval *return_value, **zvalue;
	MAKE_STD_ZVAL(return_value);
	if (FAILURE == zend_hash_index_find(intern->value, index, (void**) &zvalue))
	{
		zend_error(E_NOTICE, "undefined index:%ld in BtstoreElement", offset->value.lval);
		RETURN_NULL()
	}
	else
	{
		btstore_zval_copy(return_value, *zvalue);
		Z_SET_REFCOUNT_P(return_value, 0);
		Z_UNSET_ISREF_P(return_value);
		return return_value;
	}
}

static int btstore_object_has_property(zval *object, zval *member, int has_set_exists TSRMLS_DC)
{
	if (member->type != IS_STRING)
	{
		zend_error(E_ERROR, "invalid type:%d, only string allowed", member->type);
		return 0;
	}
	btstore_object* intern = (btstore_object*) zend_object_store_get_object(object TSRMLS_CC);
	return zend_symtable_exists(intern->value, member->value.str.val, member->value.str.len + 1);
}

static HashTable * btstore_object_get_properties(zval *object TSRMLS_DC)
{
	btstore_object* intern = (btstore_object*) zend_object_store_get_object(object TSRMLS_CC);
	return intern->value;
}

static int btstore_object_has_dimension(zval *object, zval *member, int check_empty TSRMLS_DC)
{
	long index = 0;
	switch (member->type)
	{
		case IS_STRING:
			return btstore_object_has_property(object, member, check_empty TSRMLS_CC);
		case IS_LONG:
			index = member->value.lval;
			break;
		case IS_DOUBLE:
			index = (long) member->value.dval;
			break;
		default:
			zend_error(E_ERROR, "unsupported key type:%d", member->type);
			return 0;
	}
	btstore_object* intern = (btstore_object*) zend_object_store_get_object(object TSRMLS_CC);
	return zend_hash_index_exists(intern->value, index);
}

static int btstore_object_count_elements(zval *object, long *count TSRMLS_DC)
{
	btstore_object* intern = (btstore_object*) zend_object_store_get_object(object TSRMLS_CC);
	*count = intern->value->nNumOfElements;
	return SUCCESS;
}

typedef struct
{
	zend_object_iterator intern;
	HashTable* root;
	zval *current;
} btstore_iterator;

static void btstore_iterator_dtor(zend_object_iterator *iter TSRMLS_DC);
static int btstore_iterator_valid(zend_object_iterator *iter TSRMLS_DC);
static void btstore_iterator_current_data(zend_object_iterator *iter, zval ***data TSRMLS_DC);
static int btstore_iterator_current_key(zend_object_iterator *iter, char **str_key,
        uint *str_key_len, ulong *int_key TSRMLS_DC);
static void btstore_iterator_move_forward(zend_object_iterator *iter TSRMLS_DC);
static void btstore_iterator_rewind(zend_object_iterator *iter TSRMLS_DC);
static void btstore_iterator_current(btstore_iterator *iterator)
{
	zval *return_value, **value;
	if (iterator->current)
	{
		zval_ptr_dtor(&iterator->current);
	}
	if (SUCCESS == zend_hash_get_current_data(iterator->root, (void**)&value))
	{
		MAKE_STD_ZVAL(return_value)
		btstore_zval_copy(return_value, *value);
		iterator->current = return_value;
	}
	else
	{
		iterator->current = NULL;
	}
}

zend_object_iterator_funcs btstore_iterator_funcs = { btstore_iterator_dtor,//销毁
        btstore_iterator_valid,//检查
        btstore_iterator_current_data,//当前数据
        btstore_iterator_current_key,//当前key
        btstore_iterator_move_forward,//前进
        btstore_iterator_rewind,//重新开始
        NULL /* invalidate current */
}; /* }}} */

static zend_object_iterator *btstore_object_get_iterator(zend_class_entry *ce, zval *object,
        int by_ref TSRMLS_DC)
{
	btstore_iterator *iterator = emalloc(sizeof(btstore_iterator));
	if (by_ref)
	{
		zend_error(E_ERROR, "Iterator invalid in foreach by ref");
	}
	Z_ADDREF_P(object);
	btstore_object *bobject = (btstore_object*) zend_object_store_get_object(object TSRMLS_CC);
	iterator->intern.data = (void*) object;
	iterator->intern.funcs = &btstore_iterator_funcs;
	iterator->root = bobject->value;
	iterator->current = NULL;
	return (zend_object_iterator*) iterator;
}

static void btstore_init_object()
{
	zend_class_entry ce;
	INIT_CLASS_ENTRY(ce, "BtstoreElement", btstore_class_functions);
	btstore_ce = zend_register_internal_class(&ce TSRMLS_CC);
	btstore_ce->create_object = btstore_object_new;
	btstore_ce->get_iterator = btstore_object_get_iterator;
	btstore_ce->iterator_funcs.funcs = &btstore_iterator_funcs;

	memcpy(&btstore_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	btstore_handlers.read_dimension = btstore_object_read_dimension;
	btstore_handlers.read_property = btstore_object_read_property;
	btstore_handlers.has_dimension = btstore_object_has_dimension;
	btstore_handlers.has_property = btstore_object_has_property;
	btstore_handlers.get_properties = btstore_object_get_properties;
	btstore_handlers.count_elements = btstore_object_count_elements;

	zend_class_implements(btstore_ce TSRMLS_CC, 1, zend_ce_traversable);
	btstore_ce->ce_flags |= ZEND_ACC_FINAL_CLASS;
}

/* release all resources associated with this iterator instance */
static void btstore_iterator_dtor(zend_object_iterator *iter TSRMLS_DC)
{
	btstore_iterator *iterator = (btstore_iterator *) iter;
	if (iterator->current)
	{
		zval_ptr_dtor(&iterator->current);
	}
	zval *intern = (zval*) iterator->intern.data;
	if (intern)
	{
		zval_ptr_dtor(&intern);
	}
	efree(iterator);
}

/* check for end of iteration (FAILURE or SUCCESS if data is valid) */
static int btstore_iterator_valid(zend_object_iterator *iter TSRMLS_DC)
{
	btstore_iterator *iterator = (btstore_iterator *) iter;
	return iterator->current ? SUCCESS : FAILURE;
}

/* fetch the item data for the current element */
static void btstore_iterator_current_data(zend_object_iterator *iter, zval ***data TSRMLS_DC)
{
	btstore_iterator *iterator = (btstore_iterator *) iter;
	*data = &iterator->current;
}

/* fetch the key for the current element (return HASH_KEY_IS_STRING or HASH_KEY_IS_LONG) (optional, may be NULL) */
static int btstore_iterator_current_key(zend_object_iterator *iter, char **str_key,
        uint *str_key_len, ulong *int_key TSRMLS_DC)
{
	btstore_iterator *iterator = (btstore_iterator *) iter;
	return zend_hash_get_current_key_ex(iterator->root, str_key, str_key_len, int_key, 1, NULL);
}

/* step forwards to next element */
static void btstore_iterator_move_forward(zend_object_iterator *iter TSRMLS_DC)
{
	btstore_iterator *iterator = (btstore_iterator *) iter;
	zend_hash_move_forward(iterator->root);
	btstore_iterator_current(iterator);
}

/* rewind to start of data (optional, may be NULL) */
static void btstore_iterator_rewind(zend_object_iterator *iter TSRMLS_DC)
{
	btstore_iterator *iterator = (btstore_iterator *) iter;
	zend_hash_internal_pointer_reset(iterator->root);
	btstore_iterator_current(iterator);
}

static void btstore_deep_copy_emalloc(zval** p)
{
	zval* orig = *p;
	zend_uchar type = Z_TYPE_P(orig);
	switch (type)
	{
		case IS_STRING:
			MAKE_STD_ZVAL(*p)
			(*p)->type = type;
			(*p)->value.str.len = orig->value.str.len;
			(*p)->value.str.val = estrndup(orig->value.str.val, orig->value.str.len);
			break;
		case IS_ARRAY:
			MAKE_STD_ZVAL(*p)
			(*p)->type = type;
			(*p)->value = orig->value;
			ALLOC_HASHTABLE((*p)->value.ht);
			zend_hash_init((*p)->value.ht, orig->value.ht->nTableSize, NULL, ZVAL_PTR_DTOR, 0);
			zend_hash_copy((*p)->value.ht, orig->value.ht,
			        (copy_ctor_func_t) btstore_deep_copy_emalloc, NULL, sizeof(zval*));
			break;
		case IS_DOUBLE:
			MAKE_STD_ZVAL(*p)
			(*p)->type = type;
			(*p)->value.dval = orig->value.dval;
			break;
		case IS_LONG:
			MAKE_STD_ZVAL(*p)
			(*p)->type = type;
			(*p)->value.lval = orig->value.lval;
			break;
		case IS_NULL:
			MAKE_STD_ZVAL(*p)
			(*p)->type = type;
			break;
		case IS_BOOL:
			MAKE_STD_ZVAL(*p)
			(*p)->type = type;
			(*p)->value.lval = orig->value.lval;
			break;
		default:
			zend_error(E_ERROR, "unsupported type:%d", Z_TYPE_P(orig));
			return;
	}
	return;
}

PHP_METHOD(btstore_element, toArray)
{
	btstore_object *intern;
	intern = (btstore_object*) zend_object_store_get_object(getThis() TSRMLS_CC);
	array_init(return_value);
	zend_hash_copy(return_value->value.ht, intern->value,
	        (copy_ctor_func_t) btstore_deep_copy_emalloc, NULL, sizeof(zval*));
}

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION( btstore)
{
	REGISTER_INI_ENTRIES();
	btstore_init_object();
	if (type == MODULE_PERSISTENT && strcmp(sapi_module.name, "cgi") != 0 && strcmp(
	        sapi_module.name, "cli") != 0)
	{
#ifdef BTSTORE_SHM
		if (FAILURE == btstore_init_shm(S_IRUSR))
		{
			return FAILURE;
		}
		gData = gStart;
		return SUCCESS;
#else
		return php_btstore_init(INI_STR("btstore.dir"));
#endif
	}
	else
	{
		gData = NULL;
		return SUCCESS;
	}
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION( btstore)
{
	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION( btstore)
{
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION( btstore)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION( btstore)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "btstore support", "enabled");
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}
/* }}} */

/* Remove the following function when you have succesfully modified config.m4
 so that your module can be compiled into PHP, it exists only for testing
 purposes. */

/* Every user-visible function in PHP should document itself in the source */
/* {{{ proto BtstoreElement btstore_get()
 Return a object to represent BtstoreElement object */
PHP_FUNCTION( btstore_get)
{

	if (gData == NULL)
	{
#ifdef BTSTORE_SHM
		if (FAILURE == btstore_init_shm(S_IRUSR))
		{
			return;
		}
		else
		{
			gData = gStart;
		}
#else
		if (FAILURE == php_btstore_init(INI_STR("btstore.dir")))
		{
			return;
		}
#endif
	}
	btstore_object* intern = btstore_object_init(btstore_ce);
	intern->value = gData;
	zend_object_value value = btstore_object_register(intern);
	return_value->type = IS_OBJECT;
	return_value->value.obj = value;
}

PHP_FUNCTION(btstore_reload)
{
#ifdef BTSTORE_SHM
	if (gData != NULL)
	{
		zend_error(E_ERROR, "btstore_reload only run in php mode");
	}
	else
	{
		php_btstore_init(INI_STR("btstore.dir"));
	}
#endif
}

/* }}} */
/* The previous line is meant for vim and emacs, so it can correctly fold and
 unfold functions in source code. See the corresponding marks just before
 function definition, where the functions purpose is also documented. Please
 follow this convention for the convenience of others editing your code.
 */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
