/*
 * Copyright (c) Splendid Data Product Development B.V. 2025
 *
 * This program is free software: You may redistribute and/or modify under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at Client's option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, Client should obtain one via www.gnu.org/licenses/.
 */

#include "postgres.h"
#include "miscadmin.h"
#include "commands/dbcommands.h"
#include "executor/spi.h"
#include "utils/guc_tables.h"
#include "utils/memutils.h"

#if PG_VERSION_NUM < 150000
#error "Postgres version must be 15 or higher"
#endif

#if PG_VERSION_NUM < 170000
#include "utils/snapmgr.h"
#define AmBackgroundWorkerProcess() (IsBackgroundWorker)
#endif

#ifdef PG_MODULE_MAGIC_EXT
PG_MODULE_MAGIC_EXT(.name = "server_promotion_hook", .version = "1.0");
#elif defined PG_MODULE_MAGIC
PG_MODULE_MAGIC
;
#endif

static char *version = "1.0";

static void server_promotion_hook_xact_callback(XactEvent event, void *arg);

static bool isExecutingHook = false;

static const char *selectFunctionOidSql = "select pg_proc.oid "
		"from pg_extension left join pg_proc on extnamespace = pronamespace "
		"and proname = 'on_promote_server' "
		"and prokind = 'f' "
		"and pronargs = 1 "
		"and proargtypes[0] = 'pg_catalog.bool'::regtype "
		"and prorettype = 'pg_catalog.void'::regtype "
		"where extname = 'server_promotion_hook'";

static bool currentStandbyStatus = false;

void _PG_init(void);
/*
 * Registers a transaction callback that will check for recovery status changes
 * at pre_commit time.
 */
void _PG_init(void)
{
	elog(DEBUG1,
			"server_promotion_hook: _PG_init() in server_promotion_hook.so, MyProcPid=%d, MyDatabaseId=%d, AmBackgroundWorkerProcess()=%d, server_promotion_hook version=%s",
			MyProcPid, MyDatabaseId, AmBackgroundWorkerProcess(), version);
	/*
	 * If no database is selected, then it makes no sense trying to execute
	 * server_promotion code. This may occur for example in a replication
	 * target database.
	 */
	if (!OidIsValid(MyDatabaseId))
	{
		elog(DEBUG1,
				"server_promotion_hook: No database selected so server_promotion_hook will not execute");
		return;
	}

	/*
	 * Parallel workers have their own initialisation. The server_promotion()
	 * function must not be invoked for them.
	 */
	if (AmBackgroundWorkerProcess())
	{
		elog(DEBUG1,
				"server_promotion_hook: did not do anything because we are in a background worker");
		return;
	}

	currentStandbyStatus = RecoveryInProgress();
	RegisterXactCallback((XactCallback) server_promotion_hook_xact_callback,
	NULL);
	elog(DEBUG1, "server_promotion_hook: exit _PG_init()");
}

/*
 * Transaction callback that will check for recover status changes of the server
 * at pre_commit time. If the recovery status changes, it will invoke the user
 * defined server_promotion_hook.on_promote_server(bool) function, provided
 * that it exists and that the server_promotion_hook extension is installed
 * in the database.
 */
