#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <malloc.h>
#include <idna.h>

#include <mopher.h>

#define REGDOM_BUCKETS 16384


static char *regdom_rules_buffer;
static sht_t regdom_ht;


void
regdom_clear (void)
{
	sht_clear(&regdom_ht);
	free(regdom_rules_buffer);

	return;
}


regdom_rule_t *
regdom_rule_create(char *name, int wildcard, int exception, int flags)
{
	regdom_rule_t *rule;

	rule = (regdom_rule_t *) malloc(sizeof (regdom_rule_t));
	if (rule == NULL)
	{
		log_die(EX_SOFTWARE, "regdom_rule_create: malloc failed");
	}

	rule->r_name = name;
	rule->r_wildcard = wildcard;
	rule->r_exception = exception;
	rule->r_flags = flags;

	return rule;
}

void
regdom_rule_destroy(regdom_rule_t *rule)
{
	if (rule->r_flags & REGDOM_FREE_NAME)
	{
		free(rule->r_name);
	}
	free(rule);

	return;
}

int
regdom_has_nonascii(char *name)
{
	unsigned char *p;

	// Check if a translation to punycode is neccessary.
	for (p = (unsigned char *) name; *p; ++p)
	{
		if (*p > 127)
		{
			return 1;
		}
	}

	return 0;
}

char *
regdom_strdup_idna(char *name)
{
	char *dup = NULL;

	// No translation required
	if (!regdom_has_nonascii(name))
	{
		dup = strdup(name);
		if (dup == NULL)
		{
			log_error("regdom_strdup_idna: malloc failed");
		}
	}
	else
	{
		if (idna_to_ascii_8z(name, &dup, 0) != IDNA_SUCCESS)
		{
			log_error(
				"regdom_strdup_idna: idna_to_ascii_8z failed");
		}
	}

	// Convert to lowercase
	if (dup != NULL)
	{
		util_tolower(dup);
	}

	return dup;
}

void
regdom_load_rules (char *path)
{
	int n, wildcard, exception;
	char *p, *name, *saveptr;
	char *puny;
	regdom_rule_t *rule;

	n = util_file(path, &regdom_rules_buffer);
	if (n <= 0)
	{
		log_die(EX_SOFTWARE, "regdom_load_rules: failed to load rules "
			"from \"%s\"", path);
	}

	log_debug("regdom: %s: %d bytes\n", path, n);

	name = strtok_r(regdom_rules_buffer, "\n", &saveptr);
	n = 0;
	while (name != NULL)
	{
		// Strip comments
		p = strstr(name, "//");
		if (p != NULL)
		{
			*p = 0;
		}

		// Strip trailing spaces
		for(p = name + strlen(name); p > name && isspace(*p); --p);
		if (p < name + strlen(name))
		{
			*(p + 1) = 0;
		}

		// Skip empty lines
		if (strlen(name) == 0)
		{
			name = strtok_r(NULL, "\n", &saveptr);
			continue;
		}

		wildcard = 0;
		exception = 0;

		// Wildcard rule
		if (*name == '*')
		{
			++name;
			// Make the point optional
			if (*name == '.')
			{
				++name;
			}
			wildcard = 1;
		}

		// Exception rule
		if (*name == '!')
		{
			++name;
			exception = 1;
		}

		// Convert to lowercase
		util_tolower(name);

		// Add rule to regdom_ht
		rule = regdom_rule_create(name, wildcard, exception, 0);
		if (sht_insert(&regdom_ht, rule->r_name, rule))
		{
			log_die(EX_SOFTWARE, "regdom_load_rules: sht_insert "
				" failed");
		}

		// UTF-8 rules also need punycode
		if (regdom_has_nonascii(name))
		{
			puny = regdom_strdup_idna(name);
			if (puny == NULL)
			{
				log_die(EX_SOFTWARE, "regdom_load_rules: "
					"regdom_strdup_idna failed");
			}

			// Add punycode rule to regdom_ht
			rule = regdom_rule_create(puny, wildcard, exception,
				REGDOM_FREE_NAME);
			if (sht_insert(&regdom_ht, rule->r_name, rule))
			{
				log_die(EX_SOFTWARE, "regdom_load_rules: sht_insert "
					" failed");
			}
		}

		++n;
		name = strtok_r(NULL, "\n", &saveptr);
	}

	free(regdom_rules_buffer);

	log_debug("regdom: loaded %d rules", n);
}

void
regdom_init (void)
{
	if(sht_init(&regdom_ht, REGDOM_BUCKETS, (void *) regdom_rule_destroy))
	{
		log_die(EX_SOFTWARE, "regdom_init: sht_init failed");
	}

	regdom_load_rules(defs_regdom_rules);

#ifdef DEBUG
	regdom_test();
#endif
	return;
}

char*
regdom (char* name)
{
	regdom_rule_t* r;
	char* prev = NULL;
	char* curr = NULL;
	char* next = name;

	if (name == NULL)
	{
		return NULL;
	}

	if (*name == '.')
	{
		return NULL;
	}

	do {
		for (; *next == '.'; ++next);
		if ((r = sht_lookup(&regdom_ht, next)))
		{
			if (r->r_exception)
			{
				return next;
			}
			if (r->r_wildcard)
			{
				return prev;
			}
			break;
		}
		prev = curr;
		curr = next;
	} while ((next = strchr(next, '.')));

	if (curr && !strchr(curr, '.'))
	{
		return prev;
	}
	return curr;
}

int
regdom_punycode (char *buffer, int size, char* name)
{
	char *puny = NULL;
	char *result;
	int len = 0;

	// Clear buffer for safety
	memset(buffer, 0, size);

	// Why does this need to work?
	if (name == NULL)
	{
		goto exit;
	}

	// Get punycode copy
	puny = regdom_strdup_idna(name);
	if (puny == NULL)
	{
		log_error("regdom: regdom_strdup_idna failed");
		goto error;
	}

	result = regdom(puny);
	if (result == NULL)
	{
		goto exit;
	}
	
	len = strlen(result);
	if (len >= size)
	{
		log_error("regdom: buffer exhausted");
		goto error;
	}

	strcpy(buffer, result);

exit:
	if (puny)
	{
		free(puny);
	}

	return len;

error:
	if (puny)
	{
		free(puny);
	}

	return -1;
}


#ifdef DEBUG
static void
regdom_assert (char* test, char* exp)
{
	char *got;
	char *dup;

	if (test == NULL)
	{
		dup = NULL;
	}
	else
	{
		dup = strdup(test);
		if (dup == NULL)
		{
			log_die(EX_SOFTWARE, "regdom_assert: strdup failed");
		}
		util_tolower(dup);
	}

	got = regdom(dup);

	exp = exp ? exp: "NULL";
	got = got ? got: "NULL";

	if (strcmp(got, exp))
	{
		log_debug("regdom_assert: test \"%s\" "
			"expected \"%s\" got \"%s\"", test, exp, got);
	}

	free(dup);
}
#include "regdom_test.c"
#endif
