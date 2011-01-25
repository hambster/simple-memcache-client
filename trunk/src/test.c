#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memcacheclient/memcacheclient.h"

struct test
{
	int id;
	char name[20];
};

int
main(void)
{
	int ret = 0;
	MemCacheServer server;
	MemCacheData data;

	memset(&server, 0, sizeof(MemCacheServer));
	memset(&data, 0, sizeof(MemCacheData));

	if (MCACHE_OK != (ret = MCACHE_ServerInit(&server, "127.0.0.1", 11211, 200, 0))) {
		printf("init error (%d)\n", ret);
		goto end;
	}

	data.pszDataKey = "name@wayne_lin";

	printf("before gets\n");
	if (MCACHE_OK != (ret = MCACHE_DataGets(&server, &data, 1))) {
		printf("get error (%d)\n", ret);
	}
	else {
		struct test *t = (struct test *) data.pDataValue;
		printf("id = %d, name = %s\n", t->id, t->name);
	}


	MemCacheData *dataList = NULL;
	char key[20];
	int num = 20;
	int i = 0;
	struct test tt;
	memset(&tt, 0, sizeof(struct test));
	if (NULL != (dataList = malloc(sizeof(MemCacheData) * num))) {
		memset(dataList, 0, sizeof(MemCacheData) * num);
		for (i = 0; i < num; i++) {
			snprintf(key, 20, "%d", i + 1);
			dataList[i].pszDataKey = strdup(key);
			dataList[i].pDataValue = &tt;
			tt.id = i  + 10;
			snprintf(tt.name, 19, "name %d", i + 15);
			dataList[i].nDataLen = sizeof(struct test);
			printf("set [%d] = %d\n", i, MCACHE_DataSet(&server, dataList + i));
		}
	}

	ret = MCACHE_DataGets(&server, dataList, num);
	printf("gets = (%d)\n", ret);
	if (MCACHE_OK == ret || MCACHE_ERR_PARTIAL == ret) {
		for (i = 0; i < num; i++) {
			struct test *ttt = dataList[i].pDataValue;
			printf("[%d] = %d, %s\n", i, ttt->id, ttt->name);
		}

		if (NULL != dataList[0].pDataValue) {
			printf("[%d] = %s, increment:\n", 0, dataList[0].pDataValue);
			snprintf(key, 19, "%d", 1);
			ret = MCACHE_DataIncrement(&server, dataList, 5);
			if (MCACHE_OK == ret) {
				printf("\t%s\n", dataList[0].pDataValue);
			}
			else {
				printf("increment = (%d)\n", ret);
			}
		}
	}

	MemCacheStats stats;

	memset(&stats, 0, sizeof(MemCacheStats));

	ret = MCACHE_ServerStats(&server, &stats);
	printf("stats = (%d)\n", ret);

	if (MCACHE_OK == ret) {
		printf("stats pid = %d\n", stats.nPid);
		printf("stats uptime = %d\n", stats.nUptime);
		printf("stats time = %d\n", stats.tTime);
		printf("stats version = %s\n", stats.pszVersion);
		printf("stats pointer size = %d\n", stats.nPointerSize);
		printf("stats rusage user = %s\n", stats.pszRUsageUser);
		printf("stats rusage system = %s\n", stats.pszRUsageSystem);
		printf("stats current items = %d\n", stats.nCurrentItems);
		printf("stats total items = %d\n", stats.nTotalItems);
		printf("stats bytes = %llu\n", stats.nBytes);
		printf("stats current conns = %d\n", stats.nCurrentConnections);
		printf("stats total conns = %d\n", stats.nTotalConnections);
		printf("stats conn structures = %d\n", stats.nConnectionStructures);
		printf("stats cmd get = %lld\n", stats.nCmdGet);
		printf("stats cmd set = %lld\n", stats.nCmdSet);
		printf("stats get hits = %lld\n", stats.nGetHits);
		printf("stats get misses = %lld\n", stats.nGetMisses);
		printf("stats evictions = %lld\n", stats.nEvictions);
		printf("stats bytes read = %lld\n", stats.nBytesRead);
		printf("stats bytes written = %lld\n", stats.nBytesWritten);
		printf("stats limit maxbytes = %d\n", stats.nLimitMaxbytes);
		printf("stats threads = %d\n", stats.nThreads);
	}



end:
	exit(0);
}
