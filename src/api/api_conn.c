/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2008-2011 WiredTiger, Inc.
 *	All rights reserved.
 */

#include "wt_internal.h"

static int __conn_home(WT_CONNECTION_IMPL *, const char *, const char **);

/*
 * api_err_printf --
 *	Extension API call to print to the error stream.
 */
static void
__api_err_printf(WT_SESSION *wt_session, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__wt_errv((WT_SESSION_IMPL *)wt_session, 0, NULL, 0, fmt, ap);
	va_end(ap);
}

static WT_EXTENSION_API __api = {
	__api_err_printf,
	__wt_scr_alloc_ext,
	__wt_scr_free_ext
};

/*
 * __conn_load_extension --
 *	WT_CONNECTION->load_extension method.
 */
static int
__conn_load_extension(
    WT_CONNECTION *wt_conn, const char *path, const char *config)
{
	WT_CONFIG_ITEM cval;
	WT_CONNECTION_IMPL *conn;
	WT_DLH *dlh;
	WT_SESSION_IMPL *session;
	int (*entry)(WT_SESSION *, WT_EXTENSION_API *, const char *);
	int ret;
	const char *entry_name;

	dlh = NULL;

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	CONNECTION_API_CALL(conn, session, load_extension, config, cfg);

	entry_name = NULL;
	WT_ERR(__wt_config_gets(session, cfg, "entry", &cval));
	WT_ERR(__wt_strndup(session, cval.str, cval.len, &entry_name));

	/*
	 * This assumes the underlying shared libraries are reference counted,
	 * that is, that re-opening a shared library simply increments a ref
	 * count, and closing it simply decrements the ref count, and the last
	 * close discards the reference entirely -- in other words, we do not
	 * check to see if we've already opened this shared library.
	 */
	WT_ERR(__wt_dlopen(session, path, &dlh));
	WT_ERR(__wt_dlsym(session, dlh, entry_name, &entry));

	/* Call the entry function. */
	WT_ERR(entry(&session->iface, &__api, config));

	/* Link onto the environment's list of open libraries. */
	__wt_lock(session, conn->mtx);
	TAILQ_INSERT_TAIL(&conn->dlhqh, dlh, q);
	__wt_unlock(session, conn->mtx);

	if (0) {
err:		if (dlh != NULL)
			(void)__wt_dlclose(session, dlh);
	}
	__wt_free(session, entry_name);

	API_END(session);

	return (ret);
}

/*
 * __conn_add_cursor_type --
 *	WT_CONNECTION->add_cursor_type method.
 */
static int
__conn_add_cursor_type(WT_CONNECTION *wt_conn,
    const char *prefix, WT_CURSOR_TYPE *ctype, const char *config)
{
	WT_CONNECTION_IMPL *conn;
	WT_SESSION_IMPL *session;
	int ret;

	WT_UNUSED(prefix);
	WT_UNUSED(ctype);
	ret = ENOTSUP;

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	CONNECTION_API_CALL(conn, session, add_cursor_type, config, cfg);
	WT_UNUSED(cfg);
err:	API_END(session);

	return (ret);
}

/*
 * __conn_add_collator --
 *	WT_CONNECTION->add_collator method.
 */
static int
__conn_add_collator(WT_CONNECTION *wt_conn,
    const char *name, WT_COLLATOR *collator, const char *config)
{
	WT_CONNECTION_IMPL *conn;
	WT_SESSION_IMPL *session;
	int ret;

	WT_UNUSED(name);
	WT_UNUSED(collator);
	ret = ENOTSUP;

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	CONNECTION_API_CALL(conn, session, add_collator, config, cfg);
	WT_UNUSED(cfg);
err:	API_END(session);

	return (ret);
}

/*
 * __conn_add_compressor --
 *	WT_CONNECTION->add_compressor method.
 */
