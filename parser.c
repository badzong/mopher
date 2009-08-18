#include <sys/types.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "log.h"

int8_t
parser(char *path, FILE ** input, int (*parser) (void))
{
	struct stat fs;

	if (stat(path, &fs) == -1) {
		log_error("parser: stat '%s'", path);
		return -1;
	}

	if (fs.st_size == 0) {
		log_notice("parser: '%s' is empty", path);
		return -1;
	}

	if ((*input = fopen(path, "r")) == NULL) {
		log_error("parser: fopen '%s'", path);
		return -1;
	}

	if (parser()) {
		log_error("parser: supplied parser failed");
		return -1;
	}

	fclose(*input);

	return 0;
}
