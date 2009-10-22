#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <glob.h>
#include <string.h>

#include "log.h"
#include "ll.h"

#define BUFLEN 1024

static ll_t *module_list = NULL;

void *
module_open(const char *path)
{
	void *handle;
	int (*init) (void);
	char *error;

	log_debug("module_open: load \"%s\"", path);

	if ((handle = dlopen(path, RTLD_LAZY)) == NULL) {
		log_error("module_load: dlopen: %s", dlerror());
		return NULL;
	}

	dlerror();		/* Clear any existing error */

	/*
	 * Run init() if exists
	 */
	if ((init = dlsym(handle, "init"))) {
		if (init()) {
			log_die(EX_SOFTWARE, "module_open: \"%s\": init"
				" failed", path);
		}
	}

	error = (char *) dlerror();
	if (error != NULL)
	{
		log_die(EX_SOFTWARE, "module_open: dlsym: %s", error);
	}

	if (LL_INSERT(module_list, handle) == -1) {
		log_die(EX_SOFTWARE, "module_open: LL_INSERT failed");
	}

	return handle;
}

int
module_load(const char *path)
{
	glob_t pglob;
	char pattern[BUFLEN];
	char **module;
	void *handle;

	memset(&pglob, 0, sizeof(pglob));

	snprintf(pattern, sizeof(pattern), "%s/*.so", path);

	switch (glob(pattern, GLOB_ERR, NULL, &pglob)) {

	case 0:
		break;

	case GLOB_NOMATCH:
		log_error("module_load: no modules found in \"%s\"", path);
		return 0;

	default:
		log_die(EX_NOPERM, "module_load: can't glob \"%s\"", path);
	}

	log_debug("module_load: found %d modules in \"%s\"", pglob.gl_pathc,
		  path);

	for (module = pglob.gl_pathv; *module; ++module) {
		if ((handle = module_open(*module)) == NULL) {
			log_die(EX_SOFTWARE, "module_load: module_open \"%s\""
				" failed", *module);
		}
	}

	globfree(&pglob);

	return 0;
}

int
module_init(void)
{
	if ((module_list = ll_create()) == NULL) {
		log_die(EX_SOFTWARE, "module_init: ll_create failed");
	}

	return 0;
}

void
module_clear(void)
{
	void *handle;
	void (*fini) (void);
	char *error;

	if (module_list == NULL) {
		return;
	}

	ll_rewind(module_list);
	while ((handle = ll_next(module_list))) {
		dlerror();	/* Clear any existing error */

		if ((fini = dlsym(handle, "fini"))) {
			fini();
		}

		error = (char *) dlerror();
		if (error != NULL)
		{
			log_info("module_clear: dlsym: %s", error);
		}

		dlclose(handle);
	}
	
	ll_delete(module_list, NULL);

	return;
}