static int
__conn_add_compressor(WT_CONNECTION *wt_conn,
    const char *name, WT_COMPRESSOR *compressor, const char *config)
{
	WT_CONNECTION_IMPL *conn;
	WT_SESSION_IMPL *session;
	WT_NAMED_COMPRESSOR *ncomp;
	int ret;

	WT_UNUSED(name);
	WT_UNUSED(compressor);

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	CONNECTION_API_CALL(conn, session, add_compressor, config, cfg);
	WT_UNUSED(cfg);

	WT_ERR(__wt_calloc_def(session, 1, &ncomp));
	WT_ERR(__wt_strdup(session, name, &ncomp->name));
	ncomp->compressor = compressor;

	__wt_lock(session, conn->mtx);
	TAILQ_INSERT_TAIL(&conn->compqh, ncomp, q);
	__wt_unlock(session, conn->mtx);
	ncomp = NULL;
err:	API_END(session);
	__wt_free(session, ncomp);
	return (ret);
}

/*
 * __conn_remove_compressor --
 *	remove compressor added by WT_CONNECTION->add_compressor,
 *	only used internally.
 */
static int
__conn_remove_compressor(WT_CONNECTION *wt_conn, WT_COMPRESSOR *compressor)
{
	WT_CONNECTION_IMPL *conn;
	WT_SESSION_IMPL *session;
	WT_NAMED_COMPRESSOR *ncomp;
	int ret;

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	session = &conn->default_session;

	/* Remove from the connection's list. */
	__wt_lock(session, conn->mtx);
	TAILQ_FOREACH(ncomp, &conn->compqh, q) {
		if (ncomp->compressor == compressor)
			break;
	}
	if (ncomp != NULL)
		TAILQ_REMOVE(&conn->compqh, ncomp, q);
	__wt_unlock(session, conn->mtx);

	/* Free associated memory */
	if (ncomp != NULL) {
		__wt_free(session, ncomp->name);
		__wt_free(session, ncomp);
		ret = 0;
	}
	else
		ret = ENOENT;

	return ret;
}

/*
 * __conn_add_extractor --
 *	WT_CONNECTION->add_extractor method.
 */
static int
__conn_add_extractor(WT_CONNECTION *wt_conn,
    const char *name, WT_EXTRACTOR *extractor, const char *config)
{
	WT_CONNECTION_IMPL *conn;
	WT_SESSION_IMPL *session;
	int ret;

	WT_UNUSED(name);
	WT_UNUSED(extractor);
	ret = ENOTSUP;

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	CONNECTION_API_CALL(conn, session, add_extractor, config, cfg);
	WT_UNUSED(cfg);
err:	API_END(session);

	return (ret);
}

static const char *
__conn_get_home(WT_CONNECTION *wt_conn)
{
	return (((WT_CONNECTION_IMPL *)wt_conn)->home);
}

/*
 * __conn_is_new --
 *	WT_CONNECTION->is_new method.
 */
static int
__conn_is_new(WT_CONNECTION *wt_conn)
{
	WT_UNUSED(wt_conn);

	return (0);
}

/*
 * __conn_close --
 *	WT_CONNECTION->close method.
 */
static int
__conn_close(WT_CONNECTION *wt_conn, const char *config)
{
	WT_CONNECTION_IMPL *conn;
	WT_SESSION_IMPL *s, *session, **tp;
	WT_SESSION *wt_session;
	WT_NAMED_COMPRESSOR *ncomp;
	int ret;

	ret = 0;
	conn = (WT_CONNECTION_IMPL *)wt_conn;

	CONNECTION_API_CALL(conn, session, close, config, cfg);
	WT_UNUSED(cfg);

	/* Close open sessions. */
	for (tp = conn->sessions; (s = *tp) != NULL;) {
		if (!F_ISSET(s, WT_SESSION_INTERNAL)) {
			wt_session = &s->iface;
			WT_TRET(wt_session->close(wt_session, config));

			/*
			 * We closed a session, which has shuffled pointers
			 * around.  Restart the search.
			 */
			tp = conn->sessions;
		} else
			++tp;
	}

	/* Free memory for compressors */
	while ((ncomp = TAILQ_FIRST(&conn->compqh)) != NULL)
		WT_TRET(__conn_remove_compressor(wt_conn, ncomp->compressor));

	WT_TRET(__wt_connection_close(conn));
	/* We no longer have a session, don't try to update it. */
	session = NULL;
err:	API_END(session);

	return (ret);
}

/*
 * __conn_open_session --
 *	WT_CONNECTION->open_session method.
 */
