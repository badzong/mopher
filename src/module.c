#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <glob.h>
#include <string.h>

#include <mopher.h>

#define BUFLEN 1024

static ll_t *module_list;
static ll_t *module_buffers;


static void
module_symbol_name(char *path, char *buffer, int size)
{
	char *base, *ext;
	int len;

	base = strrchr(path, '/');
	if (base == NULL)
	{
		base = path;
	}
	else
	{
		++base;		/* Skip leading slash */
	}

	ext = strrchr(base, '.');
	if (ext == NULL)
	{
		len = strlen(base);
	}
	else
	{
		len = ext - base;
	}

	if (size < len + 1)
	{
		log_die(EX_SOFTWARE, "module_symbol_name: buffer exhausted");
	}

	strncpy(buffer, base, len);

	buffer[len] = 0;

	return;
}

static void *
module_symbol_load(void *handle, char *path, char *suffix, int die)
{
	char symbol[BUFLEN];
	char *error;
	int len;
	void *p;

	module_symbol_name(path, symbol, sizeof symbol);

	len = strlen(symbol);

	/*
	 * Append _suffix
	 */
	if (sizeof symbol < len + strlen(suffix) + 2)
	{
		log_die(EX_SOFTWARE, "module_symbol_load: buffer exhausted");
	}

	symbol[len++] = '_';

	strcpy(symbol + len, suffix);

	/*
	 * Clear existing error
	 */
	dlerror();

	/*
	 * Load symbol
	 */
	p = dlsym(handle, symbol);

	error = (char *) dlerror();
	if (error == NULL)
	{
		return p;
	}

	if(die)
	{
		log_sys_die(EX_SOFTWARE, "module_symbol_load: dlsym: %s", error);
	}

	log_debug("module_symbol_load: dlsym: %s", error);

	return p;
}


void
module_glob(const char *path)
{
	glob_t pglob;
	char pattern[BUFLEN];
	char **file;
	module_t *mod;
	int i;

	memset(&pglob, 0, sizeof(pglob));
	snprintf(pattern, sizeof(pattern), "%s/*.so", path);

	/*
	 * Glob path
	 */
	switch (glob(pattern, GLOB_ERR, NULL, &pglob))
	{
	case 0:
		break;

	case GLOB_NOMATCH:
		log_error("module_glob: no modules found in \"%s\"", path);
		return;

	default:
		log_die(EX_NOPERM, "module_glob: can't glob \"%s\"", path);
	}

	log_debug("module_glob: found %d modules in \"%s\"", pglob.gl_pathc,
	    path);

	/*
	 * Create module_t array
	 */
	mod = (module_t *) malloc((pglob.gl_pathc + 1) * sizeof (module_t));
	if (mod == NULL)
	{
		log_sys_die(EX_SOFTWARE, "module_glob: malloc");
	}

	/*
	 * Append buffer to module_buffers
	 */
	if (LL_INSERT(module_buffers, mod) == -1)
	{
		log_die(EX_SOFTWARE, "module_glob: LL_INSERT failed");
	}

	/*
	 * Prepare modules
	 */
	for (i = 0, file = pglob.gl_pathv; *file; ++file, ++i)
	{
		log_debug("module_glob: load \"%s\"", *file);

		mod[i].mod_name = strdup(*file);

		/*
		 * Open module
		 */
		mod[i].mod_handle = dlopen(*file, RTLD_LAZY);
		if (mod[i].mod_handle == NULL)
		{
			log_sys_die(EX_SOFTWARE, "module_glob: dlopen: %s",
			    dlerror());
		}

		/*
		 * Load Symbols
		 */
		mod[i].mod_init = module_symbol_load(mod[i].mod_handle, *file,
		    "init", 1);
		mod[i].mod_fini = module_symbol_load(mod[i].mod_handle, *file,
		    "fini", 0);
	}

	/*
	 * Close array
	 */
	memset(&mod[i], 0, sizeof (module_t));

	/*
	 * Load modules
	 */
	module_load(mod);

	globfree(&pglob);

	return;
}


void
module_load(module_t *mod)
{
	int i;

	/*
	 * Append modules
	 */
	for(i = 0; mod[i].mod_name; ++i)
	{
		if (mod[i].mod_init())
		{
			log_die(EX_SOFTWARE, "module_load: %s: init failed",
			    mod[i].mod_name);
		}

		if (LL_INSERT(module_list, &mod[i]) == -1)
		{
			log_die(EX_SOFTWARE, "module_create: LL_INSERT "
			    "failed");
		}
	}

	return;
}


void
module_init(void)
{
	/*
	 * Create module list
	 */
	module_list = ll_create();
	if (module_list == NULL)
	{
		log_die(EX_SOFTWARE, "module_load: ll_create failed");
	}

	/*
	 * Create module buffers
	 */
	module_buffers = ll_create();
	if (module_buffers == NULL)
	{
		log_die(EX_SOFTWARE, "module_glob: ll_create failed");
	}

	module_glob(cf_module_path);
}


static void
module_delete(module_t *mod)
{
	if (mod->mod_fini)
	{
		log_debug("module_delete: %s: fini", mod->mod_name);
		mod->mod_fini();
	}

	if (mod->mod_handle)
	{
		dlclose(mod->mod_handle);
	}

	free(mod->mod_name);

	return;
}

void
module_clear(void)
{
	ll_delete(module_list, (ll_delete_t) module_delete);
	ll_delete(module_buffers, (ll_delete_t) free);

	return;
}
