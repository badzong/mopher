#include "mopher.h"


void msync(char *table, var_t *record)
{
	char buffer[1024];

	var_dump_data(record, buffer, sizeof buffer);

	printf("SYNC: %s = %s\n", table, buffer);

	return;
}