static int
__conn_open_session(WT_CONNECTION *wt_conn,
    WT_EVENT_HANDLER *event_handler, const char *config,
    WT_SESSION **wt_sessionp)
{
	WT_CONNECTION_IMPL *conn;
	WT_SESSION_IMPL *session, *session_ret;
	int ret;

	conn = (WT_CONNECTION_IMPL *)wt_conn;
	session_ret = NULL;
	ret = 0;

	CONNECTION_API_CALL(conn, session, open_session, config, cfg);
	WT_UNUSED(cfg);

	__wt_lock(session, conn->mtx);
	WT_TRET(__wt_open_session(conn, event_handler, config, &session_ret));
	__wt_unlock(session, conn->mtx);

	STATIC_ASSERT(offsetof(WT_CONNECTION_IMPL, iface) == 0);
	*wt_sessionp = &session_ret->iface;
err:	API_END(session);

	return (ret);
}

/*
 * wiredtiger_open --
 *	Main library entry point: open a new connection to a WiredTiger
 *	database.
 */
int
wiredtiger_open(const char *home, WT_EVENT_HANDLER *event_handler,
    const char *config, WT_CONNECTION **wt_connp)
{
	static int library_init = 0;
	static WT_CONNECTION stdc = {
		__conn_load_extension,
		__conn_add_cursor_type,
		__conn_add_collator,
		__conn_add_compressor,
		__conn_add_extractor,
		__conn_close,
		__conn_get_home,
		__conn_is_new,
		__conn_open_session
	};
	static struct {
		const char *vname;
		uint32_t vflag;
	} *vt, verbtypes[] = {
		{ "allocate",	WT_VERB_ALLOCATE },
		{ "evictserver",WT_VERB_EVICTSERVER },
		{ "fileops",	WT_VERB_FILEOPS },
		{ "hazard",	WT_VERB_HAZARD },
		{ "mutex",	WT_VERB_MUTEX },
		{ "read",	WT_VERB_READ },
		{ "readserver",	WT_VERB_READSERVER },
		{ "reconcile",	WT_VERB_RECONCILE },
		{ "salvage",	WT_VERB_SALVAGE },
		{ "write",	WT_VERB_WRITE },
		{ NULL, 0 }
	};
	WT_BUF expath, exconfig;
	WT_CONFIG subconfig;
	WT_CONFIG_ITEM cval, skey, sval;
	WT_CONNECTION_IMPL *conn;
	WT_SESSION_IMPL *session;
	const char *cfg[] = { __wt_confdfl_wiredtiger_open, config, NULL };
	int opened, ret;

	*wt_connp = NULL;
	session = NULL;
	WT_CLEAR(expath);
	WT_CLEAR(exconfig);
	opened = 0;

	/*
	 * We end up here before we do any real work.   Check the build itself,
	 * and do some global stuff.
	 */
	if (library_init == 0) {
		WT_RET(__wt_library_init());
		library_init = 1;
	}

	/*
	 * !!!
	 * We don't yet have a session handle to pass to the memory allocation
	 * functions.
	 */
	WT_RET(__wt_calloc_def(NULL, 1, &conn));
	conn->iface = stdc;

	session = &conn->default_session;
	session->iface.connection = &conn->iface;
	session->name = "wiredtiger_open";

	/*
	 * If the application didn't configure an event handler, use the default
	 * one, use the default entries for any not set by the application.
	 */
	if (event_handler == NULL)
		event_handler = __wt_event_handler_default;
	else {
		if (event_handler->handle_error == NULL)
			event_handler->handle_error =
			    __wt_event_handler_default->handle_error;
		if (event_handler->handle_message == NULL)
			event_handler->handle_message =
			    __wt_event_handler_default->handle_message;
		if (event_handler->handle_progress == NULL)
			event_handler->handle_progress =
			    __wt_event_handler_default->handle_progress;
	}
	session->event_handler = event_handler;

	WT_ERR(__wt_connection_init(conn));

	WT_ERR(
	   __wt_config_check(session, __wt_confchk_wiredtiger_open, config));

	WT_ERR(__conn_home(conn, home, cfg));

	WT_ERR(__wt_config_gets(session, cfg, "cache_size", &cval));
	conn->cache_size = cval.val;
	WT_ERR(__wt_config_gets(session, cfg, "hazard_max", &cval));
	conn->hazard_size = (uint32_t)cval.val;
	WT_ERR(__wt_config_gets(session, cfg, "session_max", &cval));
	conn->session_size = (uint32_t)cval.val;

	conn->verbose = 0;
#ifdef HAVE_VERBOSE
	WT_ERR(__wt_config_gets(session, cfg, "verbose", &cval));
	for (vt = verbtypes; vt->vname != NULL; vt++) {
		WT_ERR(__wt_config_subinit(session, &subconfig, &cval));
		skey.str = vt->vname;
		skey.len = strlen(vt->vname);
		ret = __wt_config_getraw(&subconfig, &skey, &sval);
		if (ret == 0 && sval.val)
			FLD_SET(conn->verbose, vt->vflag);
		else if (ret != WT_NOTFOUND)
			goto err;
	}
#endif
	WT_ERR(__wt_config_gets(session, cfg, "multithread", &cval));
	if (cval.val != 0)
		F_SET(conn, WT_MULTITHREAD);

	WT_ERR(__wt_connection_open(conn, home, 0644));
	opened = 1;

	WT_ERR(__wt_config_gets(session, cfg, "logging", &cval));
	if (cval.val != 0)
		WT_ERR(__wt_open(session, "__wt.log", 0666, 1, &conn->log_fh));

	/* Load any extensions referenced in the config. */
	WT_ERR(__wt_config_gets(session, cfg, "extensions", &cval));
	WT_ERR(__wt_config_subinit(session, &subconfig, &cval));
	while ((ret = __wt_config_next(&subconfig, &skey, &sval)) == 0) {
		WT_ERR(__wt_buf_sprintf(session, &expath,
		    "%.*s", (int)skey.len, skey.str));
		if (sval.len > 0)
			WT_ERR(__wt_buf_sprintf(session, &exconfig,
			    "entry=%.*s\n", (int)sval.len, sval.str));
		WT_ERR(conn->iface.load_extension(&conn->iface,
		    expath.data, (sval.len > 0) ? exconfig.data : NULL));
	}
	if (ret == WT_NOTFOUND)
		ret = 0;
	else if (ret != 0)
		goto err;

	STATIC_ASSERT(offsetof(WT_CONNECTION_IMPL, iface) == 0);
	*wt_connp = &conn->iface;

	if (0) {
err:		if (opened)
			(void)__wt_connection_close(conn);
		else
			__wt_connection_destroy(conn);
	}
	__wt_buf_free(session, &expath);
	__wt_buf_free(session, &exconfig);

	return (ret);
}

