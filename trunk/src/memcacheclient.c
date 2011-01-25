/**
 * @file memcacheclient.c
 *
 * @brief implements library functions defines in memcacheclient.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <resolv.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/time.h>

#include "memcacheclient/memcacheclient.h"

#define SELECT_TIMEOUT_SEC 0
#define SELECT_TIMEOUT_USEC 50

struct statInfo
{
	int idx;
	char *value;
};

enum
{
	MCACHE_OP_SET = 1,
	MCACHE_OP_ADD,
	MCACHE_OP_APPEND,
	MCACHE_OP_PREPEND,
	MCACHE_OP_REPLACE,
	MCACHE_OP_CAS,
	MCACHE_OP_GET,
	MCACHE_OP_GETS,
	MCACHE_OP_DELETE,
	MCACHE_OP_INCREMENT,
	MCACHE_OP_DECREMENT,
	MCACHE_OP_STATS
};

static int
s_isSockReadable(int nSockFD, int nTimeoutSec, int nTimeoutUSec)
{
	int ret = MCACHE_OK;
	int len = sizeof(ret);

	struct timeval timeout;
	fd_set read_sets;
	
	FD_ZERO(&read_sets);
	FD_SET(nSockFD, &read_sets);

	timeout.tv_sec = nTimeoutSec;
	timeout.tv_usec = nTimeoutUSec;

	ret = select(nSockFD + 1, &read_sets, NULL, NULL, &timeout);

	if (0 > ret)
		return MCACHE_ERR_NET;

	if (0 == ret)
		return MCACHE_ERR_TIMEOUT;

	if (! FD_ISSET(nSockFD, &read_sets))
		return MCACHE_ERR_NET;

	if (0 > getsockopt(nSockFD, SOL_SOCKET, SO_ERROR, &ret, &len) ||
		0 != ret)
		return MCACHE_ERR_NET;

	return MCACHE_OK;
}

static int
s_isSockWritable(int nSockFD, int nTimeoutSec, int nTimeoutUSec)
{
	int ret = MCACHE_OK;
	int len = sizeof(ret);

	struct timeval timeout;
	fd_set write_sets;

	FD_ZERO(&write_sets);
	FD_SET(nSockFD, &write_sets);

	timeout.tv_sec = nTimeoutSec;
	timeout.tv_usec = nTimeoutUSec;

	ret = select(nSockFD + 1, NULL, &write_sets, NULL, &timeout);

	if (0 > ret)
		return MCACHE_ERR_NET;

	if (0 == ret)
		return MCACHE_ERR_TIMEOUT;

	if (! FD_ISSET(nSockFD, &write_sets))
		return MCACHE_ERR_NET;

	if (0 > getsockopt(nSockFD, SOL_SOCKET, SO_ERROR, &ret, &len) ||
		0 != ret)
		return MCACHE_ERR_NET;

	return MCACHE_OK;
}

static int
s_isSockConnected(int nSockFD, int nTimeout)
{
	int ret = MCACHE_OK;
	int len = sizeof(ret);

	struct timeval timeout;
	fd_set read_sets;
	fd_set write_sets;
	fd_set exce_sets;
	
	FD_ZERO(&read_sets);
	FD_SET(nSockFD, &read_sets);
	write_sets = read_sets;
	exce_sets = read_sets;
	
	timeout.tv_sec = nTimeout;
	timeout.tv_usec = 0;

	ret = select(nSockFD + 1, &read_sets, &write_sets, &exce_sets, &timeout);

	if (0 > ret)
		return MCACHE_ERR_NET;

	if (0 == ret)
		return MCACHE_ERR_TIMEOUT;

	if (!FD_ISSET(nSockFD, &read_sets) && !FD_ISSET(nSockFD, &write_sets))
		return MCACHE_ERR_NET;
	
	if (0 > getsockopt(nSockFD, SOL_SOCKET, SO_ERROR, &ret, &len) ||
		0 != ret)
		return MCACHE_ERR_NET;

	return MCACHE_OK;
}

static int
s_ConnectIPv4(const char *pszHost, int nPort, int nSockFD, int nTimeout)
{
	int ret = MCACHE_OK;
	int sock_flags = 0;
	struct sockaddr_in dest;

	bzero(&dest, sizeof(dest));

	dest.sin_family = AF_INET;
	dest.sin_port = htons(nPort);

	if (inet_aton(pszHost, (struct in_addr *) &dest.sin_addr.s_addr) == 0)
		return MCACHE_ERR_NET;

	if (0 > (sock_flags = fcntl(nSockFD, F_GETFL, 0)))
		return MCACHE_ERR_NET;

	if (0 > fcntl(nSockFD, F_SETFL, sock_flags | O_NONBLOCK))
		return MCACHE_ERR_NET;

	if ((ret = connect(nSockFD, (struct sockaddr *) &dest, sizeof(dest))) &&
	    errno != EINPROGRESS)
		return MCACHE_ERR_NET;

	if (0 == ret) {
		ret = MCACHE_OK;
		goto end;
	}

	ret = s_isSockConnected(nSockFD, nTimeout);

end:
	return ret;
}

static int
s_ConnectIPv6(const char *pszHost, int nPort, int nSockFD, int nTimeout)
{
	int ret = MCACHE_OK;
	int sock_flags = 0;
	struct sockaddr_in6 dest;
	
	bzero(&dest, sizeof(dest));
	
	dest.sin6_family = AF_INET6;
	dest.sin6_port = htons(nPort);
	
	if (0 > inet_pton(AF_INET6, pszHost,&dest.sin6_addr))
		return MCACHE_ERR_NET;

	if (0 > (sock_flags = fcntl(nSockFD, F_GETFL, 0)))
		return MCACHE_ERR_NET;

	if (0 > fcntl(nSockFD, F_SETFL, sock_flags | O_NONBLOCK))
		return MCACHE_ERR_NET;

	if ((ret = connect(nSockFD, (struct sockaddr *) &dest, sizeof(dest))) &&
		errno != EINPROGRESS)
		return MCACHE_ERR_NET;

	if (0 == ret) {
		ret = MCACHE_OK;
		goto end;
	}

	ret = s_isSockConnected(nSockFD, nTimeout);
	
end:
	return ret;
}

static int
s_ConnectTCP(const char *pszHost, int nPort, int nTimeout, int nFlag)
{
	int sock_fd = 0;
	int domain = 0;

	domain = (MCACHE_FLAG_IPv6 == (nFlag & MCACHE_FLAG_IPv6))?AF_INET6:AF_INET;

	if (0 > (sock_fd = socket(domain, SOCK_STREAM, 0)))
		return -1;

	if (AF_INET == domain) {
		if (MCACHE_OK != s_ConnectIPv4(pszHost, nPort, sock_fd, nTimeout)) {
			close(sock_fd);
			return -1;
		}
	}
	else {
		if (MCACHE_OK != s_ConnectIPv6(pszHost, nPort, sock_fd, nTimeout)) {
			close(sock_fd);
			return -1;
		}
	}

	return sock_fd;
}

int
s_SockWrite(int nSockFD, const void *pData, size_t nDataLen, int nTimeout)
{
	int i = 0;
	int ret = MCACHE_OK;
	int sent_size = 0;
	int sent_count = 0;
	time_t timeout = time(NULL) + nTimeout;

	while (1) {
		ret = s_isSockWritable(nSockFD, SELECT_TIMEOUT_SEC, SELECT_TIMEOUT_USEC);

		if (timeout < time(NULL)) {
			ret = MCACHE_ERR_TIMEOUT;
			goto end;
		}

		if (MCACHE_ERR_TIMEOUT == ret) {
			usleep(SELECT_TIMEOUT_USEC);
			continue;
		}

		if (MCACHE_OK == ret) {
			sent_size = write(nSockFD, pData + sent_count, nDataLen - sent_count);

			if (0 > sent_size && EINTR == errno)
				continue;
			
			if (0 > sent_size) {
				ret = MCACHE_ERR_NET;
				goto end;
			}

			if (0 == sent_size)
				break;

			sent_count += sent_size;

			if (nDataLen == sent_count)
				break;
		}
		else {
			//other error
			goto end;
		}
	}

	if (nDataLen != sent_count)
		ret = MCACHE_ERR_TIMEOUT;

end:
	return ret;
}

int
s_SockRead(int nSockFD, const void *pData, size_t nDataLen, int *pnReadNum, int nTimeout)
{
	int i = 0;
	int ret = MCACHE_OK;
	int read_size = 0;
	int read_count = 0;
	time_t timeout = time(NULL) + nTimeout;

	if (0 >= nTimeout)
		return MCACHE_ERR_INVAL;

	while (1) {
		ret = s_isSockReadable(nSockFD, SELECT_TIMEOUT_SEC, SELECT_TIMEOUT_USEC);

		if (timeout < time(NULL)) {
			ret = MCACHE_ERR_TIMEOUT;
			goto end;
		}

		if (MCACHE_ERR_TIMEOUT == ret) {
			if (MCACHE_OK == s_isSockWritable(nSockFD, SELECT_TIMEOUT_SEC, SELECT_TIMEOUT_USEC) && 0 < read_count) {
				//read completes
				ret = MCACHE_OK;
				break;
			}
			usleep(SELECT_TIMEOUT_USEC);
			continue;
		}

		if (MCACHE_OK == ret) {
			read_size = read(nSockFD, ((void *) pData) + read_count, nDataLen - read_count);

			if (0 > read_size && EINTR == errno)
				continue;

			if (0 > read_size) {
				ret = MCACHE_ERR_NET;
				goto end;
			}

			if (0 == read_size) {
				ret = MCACHE_OK;
				break;
			}

			read_count += read_size;
			if (nDataLen == read_count) {
				//buffer is full
				ret = MCACHE_OK;
				break;
			}

			continue;
		}
		else {
			//other error
			goto end;
		}
	}
	
	*pnReadNum = read_count;

end:
	return ret;
}

int
s_ChkInput(MemCacheServer *pstMCServer, MemCacheData *pstMCData, int nOpFlag)
{
	int ret = MCACHE_OK;

	if (NULL == pstMCServer || NULL == pstMCData || 0 > pstMCServer->nSockFD ||
		NULL == pstMCServer->pszServerAddr)
		return MCACHE_ERR_INVAL;

	switch (nOpFlag) {
		case MCACHE_OP_SET:
		case MCACHE_OP_ADD:
		case MCACHE_OP_REPLACE:
		case MCACHE_OP_APPEND:
		case MCACHE_OP_PREPEND:
			if (NULL == pstMCData->pszDataKey || NULL == pstMCData->pDataValue)
				ret = MCACHE_ERR_INVAL;
			
			if (MCACHE_VALUE_MAX < pstMCData->nDataLen)
				ret = MCACHE_ERR_INVAL;
			break;
		case MCACHE_OP_GET:
		case MCACHE_OP_GETS:
		case MCACHE_OP_INCREMENT:
		case MCACHE_OP_DECREMENT:
			if (NULL == pstMCData->pszDataKey)
				ret = MCACHE_ERR_INVAL;
			break;
		default:
			ret = MCACHE_ERR_INVAL;
			break;
	}

	return ret;
}

int
s_DataManipulate(MemCacheServer *pstMCServer, MemCacheData *pstMCData, int nOpFlag)
{
	int ret = MCACHE_OK;
	int buffer_size = MCACHE_VALUE_MAX + MCACHE_KEY_MAX * 2;
	char buffer[MCACHE_VALUE_MAX + MCACHE_KEY_MAX * 2];
	int timeout = 0;
	int read_count = 0;
	time_t check_time = 0;

	switch (nOpFlag) {
		case MCACHE_OP_SET:
		case MCACHE_OP_ADD:
		case MCACHE_OP_APPEND:
		case MCACHE_OP_PREPEND:
		case MCACHE_OP_REPLACE:
		case MCACHE_OP_CAS:
			ret = s_ChkInput(pstMCServer, pstMCData, nOpFlag);
			break;
		default:
			ret = MCACHE_ERR_INVAL;
			break;
	}

	if (MCACHE_OK != ret)
		return ret;

	memset(buffer, 0, buffer_size);
	if (MCACHE_OP_SET == nOpFlag) {
		snprintf(buffer, buffer_size, "set %s %d %d %d\r\n", pstMCData->pszDataKey,
			pstMCData->nFlags, pstMCData->nExpiration, pstMCData->nDataLen);
	}
	else if (MCACHE_OP_ADD == nOpFlag) {
		snprintf(buffer, buffer_size, "add %s %d %d %d\r\n", pstMCData->pszDataKey,
			pstMCData->nFlags, pstMCData->nExpiration, pstMCData->nDataLen);
	}
	else if (MCACHE_OP_APPEND == nOpFlag) {
		snprintf(buffer, buffer_size, "append %s %d %d %d\r\n", pstMCData->pszDataKey,
			pstMCData->nFlags, pstMCData->nExpiration, pstMCData->nDataLen);
	}
	else if (MCACHE_OP_PREPEND == nOpFlag) {
		snprintf(buffer, buffer_size, "prepend %s %d %d %d\r\n", pstMCData->pszDataKey,
			pstMCData->nFlags, pstMCData->nExpiration, pstMCData->nDataLen);
	}
	else if (MCACHE_OP_REPLACE == nOpFlag) {
		snprintf(buffer, buffer_size, "replace %s %d %d %d\r\n", pstMCData->pszDataKey,
			pstMCData->nFlags, pstMCData->nExpiration, pstMCData->nDataLen);
	}
	else if (MCACHE_OP_CAS == nOpFlag) {
		snprintf(buffer, buffer_size, "cas %s %d %d %d %lld\r\n", pstMCData->pszDataKey,
			pstMCData->nFlags, pstMCData->nExpiration, pstMCData->nDataLen, pstMCData->nCASUnique);
	}

	timeout = pstMCServer->nTimeout;
	check_time = time(NULL);

	if (MCACHE_OK == (ret = s_SockWrite(pstMCServer->nSockFD, buffer, strlen(buffer), timeout))) {
		timeout -= time(NULL) - check_time;
		memset(buffer, 0, buffer_size);

		if (MCACHE_OK == (ret = s_SockWrite(pstMCServer->nSockFD, pstMCData->pDataValue, pstMCData->nDataLen, timeout)) &&
			MCACHE_OK ==(ret = s_SockWrite(pstMCServer->nSockFD, "\r\n", 2, timeout))) {

			if (MCACHE_OK == (ret = s_SockRead(pstMCServer->nSockFD, buffer, buffer_size, &read_count, timeout))) {
				if (0 == strncmp(buffer, "STORED\r\n", 8))
					ret = MCACHE_OK;
				else if (0 == strncmp(buffer, "ERROR\r\n", 7) || buffer == strstr(buffer, "CLIENT_ERROR"))
					ret = MCACHE_ERR_ERROR;
				else if (0 == strncmp(buffer, "EXISTS\r\n", 8))
					ret = MCACHE_ERR_EXISTS;
				else if (0 == strncmp(buffer, "NOT_STORED\r\n", 12))
					ret = MCACHE_ERR_NOT_STORED;
				else
					ret = MCACHE_ERR_DATA;
			}
		}
		
	}

end:

	return ret;
}

static int
s_DataCalculate(MemCacheServer *pstMCServer, MemCacheData *pstMCData, size_t nNum, int nOpFlag)
{
	int ret = MCACHE_OK;
	int read_count = 0;
	char buffer[MCACHE_VALUE_MAX];
	size_t timeout = 0;
	time_t check_time = 0;

	switch (nOpFlag) {
		case MCACHE_OP_INCREMENT:
		case MCACHE_OP_DECREMENT:
			ret = s_ChkInput(pstMCServer, pstMCData, nOpFlag);
			break;
		default:
			ret = MCACHE_ERR_INVAL;
			break;
	}

	if (MCACHE_OK != ret)
		return ret;

	memset(buffer, 0, MCACHE_VALUE_MAX);

	if (MCACHE_OP_INCREMENT == nOpFlag)
		snprintf(buffer, MCACHE_VALUE_MAX - 1, "incr %s %d\r\n", pstMCData->pszDataKey, nNum);
	else if (MCACHE_OP_DECREMENT == nOpFlag)
		snprintf(buffer, MCACHE_VALUE_MAX - 1, "decr %s %d\r\n", pstMCData->pszDataKey, nNum);

	timeout = pstMCServer->nTimeout;
	check_time = time(NULL);

	if (MCACHE_OK == (ret = s_SockWrite(pstMCServer->nSockFD, buffer, strlen(buffer), timeout))) {
		timeout -= time(NULL) - check_time;
		memset(buffer, 0, MCACHE_VALUE_MAX);

		if (MCACHE_OK == (ret = s_SockRead(pstMCServer->nSockFD, buffer, MCACHE_VALUE_MAX - 1, &read_count, timeout))) {
			char *tmp = NULL;
			if (buffer == strstr(buffer, "CLIENT_ERROR"))
				ret = MCACHE_ERR_ERROR;
			else if (NULL == (tmp = strstr(buffer, "\r\n"))) {
				ret = MCACHE_ERR_DATA;
			}
			else {
				*tmp = '\0';

				if (NULL == (tmp = strdup(buffer))) {
					ret = MCACHE_ERR_NOMEM;
					goto end;
				}
				
				if (MCACHE_FLAG_FREE_VALUE == (pstMCServer->nFlag & MCACHE_FLAG_FREE_VALUE) && NULL != pstMCData->pDataValue) {
					free(pstMCData->pDataValue);
				}

				pstMCData->pDataValue = (void *) tmp;
				ret = MCACHE_OK;
			}
		}
	}

end:

	return ret;
}

static int
s_GetDataByKey(MemCacheData *pstDataList, size_t nListSize, const char *pszKey)
{
	int i = 0;
	int idx = -1;

	if (NULL == pstDataList || 0 == nListSize || NULL == pszKey)
		return -1;

	for (i = 0; i < nListSize; i++) {
		if (NULL == (pstDataList + i) || NULL == pstDataList[i].pszDataKey)
			continue;

		if (0 == strcmp(pstDataList[i].pszDataKey, pszKey)) {
			idx = i;
			break;
		}
	}

	return idx;
}

static int
s_DataRetrieval(MemCacheServer *pstMCServer, MemCacheData *pstMCDataList, size_t nListSize, int nOpFlag)
{
	int i = 0;
	int ret = MCACHE_OK;
	int timeout = 0;
	int read_count = 0;
	int data_size = 0;
	size_t buffer_size = 6;/// 4 + 2 (i.e., gets + \r\n)
	size_t fetched_count = 0;
	char *buffer = NULL;
	char *cursor = NULL;
	time_t check_time = 0;

	if (NULL == pstMCServer || NULL == pstMCDataList || MCACHE_MULTIGET_MAX < nListSize)
		return MCACHE_ERR_INVAL;

	///Maximum memory usage will be (MCACHE_MULTIGET_MAX * (250 + 1) + 6)

	for (i = 0; i < nListSize; i++) {
		if (MCACHE_OK != s_ChkInput(pstMCServer, pstMCDataList + i, nOpFlag))
			continue;

		buffer_size += strlen(pstMCDataList[i].pszDataKey) + 1; // key + space
	}

	if (NULL == (buffer = (char *) malloc(buffer_size + 1)))
		return MCACHE_ERR_NOMEM;

	memset(buffer, 0, buffer_size + 1);

	if (MCACHE_OP_GET == nOpFlag) {
		snprintf(buffer, buffer_size, "get");
	}
	else {
		snprintf(buffer, buffer_size, "gets");
	}

	cursor = buffer + strlen(buffer);

	for (i = 0; i < nListSize; i++) {
		if (MCACHE_OK != s_ChkInput(pstMCServer, pstMCDataList + i, nOpFlag))
			continue;

		*cursor = ' ';
		cursor++;
		data_size = strlen(pstMCDataList[i].pszDataKey);
		
		memcpy(cursor, pstMCDataList[i].pszDataKey, data_size);
		cursor += data_size;
	}

	*cursor = '\r';
	cursor++;
	*cursor = '\n';

	timeout = pstMCServer->nTimeout;
	check_time = time(NULL);

	if (MCACHE_OK == (ret = s_SockWrite(pstMCServer->nSockFD, buffer, strlen(buffer), timeout))) {
		timeout -= time(NULL) - check_time;

		//use large buffer for safety
		if (buffer_size < (MCACHE_VALUE_MAX  * 2)) {
			free(buffer);
			buffer_size = MCACHE_VALUE_MAX * 2;
			if (NULL == (buffer = (char *) malloc(buffer_size)))
				return MCACHE_ERR_NOMEM;
		}

		memset(buffer, 0, buffer_size);
		data_size = 0;
		while (1) {

			timeout -= time(NULL) - check_time;
			ret = s_SockRead(pstMCServer->nSockFD, buffer + data_size, buffer_size - data_size - 1, &read_count, timeout);

			if (nListSize == fetched_count)
				break;

			if (MCACHE_OK == ret) {
				cursor = buffer;
				
				*(buffer + data_size + read_count) = '\0';	
				int valid_data_size = data_size + read_count;

				if (0 == strcmp(buffer, "END\r\n") || 0 == strcmp(buffer, "\r\nEND\r\n"))
					goto done;

				while (1) {
					char *key = NULL;
					char *flags = NULL;
					char *size = NULL;
					char *cas_unique = NULL;
					char *data = NULL;
					int  size_num = 0;
					int  break_outer = 0;
					int  idx = 0;
					MemCacheData *tmp_data = NULL;
					
					while (1) {
						if (0 == strcmp(cursor, "END\r\n") || 0 == strcmp(cursor, "\r\nEND\r\n"))
							goto done;

						if (NULL == (key = strstr(cursor, "VALUE ")))
							break;

						if (NULL == (data = strstr(key, "\r\n")))
							break;

						if (MCACHE_OP_GETS == nOpFlag) {
							cas_unique = data;
							while (' ' != *cas_unique)
								cas_unique--;

							size = cas_unique - 1;
							while ( ' ' != *size)
								size--;

							size++;
							*cas_unique = '\0';
							size_num = strtol(size, NULL, 10);
							*cas_unique = ' ';
						}
						else {
							size = data;
							while (' ' != *size)
								size--;

							*data = '\0';
							size++;
							size_num = strtol(size, NULL, 10);
							*data = '\r';
						}

						if (size_num > (valid_data_size - ((data - cursor) + 2))) {
							key = NULL;
							break_outer = 1;
							break;
						}

						key += 6;
						flags = strchr(key, ' ');
						*flags = '\0';
						flags++;
						*data = '\0';
						data += 2;

						if (-1 == (idx = s_GetDataByKey(pstMCDataList, nListSize, key))) {
							ret = MCACHE_ERR_DATA;
							goto end;
						}

						pstMCDataList[idx].nFlags = strtol(flags, NULL, 10);
						pstMCDataList[idx].nDataLen = size_num;

						if (MCACHE_OP_GETS == nOpFlag) {
							pstMCDataList[idx].nCASUnique = strtoll(cas_unique, NULL, 10);
						}
						
						if (MCACHE_FLAG_FREE_VALUE == (pstMCServer->nFlag & MCACHE_FLAG_FREE_VALUE) &&
							NULL != pstMCDataList[idx].pDataValue)
							free(pstMCDataList[idx].pDataValue);

						pstMCDataList[idx].pDataValue = malloc(size_num);

						if (NULL != pstMCDataList[idx].pDataValue) {
							memcpy(pstMCDataList[idx].pDataValue, data, size_num);
						}

						valid_data_size = valid_data_size - (data - cursor) - size_num;
						cursor = data + size_num;
						fetched_count++;
					}

					if (NULL == key) {
						data_size = valid_data_size;
						memmove(buffer, cursor, data_size);
						*(buffer + data_size) = '\0';
						break;
					}
					
					if (NULL == data) {
						data_size = strlen(key);
						memmove(buffer, key, data_size);
						*(buffer + data_size) = '\0';
						break;
					}
				}
			}
			else {
				//error
				break;
			}
		}
	}

done:

	if (MCACHE_OK == ret && nListSize != fetched_count)
		ret = MCACHE_ERR_PARTIAL;

end:

	if (NULL != buffer)
		free(buffer);

	return ret;
}

// Context Functions
/**
 * @fn 		int MCACHE_ServerInit(MemCacheServer *pstMCServer, const char *pszHost, int nPort, int nTimeout)
 *
 * @param 	pszMCServer 	pointer of server for intialization.
 * @param 	pszHost 	the hostname/address of server running memcached.
 * @param 	nPort		the port which memcached server serving.
 * @param 	nTimeout	maximum timeout in seconds while communicating with memcached server.
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
MCACHE_ServerInit(MemCacheServer *pstMCServer, const char *pszHost, int nPort, int nTimeout, int nFlag)
{
	if (NULL == pstMCServer || NULL == pszHost || 0 >= nPort || 65535 < nPort ||
		0 >= nTimeout || MCACHE_TIMEOUT_MAX < nTimeout)
		return MCACHE_ERR_INVAL;

	memset(pstMCServer, 0, sizeof(MemCacheServer));

	if (0 > (pstMCServer->nSockFD = s_ConnectTCP(pszHost, nPort, nTimeout, nFlag)))
		return MCACHE_ERR_NET;

	if (NULL == (pstMCServer->pszServerAddr = strdup(pszHost))) {
		return MCACHE_ERR_NOMEM;
	}

	pstMCServer->nPort = nPort;
	pstMCServer->nTimeout = nTimeout;
	pstMCServer->nFlag = nFlag;

	return MCACHE_OK;
}

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
MCACHE_ServerDisconnect(MemCacheServer *pstMCServer)
{
	if (NULL == pstMCServer || 0 > pstMCServer->nSockFD)
		return MCACHE_ERR_INVAL;

	close(pstMCServer->nSockFD);
	pstMCServer->nSockFD = -1;

	return MCACHE_OK;
}

/**
 * @fn		int MCACHE_ServerDestroy(MemCacheServer *pstMCServer)
 *
 * @param	pstMCServer	pointer of server for destroy.
 *
 * @return	MCACHE_OK for success, failure othwise.
 */
