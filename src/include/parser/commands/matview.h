//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// matview.h
//
// Identification: src/include/parser/commands/matview.h
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//


/*-------------------------------------------------------------------------
 *
 * matview.h
 *	  prototypes for matview.c.
 *
 *
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/commands/matview.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef MATVIEW_H
#define MATVIEW_H

#include "parser/catalog/objectaddress.h"
#include "parser/nodes/params.h"
#include "parser/nodes/parsenodes.h"
#include "parser/tcop/dest.h"
#include "parser/utils/relcache.h"


extern void SetMatViewPopulatedState(Relation relation, bool newstate);

extern ObjectAddress ExecRefreshMatView(RefreshMatViewStmt *stmt, const char *queryString,
				   ParamListInfo params, char *completionTag);

extern DestReceiver *CreateTransientRelDestReceiver(Oid oid);

extern bool MatViewIncrementalMaintenanceIsEnabled(void);

#endif   /* MATVIEW_H */
