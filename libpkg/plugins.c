/*
 * Copyright (c) 2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <fts.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <libutil.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

#define PLUGINS_NUMFIELDS 4

struct plugins_hook {
	pkg_plugins_hook_t hook;				/* plugin hook type */
	pkg_plugins_callback callback;                          /* plugin callback function */
	STAILQ_ENTRY(plugins_hook) next;
};

struct pkg_plugins {
	struct sbuf *fields[PLUGINS_NUMFIELDS];
	void *lh;						/* library handle */
	STAILQ_HEAD(phooks, plugins_hook) phooks;		/* plugin hooks */
	STAILQ_ENTRY(pkg_plugins) next;
};

STAILQ_HEAD(plugins_head, pkg_plugins);
static struct plugins_head ph = STAILQ_HEAD_INITIALIZER(ph);

static int pkg_plugins_free(void);
static int pkg_plugins_hook_free(struct pkg_plugins *p);
static int pkg_plugins_hook_register(struct pkg_plugins *p, pkg_plugins_hook_t hook, pkg_plugins_callback callback);
static int pkg_plugins_hook_exec(struct pkg_plugins *p, pkg_plugins_hook_t hook, void *data, struct pkgdb *db);
static int pkg_plugins_hook_list(struct pkg_plugins *p, struct plugins_hook **h);

void *
pkg_plugins_func(struct pkg_plugins *p, const char *func)
{
	return (dlsym(p->lh, func));
}

static int
pkg_plugins_hook_free(struct pkg_plugins *p)
{
	struct plugins_hook *h = NULL;

	assert(p != NULL);

	while (!STAILQ_EMPTY(&p->phooks)) {
		h = STAILQ_FIRST(&p->phooks);
		STAILQ_REMOVE_HEAD(&p->phooks, next);
		free(h);
	}

	return (EPKG_OK);
}

static int
pkg_plugins_free(void)
{
	struct pkg_plugins *p = NULL;
	unsigned int i;

        while (!STAILQ_EMPTY(&ph)) {
                p = STAILQ_FIRST(&ph);
                STAILQ_REMOVE_HEAD(&ph, next);

		for (i = 0; i < PLUGINS_NUMFIELDS; i++)
			sbuf_delete(p->fields[i]);

		pkg_plugins_hook_free(p);
		free(p);
        }

	return (EPKG_OK);
}

static int
pkg_plugins_hook_register(struct pkg_plugins *p, pkg_plugins_hook_t hook, pkg_plugins_callback callback)
{
	struct plugins_hook *new = NULL;
	
	assert(p != NULL);
	assert(callback != NULL);

	if ((new = calloc(1, sizeof(struct plugins_hook))) == NULL) {
		pkg_emit_error("Cannot allocate memory");
		return (EPKG_FATAL);
	}

	new->hook |= hook;
	new->callback = callback;

	STAILQ_INSERT_TAIL(&p->phooks, new, next);

	return (EPKG_OK);
}

static int
pkg_plugins_hook_exec(struct pkg_plugins *p, pkg_plugins_hook_t hook, void *data, struct pkgdb *db)
{
	struct plugins_hook *h = NULL;
	
	assert(p != NULL);

	while (pkg_plugins_hook_list(p, &h) != EPKG_END)
		if (h->hook == hook) {
			printf(">>> Triggering execution of plugin '%s'\n",
			       pkg_plugins_get(p, PKG_PLUGINS_NAME));
			h->callback(data, db);
		}

	return (EPKG_OK);
}

