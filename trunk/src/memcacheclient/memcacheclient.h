/**
 * @file memcacheclient.h
 *
 * @brief defines common structures and exported functions.
 *
 */

#ifndef __MEMCACHE_CLIENT_
#define __MEMCACHE_CLIENT_

/**
 * @def MEMCACHE_KEY_MAX
 * Maximum length of key for caching.
 *
 * @see MemCacheData.pszDataKey.
 *
 */
#define MCACHE_KEY_MAX	250 //250 bytes. Please refer to http://code.google.com/p/memcached/wiki/FAQ.

/**
 * @def MEMCACHE_VALUE_MAX
 * Maximum length of value for caching.
 *
 * @see MemCacheData.pszDataValue
 *
 */
#define MCACHE_VALUE_MAX	1024 * 1024 	//1Mbytes. Please refer to http://code.google.com/p/memcached/wiki/FAQ.

#define MCACHE_TIMEOUT_MAX	300		///< 300 seconds

#define MCACHE_MULTIGET_MAX	1000		///< 1000 data for multiget maximum

enum
{
	MCACHE_OK = 0,
	MCACHE_ERR_PARTIAL,	///< Not all data/value was fetched from remote server by MCACHE_DataGet or MCACHE_DataGets
	MCACHE_ERR_NET,		///< Network error
	MCACHE_ERR_IO,		///< IO error
	MCACHE_ERR_TIMEOUT,	///< Timeout occured
	MCACHE_ERR_NOMEM,	///< Memory error
	MCACHE_ERR_INVAL,	///< Invalid argument
	MCACHE_ERR_EXISTS,	///< Item you are trying to store with a "cas" (i.e., Check and Set) command has been modified or deleted.
	MCACHE_ERR_NOT_STORED,	///< Item was not stored for invalid condition (e.g., add, replace.)
	MCACHE_ERR_NOT_FOUND,	///< Item you are trying to store with a "cas" (i.e., Check and Set) command did not exists or has been deleted.
	MCACHE_ERR_ERROR,	///< Error raised by request data
	MCACHE_ERR_DATA		///< Invalid data
};

enum
{
	MCACHE_FLAG_NONE = 0,
	MCACHE_FLAG_FREE_KEY 	= 1 << 0,
	MCACHE_FLAG_FREE_VALUE	= 1 << 1,
	MCACHE_FLAG_IPv6	= 1 << 2
};

typedef struct
{
	size_t	nPid;
	size_t	nUptime;
	time_t	tTime;
	char	*pszVersion;
	size_t	nPointerSize;
	char	*pszRUsageUser;
	char	*pszRUsageSystem;
	size_t	nCurrentItems;
	size_t	nTotalItems;
	int64_t	nBytes;
	size_t	nCurrentConnections;
	size_t	nTotalConnections;
	size_t	nConnectionStructures;
	int64_t	nCmdGet;
	int64_t	nCmdSet;
	int64_t	nGetHits;
	int64_t	nGetMisses;
	int64_t	nEvictions;
	int64_t	nBytesRead;
	int64_t	nBytesWritten;
	size_t	nLimitMaxbytes;
	size_t	nThreads;
} MemCacheStats;

typedef struct
{
	char	*pszServerAddr;
	size_t	nPort;
	int	nSockFD;
	size_t	nTimeout;
	int	nFlag;
} MemCacheServer;

typedef struct
{
	char	*pszDataKey;
	void	*pDataValue;
	size_t	nDataLen;
	size_t	nFlags;
	size_t	nExpiration;
	int64_t	nCASUnique;
} MemCacheData;

