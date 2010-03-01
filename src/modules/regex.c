#include <stdlib.h>
#include <sys/types.h>
#include <regex.h>

#include <mopher.h>

static sht_t		*regex_compiled;
static pthread_mutex_t	 regex_mutex = PTHREAD_MUTEX_INITIALIZER;


static void
regex_delete(regex_t *preg)
{
	regfree(preg);
	free(preg);

	return;
}


static regex_t *
regex_create(char *pattern, int flags)
{
	regex_t *preg = NULL;
	char error[1024];
	int e;

	preg = malloc(sizeof (regex_t));
	if (preg == NULL)
	{
		log_error("regex_create: malloc");
		goto error;
	}

	e = regcomp(preg, pattern, flags);
	if (e)
	{
		regerror(e, preg, error, sizeof error);
		log_error("regex_create: regcomp: %s", error);

		goto error;
	}

	if (pthread_mutex_lock(&regex_mutex))
	{
		log_error("regex_create: pthread_mutex_lock");
		goto error;
	}

	if (sht_insert(regex_compiled, pattern, preg))
	{
		log_error("regex_create: sht_insert failed");
		goto error;
	}

	if (pthread_mutex_unlock(&regex_mutex))
	{
		log_error("regex_create: pthread_mutex_unlock");
	}

	return preg;

error:

	if (preg)
	{
		free(preg);
	}

	return NULL;
}


static regex_t *
regex_compile(char *pattern, int flags)
{
	regex_t *preg;

	preg = sht_lookup(regex_compiled, pattern);
	if (preg == NULL)
	{
		preg = regex_create(pattern, flags);
		if (preg == NULL)
		{
			log_error("regex_compile: regex_create failed");
			return NULL;
		}
	}

	return preg;
}


static var_t *
regex_match(int argc, void **argv)
{
	regex_t *preg;

	preg = regex_compile(argv[0], REG_EXTENDED | REG_NOSUB);
	if (preg == NULL)
	{
		log_error("regex_match: regex_compile failed");
		return NULL;
	}

	if (regexec(preg, argv[1], 0, NULL, 0))
	{
		return EXP_FALSE;
	}

	return EXP_TRUE;
}


static var_t *
regex_imatch(int argc, void **argv)
{
	regex_t *preg;

	preg = regex_compile(argv[0], REG_EXTENDED | REG_NOSUB | REG_ICASE);
	if (preg == NULL)
	{
		log_error("regex_match: regex_compile failed");
		return NULL;
	}

	if (regexec(preg, argv[1], 0, NULL, 0))
	{
		return EXP_FALSE;
	}

	return EXP_TRUE;
}


int
regex_init(void)
{
	regex_compiled = sht_create(13, (sht_delete_t) regex_delete);
	if (regex_compiled == NULL)
	{
		log_error("regex_init: sht_create failed");
		return -1;
	}

	acl_function_register("regex_match", AF_SIMPLE,
	    (acl_function_callback_t) regex_match, VT_STRING, VT_STRING, 0);
	acl_function_register("regex_imatch", AF_SIMPLE,
	    (acl_function_callback_t) regex_imatch, VT_STRING, VT_STRING, 0);

	return 0;
}

void
regex_fini(void)
{
	sht_delete(regex_compiled);

	return;
}