static void server_promotion_hook_xact_callback(XactEvent event, void *arg)
{
	Portal cursor;
	bool isnull;
	Datum queryResultValue;
	Oid procedureToInvokeOid = InvalidOid;
	bool newStandbyStatus;
	char *dbName = "unknown";

	elog(DEBUG5,
			"server_promotion_hook: in server_promotion_hook_xact_callback(event=%d)",
			event);

	if (event == XACT_EVENT_PRE_COMMIT
			|| event == XACT_EVENT_PARALLEL_PRE_COMMIT)
	{

		newStandbyStatus = RecoveryInProgress();
		if (currentStandbyStatus == newStandbyStatus)
		{
			elog(DEBUG3,
					"server_promotion_hook: nothing to do because server status '%s' didn't change",
					newStandbyStatus ? "in recovery" : "primary");
		}
		else
		{
			currentStandbyStatus = newStandbyStatus;
			elog(DEBUG3,
					"server_promotion_hook: Process server status change to '%s'",
					newStandbyStatus ? "in recovery" : "primary");
			BeginInternalSubTransaction("server_promotion_hook");
			PushActiveSnapshot(GetTransactionSnapshot());
			if (OidIsValid(MyDatabaseId))
			{
				dbName = get_database_name(MyDatabaseId);
			}
			PG_TRY();
				{
					isExecutingHook = true;
					if (SPI_connect() == SPI_OK_CONNECT)
					{
						cursor = SPI_cursor_open_with_args(NULL,
								selectFunctionOidSql, 0,
								NULL, NULL, NULL, true,
								CURSOR_OPT_BINARY | CURSOR_OPT_NO_SCROLL);
						SPI_cursor_fetch(cursor, true, 1);
						if (cursor->atEnd)
						{
							procedureToInvokeOid = InvalidOid;
							elog(DEBUG1,
									"server_promotion_hook: will not execute as extension server_promotion_hook is not installed in database %s",
									dbName);
						}
						else
						{
							queryResultValue = SPI_getbinval(
									SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, 1, &isnull);
							if (isnull)
							{
								procedureToInvokeOid = InvalidOid;
								elog(WARNING,
										"server_promotion_hook: will not execute as server_promotion_hook.on_promote_server(bool) returns void does not exist in database %s",
										dbName);
							}
							else
							{
								procedureToInvokeOid = DatumGetObjectId(
										queryResultValue);
							}
						}
						SPI_cursor_close(cursor);
						SPI_finish();
					}
					else
					{
						elog(WARNING,
								"server_promotion_hook: failed to SPI_connect() in database %s",
								dbName);
					}
					if (procedureToInvokeOid != InvalidOid)
					{
						elog(DEBUG3,
								"server_promotion_hook: executing server_promotion_hook.on_promote_server(%s) in database %s",
								currentStandbyStatus ? "false" : "true",
								dbName);
						/*
						 * Perform the user defined server_promotion_hook.on_promote_server(boolean) function
						 */
						OidFunctionCall1Coll(procedureToInvokeOid,
						InvalidOid, BoolGetDatum(!currentStandbyStatus));
						elog(DEBUG3,
								"server_promotion_hook: back from executing server_promotion_hook.on_promote_server() in database %s",
								dbName);
					}
					//Make sure function server_promotion_hook.is_executing_server_promotion_hook() will return false ever after
					isExecutingHook = false;
					PopActiveSnapshot();
					ReleaseCurrentSubTransaction();
				}
			PG_CATCH();
				{
					//Make sure function server_promotion_hook.is_executing_server_promotion_hook() will return false ever after
					isExecutingHook = false;
					PopActiveSnapshot();
					RollbackAndReleaseCurrentSubTransaction();
					if (superuser())
					{
						ErrorData *edata = CopyErrorData();
						ereport(WARNING,
								(errcode(edata->sqlerrcode), errmsg(
										"server_promotion_hook: Function server_promotion_hook.on_promote_server() returned with error in database %s.",
										get_database_name(MyDatabaseId)), errhint(
										"original message = %s", edata->message)));
					}
				}
			PG_END_TRY();

			elog(DEBUG3, "server_promotion_hook: done");
		}
	}
}

/*
 * function server_promotion_hook.is_executing_server_promotion_hook() returns boolean.
 *
 * This function returns true if the server_promotionhook.on_promote_server() function is executing
 * under control of the server_promotion_hook code.
 *
 * Rationale: The server_promotionhook.on_promote_server() function must be publicly executable.
 * But it may cause damage if not executed at the right time - only once directly after server
 * promotion. By checking server_promotion_hook.is_executing_server_promotion_hook(), the
 * server_promotionhook.on_promote_server() function can prevent executing code at
 * inappropriate times.
 */
PG_FUNCTION_INFO_V1(is_executing_server_promotion_hook);
PGDLLEXPORT Datum is_executing_server_promotion_hook(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(isExecutingHook);
}

/*
 * function server_promotion_hook.get_server_promotion_hook_version() returns text.
 *
 * This function returns the current code version of this database extension
 */
PG_FUNCTION_INFO_V1(get_server_promotion_hook_version);
PGDLLEXPORT Datum get_server_promotion_hook_version(PG_FUNCTION_ARGS)
{
	Datum server_promotion_hook_version = (Datum) palloc(
	VARHDRSZ + strlen(version));
	SET_VARSIZE(server_promotion_hook_version, VARHDRSZ + strlen(version));
	memcpy(VARDATA(server_promotion_hook_version), version, strlen(version));
	PG_RETURN_DATUM(server_promotion_hook_version);
}