// Context Functions
/**
 * @fn 		int MCACHE_ServerInit(MemCacheServer *pstMCServer, const char *pszHost, int nPort, int nTimeout, int nFlag)
 *
 * @param 	pszMCServer 	pointer of server for intialization.
 * @param 	pszHost 	the hostname/address of server running memcached.
 * @param 	nPort		the port which memcached server serving.
 * @param 	nTimeout	maximum timeout in seconds while communicating with memcached server.
 * @param	nFlag		flags to control servre initialization.
 *
 * @return	MCACHE_OK for success, failure otherwise.
 *
 * @brief	initialize MemCacheServer with given host, port and timeout.
 *
 * @note	upon success. socket descriptor will be available. pszServerAddr must be freed by MCACHE_ServerDestroy.
 *
 * @see		MCACHE_ServerDisconnect, MCACHE_ServerDestroy
 */
int
MCACHE_ServerInit(MemCacheServer *pstMCServer, const char *pszHost, int nPort, int nTimeout, int nFlag);

/**
 * @fn		int MCACHE_ServerDisconnect(MemCacheServer *pstMCServer)
 *
 * @param	pstMCServer	pointer of server for socket disconnection.
 *
 * @return	MCACHE_OK for success, failure otherwise.
 *
 * @brief	disconnect socket connection between client and server.
 *
 * @note	upon success. socket descriptor will be unavailable, and socket descriptor will be reset to -1.
 *
 * @see		MCACHE_ServerInit
 */
int
MCACHE_ServerDisconnect(MemCacheServer *pstMCServer);

/**
 * @fn		int MCACHE_ServerDestroy(MemCacheServer *pstMCServer)
 *
 * @param	pstMCServer	pointer of server for destroy.
 *
 * @return	MCACHE_OK for success, failure othwise.
 */
int
MCACHE_ServerDestroy(MemCacheServer *pstMCServer);

// Storage Commands
/**
 * @fn		int MCACHE_DataSet(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
 *
 * @param	pstMCServer	pointer of server for setting data.
 * @param	pstMCData	pointer of data to set.
 *
 * @return	MCACHE_OK for success, failure otherwise.
 *
 * @brief	set (add if key not exists, update if key exists)  given value with key on specified cache server.
 *
 */
int
MCACHE_DataSet(MemCacheServer *pstMCServer, MemCacheData *pstMCData);

/**
 * @fn		int MCACHE_DataAdd(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
 *
 * @param	pstMCServer	pointer of server for adding data.
 * @param	pstMCData	pointer of data to add.
 *
 * @return	MCACHE_OK for success, failure otherwise.
 *
 * @brief	add given value associated with key on specified cache server.
 *
 */
int
MCACHE_DataAdd(MemCacheServer *pstMCServer, MemCacheData *pstMCData);

/**
 * @fn		int MCACHE_DataReplace(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
 *
 * @param	pstMCServer	pointer of server for replacing data.
 * @param	pstMCData	pointer of data to replace.
 *
 * @return	MCACHE_OK for success, failure otherwise.
 *
 * @brief	replace value associated with given key on server by given value.
 *
 */
int
MCACHE_DataReplace(MemCacheServer *pstMCServer, MemCacheData *pstMCData);

/**
 * @fn		int MCACHE_DataAppend(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
 *
 * @param	pstMCServer	pointer of server for append data.
 * @param	pstMCData	pointer of data to append.
 *
 * @return	MCACHE_OK for success, failure otherwise.
 *
 * @brief	add given value to an existing key after existing value
 *
 */
int
MCACHE_DataAppend(MemCacheServer *pstMCServer, MemCacheData *pstMCData);

/**
 * @fn		int MCACHE_DataPrepend(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
 *
 * @param	pstMCServer	pointer of server for prepend data.
 * @param	pstMCData	pointer of data to prepend.
 *
 * @return	MCACHE_OK for success, failure otherwise.
 *
 * @brief	add given value to an existing key before existing data
 * 
 */
int
MCACHE_DataPrepend(MemCacheServer *pstMCServer, MemCacheData *pstMCData);

/**
 * @fn		int MCACHE_DataCheckAndSet(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
 *
 * @param	pstMCServer	pointer of server for setting data.
 * @param	pstMCData	pointer of data to check and set.
 *
 * @return	MCACHE_OK for success, failure otherwise.
 *
 * @brief	store given value but only if no one else has updated since last fetched it (by checking pstMCData->nCASUnique).
 */
