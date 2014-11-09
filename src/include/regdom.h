#ifndef _REGDOM_H_
#define _REGDOM_H_

struct regdom_rule
{
	char *r_name;
	char  r_exception;
	char  r_wildcard;
};
typedef struct regdom_rule regdom_rule_t;

void regdom_clear (void);
void regdom_init (void);
int regdom_idna(char *buffer, int size, char *name);
int regdom_punycode (char *buffer, int size, char* name);
int regdom_test_init(void);
void regdom_test (int n);

#endif /* _REGDOM_H_ */
