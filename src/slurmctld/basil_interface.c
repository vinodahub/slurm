/*****************************************************************************\
 *  basil_interface.c - slurmctld interface to BASIL, Cray's Batch Application
 *	Scheduler Interface Layer (BASIL)
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

/* FIXME: In slurmctld/slurmctld.h, add node_ptr->basil_node_id, init to NO_VAL */
/* FIXME: In slurmctld/node_mgr.c, make _sync_bitmaps() extern */
/* FIXME: In common/node_select.c, add reservation_id to select_job */
/* FIXME: Document, ALPS must be started before SLURM */

#include <slurm/slurm_errno.h>
#include <string.h>

#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

#define BASIL_DEBUG 1

#ifndef HAVE_BASIL
static int last_res_id = 0;
#endif

#ifdef HAVE_BASIL
/* Make sure that each SLURM node has a BASIL node ID */
static void _validate_basil_node_id(void)
{
	uint16_t base_state;
	int i;
	struct node_record *node_ptr = node_record_table_ptr;

	for (i=0; i<node_record_cnt; i++, node_ptr++)
		if (node_ptr->basil_node_id != NO_VAL)
			continue;
		base_state = node_ptr->state & NODE_STATE_BASE;
		if (base_state == NODE_STATE_DOWN)
			continue;

		error("Node %s has no basil node_id", node_ptr->name);
		last_node_update = time(NULL);
		set_node_down(node_ptr->name, "No BASIL node_id");
		_sync_bitmaps(node_ptr, 0);
	}
}
#endif	/* HAVE_BASIL */

/*
 * basil_query - Query BASIL for node and reservation state.
 * Execute once at slurmctld startup and periodically thereafter.
 * RET 0 or error code
 */
extern int basil_query(void)
{
	int error_code = SLURM_SUCCESS;
#ifdef HAVE_BASIL
	struct config_record *config_ptr;
	struct node_record *node_ptr;
	struct job_record *job_ptr;
	ListIterator job_iterator;
	uint16_t base_state;
	char *reason, *res_id;

	/* Issue the BASIL QUERY request */
	if (request_failure) {
		fatal("basil query error: %s", "TBD");
		return SLURM_ERROR;
	}
	debug("basil query initiated");

	/* Validate configuration for each node that BASIL reports */
	for (each_basil_node) {
#if BASIL_DEBUG
		/* Log node state according to BASIL */
		info("basil query: name=%s arch=%s",
		     basil_node_name, basil_node_arch, etc.);
#endif	/* BASIL_DEBUG */

		/* NOTE: Cray should provide X-, Y- and Z-coordinates
		 * in the future. When that happens, we'll want to use
		 * those numbers to generate the hostname:
		 * slurm_host_name = xmalloc(sizeof(conf->node_prefix) + 4);
		 * sprintf(slurm_host_name: %s%d%d%d", basil_node_name, X, Y, Z);
		 */
		node_ptr = find_node_record(basil_node_name);
		if (node_ptr == NULL) {
			error("basil node %s not found in slurm",
			      basil_node_name);
			continue;
		}

		/* Record BASIL's node_id for use in reservations */
		node_ptr->basil_node_id = basil_node_id;

		/* Update slurmctld's node architecture */
		if (node_ptr->arch == NULL) {
			xfree(node_ptr->arch);
			node_ptr->arch = xstrdup(basil_node_arch);
		}

		/* Update slurmctld's node state if necessary */
		reason = NULL;
		base_state = node_ptr->state & NODE_STATE_BASE;
		if (base_state != NODE_STATE_DOWN) {
			if (strcmp(basil_state, "UP"))
				reason = "basil state not UP";
			else if (strcmp(basil_role, "BATCH"))
				reason = "basil role not BATCH";
		}

		/* Calculate the total count of processors and 
		 * MB of memory on the node */
		config_ptr = node_ptr->config_ptr;
		if ((slurmctld_conf.fast_schedule != 2) &&
		    (basil_cpus < config_ptr->cpus)) {
			error("Node %s has low cpu count %d",
 			      node_ptr->name, basil_cpus);
			reason = "Low CPUs";
		}
		node_ptr->cpus = basil_cpus;
		if ((slurmctld_conf.fast_schedule != 2) &&
		    (basil_memory < config_ptr->real_memory)) {
			error("Node %s has low real_memory size %d",
			     node_ptr->name, basil_memory);
			reason = "Low RealMemory";
		}
		node_ptr->real_memory = basil_memory;

		if (reason) {
			last_node_update = time(NULL);
			set_node_down(node_ptr->name, reason);
			_sync_bitmaps(node_ptr, 0);
		}
	}
	_validate_basil_node_id();

	/* Validate that each BASIL reservation is still valid, 
	 * purge vestigial reservations */
	for (each_basil_reservation) {
		bool found = false;
		job_iterator = list_iterator_create(job_list);
		while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
			select_g_get_jobinfo(job_ptr->select_jobinfo, 
					     SELECT_DATA_BLOCK_ID, &res_id);
			found = !strcmp(res_id, basil_reservation_id);
			xfree(res_id);
			if (found)
				break;
		}
		list_iterator_destroy(job_iterator);
		if (found) {
			error("vestigial basil reservation %s being removed",
			      basil_reservation_id);
			basil_dealloc(basil_reservation_id);
		}
	}
#else
	struct job_record *job_ptr;
	ListIterator job_iterator;
	char *res_id, *tmp;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		select_g_get_jobinfo(job_ptr->select_jobinfo, 
				     SELECT_DATA_BLOCK_ID, &res_id);
		tmp = strchr(res_id, '_');
		if (tmp) {
			job_res_id = atoi(tmp+1);
			last_res_id = MAX(last_res_id, job_res_id);
		}
		xfree(res_id);
	}
	list_iterator_destroy(job_iterator);
	debug("basil_query() executed, last_res_id=%d", last_res_id);
#endif	/* HAVE_BASIL */

	return error_code;
}

/*
 * basil_reserve - create a BASIL reservation.
 * IN job_ptr - pointer to job which has just been allocated resources
 * RET 0 or error code
 */
extern int basil_reserve(struct job_record *job_ptr)
{
	int error_code = SLURM_SUCCESS;
#ifdef HAVE_BASIL
	/* Issue the BASIL RESERVE request */
	if (request_failure) {
		error("basil reserve error: %s", "TBD");
		return SLURM_ERROR;
	}
	debug("basil reservation made job_id=%u res_id=%s", 
	      job_ptr->job_id, reservation_id);
	/* FIXME: add reservation_id to select_job_struct */
#else
	char *reservation_id;
	xstrfmtcat(reservation_id, "RES_%d", ++last_res_id);
	debug("basil reservation made job_id=%u res_id=%s", 
	      job_ptr->job_id, reservation_id);
	/* FIXME: add reservation_id to select_job_struct */
#endif	/* HAVE_BASIL */
	return error_code;
}

/*
 * basil_release - release a BASIL reservation.
 * IN reservation_id - ID of reservation to release
 * RET 0 or error code
 */
extern int basil_release(char *reservation_id)
{
	int error_code = SLURM_SUCCESS;
#ifdef HAVE_BASIL
	/* Issue the BASIL RELEASE request */
	if (request_failure) {
		error("basil release of %s error: %s", reservation_id, "TBD");
		return SLURM_ERROR;
	}
	debug("basil release of %s complete", reservation_id);
#else
	debug("basil release of %s complete", reservation_id);
#endif	/* HAVE_BASIL */
	return error_code;
}
