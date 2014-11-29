#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <glob.h>
#include <string.h>

#include <mopher.h>

#define BUFLEN 1024

static ll_t *module_list;


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
module_load(char *file)
{
	module_t *mod;

	log_debug("module_glob: load \"%s\"", file);

	mod = (module_t *) malloc(sizeof (module_t));
	if (mod == NULL)
	{
		log_sys_die(EX_SOFTWARE, "module_load: malloc");
	}
	mod->mod_path = strdup(file);
	mod->mod_name = strrchr(mod->mod_path, '/') + 1;

	/*
	 * Open module
	 */
	mod->mod_handle = dlopen(file, RTLD_LAZY);
	if (mod->mod_handle == NULL)
	{
		log_sys_die(EX_SOFTWARE, "module_load: dlopen: %s", dlerror());
	}

	/*
	 * Load Symbols
	 */
	mod->mod_init = module_symbol_load(mod->mod_handle, file, "init", 1);
	mod->mod_fini = module_symbol_load(mod->mod_handle, file, "fini", 0);

	/*
	 * Initialize module
	 */
	if (mod->mod_init())
	{
		log_die(EX_SOFTWARE, "module_load: %s: init failed",
		    mod->mod_name);
	}

	if (LL_INSERT(module_list, mod) == -1)
	{
		log_die(EX_SOFTWARE, "module_load: LL_INSERT failed");
	}

	return;
}


void
module_glob(const char *path)
{
	glob_t pglob;
	char pattern[BUFLEN];
	char **file;

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
	 * Prepare modules
	 */
	for (file = pglob.gl_pathv; *file; ++file)
	{
		module_load(*file);
	}

	globfree(&pglob);

	return;
}


int
module_exists(char *mod)
{
	char buffer[BUFLEN];
	char *module_path;
	int n;

	module_path = cf_module_path? cf_module_path: defs_module_path;

	n = snprintf(buffer, sizeof buffer, "%s/%s", module_path, mod);
	if (n >= sizeof buffer)
	{
		log_error("module_exists: buffer exhausted");
		return -1;
	}

	return util_file_exists(buffer);
}

void
module_init(int glob, ...)
{
	va_list ap;
	char *module_name;
	char *module_path;
	char buffer[BUFLEN];

	module_list = ll_create();
	if (module_list == NULL)
	{
		log_die(EX_SOFTWARE, "module_init: ll_create failed");
	}

	module_path = cf_module_path? cf_module_path: defs_module_path;

	// Search modules in the filesystem
	if (glob)
	{
		module_glob(module_path);
		return;
	}

	// Load modules supplied in va_list
	va_start(ap, glob);
	for (;;)
	{
		module_name = va_arg(ap, char *);
		if (module_name == NULL)
		{
			break;
		}

		if (strlen(module_path) + strlen(module_name) + 2 > sizeof buffer)
		{
			log_die(EX_SOFTWARE, "module_init: buffer exhausted");
		}

		strcpy(buffer, module_path);
		strcat(buffer, "/");
		strcat(buffer, module_name);

		module_load(buffer);
	}
	va_end(ap);

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

	free(mod->mod_path);
	free(mod);

	return;
}

void
module_clear(void)
{
	if (module_list)
	{
		ll_delete(module_list, (ll_delete_t) module_delete);
	}

	return;
}