int
MCACHE_DataCheckAndSet(MemCacheServer *pstMCServer, MemCacheData *pstMCData);

/**
 * @fn		int MCACHE_DataFree(MemCacheData *pstMCData)
 *
 * @param	pstMCData	pointer of data to free.
 *
 * @brief	free pstMCData.pszDataValue if it's not NULL.
 *
 * @note	MCACHE_DataFree will be used for those data fetched by MCACHE_DataGet.
 *
 */
int
MCACHE_DataFree(MemCacheData *pstMCData);

// Retrival commands
/**
 * @fn		int MCACHE_DataGet(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
 *
 * @param	pstMCServer	pointer of server for getting data.
 * @param	pstMCDataList	pointer of data list to hold key and stored fetched value.
 * @param	nListSize	number of data in data list
 *
 * @return	MCACHE_OK for success, failure otherwise.
 *
 * @brief	fetch data associted with given key and store pstMCData.
 *       	fetched value will be stored in malloced memory, and caller must invoked MCACHE_DataFree to free it.
 *
 */
int
MCACHE_DataGet(MemCacheServer *pstMCServer, MemCacheData *pstMCDataList, size_t nListSize);

/**
 * @fn		int MCACHE_DataGets(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
 *
 * @param	pstMCServer	pointer of server for getting data.
 * @param	pstMCData	pointer of data to hold key and stored fetched value.
 * @param	nListSize	number of data in data list.
 *
 * @return	MCACHE_OK for success, failure otherwise.
 *
 * @brief	fetched data associted with given key to pst
 *
 */
int
MCACHE_DataGets(MemCacheServer *pstMCServer, MemCacheData *pstMCDataList, size_t nListSize);

// Delete commands
/**
 * @fn		int MCACHE_DataDelete(MemCacheServer *pstMCServer, MemCacheData *pstMCData, int nTime)
 *
 * @param	pstMCServer	pointer of server for deleting data.
 * @param	pstMCData	pointer of data to hold key and stored fetched value.
 * @param	nTime		seconds for server to refuse "add" and "replace" commands with this key.
 *
 * @return	MCACHE_OK for success, failure otherwise.
 *
 * @brief	delete data on cache server associated with given key.
 */
int
MCACHE_DataDelete(MemCacheServer *pstMCServer, MemCacheData *pstMCData, size_t nTime);

// Increment/Decrement commands
/**
 * @fn		int MCACHE_DataIncrement(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
 *
 * @param	pstMCServer	pointer of server for addition.
 * @param	pstMCData	pointer of data to add.
 *
 * @return	MCACHE_OK for success, failure otherwise.
 *
 * @brief	take the data associated with given key on server as 64-bit unsigned integer, and add given data and original data together on server.
 *
 */
int
MCACHE_DataIncrement(MemCacheServer *pstMCServer, MemCacheData *pstMCData, size_t nNum);

/**
 * @fn		int MCACHE_DataDecrement(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
 *
 * @param	pstMCServer	pointer of server for substraction.
 * @param	pstMCData	pointer of data to substract.
 *
 * @return	MCACHE_OK for success, failure otherwise.
 *
 * @brief	take the data associated with given key on server as 64-bit unsigned integer, and substract given dat from original data on server.
 */
int
MCACHE_DataDecrement(MemCacheServer *pstMCServer, MemCacheData *pstMCData, size_t nNum);

// Stats commands
/**
 * @fn		int MCACHE_ServerStats(MemCacheServer *pstMCServer, MemCacheStats *pstMCStats)
 *
 * @param	pstMCServer	pointer of server for fetching status.
 * @param	pstMCStats	pointer of status data.
 *
 * @return	MCACHE_OK for success, failure otherwise.
 * 
 * @brief	get server status and store status data in pstMCStats.
 */
int
MCACHE_ServerStats(MemCacheServer *pstMCServer, MemCacheStats *pstMCStats);


#endif
