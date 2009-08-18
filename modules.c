#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <glob.h>
#include <string.h>

#include "log.h"
#include "ll.h"

#define BUFLEN 1024

static ll_t *modules = NULL;

void *
modules_open(const char *path)
{
	void *handle;
	int (*init) (void);
	char *error;

	log_debug("modules_open: load \"%s\"", path);

	if ((handle = dlopen(path, RTLD_LAZY)) == NULL) {
		log_error("modules_load: dlopen: %s", dlerror());
		return NULL;
	}

	dlerror();		/* Clear any existing error */

	/*
	 * Run init() if exists
	 */
	if ((init = dlsym(handle, "init"))) {
		if (init()) {
			log_die(EX_SOFTWARE, "modules_open: \"%s\": init"
				" failed", path);
		}
	}

	if ((error = dlerror()) != NULL) {
		log_die(EX_SOFTWARE, "modules_open: dlsym: %s", error);
	}

	if (LL_INSERT(modules, handle) == -1) {
		log_die(EX_SOFTWARE, "modules_open: LL_INSERT failed");
	}

	return handle;
}

int
modules_load(const char *path)
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
		log_error("modules_load: no modules found in \"%s\"", path);
		return 0;

	default:
		log_die(EX_NOPERM, "modules_load: can't glob \"%s\"", path);
	}

	log_debug("modules_load: found %d modules in \"%s\"", pglob.gl_pathc,
		  path);

	for (module = pglob.gl_pathv; *module; ++module) {
		if ((handle = modules_open(*module)) == NULL) {
			log_die(EX_SOFTWARE, "modules_load: modules_open \"%s\""
				" failed", *module);
		}
	}

	return 0;
}

int
modules_init(void)
{
	if ((modules = ll_create()) == NULL) {
		log_die(EX_SOFTWARE, "modules_init: ll_create failed");
	}

	return 0;
}

void
modules_clear(void)
{
	void *handle;
	void (*fini) (void);
	char *error;

	if (modules == NULL) {
		return;
	}

	ll_rewind(modules);
	while ((handle = ll_next(modules))) {
		dlerror();	/* Clear any existing error */

		if ((fini = dlsym(handle, "fini"))) {
			fini();
		}

		if ((error = dlerror()) != NULL) {
			log_info("modules_clear: dlsym: %s", error);
		}

		dlclose(handle);
	}

	return;
}
