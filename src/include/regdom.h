#ifndef _REGDOM_H_
#define _REGDOM_H_

#define NS_MAXDNAME	1025	/* arpa/nameser.h */

struct rule
{
	char name[NS_MAXDNAME];
	int exception;
	int wildcard;
};
typedef struct rule rule_t;

char* regdom (char* name);
void regdom_test (void);

#endif /* _REGDOM_H_ */
