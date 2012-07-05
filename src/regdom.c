#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include <mopher.h>

static sht_t* rules_ht = NULL;
static rule_t rules[] =
#	include "regdom_rules"
;

static void
regdom_init ()
{
	int i;
	int n = sizeof rules / sizeof (rule_t);

	rules_ht = sht_create(2*n, NULL);
	for (i=0; i<n; i++) {
		sht_insert(rules_ht, rules[i].name, &rules[i]);
	}
}

char*
regdom (char* name)
{
	rule_t* r;
	char* prev = NULL;
	char* curr = NULL;
	char* next = name;

	if (!next) {
		return NULL;
	}
	if (!rules_ht) {
		regdom_init();
	}
	do {
		while (*next == '.') {
			next++;
		}
		if (r = sht_lookup(rules_ht, next)) {
			if (r->exception) {
				return next;
			}
			if (r->wildcard) {
				return prev;
			}
			break;
		}
		prev = curr;
		curr = next;
	} while (next = strchr(next, '.'));

	if (curr && !strchr(curr, '.')) {
		return prev;
	}
	return curr;
}

static void
regdom_assert (char* in, char* out)
{
	char* msg;
	char* res = regdom(in);
	res = res ?res :"NULL";
	out = out ?out :"NULL";
	msg = strcmp(res, out) ?"FAIL" :"OK";

	printf("%4s:	%-40s %-20s %s\n", msg, in, res, out);
}

void
regdom_test (void)
{
	/* custom */
	regdom_assert("mail-wg0-f45.google.com", "google.com");
	regdom_assert("blu0-omc2-s37.blu0.hotmail.com", "hotmail.com");
	regdom_assert("ng3-ip10.bullet.mail.bf1.yahoo.com", "yahoo.com");

	/* http://publicsuffix.org/list/test.txt */
	/* # NULL input. */
	regdom_assert(NULL, NULL);
	/* # Mixed case. */
	/* regdom_assert("COM", NULL); */
	/* regdom_assert("example.COM", "example.com"); */
	/* regdom_assert("WwW.example.COM", "example.com"); */
	/* # Leading dot. */
	/* regdom_assert(".com", NULL); */
	/* regdom_assert(".example", NULL); */
	/* regdom_assert(".example.com", NULL); */
	/* regdom_assert(".example.example", NULL); */
	/* # Unlisted TLD. */
	/* regdom_assert("example", NULL); */
	/* regdom_assert("example.example", NULL); */
	/* regdom_assert("b.example.example", NULL); */
	/* regdom_assert("a.b.example.example", NULL); */
	/* # Listed, but non-Internet, TLD. */
	/* #regdom_assert("local", NULL); */
	/* #regdom_assert("example.local", NULL); */
	/* #regdom_assert("b.example.local", NULL); */
	/* #regdom_assert("a.b.example.local", NULL); */
	/* # TLD with only 1 rule. */
	regdom_assert("biz", NULL);
	regdom_assert("domain.biz", "domain.biz");
	regdom_assert("b.domain.biz", "domain.biz");
	regdom_assert("a.b.domain.biz", "domain.biz");
	/* # TLD with some 2-level rules. */
	regdom_assert("com", NULL);
	regdom_assert("example.com", "example.com");
	regdom_assert("b.example.com", "example.com");
	regdom_assert("a.b.example.com", "example.com");
	regdom_assert("uk.com", NULL);
	regdom_assert("example.uk.com", "example.uk.com");
	regdom_assert("b.example.uk.com", "example.uk.com");
	regdom_assert("a.b.example.uk.com", "example.uk.com");
	regdom_assert("test.ac", "test.ac");
	/* # TLD with only 1 (wildcard) rule. */
	regdom_assert("cy", NULL);
	regdom_assert("c.cy", NULL);
	regdom_assert("b.c.cy", "b.c.cy");
	regdom_assert("a.b.c.cy", "b.c.cy");
	/* # More complex TLD. */
	regdom_assert("jp", NULL);
	regdom_assert("test.jp", "test.jp");
	regdom_assert("www.test.jp", "test.jp");
	regdom_assert("ac.jp", NULL);
	regdom_assert("test.ac.jp", "test.ac.jp");
	regdom_assert("www.test.ac.jp", "test.ac.jp");
	regdom_assert("kyoto.jp", NULL);
	regdom_assert("c.kyoto.jp", NULL);
	regdom_assert("b.c.kyoto.jp", "b.c.kyoto.jp");
	regdom_assert("a.b.c.kyoto.jp", "b.c.kyoto.jp");
	regdom_assert("pref.kyoto.jp", "pref.kyoto.jp");	/* # Exception rule. */
	regdom_assert("www.pref.kyoto.jp", "pref.kyoto.jp");	/* # Exception rule. */
	regdom_assert("city.kyoto.jp", "city.kyoto.jp");	/* # Exception rule. */
	regdom_assert("www.city.kyoto.jp", "city.kyoto.jp");	/* # Exception rule. */
	/* # TLD with a wildcard rule and exceptions. */
	regdom_assert("om", NULL);
	regdom_assert("test.om", NULL);
	regdom_assert("b.test.om", "b.test.om");
	regdom_assert("a.b.test.om", "b.test.om");
	regdom_assert("songfest.om", "songfest.om");
	regdom_assert("www.songfest.om", "songfest.om");
	/* # US K12. */
	regdom_assert("us", NULL);
	regdom_assert("test.us", "test.us");
	regdom_assert("www.test.us", "test.us");
	regdom_assert("ak.us", NULL);
	regdom_assert("test.ak.us", "test.ak.us");
	regdom_assert("www.test.ak.us", "test.ak.us");
	regdom_assert("k12.ak.us", NULL);
	regdom_assert("test.k12.ak.us", "test.k12.ak.us");
	regdom_assert("www.test.k12.ak.us", "test.k12.ak.us");
}