static int
pkg_plugins_hook_list(struct pkg_plugins *p, struct plugins_hook **h)
{
	assert(p != NULL);
	
	if ((*h) == NULL)
		(*h) = STAILQ_FIRST(&(p->phooks));
	else
		(*h) = STAILQ_NEXT((*h), next);

	if ((*h) == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

int
pkg_plugins_hook(const char *pluginname, pkg_plugins_hook_t hook, pkg_plugins_callback callback)
{
	struct pkg_plugins *p = NULL;
	const char *pname = NULL;
	bool plugin_found = false;
	
	assert(pluginname != NULL);
	assert(callback != NULL);

	/* locate the plugin */
	while (pkg_plugins_list(&p) != EPKG_END) {
		pname = pkg_plugins_get(p, PKG_PLUGINS_NAME);
		if ((strcmp(pname, pluginname)) == 0) {
			pkg_plugins_hook_register(p, hook, callback);
			plugin_found = true;
		}
	}

	if (plugin_found == false) {
		pkg_emit_error("Plugin name '%s' was not found in the registry, cannot hook",
			       pluginname);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

int
pkg_plugins_hook_run(pkg_plugins_hook_t hook, void *data, struct pkgdb *db)
{
	struct pkg_plugins *p = NULL;

	while (pkg_plugins_list(&p) != EPKG_END)
			pkg_plugins_hook_exec(p, hook, data, db);

	return (EPKG_OK);
}

int
pkg_plugins_set(struct pkg_plugins *p, pkg_plugins_key key, const char *str)
{
	assert(p != NULL);

	return (sbuf_set(&p->fields[key], str));
}

const char *
pkg_plugins_get(struct pkg_plugins *p, pkg_plugins_key key)
{
	assert(p != NULL);

	return (sbuf_get(p->fields[key]));
}

int
pkg_plugins_list(struct pkg_plugins **plugin)
{
	assert(&ph != NULL);
	
	if ((*plugin) == NULL)
		(*plugin) = STAILQ_FIRST(&ph);
	else
		(*plugin) = STAILQ_NEXT((*plugin), next);

	if ((*plugin) == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

int
pkg_plugins_init(void)
{
	struct pkg_plugins *p = NULL;
	struct pkg_config_value *v = NULL;
	char pluginfile[MAXPATHLEN];
	const char *plugdir;
	int (*init_func)(struct pkg_plugins *);

	/*
	 * Discover available plugins
	 */
	pkg_config_string(PKG_CONFIG_PLUGINS_DIR, &plugdir);

	while (pkg_config_list(PKG_CONFIG_PLUGINS, &v) == EPKG_OK) {
		/*
		 * Load the plugin
		 */
		snprintf(pluginfile, MAXPATHLEN, "%s/%s.so", plugdir,
		    pkg_config_value(v));
		p = malloc(sizeof(struct pkg_plugins));
		for (int i = 0; i < PLUGINS_NUMFIELDS; i++)
			p->fields[i] = NULL;
		if ((p->lh = dlopen(pluginfile, RTLD_LAZY)) == NULL) {
			pkg_emit_error("Loading of plugin '%s' failed: %s",
			    pkg_config_value(v), dlerror());
			return (EPKG_FATAL);
		}
		if ((init_func = dlsym(p->lh, "init")) == NULL) {
			pkg_emit_error("Cannot load init function for plugin '%s'",
			     pkg_config_value(v));
			pkg_emit_error("Plugin '%s' will not be loaded: %s",
			      pkg_config_value(v), dlerror());
		}
		pkg_plugins_set(p, PKG_PLUGINS_PLUGINFILE, pluginfile);
		if (init_func(p) == EPKG_OK) {
			STAILQ_INSERT_TAIL(&ph, p, next);
		} else {
			dlclose(p->lh);
			free(p);
		}
	}

	return (EPKG_OK);
}

int
pkg_plugins_shutdown(void)
{
	struct pkg_plugins *p = NULL;
	int (*shutdown_func)(struct pkg_plugins *p);

	/*
	 * Unload any previously loaded plugins
	 */
	while (pkg_plugins_list(&p) != EPKG_END) {
		if ((shutdown_func = dlsym(p->lh, "shutdown")) != NULL) {
			shutdown_func(p);
		}
		dlclose(p->lh);
	}

	/*
	 * Deallocate memory used by the plugins
	 */
	pkg_plugins_free();

	return (EPKG_OK);
}