/*
 * __conn_home --
 *	Set the database home directory.
 */
static int
__conn_home(WT_CONNECTION_IMPL *conn, const char *home, const char **cfg)
{
	WT_CONFIG_ITEM cval;
	WT_SESSION_IMPL *session;

	session = &conn->default_session;

	/* If the application specifies a home directory, use it. */
	if (home != NULL)
		goto copy;

	/* If there's no WIREDTIGER_HOME environment variable, use ".". */
	if ((home = getenv("WIREDTIGER_HOME")) == NULL) {
		home = ".";
		goto copy;
	}

	/*
	 * Security stuff:
	 *
	 * If the "home_environment" configuration string is set, use the
	 * environment variable for all processes.
	 */
	WT_RET(__wt_config_gets(session, cfg, "home_environment", &cval));
	if (cval.val != 0)
		goto copy;

	/*
	 * If the "home_environment_priv" configuration string is set, use the
	 * environment variable if the process has appropriate privileges.
	 */
	WT_RET(__wt_config_gets(session, cfg, "home_environment_priv", &cval));
	if (cval.val == 0) {
		__wt_errx(session, "%s",
		    "WIREDTIGER_HOME environment variable set but WiredTiger "
		    "not configured to use that environment variable");
		return (WT_ERROR);
	}

	if (!__wt_has_priv()) {
		__wt_errx(session, "%s",
		    "WIREDTIGER_HOME environment variable set but process "
		    "lacks privileges to use that environment variable");
		return (WT_ERROR);
	}

copy:	return (__wt_strdup(session, home, &conn->home));
}
