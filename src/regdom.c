#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <idna.h>
#include <stringprep.h>
#include <punycode.h>

#include <mopher.h>

#define REGDOM_BUCKETS 16384
#define BUFLEN 1024


static char *regdom_rules_buffer;
static sht_t regdom_ht;


void
regdom_clear (void)
{
	sht_clear(&regdom_ht);
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

	rule->r_name = strdup(name);
	if (rule->r_name == NULL)
	{
		log_die(EX_SOFTWARE, "regdom_rule_create: malloc failed");
	}

	rule->r_wildcard = wildcard;
	rule->r_exception = exception;

	return rule;
}

void
regdom_rule_destroy(regdom_rule_t *rule)
{
	free(rule->r_name);
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

int
regdom_idna(char *buffer, int size, char *input)
{
	size_t items;
	int domain_len;
	char *saveptr;
	char *domain;
	size_t olen;
	int len = 0;
	char *name = NULL;
	uint32_t *ucs4 = NULL;
	int r;

	buffer[0] = 0;

	name = strdup(input);
	if (name == NULL)
	{
		log_error("regdom_idna: malloc failed");
		return -1;
	}

	domain = strtok_r(name, ".", &saveptr);
	for (; domain != NULL; domain = strtok_r(NULL, ".", &saveptr))
	{
		// Append a dot for the next domain
		if (len)
		{
			if (len + 2 > size)
			{
				goto nospace;
			}
			strcat(buffer, ".");
			++len;
		}

		domain_len = strlen(domain);

		// Domain is all-ASCII
		if (!regdom_has_nonascii(domain))
		{
			if (len + domain_len + 1 > size)
			{
				goto nospace;
			}
			strcat(buffer, domain);
			len += domain_len;
			continue;
		}

		// Unicode Domain
		if (len + 5 > size)
		{
			goto nospace;
		}
		strcat(buffer, "xn--");
		len += 4;

		// Convert domain to UCS-4
		ucs4 = stringprep_utf8_to_ucs4(domain, domain_len, &items);
		if (ucs4 == NULL)
		{
			log_error("regdom_idna: stringprep_utf8_to_ucs4 failed");
			goto error;
		}

		// Convert domain to punycode
		olen = size - len - 1;
		r = punycode_encode(items, ucs4, NULL, &olen, buffer + len);
		if (r != PUNYCODE_SUCCESS)
		{
			log_error("regdom_idna: punycode_encode: %s", punycode_strerror(r));
			goto error;
		}
		buffer[len + olen] = 0;

		free(ucs4);
		ucs4 = NULL;

		len += olen;
	}

	free(name);

	return 0;

nospace:
	log_error("regdom_idna: buffer exhausted");

error:
	if (name)
	{
		free(name);
	}
	if (ucs4)
	{
		free(ucs4);
	}

	return -1;
}


void
regdom_load_rules (char *path)
{
	int n, wildcard, exception;
	char *p, *name, *saveptr;
	char puny[BUFLEN];
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
		for(p = name + strlen(name); p > name && isspace((int) *p); --p);

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
		if (!regdom_has_nonascii(name))
		{
			util_tolower(name);
		}

		// Add rule to regdom_ht
		rule = regdom_rule_create(name, wildcard, exception);
		if (sht_insert(&regdom_ht, rule->r_name, rule))
		{
			log_die(EX_SOFTWARE, "regdom_load_rules: sht_insert "
				" failed");
		}

		// UTF-8 rules also need punycode
		if (regdom_has_nonascii(name))
		{
			regdom_idna(puny, sizeof puny, name);

			// Add punycode rule to regdom_ht
			rule = regdom_rule_create(puny, wildcard, exception);
			if (sht_insert(&regdom_ht, rule->r_name, rule))
			{
				log_die(EX_SOFTWARE, "regdom_load_rules: sht_insert "
					" failed");
			}
		}

		++n;
		name = strtok_r(NULL, "\n", &saveptr);
	}

	log_debug("regdom: loaded %d rules", n);
	free(regdom_rules_buffer);
}

void
regdom_init (void)
{
	if(sht_init(&regdom_ht, REGDOM_BUCKETS, (void *) regdom_rule_destroy))
	{
		log_die(EX_SOFTWARE, "regdom_init: sht_init failed");
	}

	regdom_load_rules(defs_regdom_rules);

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
	char puny[BUFLEN];
	char *result;
	int len = 0;

	// Set buffer to zero-length string for safety
	buffer[0] = 0;

	// Why does this need to work?
	if (name == NULL)
	{
		return 0;
	}

	// Get punycode copy
	if (regdom_idna(puny, sizeof puny, name))
	{
		log_error("regdom: regdom_idna failed");
		return -1;
	}

	result = regdom(puny);
	if (result == NULL)
	{
		return 0;
	}
	
	len = strlen(result);
	if (len >= size)
	{
		log_error("regdom: buffer exhausted");
		return -1;
	}

	strcpy(buffer, result);

	return len;
}


#ifdef DEBUG

struct regdom_test_case {
	char *rtc_test;
	char *rtc_exp;
	int   rtc_last;
};

int
regdom_test_init(void)
{
	regdom_init();
	return 0;
}

void
regdom_test(int n)
{
	struct regdom_test_case *rtc;
	struct regdom_test_case test_domains[] = {
#include "regdom_test.c"
		{NULL, NULL, 1}
	};
	char *test, *exp, *got, *dup;

	for (rtc = test_domains; !rtc->rtc_last; ++rtc)
	{
		test = rtc->rtc_test;
		exp = rtc->rtc_exp;

		if (test == NULL)
		{
			dup = NULL;
		}
		else
		{
			dup = strdup(test);
			if (!regdom_has_nonascii(dup))
			{
				util_tolower(dup);
			}
		}

		got = regdom(dup);

		exp = exp ? exp: "NULL";
		got = got ? got: "NULL";

		TEST_ASSERT(strcmp(got, exp) == 0, "\"%s\" expected \"%s\" "
			"got \"%s\"", test, exp, got);

		if (dup != NULL)
		{
			free(dup);
		}
	}

	return;
}
#endif
