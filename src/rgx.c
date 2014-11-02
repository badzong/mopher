#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <regex.h>

#include <mopher.h>

void
rgx_delete(regex_t *rgx)
{
	regfree(rgx);
	free(rgx);

	return;
}


regex_t *
rgx_create(char *raw_pattern)
{
	regex_t *rgx = NULL;
	char error[1024];
	int e;
	int len;
	char *pattern;
	int flags = REG_EXTENDED | REG_NOSUB;

	len = strlen(raw_pattern);

	if (raw_pattern[len - 1] == 'i')
	{
		flags |= REG_ICASE;
		raw_pattern[len - 1] = 0;
	}

	pattern = util_strdupenc(raw_pattern, "//");

	log_debug("rgx_create: compile regex pattern %s", pattern);

	rgx = malloc(sizeof (regex_t));
	if (rgx == NULL)
	{
		log_sys_error("rgx_create: malloc");
		goto error;
	}

	e = regcomp(rgx, pattern, flags);
	if (e)
	{
		regerror(e, rgx, error, sizeof error);
		log_error("rgx_create: regcomp: %s", error);
		goto error;
	}

	free(pattern);

	return rgx;

error:
	if (rgx)
	{
		free(rgx);
	}

	return NULL;
}


int
rgx_match(regex_t *rgx, char *str)
{
	if (regexec(rgx, str, 0, NULL, 0))
	{
		return 0;
	}

	return 1;
}
