#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <malloc.h>

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
regdom_rule_create(char *name, int wildcard, int exception)
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

	return rule;
}


void
regdom_load_rules (char *path)
{
	int n, wildcard, exception;
	char *p, *name, *saveptr;
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

		rule = regdom_rule_create(name, wildcard, exception);
		if (sht_insert(&regdom_ht, rule->r_name, rule))
		{
			log_die(EX_SOFTWARE, "regdom_load_rules: sht_insert "
				" failed");
		}

		++n;
		name = strtok_r(NULL, "\n", &saveptr);
	}

	log_debug("regdom: loaded %d rules", n);
}

void
regdom_init (void)
{
	if(sht_init(&regdom_ht, REGDOM_BUCKETS, free))
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

	if (!next)
	{
		return NULL;
	}

	do {
		while (*next == '.')
		{
			next++;
		}
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

#ifdef DEBUG
static void
regdom_assert (char* test, char* exp)
{
	char* got = regdom(test);
	got = got ?got :"NULL";
	exp = exp ?exp :"NULL";

	if (!strcmp(got, exp)) {
		return;
	}
	log_debug("regdom_assert: test \"%s\" "
		"expected \"%s\" got \"%s\"", test, exp, got);
}
#include "regdom_test.c"
#endif