int
MCACHE_ServerDestroy(MemCacheServer *pstMCServer)
{
	if (NULL == pstMCServer)
		return MCACHE_ERR_INVAL;

	if (NULL != pstMCServer->pszServerAddr) {
		free(pstMCServer->pszServerAddr);
		pstMCServer->pszServerAddr = NULL;
	}

	return MCACHE_OK;
}

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
MCACHE_DataSet(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
{
	return s_DataManipulate(pstMCServer, pstMCData, MCACHE_OP_SET);
}


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
MCACHE_DataAdd(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
{
	return s_DataManipulate(pstMCServer, pstMCData, MCACHE_OP_ADD);
}

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
MCACHE_DataReplace(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
{
	return s_DataManipulate(pstMCServer, pstMCData, MCACHE_OP_REPLACE);
}

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
MCACHE_DataAppend(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
{
	return s_DataManipulate(pstMCServer, pstMCData, MCACHE_OP_APPEND);
}

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
MCACHE_DataPrepend(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
{
	return s_DataManipulate(pstMCServer, pstMCData, MCACHE_OP_PREPEND);
}

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
MCACHE_DataCheckAndSet(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
{
	return s_DataManipulate(pstMCServer, pstMCData, MCACHE_OP_CAS);
}

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
MCACHE_DataFree(MemCacheData *pstMCData)
{
	if (NULL == pstMCData || NULL == pstMCData->pDataValue)
		return MCACHE_ERR_INVAL;

	free(pstMCData->pDataValue);
	pstMCData->pDataValue = NULL;

	return MCACHE_OK;
}

// Retrival commands
/**
 * @fn		int MCACHE_DataGet(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
 *
 * @param	pstMCServer	pointer of server for getting data.
 * @param	pstMCDataList	pointer of data list to hold key and stored fetched value.
 * @param	nListSize	number of data in data list
 *
 * @return	MCACHE_OK for success, MCACHE_ERR_PARTIAL for some data could not be fetched correctly, failure otherwise.
 *
 * @brief	fetch data associted with given key and store pstMCData.
 *       	fetched value will be stored in malloced memory, and caller must invoked MCACHE_DataFree to free it.
 *
 */
int
MCACHE_DataGet(MemCacheServer *pstMCServer, MemCacheData *pstMCDataList, size_t nListSize)
{
	return s_DataRetrieval(pstMCServer, pstMCDataList, nListSize, MCACHE_OP_GET);
}

/**
 * @fn		int MCACHE_DataGets(MemCacheServer *pstMCServer, MemCacheData *pstMCData)
 *
 * @param	pstMCServer	pointer of server for getting data.
 * @param	pstMCData	pointer of data to hold key and stored fetched value.
 * @param	nListSize	number of data in data list.
 *
 * @return	MCACHE_OK for success, MCACHE_ERR_PARTIAL for some data could not be fetched correctly, failure otherwise.
 *
 * @brief	fetched data associted with given key to pst
 *
 */
int
MCACHE_DataGets(MemCacheServer *pstMCServer, MemCacheData *pstMCDataList, size_t nListSize)
{
	return s_DataRetrieval(pstMCServer, pstMCDataList, nListSize, MCACHE_OP_GETS);
}

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
MCACHE_DataDelete(MemCacheServer *pstMCServer, MemCacheData *pstMCData, size_t nTime)
{
	int ret = MCACHE_OK;
	int read_count = 0;
	char buffer[MCACHE_VALUE_MAX];
	size_t timeout = 0;
	time_t check_time = 0;

	if (MCACHE_OK != (ret = s_ChkInput(pstMCServer, pstMCData, MCACHE_OP_DELETE)))
		return ret;

	memset(buffer, 0, MCACHE_VALUE_MAX);

	if (0 != nTime) {
		snprintf(buffer, MCACHE_VALUE_MAX - 1, "delete %s %d\r\n", pstMCData->pszDataKey,
			 nTime);
	}
	else {
		snprintf(buffer, MCACHE_VALUE_MAX - 1, "delete %s\r\n", pstMCData->pszDataKey);
	}

	timeout = pstMCServer->nTimeout;
	check_time = time(NULL);
	if (MCACHE_OK != (ret = s_SockWrite(pstMCServer->nSockFD, buffer, strlen(buffer), timeout))) {
		timeout -= time(NULL) - check_time;
		memset(buffer, 0, MCACHE_VALUE_MAX);

		if (MCACHE_OK == (ret = s_SockRead(pstMCServer->nSockFD, buffer, MCACHE_VALUE_MAX - 1, &read_count, timeout))) {
			if (0 == strncmp(buffer, "DELETED\r\n", 9))
				ret = MCACHE_OK;
			else if (0 == strncmp(buffer, "NOT_FOUND\r\n", 11))
				ret = MCACHE_ERR_NOT_FOUND;
			else
				ret = MCACHE_ERR_DATA;
		}
	}

end:

	return ret;
}

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
MCACHE_DataIncrement(MemCacheServer *pstMCServer, MemCacheData *pstMCData, size_t nNum)
{
	return s_DataCalculate(pstMCServer, pstMCData, nNum, MCACHE_OP_INCREMENT);
}

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
MCACHE_DataDecrement(MemCacheServer *pstMCServer, MemCacheData *pstMCData, size_t nNum)
{
	return s_DataCalculate(pstMCServer, pstMCData, nNum, MCACHE_OP_DECREMENT);
}

// Stats commands
/**
 * @fn		int MCACHE_ServerStats(MemCacheServer *pstMCServer, MemCacheStats *pstMCStats)
 *
 * @param	pstMCServer	pointer of server for fetching status.
 * @param	pstMCStats	pointer of status.
 *
 * @return	MCACHE_OK for success, fail otherwise.
 *
 * @brief	get the status of server.
 */
int
MCACHE_ServerStats(MemCacheServer *pstMCServer, MemCacheStats *pstMCStats)
{
	int ret = MCACHE_OK;
	char buffer[MCACHE_VALUE_MAX * 2];
	int timeout = 0;
	int read_count = 0;
	time_t check_time = 0;
	int buffer_size = MCACHE_VALUE_MAX  * 2;
	char i = 0;

	enum {
		STAT_PID = 0,
		STAT_UPTIME,
		STAT_TIME,
		STAT_VERSION,
		STAT_POINTER_SIZE,
		STAT_RUSAGE_USER,
		STAT_RUSAGE_SYSTEM,
		STAT_CURR_CONNS,
		STAT_TOTAL_CONNS,
		STAT_CONN_STRUCTURES,
		STAT_CMD_GET,
		STAT_CMD_SET,
		STAT_CMD_FLUSH,
		STAT_GET_HITS,
		STAT_GET_MISSES,
		STAT_DELETE_MISSES,
		STAT_DELETE_HITS,
		STAT_INCR_MISSES,
		STAT_INCR_HITS,
		STAT_DECR_MISSES,
		STAT_DECR_HITS,
		STAT_CAS_MISSES,
		STAT_CAS_HITS,
		STAT_BADVAL,
		STAT_AUTH_CMDS,
		STAT_AUTH_ERROS,
		STAT_BYTES_READ,
		STAT_BYTES_WRITTEN,
		STAT_LIMIT_MAXBYTES,
		STAT_ACCEPTING_CONNS,
		STAT_LISTEN_DISABLED_NUM,
		STAT_THREADS,
		STAT_CONN_YIELDS,
		STAT_BYTES,
		STAT_CURR_ITEMS,
		STAT_TOTAL_ITEMS,
		STAT_EVICTIONS,
		STAT_RECLAIMED
	};

	struct statInfo stat_list[] = {
		{STAT_PID, "STAT pid "},
		{STAT_UPTIME, "STAT uptime"},
		{STAT_TIME, "STAT time"},
		{STAT_VERSION, "STAT version"},
		{STAT_POINTER_SIZE, "STAT pointer_size"},
		{STAT_RUSAGE_USER, "STAT rusage_user"},
		{STAT_RUSAGE_SYSTEM, "STAT rusage_system"},
		{STAT_CURR_CONNS, "STAT curr_connections"},
		{STAT_TOTAL_CONNS, "STAT total_connections"},
		{STAT_CONN_STRUCTURES, "STAT connection_structures"},
		{STAT_CMD_GET, "STAT cmd_get"}, 
		{STAT_CMD_SET, "STAT cmd_set"},
		{STAT_CMD_FLUSH, "STAT cmd_flush"},
		{STAT_GET_HITS, "STAT get_hits"},
		{STAT_GET_MISSES, "STAT get_misses"},
		{STAT_DELETE_MISSES, "STAT delete_misses"},
		{STAT_DELETE_HITS, "STAT delete_hits"},
		{STAT_INCR_MISSES, "STAT incr_misses"},
		{STAT_INCR_HITS, "STAT incr_hits"},
		{STAT_DECR_MISSES, "STAT decr_misses"},
		{STAT_DECR_HITS, "STAT decr_hits"},
		{STAT_CAS_MISSES, "STAT cas_misses"},
		{STAT_CAS_HITS, "STAT cas_hits"},
		{STAT_BYTES_READ, "STAT bytes_read"},
		{STAT_BYTES_WRITTEN, "STAT bytes_written"},
		{STAT_LIMIT_MAXBYTES, "STAT limit_maxbytes"},
		{STAT_LISTEN_DISABLED_NUM, "STAT listen_disabled_num"},
		{STAT_THREADS, "STAT threads"},
		{STAT_CONN_YIELDS, "STAT conn_yields"},
		{STAT_BYTES, "STAT bytes"},
		{STAT_CURR_ITEMS, "STAT curr_items"},
		{STAT_TOTAL_ITEMS, "STAT total_items"},
		{STAT_EVICTIONS, "STAT evictions"},
		{STAT_RECLAIMED, "STAT reclaimed"},
		{-1, NULL}
	};

	if (NULL == pstMCServer || NULL == pstMCStats || 0 > pstMCServer->nSockFD || 0 == pstMCServer->nTimeout)
		return MCACHE_ERR_INVAL;

	memset(buffer, 0, buffer_size);
	memcpy(buffer, "stats\r\n", 7);

	timeout = pstMCServer->nTimeout;
	check_time = time(NULL);

	if (MCACHE_OK == (ret = s_SockWrite(pstMCServer->nSockFD, buffer, strlen(buffer), timeout))) {
		timeout -= time(NULL) - check_time;
		memset(buffer, 0, buffer_size);

		if (MCACHE_OK == (ret = s_SockRead(pstMCServer->nSockFD, buffer, MCACHE_VALUE_MAX - 1, &read_count, timeout))) {
			char *bgn = NULL;
			char *end = NULL;

			while (1) {
				if (-1 == stat_list[i].idx || NULL == stat_list[i].value)
					break;

				if (NULL == (bgn = strstr(buffer, stat_list[i].value)) || NULL == (end = strstr(bgn, "\r\n"))) {
					i++;
					continue;
				}

				bgn += strlen(stat_list[i].value);
				*end = '\0';

				switch (stat_list[i].idx) {
					case STAT_PID:
						pstMCStats->nPid = strtol(bgn, NULL, 10);
						break;
					case STAT_UPTIME:
						pstMCStats->nUptime = strtol(bgn, NULL, 10);
						break;
					case STAT_TIME:
						pstMCStats->tTime = strtol(bgn, NULL, 10);
						break;
					case STAT_VERSION:
						if (NULL != pstMCStats->pszVersion)
							free(pstMCStats->pszVersion);

						pstMCStats->pszVersion = strdup(bgn);
						break;
					case STAT_POINTER_SIZE:
						pstMCStats->nPointerSize = strtol(bgn, NULL, 10);
						break;
					case STAT_RUSAGE_USER:
						if (NULL != pstMCStats->pszRUsageUser)
							free(pstMCStats->pszRUsageUser);

						pstMCStats->pszRUsageUser = strdup(bgn);
						break;
					case STAT_RUSAGE_SYSTEM:
						if (NULL != pstMCStats->pszRUsageSystem)
							free(pstMCStats->pszRUsageSystem);

						pstMCStats->pszRUsageSystem = strdup(bgn);
						break;
					case STAT_CURR_CONNS:
						pstMCStats->nCurrentConnections = strtol(bgn, NULL, 10);
						break;
					case STAT_TOTAL_CONNS:
						pstMCStats->nTotalConnections = strtol(bgn, NULL, 10);
						break;
					case STAT_CONN_STRUCTURES:
						pstMCStats->nConnectionStructures = strtol(bgn, NULL, 10);
						break;
					case STAT_CMD_GET:
						pstMCStats->nCmdGet = strtoll(bgn, NULL, 10);
						break;
					case STAT_CMD_SET:
						pstMCStats->nCmdSet = strtoll(bgn, NULL, 10);
						break;
					case STAT_GET_HITS:
						pstMCStats->nGetHits = strtoll(bgn, NULL, 10);
						break;
					case STAT_GET_MISSES:
						pstMCStats->nGetMisses = strtoll(bgn, NULL, 10);
						break;
					case STAT_BYTES_READ:
						pstMCStats->nBytesRead = strtoll(bgn, NULL, 10);
						break;
					case STAT_BYTES_WRITTEN:
						pstMCStats->nBytesWritten = strtoll(bgn, NULL, 10);
						break;
					case STAT_LIMIT_MAXBYTES:
						pstMCStats->nLimitMaxbytes = strtol(bgn, NULL, 10);
						break;
					case STAT_THREADS:
						pstMCStats->nThreads = strtol(bgn, NULL, 10);
						break;
					case STAT_BYTES:
						pstMCStats->nBytes = strtoll(bgn, NULL, 10);
						break;
					case STAT_CURR_ITEMS:
						pstMCStats->nCurrentItems = strtol(bgn, NULL, 10);
						break;
					case STAT_TOTAL_ITEMS:
						pstMCStats->nTotalItems = strtol(bgn, NULL, 10);
						break;
					case STAT_EVICTIONS:
						pstMCStats->nEvictions = strtoll(bgn, NULL, 10);
						break;
				}				

				*end = '\r';
				i++;
			}
			
		}
	}
end:
	return ret;
}
