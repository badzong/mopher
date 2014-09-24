#ifndef _MODULES_H_
#define _MODULES_H_

typedef int (*module_init_t)(void);
typedef void (*module_fini_t)(void);

typedef struct module
{
	char		*mod_name;
	char		*mod_path;
	module_init_t	 mod_init;
	module_fini_t	 mod_fini;
	void		*mod_handle;
} module_t;

/*
 * Prototypes
 */

void module_glob(const char *path);
void module_load(char *file);
void module_init(int glob, ...);
void module_clear(void);

#endif /* _MODULES_H_ */
