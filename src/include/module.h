#ifndef _MODULES_H_
#define _MODULES_H_

typedef int (*module_init_t)(void);
typedef void (*module_fini_t)(void);

typedef struct module
{
	char		*mod_name;
	module_init_t	 mod_init;
	module_fini_t	 mod_fini;
	void		*mod_handle;
} module_t;

#ifdef DYNAMIC

#define MODULE_LOAD_DB		module_glob(cf_dbt_mod_path)
#define MODULE_LOAD_TABLE	module_glob(cf_tables_mod_path)
#define MODULE_LOAD_ACL		module_glob(cf_acl_mod_path)

#else

#define MODULE_LOAD_DB		module_load_db()
#define MODULE_LOAD_TABLE	module_load_tables()
#define MODULE_LOAD_ACL		module_load_acl()

void module_load_db(void);
void module_load_tables(void);
void module_load_acl(void);


/*
 * Module Prototypes
 */

int bdb_init(void);
int sakila_init(void);
int rbl_init(void);
int spamd_init(void);
int spf_init(void);
int string_init(void);
int cast_init(void);
int test_init(void);

int delivered_init(void);

void sakila_fini(void);
void rbl_fini(void);
void spf_fini(void);
void test_fini(void);

#endif /* DYNAMIC */

/*
 * Prototypes
 */

void module_glob(const char *path);
void module_load(module_t *mod);
void module_init(void);
void module_clear(void);
#endif /* _MODULES_H_ */
