#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include <mopher.h>

#define nsnull NULL
#define PR_TRUE 1
#define PR_FALSE 0

static sht_t regdom_ht;
static regdom_rule_t regdom_rules[] =
#include "regdom_rules.c"
;


void
regdom_clear (void)
{
	sht_clear(&regdom_ht);

	return;
}

void
regdom_init (void)
{
	int buckets = sizeof regdom_rules / sizeof (regdom_rule_t) * 2;
	regdom_rule_t *r;


	if(sht_init(&regdom_ht, buckets, NULL))
	{
		log_die(EX_SOFTWARE, "regdom_init: sht_init failed");
	}

	for (r = regdom_rules; r->r_name; r++)
	{
		sht_insert(&regdom_ht, r->r_name, r);
	}

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
