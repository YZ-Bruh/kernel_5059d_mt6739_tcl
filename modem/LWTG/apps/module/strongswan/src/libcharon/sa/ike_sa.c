/*
 * Copyright (C) 2006-2013 Tobias Brunner
 * Copyright (C) 2006 Daniel Roethlisberger
 * Copyright (C) 2005-2009 Martin Willi
 * Copyright (C) 2005 Jan Hutter
 * Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

#include "ike_sa.h"

#include <library.h>
#include <hydra.h>
#include <daemon.h>
#include <collections/array.h>
#include <utils/lexparser.h>
#include <processing/jobs/retransmit_job.h>
#include <processing/jobs/delete_ike_sa_job.h>
#include <processing/jobs/send_dpd_job.h>
#include <processing/jobs/send_keepalive_job.h>
#include <processing/jobs/rekey_ike_sa_job.h>
#include <processing/jobs/retry_initiate_job.h>
#include <processing/jobs/oos_timeout_job.h>
#include <sa/ikev2/tasks/ike_auth_lifetime.h>
#include <credentials/sets/auth_cfg_wrapper.h>

#ifdef ME
#include <sa/ikev2/tasks/ike_me.h>
#include <processing/jobs/initiate_mediation_job.h>
#endif

#include <utils/cust_settings.h>
#include <network/wod_channel.h>

#ifdef STRONGSWAN_UNIT_TEST
#include "ut_epdg_utils.h"
#endif

ENUM(ike_sa_state_names, IKE_CREATED, IKE_DESTROYING,
	"CREATED",
	"CONNECTING",
	"ESTABLISHED",
	"PASSIVE",
	"REKEYING",
	"DELETING",
	"DESTROYING",
);

typedef struct private_ike_sa_t private_ike_sa_t;
typedef struct attribute_entry_t attribute_entry_t;

/**
 * Private data of an ike_sa_t object.
 */
struct private_ike_sa_t {

	/**
	 * Public members
	 */
	ike_sa_t public;

	/**
	 * Identifier for the current IKE_SA.
	 */
	ike_sa_id_t *ike_sa_id;

	/**
	 * IKE version of this SA.
	 */
	ike_version_t version;

	/**
	 * unique numerical ID for this IKE_SA.
	 */
	u_int32_t unique_id;

	/**
	 * Current state of the IKE_SA
	 */
	ike_sa_state_t state;

	/**
	 * IKE configuration used to set up this IKE_SA
	 */
	ike_cfg_t *ike_cfg;

	/**
	 * Peer and authentication information to establish IKE_SA.
	 */
	peer_cfg_t *peer_cfg;

	/**
	 * currently used authentication ruleset, local
	 */
	auth_cfg_t *my_auth;

	/**
	 * currently used authentication constraints, remote
	 */
	auth_cfg_t *other_auth;

	/**
	 * Array of completed local authentication rounds (as auth_cfg_t)
	 */
	array_t *my_auths;

	/**
	 * Array of completed remote authentication rounds (as auth_cfg_t)
	 */
	array_t *other_auths;

	/**
	 * Selected IKE proposal
	 */
	proposal_t *proposal;

	/**
	 * Juggles tasks to process messages
	 */
	task_manager_t *task_manager;

	/**
	 * Address of local host
	 */
	host_t *my_host;

	/**
	 * Address of remote host
	 */
	host_t *other_host;

#ifdef ME
	/**
	 * Are we mediation server
	 */
	bool is_mediation_server;

	/**
	 * Server reflexive host
	 */
	host_t *server_reflexive_host;

	/**
	 * Connect ID
	 */
	chunk_t connect_id;
#endif /* ME */

	/**
	 * Identification used for us
	 */
	identification_t *my_id;

	/**
	 * Identification used for other
	 */
	identification_t *other_id;

	/**
	 * set of extensions the peer supports
	 */
	ike_extension_t extensions;

	/**
	 * set of condition flags currently enabled for this IKE_SA
	 */
	ike_condition_t conditions;

	/**
	 * Array containing the child sa's of the current IKE_SA.
	 */
	array_t *child_sas;

	/**
	 * keymat of this IKE_SA
	 */
	keymat_t *keymat;

	/**
	 * Virtual IPs on local host
	 */
	array_t *my_vips;

	/**
	 * Virtual IPs on remote host
	 */
	array_t *other_vips;

	/**
	 * DNS server for local side
	 */
	array_t *my_dnss;

	/**
	 * PCSCF server for local side
	 */
	array_t *my_pcscfs;

	/**
	 * netmask for local side
	 */
	array_t *my_intnetmasks;

	/**
	 * subnet for local side
	 */
	array_t *my_intsubnets;

	/**
	 * List of configuration attributes (attribute_entry_t)
	 */
	array_t *attributes;

	/**
	 * list of peer's addresses, additional ones transmitted via MOBIKE
	 */
	array_t *peer_addresses;

	/**
	 * previously value of received DESTINATION_IP hash
	 */
	chunk_t nat_detection_dest;

	/**
	 * number pending UPDATE_SA_ADDRESS (MOBIKE)
	 */
	u_int32_t pending_updates;

	/**
	 * NAT keep alive interval
	 */
	u_int32_t keepalive_interval;

	/**
	 * interval for retries during initiation (e.g. if DNS resolution failed),
	 * 0 to disable (default)
	 */
	u_int32_t retry_initiate_interval;

	/**
	 * TRUE if a retry_initiate_job has been queued
	 */
	bool retry_initiate_queued;

	/**
	 * Timestamps for this IKE_SA
	 */
	u_int32_t stats[STAT_MAX];

	/**
	 * how many times we have retried so far (keyingtries)
	 */
	u_int32_t keyingtry;

	/**
	 * local host address to be used for IKE, set via MIGRATE kernel message
	 */
	host_t *local_host;

	/**
	 * remote host address to be used for IKE, set via MIGRATE kernel message
	 */
	host_t *remote_host;

	/**
	 * Flush auth configs once established?
	 */
	bool flush_auth_cfg;

	/**
	 * Maximum length of a single fragment, 0 for address-specific defaults
	 */
	size_t fragment_size;

	/**
	 * Read keep alive settings from config
	 */
	bool read_cfg;

	/**
	 * Keep alive enabled?
	 */
	bool keepalive;

	/**
	 * Keep alive via WiFi driver enabled?
	 */
	bool wdrv_keepalive;

	/**
	 * Keep alive notification status
	 */
	bool keepalive_ntfy_status;

	/**
	 * Current state of the Keep alive job
	 */
	keepalive_job_state_t keepalive_job_state;

	/**
	 * Out of WiFi service?
	 */
	bool oos;

	/**
	 * OOS sequence number
	 */
	u_int32_t oos_seq_num;

	/**
	 * Time of WiFi OOS started?
	 */
	time_t oos_start_time;

	/**
	 * Status code to notify wo daemon
	 */
	int status_code;

	/**
	 * Sub-error code to notify wo daemon
	 */
	int sub_error_code;

	/**
	 * backoff_timer_value
	 */
	int backoff_timer_value;

	/**
	 * pcscf need update?
	 */
	bool pcscf_need_update;

	/**
	 * whether need notify device_identity flag
	 */
	bool notify_device_identity;
};

/**
 * Entry to maintain install configuration attributes during IKE_SA lifetime
 */
struct attribute_entry_t {
	/** handler used to install this attribute */
	attribute_handler_t *handler;
	/** attribute type */
	configuration_attribute_type_t type;
	/** attribute data */
	chunk_t data;
};

/**
 * get the time of the latest traffic processed by the kernel
 */
static time_t get_use_time(private_ike_sa_t* this, bool inbound)
{
	enumerator_t *enumerator;
	child_sa_t *child_sa;
	time_t use_time, current;

	if (inbound)
	{
		use_time = this->stats[STAT_INBOUND];
	}
	else
	{
		use_time = this->stats[STAT_OUTBOUND];
	}

	enumerator = array_create_enumerator(this->child_sas);
	while (enumerator->enumerate(enumerator, &child_sa))
	{
		child_sa->get_usestats(child_sa, inbound, &current, NULL, NULL);
		use_time = max(use_time, current);
	}
	enumerator->destroy(enumerator);

	return use_time;
}

METHOD(ike_sa_t, get_unique_id, u_int32_t,
	private_ike_sa_t *this)
{
	return this->unique_id;
}

METHOD(ike_sa_t, get_name, char*,
	private_ike_sa_t *this)
{
	if (this->peer_cfg)
	{
		return this->peer_cfg->get_name(this->peer_cfg);
	}
	return "(unnamed)";
}

METHOD(ike_sa_t, get_statistic, u_int32_t,
	private_ike_sa_t *this, statistic_t kind)
{
	if (kind < STAT_MAX)
	{
		return this->stats[kind];
	}
	return 0;
}

METHOD(ike_sa_t, set_statistic, void,
	private_ike_sa_t *this, statistic_t kind, u_int32_t value)
{
	if (kind < STAT_MAX)
	{
		this->stats[kind] = value;
	}
}

METHOD(ike_sa_t, get_my_host, host_t*,
	private_ike_sa_t *this)
{
	return this->my_host;
}

METHOD(ike_sa_t, set_my_host, void,
	private_ike_sa_t *this, host_t *me)
{
	DESTROY_IF(this->my_host);
	this->my_host = me;
}

METHOD(ike_sa_t, get_other_host, host_t*,
	private_ike_sa_t *this)
{
	return this->other_host;
}

METHOD(ike_sa_t, set_other_host, void,
	private_ike_sa_t *this, host_t *other)
{
	DESTROY_IF(this->other_host);
	this->other_host = other;
}

METHOD(ike_sa_t, get_peer_cfg, peer_cfg_t*,
	private_ike_sa_t *this)
{
	return this->peer_cfg;
}

METHOD(ike_sa_t, set_peer_cfg, void,
	private_ike_sa_t *this, peer_cfg_t *peer_cfg)
{
	peer_cfg->get_ref(peer_cfg);
	DESTROY_IF(this->peer_cfg);
	this->peer_cfg = peer_cfg;

	if (this->ike_cfg == NULL)
	{
		this->ike_cfg = peer_cfg->get_ike_cfg(peer_cfg);
		this->ike_cfg->get_ref(this->ike_cfg);
	}
}

METHOD(ike_sa_t, get_auth_cfg, auth_cfg_t*,
	private_ike_sa_t *this, bool local)
{
	if (local)
	{
		return this->my_auth;
	}
	return this->other_auth;
}

METHOD(ike_sa_t, add_auth_cfg, void,
	private_ike_sa_t *this, bool local, auth_cfg_t *cfg)
{
	if (local)
	{
		array_insert(this->my_auths, ARRAY_TAIL, cfg);
	}
	else
	{
		array_insert(this->other_auths, ARRAY_TAIL, cfg);
	}
}

METHOD(ike_sa_t, create_auth_cfg_enumerator, enumerator_t*,
	private_ike_sa_t *this, bool local)
{
	if (local)
	{
		return array_create_enumerator(this->my_auths);
	}
	return array_create_enumerator(this->other_auths);
}

/**
 * Flush the stored authentication round information
 */
static void flush_auth_cfgs(private_ike_sa_t *this)
{
	auth_cfg_t *cfg;

	this->my_auth->purge(this->my_auth, FALSE);
	this->other_auth->purge(this->other_auth, FALSE);

	while (array_remove(this->my_auths, ARRAY_TAIL, &cfg))
	{
		cfg->destroy(cfg);
	}
	while (array_remove(this->other_auths, ARRAY_TAIL, &cfg))
	{
		cfg->destroy(cfg);
	}
}

METHOD(ike_sa_t, verify_peer_certificate, bool,
	private_ike_sa_t *this)
{
	enumerator_t *e1, *e2, *certs;
	auth_cfg_t *cfg, *cfg_done;
	certificate_t *peer, *cert;
	public_key_t *key;
	auth_cfg_t *auth;
	auth_cfg_wrapper_t *wrapper;
	time_t not_before, not_after;
	bool valid = TRUE, found;

	if (this->state != IKE_ESTABLISHED)
	{
		DBG1(DBG_IKE, "unable to verify peer certificate in state %N",
			 ike_sa_state_names, this->state);
		return FALSE;
	}

	if (!this->flush_auth_cfg &&
		lib->settings->get_bool(lib->settings,
								"%s.flush_auth_cfg", FALSE, lib->ns))
	{	/* we can do this check only once if auth configs are flushed */
		DBG1(DBG_IKE, "unable to verify peer certificate as authentication "
			 "information has been flushed");
		return FALSE;
	}
	this->public.set_condition(&this->public, COND_ONLINE_VALIDATION_SUSPENDED,
							   FALSE);

	e1 = this->peer_cfg->create_auth_cfg_enumerator(this->peer_cfg, FALSE);
	e2 = array_create_enumerator(this->other_auths);
	while (e1->enumerate(e1, &cfg))
	{
		if (!e2->enumerate(e2, &cfg_done))
		{	/* this should not happen as the authentication should never have
			 * succeeded */
			valid = FALSE;
			break;
		}
		if ((uintptr_t)cfg_done->get(cfg_done,
									 AUTH_RULE_AUTH_CLASS) != AUTH_CLASS_PUBKEY)
		{
			continue;
		}
		peer = cfg_done->get(cfg_done, AUTH_RULE_SUBJECT_CERT);
		if (!peer)
		{
			DBG1(DBG_IKE, "no subject certificate found, skipping certificate "
				 "verification");
			continue;
		}
		if (!peer->get_validity(peer, NULL, &not_before, &not_after))
		{
			DBG1(DBG_IKE, "peer certificate invalid (valid from %T to %T)",
				 &not_before, FALSE, &not_after, FALSE);
			valid = FALSE;
			break;
		}
		key = peer->get_public_key(peer);
		if (!key)
		{
			DBG1(DBG_IKE, "unable to retrieve public key, skipping certificate "
				 "verification");
			continue;
		}
		DBG1(DBG_IKE, "verifying peer certificate");
		/* serve received certificates */
		wrapper = auth_cfg_wrapper_create(cfg_done);
		lib->credmgr->add_local_set(lib->credmgr, &wrapper->set, FALSE);
		certs = lib->credmgr->create_trusted_enumerator(lib->credmgr,
							key->get_type(key), peer->get_subject(peer), TRUE);
		key->destroy(key);

		found = FALSE;
		while (certs->enumerate(certs, &cert, &auth))
		{
			if (peer->equals(peer, cert))
			{
				cfg_done->add(cfg_done, AUTH_RULE_CERT_VALIDATION_SUSPENDED,
							  FALSE);
				cfg_done->merge(cfg_done, auth, FALSE);
				valid = cfg_done->complies(cfg_done, cfg, TRUE);
				found = TRUE;
				break;
			}
		}
		certs->destroy(certs);
		lib->credmgr->remove_local_set(lib->credmgr, &wrapper->set);
		wrapper->destroy(wrapper);
		if (!found || !valid)
		{
			valid = FALSE;
			break;
		}
	}
	e1->destroy(e1);
	e2->destroy(e2);

	if (this->flush_auth_cfg)
	{
		this->flush_auth_cfg = FALSE;
		flush_auth_cfgs(this);
	}
	return valid;
}

METHOD(ike_sa_t, get_proposal, proposal_t*,
	private_ike_sa_t *this)
{
	return this->proposal;
}

METHOD(ike_sa_t, set_proposal, void,
	private_ike_sa_t *this, proposal_t *proposal)
{
	DESTROY_IF(this->proposal);
	this->proposal = proposal->clone(proposal);
}

METHOD(ike_sa_t, set_message_id, void,
	private_ike_sa_t *this, bool initiate, u_int32_t mid)
{
	if (initiate)
	{
		this->task_manager->reset(this->task_manager, mid, UINT_MAX);
	}
	else
	{
		this->task_manager->reset(this->task_manager, UINT_MAX, mid);
	}
}

METHOD(ike_sa_t, send_keepalive, void,
	private_ike_sa_t *this)
{
	send_keepalive_job_t *job;
	time_t last_out, now, diff;

	if (!this->read_cfg)
	{
		this->wdrv_keepalive = this->peer_cfg->use_wdrv_keepalive(this->peer_cfg);
		this->keepalive_interval = this->peer_cfg->get_keepalive_time(this->peer_cfg);
		this->read_cfg = TRUE;
	}

	if (this->keepalive_interval >= this->peer_cfg->get_dpd(this->peer_cfg))
	{
		DBG1(DBG_IKE, "No need to send Keepalive, DPD will refresh NAT mappings"
				"(Keepalive: [%ds], DPD: [%ds])", this->keepalive_interval,
				this->peer_cfg->get_dpd(this->peer_cfg));
		return;
	}

	if (!(this->conditions & COND_NAT_HERE) || this->keepalive_interval == 0 ||
		this->state == IKE_PASSIVE)
	{	/* disable keep alives if we are not NATed anymore, or we are passive */
		this->keepalive = FALSE;
		return;
	}

	this->keepalive = TRUE;
	if (this->wdrv_keepalive)
	{	/* NATT keepalive by Wi-Fi driver */
		DBG1(DBG_IKE, "NATT keepalive through Wi-Fi driver is enabled, interval:%d", this->keepalive_interval);
		if (this->keepalive_job_state == KEEPALIVE_JOB_EXECUTED)
		{
			this->keepalive_job_state = KEEPALIVE_JOB_NONE;
		}
		return;
	}
	DBG1(DBG_IKE, "NATT keepalive through charon is enabled, interval:%d", this->keepalive_interval);

	last_out = get_use_time(this, FALSE);
	now = time_monotonic(NULL);

	diff = now - last_out;

	if (diff >= this->keepalive_interval)
	{
		packet_t *packet;
		chunk_t data;

		packet = packet_create();
		packet->set_source(packet, this->my_host->clone(this->my_host));
		packet->set_destination(packet, this->other_host->clone(this->other_host));
		data.ptr = malloc(1);
		data.ptr[0] = 0xFF;
		data.len = 1;
		packet->set_data(packet, data);
		DBG1(DBG_IKE, "sending keep alive to %#H", this->other_host);
		charon->sender->send_no_marker(charon->sender, packet);
		diff = 0;
	}
	if (this->keepalive_job_state == KEEPALIVE_JOB_NONE ||
		this->keepalive_job_state == KEEPALIVE_JOB_EXECUTED)
	{
		job = send_keepalive_job_create(this->ike_sa_id);
		lib->scheduler->schedule_job(lib->scheduler, (job_t*)job,
									 this->keepalive_interval - diff);
		this->keepalive_job_state = KEEPALIVE_JOB_CREATED;
	}
}

METHOD(ike_sa_t, update_keepalive_info, void,
	private_ike_sa_t *this, bool enable, bool restart_orig)
{
	keepalive_info_prop ka_info;

	if (!this->keepalive)
	{
		return;
	}
	if (restart_orig && this->wdrv_keepalive)
	{	/* Restart default software keep-alive in IKE_SA */
		this->wdrv_keepalive = FALSE;
		send_keepalive(this);
	}
	if (this->keepalive_ntfy_status == enable) {
		return;
	}
	ka_info.enable = this->keepalive_ntfy_status = enable;
	ka_info.interval = this->keepalive_interval;
	snprintf(ka_info.src_ip, IP6_ADDR_LEN, "%H", this->my_host);
	ka_info.src_port = charon->socket->get_port(charon->socket, TRUE);
	snprintf(ka_info.dst_ip, IP6_ADDR_LEN, "%H", this->other_host);
	ka_info.dst_port = IKEV2_NATT_PORT;
	notify_wod(N_KEEP_ALIVE, get_name(this), (void *)&ka_info);
}

METHOD(ike_sa_t, get_ike_cfg, ike_cfg_t*,
	private_ike_sa_t *this)
{
	return this->ike_cfg;
}

METHOD(ike_sa_t, set_ike_cfg, void,
	private_ike_sa_t *this, ike_cfg_t *ike_cfg)
{
	ike_cfg->get_ref(ike_cfg);
	this->ike_cfg = ike_cfg;
}

METHOD(ike_sa_t, enable_extension, void,
	private_ike_sa_t *this, ike_extension_t extension)
{
	this->extensions |= extension;
}

METHOD(ike_sa_t, supports_extension, bool,
	private_ike_sa_t *this, ike_extension_t extension)
{
	return (this->extensions & extension) != FALSE;
}

METHOD(ike_sa_t, has_condition, bool,
	private_ike_sa_t *this, ike_condition_t condition)
{
	return (this->conditions & condition) != FALSE;
}

METHOD(ike_sa_t, set_condition, void,
	private_ike_sa_t *this, ike_condition_t condition, bool enable)
{
	if (has_condition(this, condition) != enable)
	{
		if (enable)
		{
			this->conditions |= condition;
			switch (condition)
			{
				case COND_NAT_HERE:
					DBG1(DBG_IKE, "local host is behind NAT, sending keep alives");
					this->conditions |= COND_NAT_ANY;
					send_keepalive(this);
					break;
				case COND_NAT_THERE:
					DBG1(DBG_IKE, "remote host is behind NAT");
					this->conditions |= COND_NAT_ANY;
					break;
				case COND_NAT_FAKE:
					DBG1(DBG_IKE, "faking NAT situation to enforce UDP encapsulation");
					this->conditions |= COND_NAT_ANY;
					break;
				default:
					break;
			}
		}
		else
		{
			this->conditions &= ~condition;
			switch (condition)
			{
				case COND_NAT_HERE:
				case COND_NAT_FAKE:
				case COND_NAT_THERE:
					set_condition(this, COND_NAT_ANY,
								  has_condition(this, COND_NAT_HERE) ||
								  has_condition(this, COND_NAT_THERE) ||
								  has_condition(this, COND_NAT_FAKE));
					break;
				default:
					break;
			}
		}
	}
}

METHOD(ike_sa_t, send_dpd, status_t,
	private_ike_sa_t *this)
{
	job_t *job;
	time_t diff, delay;
	bool task_queued = FALSE;

	if (this->state == IKE_PASSIVE)
	{
		return INVALID_STATE;
	}
	delay = this->peer_cfg->get_dpd(this->peer_cfg);
	if (this->task_manager->busy(this->task_manager))
	{
		/* an exchange is in the air, no need to start a DPD check */
		diff = 0;
	}
	else
	{
		/* check if there was any inbound traffic */
		time_t last_in, now;
		last_in = get_use_time(this, TRUE);
		now = time_monotonic(NULL);
		diff = now - last_in;

		if (this->oos)
		{
			delay = this->peer_cfg->get_oos_time(this->peer_cfg);
			diff = now - this->oos_start_time;
			DBG1(DBG_IKE, "OOS sending DPD - diff: %d, delay: %d", diff, delay);
		}
		if (!delay || diff >= delay)
		{
			/* too long ago, initiate dead peer detection */
			DBG1(DBG_IKE, "sending DPD request");
			this->task_manager->queue_dpd(this->task_manager);
			task_queued = TRUE;
			diff = 0;
		}
	}
	/* recheck in "interval" seconds */
	if (delay)
	{
		job = (job_t*)send_dpd_job_create(this->ike_sa_id);
		lib->scheduler->schedule_job(lib->scheduler, job, delay - diff);
	}
	if (task_queued)
	{
		return this->task_manager->initiate(this->task_manager);
	}
	return SUCCESS;
}

METHOD(ike_sa_t, get_state, ike_sa_state_t,
	private_ike_sa_t *this)
{
	return this->state;
}

METHOD(ike_sa_t, set_state, void,
	private_ike_sa_t *this, ike_sa_state_t state)
{
	bool trigger_dpd = FALSE, keepalives = FALSE;

	DBG2(DBG_IKE, "IKE_SA %s[%d] state change: %N => %N",
		 get_name(this), this->unique_id,
		 ike_sa_state_names, this->state,
		 ike_sa_state_names, state);

	switch (state)
	{
		case IKE_ESTABLISHED:
		{
			if (this->state == IKE_CONNECTING ||
				this->state == IKE_PASSIVE)
			{
				job_t *job;
				u_int32_t t;

				/* calculate rekey, reauth and lifetime */
				this->stats[STAT_ESTABLISHED] = time_monotonic(NULL);

				/* schedule rekeying if we have a time which is smaller than
				 * an already scheduled rekeying */
				t = this->peer_cfg->get_rekey_time(this->peer_cfg, TRUE);
				if (t && (this->stats[STAT_REKEY] == 0 ||
					(this->stats[STAT_REKEY] > t + this->stats[STAT_ESTABLISHED])))
				{
					this->stats[STAT_REKEY] = t + this->stats[STAT_ESTABLISHED];
					job = (job_t*)rekey_ike_sa_job_create(this->ike_sa_id, FALSE);
#ifdef STRONGSWAN_UNIT_TEST
					lib->scheduler->ut_start_timer(lib->scheduler,job, t*1000, UT_SIMULATE_REKEY_TIMER_PROPERTY);
#else
					lib->scheduler->schedule_job(lib->scheduler, job, t);
#endif
					DBG1(DBG_IKE, "scheduling rekeying in %ds", t);
				}
				t = this->peer_cfg->get_reauth_time(this->peer_cfg, TRUE);
				if (t && (this->stats[STAT_REAUTH] == 0 ||
					(this->stats[STAT_REAUTH] > t + this->stats[STAT_ESTABLISHED])))
				{
					this->stats[STAT_REAUTH] = t + this->stats[STAT_ESTABLISHED];
					job = (job_t*)rekey_ike_sa_job_create(this->ike_sa_id, TRUE);
					lib->scheduler->schedule_job(lib->scheduler, job, t);
					DBG1(DBG_IKE, "scheduling reauthentication in %ds", t);
				}
				t = this->peer_cfg->get_over_time(this->peer_cfg);
				if (this->stats[STAT_REKEY] || this->stats[STAT_REAUTH])
				{
					if (this->stats[STAT_REAUTH] == 0)
					{
						this->stats[STAT_DELETE] = this->stats[STAT_REKEY];
					}
					else if (this->stats[STAT_REKEY] == 0)
					{
						this->stats[STAT_DELETE] = this->stats[STAT_REAUTH];
					}
					else
					{
						this->stats[STAT_DELETE] = min(this->stats[STAT_REKEY],
													   this->stats[STAT_REAUTH]);
					}
					this->stats[STAT_DELETE] += t;
					t = this->stats[STAT_DELETE] - this->stats[STAT_ESTABLISHED];
					job = (job_t*)delete_ike_sa_job_create(this->ike_sa_id, TRUE);
					lib->scheduler->schedule_job(lib->scheduler, job, t);
					DBG1(DBG_IKE, "maximum IKE_SA lifetime %ds", t);
				}
				trigger_dpd = this->peer_cfg->get_dpd(this->peer_cfg);
				if (trigger_dpd)
				{
					/* Some peers delay the DELETE after rekeying an IKE_SA.
					 * If this delay is longer than our DPD delay, we would
					 * send a DPD request here. The IKE_SA is not ready to do
					 * so yet, so prevent that. */
					this->stats[STAT_INBOUND] = this->stats[STAT_ESTABLISHED];
				}
				if (this->state == IKE_PASSIVE)
				{
					keepalives = TRUE;
				}
			}
			break;
		}
		default:
			break;
	}
	charon->bus->ike_state_change(charon->bus, &this->public, state);
	this->state = state;

	if (trigger_dpd)
	{
		if (supports_extension(this, EXT_DPD))
		{
			send_dpd(this);
		}
		else
		{
			DBG1(DBG_IKE, "DPD not supported by peer, disabled");
		}
	}
	if (keepalives)
	{
		send_keepalive(this);
	}
}

METHOD(ike_sa_t, reset, void,
	private_ike_sa_t *this)
{
	/*  the responder ID is reset, as peer may choose another one */
	if (this->ike_sa_id->is_initiator(this->ike_sa_id))
	{
		this->ike_sa_id->set_responder_spi(this->ike_sa_id, 0);
	}

	set_state(this, IKE_CREATED);

	flush_auth_cfgs(this);

	this->keymat->destroy(this->keymat);
	this->keymat = keymat_create(this->version,
							this->ike_sa_id->is_initiator(this->ike_sa_id));

	this->task_manager->reset(this->task_manager, 0, 0);
}

METHOD(ike_sa_t, get_keymat, keymat_t*,
	private_ike_sa_t *this)
{
	return this->keymat;
}

static bool host_cmp(host_t *my_ip, host_t *cmp_ip)
{
	return my_ip->ip_equals(my_ip, cmp_ip);
}

static bool hostarray_find(array_t *my_vips, host_t *ip)
{
	int count = array_count(my_vips);
	int i = 0;
	host_t *vip;

	while (array_get(my_vips, i, &vip))
	{
		if (host_cmp(vip, ip))
			return true;
		i++;
	}
	return false;
}

METHOD(ike_sa_t, add_virtual_ip, void,
	private_ike_sa_t *this, bool local, host_t *ip)
{
	if (local)
	{
		char *iface;

		iface = strdupnull(this->peer_cfg->get_vif(this->peer_cfg));
		if (iface || hydra->kernel_interface->get_interface(hydra->kernel_interface, this->my_host, &iface))
		{
			//	if vip in my_vips, did not remove it
			if (hostarray_find(this->my_vips, ip))
			{
				DBG1(DBG_IKE, "%H Already in local virtual IPs", ip);
			}
			else if (ip->get_netbits(ip) &&
					hydra->kernel_interface->add_ip(hydra->kernel_interface, ip, ip->get_netbits(ip), iface) == SUCCESS)
			{
				array_insert_create(&this->my_vips, ARRAY_TAIL, ip->clone(ip));
			}
			else if (hydra->kernel_interface->add_ip(hydra->kernel_interface,
													ip, -1, iface) == SUCCESS)
			{
				array_insert_create(&this->my_vips, ARRAY_TAIL, ip->clone(ip));
			}
			else
			{
				DBG1(DBG_IKE, "installing virtual IP %H failed", ip);
			}
			free(iface);
		}
		else
		{
			DBG1(DBG_IKE, "looking up interface for virtual IP %H failed", ip);
		}
	}
	else
	{
		array_insert_create(&this->other_vips, ARRAY_TAIL, ip->clone(ip));
	}
}

METHOD(ike_sa_t, get_virtual_ips,  enumerator_t*,
	private_ike_sa_t *this)
{
	return array_create_enumerator(this->my_vips);
}

METHOD(ike_sa_t, clear_virtual_ips, void,
	private_ike_sa_t *this, bool local)
{
	array_t *vips;
	host_t *vip;

	vips = local ? this->my_vips : this->other_vips;
	if (!local && array_count(vips))
	{
		charon->bus->assign_vips(charon->bus, &this->public, FALSE);
	}
	while (array_remove(vips, ARRAY_HEAD, &vip))
	{
		if (local)
		{
			hydra->kernel_interface->del_ip(hydra->kernel_interface,
											vip, -1, TRUE);
		}
		vip->destroy(vip);
	}
}

METHOD(ike_sa_t, clear_virtual_ips2, void,
	private_ike_sa_t *this, bool local, linked_list_t *new_vips)
{
	array_t *vips, *keep_vips = NULL;
	host_t *vip;

	vips = local ? this->my_vips : this->other_vips;
	if (!local && array_count(vips))
	{
		charon->bus->assign_vips(charon->bus, &this->public, FALSE);
	}
	while (array_remove(vips, ARRAY_HEAD, &vip))
	{
		if (local)
		{
			//	 if vip in my_vips, did not remove it
			if (new_vips->find_first(new_vips, (void *)host_cmp, NULL, vip) == SUCCESS)
			{
				array_insert_create(&keep_vips, ARRAY_TAIL, vip->clone(vip));
				DBG1(DBG_IKE, "vip(%H) in my_vips, keep it", vip);
			}
			else
			{
				hydra->kernel_interface->del_ip(hydra->kernel_interface, vip, -1, TRUE);
			}
		}
		vip->destroy(vip);
	}

	if (local)
	{
		while (array_remove(keep_vips, ARRAY_HEAD, &vip))
		{
			array_insert_create(&vips, ARRAY_TAIL, vip);
		}
		array_destroy(keep_vips);
	}
}


METHOD(ike_sa_t, create_virtual_ip_enumerator, enumerator_t*,
	private_ike_sa_t *this, bool local)
{
	if (local)
	{
		return array_create_enumerator(this->my_vips);
	}
	return array_create_enumerator(this->other_vips);
}

METHOD(ike_sa_t, add_dns, void, private_ike_sa_t *this, host_t *ip)
{
	array_insert_create(&this->my_dnss, ARRAY_TAIL, ip->clone(ip));
}

METHOD(ike_sa_t, clear_dnss, void, private_ike_sa_t *this)
{
	host_t *dns;

	while (array_remove(this->my_dnss, ARRAY_HEAD, &dns))
	{
		dns->destroy(dns);
	}
}

METHOD(ike_sa_t, create_dns_enumerator, enumerator_t*,
	private_ike_sa_t *this)
{
	return array_create_enumerator(this->my_dnss);
}

METHOD(ike_sa_t, add_pcscf, void, private_ike_sa_t *this, host_t *ip)
{
	array_insert_create(&this->my_pcscfs, ARRAY_TAIL, ip->clone(ip));
}

METHOD(ike_sa_t, clear_pcscfs, void, private_ike_sa_t *this)
{
	host_t *pcscf;

	while (array_remove(this->my_pcscfs, ARRAY_HEAD, &pcscf))
	{
		pcscf->destroy(pcscf);
	}
}

METHOD(ike_sa_t, create_pcscf_enumerator, enumerator_t*, private_ike_sa_t *this)
{
	return array_create_enumerator(this->my_pcscfs);
}

METHOD(ike_sa_t, add_intsubnet, void, private_ike_sa_t *this, host_t *ip)
{
	array_insert_create(&this->my_intsubnets, ARRAY_TAIL, ip->clone(ip));
}

METHOD(ike_sa_t, clear_intsubnets, void, private_ike_sa_t *this)
{
	host_t *subnet;

	while (array_remove(this->my_intsubnets, ARRAY_HEAD, &subnet))
	{
		subnet->destroy(subnet);
	}
}

METHOD(ike_sa_t, create_intsubnet_enumerator, enumerator_t*, private_ike_sa_t *this)
{
	return array_create_enumerator(this->my_intsubnets);
}

METHOD(ike_sa_t, add_intnetmask, void, private_ike_sa_t *this, host_t *ip)
{
	array_insert_create(&this->my_intnetmasks, ARRAY_TAIL, ip->clone(ip));
}

METHOD(ike_sa_t, clear_intnetmasks, void, private_ike_sa_t *this)
{
	host_t *netmask;

	while (array_remove(this->my_intnetmasks, ARRAY_HEAD, &netmask))
	{
		netmask->destroy(netmask);
	}
}

METHOD(ike_sa_t, create_intnetmask_enumerator, enumerator_t*, private_ike_sa_t *this)
{
	return array_create_enumerator(this->my_intnetmasks);
}

METHOD(ike_sa_t, add_peer_address, void,
	private_ike_sa_t *this, host_t *host)
{
	array_insert_create(&this->peer_addresses, ARRAY_TAIL, host);
}

METHOD(ike_sa_t, create_peer_address_enumerator, enumerator_t*,
	private_ike_sa_t *this)
{
	if (this->peer_addresses)
	{
		return array_create_enumerator(this->peer_addresses);
	}
	/* in case we don't have MOBIKE */
	return enumerator_create_single(this->other_host, NULL);
}

METHOD(ike_sa_t, clear_peer_addresses, void,
	private_ike_sa_t *this)
{
	array_destroy_offset(this->peer_addresses, offsetof(host_t, destroy));
	this->peer_addresses = NULL;
}

METHOD(ike_sa_t, has_mapping_changed, bool,
	private_ike_sa_t *this, chunk_t hash)
{
	if (this->nat_detection_dest.ptr == NULL)
	{
		this->nat_detection_dest = chunk_clone(hash);
		return FALSE;
	}
	if (chunk_equals(hash, this->nat_detection_dest))
	{
		return FALSE;
	}
	free(this->nat_detection_dest.ptr);
	this->nat_detection_dest = chunk_clone(hash);
	return TRUE;
}

METHOD(ike_sa_t, set_wdrv_keepalive, void,
	private_ike_sa_t *this, bool enable)
{
	if (this->wdrv_keepalive == enable)
	{
		return;
	}

	this->wdrv_keepalive = enable;
	if (!enable)
	{
		/* Restart default software keep-alive in IKE_SA */
		send_keepalive(this);
	}
}

METHOD(ike_sa_t, set_keepalive_job_state, void,
	private_ike_sa_t *this, keepalive_job_state_t state)
{
	this->keepalive_job_state = state;
}

METHOD(ike_sa_t, get_oos, bool,
	private_ike_sa_t *this)
{
	return this->oos;
}

METHOD(ike_sa_t, set_oos, void,
	private_ike_sa_t *this, int oos)
{
	if (oos == OOS_START)
	{
		this->oos_start_time = time_monotonic(NULL);
		this->oos_seq_num++;
		this->oos = TRUE;
	}
	else
	{
		this->oos = FALSE;
	}
	notify_wod(N_OOS, get_name(this), (void*)(intptr_t)oos);
}

METHOD(ike_sa_t, get_oos_seq_num, u_int32_t,
	private_ike_sa_t *this)
{
	return this->oos_seq_num;
}

METHOD(ike_sa_t, get_status_code, int,
	private_ike_sa_t *this)
{
	return this->status_code;
}

METHOD(ike_sa_t, set_status_code, void,
	private_ike_sa_t *this, int status_code)
{
	this->status_code = status_code;
}

METHOD(ike_sa_t, set_pcscf_need_update, void,
	private_ike_sa_t *this, bool need_update)
{
	this->pcscf_need_update = need_update;
}


METHOD(ike_sa_t, set_backoff_timer, void,
	private_ike_sa_t *this, int backoff_timer)
{
	this->backoff_timer_value = backoff_timer;
}

METHOD(ike_sa_t, set_notify_device_identity ,void,
	private_ike_sa_t *this, bool need)
{
	this->notify_device_identity = need;
}

METHOD(ike_sa_t, get_sub_error_code, int,
	private_ike_sa_t *this)
{
	return this->sub_error_code;
}

METHOD(ike_sa_t, get_backoff_timer, int,
	private_ike_sa_t *this)
{
	return this->backoff_timer_value;
}

METHOD(ike_sa_t, get_pcscf_need_update, bool,
	private_ike_sa_t *this)
{
	return this->pcscf_need_update;
}

METHOD(ike_sa_t, get_notify_device_identity, bool,
	private_ike_sa_t *this)
{
	return this->notify_device_identity;
}


METHOD(ike_sa_t, set_sub_error_code, void,
	private_ike_sa_t *this, int sub_error_code)
{
	this->sub_error_code = sub_error_code;
}

METHOD(ike_sa_t, set_pending_updates, void,
	private_ike_sa_t *this, u_int32_t updates)
{
	this->pending_updates = updates;
}

METHOD(ike_sa_t, get_pending_updates, u_int32_t,
	private_ike_sa_t *this)
{
	return this->pending_updates;
}

METHOD(ike_sa_t, float_ports, void,
	   private_ike_sa_t *this)
{
	/* do not switch if we have a custom port from MOBIKE/NAT */
	if (this->my_host->get_port(this->my_host) ==
			charon->socket->get_port(charon->socket, FALSE))
	{
		this->my_host->set_port(this->my_host,
								charon->socket->get_port(charon->socket, TRUE));
	}
	if (this->other_host->get_port(this->other_host) == IKEV2_UDP_PORT)
	{
		this->other_host->set_port(this->other_host, IKEV2_NATT_PORT);
	}
}

METHOD(ike_sa_t, update_hosts, void,
	private_ike_sa_t *this, host_t *me, host_t *other, bool force)
{
	bool update = FALSE;

	if (me == NULL)
	{
		me = this->my_host;
	}
	if (other == NULL)
	{
		other = this->other_host;
	}

	/* apply hosts on first received message */
	if (this->my_host->is_anyaddr(this->my_host) ||
		this->other_host->is_anyaddr(this->other_host))
	{
		set_my_host(this, me->clone(me));
		set_other_host(this, other->clone(other));
		update = TRUE;
	}
	else
	{
		/* update our address in any case */
		if (force && !me->equals(me, this->my_host))
		{
			set_my_host(this, me->clone(me));
			update = TRUE;
		}

		if (!other->equals(other, this->other_host) &&
			(force || has_condition(this, COND_NAT_THERE)))
		{
			/* 1. update others address if we are NOT NATed
			 * 2. update other's address if we are behind a static NAT,
			 * which we assume is the case if we are not initiator */
			if (force ||
				(!has_condition(this, COND_NAT_HERE) ||
				 !has_condition(this, COND_ORIGINAL_INITIATOR)))
			{
				set_other_host(this, other->clone(other));
				update = TRUE;
			}
		}
	}

	/* update all associated CHILD_SAs, if required */
	if (update)
	{
		enumerator_t *enumerator;
		child_sa_t *child_sa;
		linked_list_t *vips;

		vips = linked_list_create_from_enumerator(
									array_create_enumerator(this->my_vips));

		enumerator = array_create_enumerator(this->child_sas);
		while (enumerator->enumerate(enumerator, &child_sa))
		{
			if (child_sa->update(child_sa, this->my_host, this->other_host,
					vips, has_condition(this, COND_NAT_ANY)) == NOT_SUPPORTED)
			{
				this->public.rekey_child_sa(&this->public,
						child_sa->get_protocol(child_sa),
						child_sa->get_spi(child_sa, TRUE));
			}
		}
		enumerator->destroy(enumerator);

		vips->destroy(vips);
	}
}

/**
 * Set configured DSCP value on packet
 */
static void set_dscp(private_ike_sa_t *this, packet_t *packet)
{
	ike_cfg_t *ike_cfg;

	/* prefer IKE config on peer_cfg, as its selection is more accurate
	 * then the initial IKE config */
	if (this->peer_cfg)
	{
		ike_cfg = this->peer_cfg->get_ike_cfg(this->peer_cfg);
	}
	else
	{
		ike_cfg = this->ike_cfg;
	}
	if (ike_cfg)
	{
		packet->set_dscp(packet, ike_cfg->get_dscp(ike_cfg));
	}
}

METHOD(ike_sa_t, generate_message, status_t,
	private_ike_sa_t *this, message_t *message, packet_t **packet)
{
	status_t status;

	if (message->is_encoded(message))
	{	/* already encoded in task, but set DSCP value */
		*packet = message->get_packet(message);
		set_dscp(this, *packet);
		return SUCCESS;
	}
	this->stats[STAT_OUTBOUND] = time_monotonic(NULL);
	message->set_ike_sa_id(message, this->ike_sa_id);
	charon->bus->message(charon->bus, message, FALSE, TRUE);
	status = message->generate(message, this->keymat, packet);
	if (status == SUCCESS)
	{
		set_dscp(this, *packet);
		charon->bus->message(charon->bus, message, FALSE, FALSE);
	}
	return status;
}

static bool filter_fragments(private_ike_sa_t *this, packet_t **fragment,
							 packet_t **packet)
{
	*packet = (*fragment)->clone(*fragment);
	set_dscp(this, *packet);
	return TRUE;
}

METHOD(ike_sa_t, generate_message_fragmented, status_t,
	private_ike_sa_t *this, message_t *message, enumerator_t **packets)
{
	enumerator_t *fragments;
	packet_t *packet;
	status_t status;
	bool use_frags = FALSE;

	if (this->ike_cfg)
	{
		switch (this->ike_cfg->fragmentation(this->ike_cfg))
		{
			case FRAGMENTATION_FORCE:
				use_frags = TRUE;
				break;
			case FRAGMENTATION_YES:
				use_frags = supports_extension(this, EXT_IKE_FRAGMENTATION);
				if (use_frags && this->version == IKEV1 &&
					supports_extension(this, EXT_MS_WINDOWS))
				{
					/* It seems Windows 7 and 8 peers only accept proprietary
					 * fragmented messages if they expect certificates. */
					use_frags = message->get_payload(message,
													 CERTIFICATE_V1) != NULL;
				}
				break;
			default:
				break;
		}
	}
	if (!use_frags)
	{
		status = generate_message(this, message, &packet);
		if (status != SUCCESS)
		{
			return status;
		}
		*packets = enumerator_create_single(packet, NULL);
		return SUCCESS;
	}

	this->stats[STAT_OUTBOUND] = time_monotonic(NULL);
	message->set_ike_sa_id(message, this->ike_sa_id);
	charon->bus->message(charon->bus, message, FALSE, TRUE);
	status = message->fragment(message, this->keymat, this->fragment_size,
							   &fragments);
	if (status == SUCCESS)
	{
		charon->bus->message(charon->bus, message, FALSE, FALSE);
		*packets = enumerator_create_filter(fragments, (void*)filter_fragments,
											this, NULL);
	}
	return status;
}

METHOD(ike_sa_t, set_kmaddress, void,
	private_ike_sa_t *this, host_t *local, host_t *remote)
{
	DESTROY_IF(this->local_host);
	DESTROY_IF(this->remote_host);
	this->local_host = local->clone(local);
	this->remote_host = remote->clone(remote);
}

#ifdef ME
METHOD(ike_sa_t, act_as_mediation_server, void,
	private_ike_sa_t *this)
{
	charon->mediation_manager->update_sa_id(charon->mediation_manager,
			this->other_id, this->ike_sa_id);
	this->is_mediation_server = TRUE;
}

METHOD(ike_sa_t, get_server_reflexive_host, host_t*,
	private_ike_sa_t *this)
{
	return this->server_reflexive_host;
}

METHOD(ike_sa_t, set_server_reflexive_host, void,
	private_ike_sa_t *this, host_t *host)
{
	DESTROY_IF(this->server_reflexive_host);
	this->server_reflexive_host = host;
}

METHOD(ike_sa_t, get_connect_id, chunk_t,
	private_ike_sa_t *this)
{
	return this->connect_id;
}

METHOD(ike_sa_t, respond, status_t,
	private_ike_sa_t *this, identification_t *peer_id, chunk_t connect_id)
{
	ike_me_t *task = ike_me_create(&this->public, TRUE);
	task->respond(task, peer_id, connect_id);
	this->task_manager->queue_task(this->task_manager, (task_t*)task);
	return this->task_manager->initiate(this->task_manager);
}

METHOD(ike_sa_t, callback, status_t,
	private_ike_sa_t *this, identification_t *peer_id)
{
	ike_me_t *task = ike_me_create(&this->public, TRUE);
	task->callback(task, peer_id);
	this->task_manager->queue_task(this->task_manager, (task_t*)task);
	return this->task_manager->initiate(this->task_manager);
}

METHOD(ike_sa_t, relay, status_t,
	private_ike_sa_t *this, identification_t *requester, chunk_t connect_id,
	chunk_t connect_key, linked_list_t *endpoints, bool response)
{
	ike_me_t *task = ike_me_create(&this->public, TRUE);
	task->relay(task, requester, connect_id, connect_key, endpoints, response);
	this->task_manager->queue_task(this->task_manager, (task_t*)task);
	return this->task_manager->initiate(this->task_manager);
}

METHOD(ike_sa_t, initiate_mediation, status_t,
	private_ike_sa_t *this, peer_cfg_t *mediated_cfg)
{
	ike_me_t *task = ike_me_create(&this->public, TRUE);
	task->connect(task, mediated_cfg->get_peer_id(mediated_cfg));
	this->task_manager->queue_task(this->task_manager, (task_t*)task);
	return this->task_manager->initiate(this->task_manager);
}

METHOD(ike_sa_t, initiate_mediated, status_t,
	private_ike_sa_t *this, host_t *me, host_t *other, chunk_t connect_id)
{
	set_my_host(this, me->clone(me));
	set_other_host(this, other->clone(other));
	chunk_free(&this->connect_id);
	this->connect_id = chunk_clone(connect_id);
	return this->task_manager->initiate(this->task_manager);
}
#endif /* ME */

/**
 * Resolve DNS host in configuration
 */
static void resolve_hosts(private_ike_sa_t *this)
{
	host_t *host;
	int family = AF_UNSPEC;

	switch (charon->socket->supported_families(charon->socket))
	{
		case SOCKET_FAMILY_IPV4:
			family = AF_INET;
			break;
		case SOCKET_FAMILY_IPV6:
			family = AF_INET6;
			break;
		case SOCKET_FAMILY_BOTH:
		case SOCKET_FAMILY_NONE:
			break;
	}

	/* if an IP address is set locally, use the same family to resolve remote */
	if (family == AF_UNSPEC && !this->remote_host)
	{
		if (this->local_host)
		{
			family = this->local_host->get_family(this->local_host);
		}
		else
		{
			family = ike_cfg_get_family(this->ike_cfg, TRUE);
		}
	}

	if (this->remote_host)
	{
		host = this->remote_host->clone(this->remote_host);
		host->set_port(host, IKEV2_UDP_PORT);
	}
	else
	{
		host = this->ike_cfg->resolve_other(this->ike_cfg, family);
	}
	if (host)
	{
		if (!host->is_anyaddr(host) ||
			this->other_host->is_anyaddr(this->other_host))
		{	/* don't set to %any if we currently have an address, but the
			 * address family might have changed */
			set_other_host(this, host);
		}
		else
		{	/* reuse the original port as some implementations might not like
			 * initial IKE messages on other ports */
			this->other_host->set_port(this->other_host, host->get_port(host));
			host->destroy(host);
		}
	}

	if (this->local_host)
	{
		host = this->local_host->clone(this->local_host);
		host->set_port(host, charon->socket->get_port(charon->socket, FALSE));
	}
	else
	{
		/* use same address family as for other */
		if (!this->other_host->is_anyaddr(this->other_host))
		{
			family = this->other_host->get_family(this->other_host);
		}
		host = this->ike_cfg->resolve_me(this->ike_cfg, family);

		if (host && host->is_anyaddr(host) &&
			!this->other_host->is_anyaddr(this->other_host))
		{
			host->destroy(host);
			host = hydra->kernel_interface->get_source_addr(
							hydra->kernel_interface, this->other_host, NULL);
			if (host)
			{
				host->set_port(host, this->ike_cfg->get_my_port(this->ike_cfg));
			}
			else
			{	/* fallback to address family specific %any(6), if configured */
				host = this->ike_cfg->resolve_me(this->ike_cfg, family);
			}
		}
	}
	if (host)
	{
		set_my_host(this, host);
	}
}

METHOD(ike_sa_t, initiate, status_t,
	private_ike_sa_t *this, child_cfg_t *child_cfg, u_int32_t reqid,
	traffic_selector_t *tsi, traffic_selector_t *tsr)
{
	bool defer_initiate = FALSE;

	if (this->state == IKE_CREATED)
	{
		if (this->my_host->is_anyaddr(this->my_host) ||
			this->other_host->is_anyaddr(this->other_host))
		{
			resolve_hosts(this);
		}

		if (this->other_host->is_anyaddr(this->other_host)
#ifdef ME
			&& !this->peer_cfg->get_mediated_by(this->peer_cfg)
#endif /* ME */
			)
		{
			char *addr;

			addr = this->ike_cfg->get_other_addr(this->ike_cfg);
			if (!this->retry_initiate_interval)
			{
				DBG1(DBG_IKE, "unable to resolve %s, initiate aborted",
					 addr);
				DESTROY_IF(child_cfg);
				charon->bus->alert(charon->bus, ALERT_PEER_ADDR_FAILED);
				return DESTROY_ME;
			}
			DBG1(DBG_IKE, "unable to resolve %s, retrying in %ds",
				 addr, this->retry_initiate_interval);
			defer_initiate = TRUE;
		}

		set_condition(this, COND_ORIGINAL_INITIATOR, TRUE);
		this->task_manager->queue_ike(this->task_manager);
	}

#ifdef ME
	if (this->peer_cfg->is_mediation(this->peer_cfg))
	{
		if (this->state == IKE_ESTABLISHED)
		{
			/* mediation connection is already established, retrigger state
			 * change to notify bus listeners */
			DBG1(DBG_IKE, "mediation connection is already up");
			set_state(this, IKE_ESTABLISHED);
		}
		DESTROY_IF(child_cfg);
	}
	else
#endif /* ME */
	if (child_cfg)
	{
		/* normal IKE_SA with CHILD_SA */
		this->task_manager->queue_child(this->task_manager, child_cfg, reqid,
										tsi, tsr);
#ifdef ME
		if (this->peer_cfg->get_mediated_by(this->peer_cfg))
		{
			/* mediated connection, initiate mediation process */
			job_t *job = (job_t*)initiate_mediation_job_create(this->ike_sa_id);
			lib->processor->queue_job(lib->processor, job);
			return SUCCESS;
		}
#endif /* ME */
	}

	if (defer_initiate)
	{
		if (!this->retry_initiate_queued)
		{
			job_t *job = (job_t*)retry_initiate_job_create(this->ike_sa_id);
			lib->scheduler->schedule_job(lib->scheduler, (job_t*)job,
										 this->retry_initiate_interval);
			this->retry_initiate_queued = TRUE;
		}
		return SUCCESS;
	}
	this->retry_initiate_queued = FALSE;
	return this->task_manager->initiate(this->task_manager);
}

METHOD(ike_sa_t, retry_initiate, status_t,
	private_ike_sa_t *this)
{
	if (this->retry_initiate_queued)
	{
		this->retry_initiate_queued = FALSE;
		return initiate(this, NULL, 0, NULL, NULL);
	}
	return SUCCESS;
}

METHOD(ike_sa_t, process_message, status_t,
	private_ike_sa_t *this, message_t *message)
{
	status_t status;

	if (this->state == IKE_PASSIVE)
	{	/* do not handle messages in passive state */
		return FAILED;
	}
	if (message->get_major_version(message) != this->version)
	{
		DBG1(DBG_IKE, "ignoring %N IKEv%u exchange on %N SA",
			 exchange_type_names, message->get_exchange_type(message),
			 message->get_major_version(message),
			 ike_version_names, this->version);
		/* TODO-IKEv1: fall back to IKEv1 if we receive an IKEv1
		 * INVALID_MAJOR_VERSION on an IKEv2 SA. */
		return FAILED;
	}
	status = this->task_manager->process_message(this->task_manager, message);
	if (status == DESTROY_ME && this->status_code)
	{
		notify_wod_attach_failed(get_name(this),
								this->status_code, this->sub_error_code, this->backoff_timer_value);
	}
	if (this->flush_auth_cfg && this->state == IKE_ESTABLISHED)
	{
		/* authentication completed */
		this->flush_auth_cfg = FALSE;
		flush_auth_cfgs(this);
	}
	return status;
}

METHOD(ike_sa_t, get_id, ike_sa_id_t*,
	private_ike_sa_t *this)
{
	return this->ike_sa_id;
}

METHOD(ike_sa_t, get_version, ike_version_t,
	private_ike_sa_t *this)
{
	return this->version;
}

METHOD(ike_sa_t, get_my_id, identification_t*,
	private_ike_sa_t *this)
{
	return this->my_id;
}

METHOD(ike_sa_t, set_my_id, void,
	private_ike_sa_t *this, identification_t *me)
{
	DESTROY_IF(this->my_id);
	this->my_id = me;
}

METHOD(ike_sa_t, get_other_id, identification_t*,
	private_ike_sa_t *this)
{
	return this->other_id;
}

METHOD(ike_sa_t, get_other_eap_id, identification_t*,
	private_ike_sa_t *this)
{
	identification_t *id = NULL, *current;
	enumerator_t *enumerator;
	auth_cfg_t *cfg;

	enumerator = array_create_enumerator(this->other_auths);
	while (enumerator->enumerate(enumerator, &cfg))
	{
		/* prefer EAP-Identity of last round */
		current = cfg->get(cfg, AUTH_RULE_EAP_IDENTITY);
		if (!current || current->get_type(current) == ID_ANY)
		{
			current = cfg->get(cfg, AUTH_RULE_XAUTH_IDENTITY);
		}
		if (!current || current->get_type(current) == ID_ANY)
		{
			current = cfg->get(cfg, AUTH_RULE_IDENTITY);
		}
		if (current && current->get_type(current) != ID_ANY)
		{
			id = current;
			continue;
		}
	}
	enumerator->destroy(enumerator);
	if (id)
	{
		return id;
	}
	return this->other_id;
}

METHOD(ike_sa_t, set_other_id, void,
	private_ike_sa_t *this, identification_t *other)
{
	DESTROY_IF(this->other_id);
	this->other_id = other;
}

METHOD(ike_sa_t, add_child_sa, void,
	private_ike_sa_t *this, child_sa_t *child_sa)
{
	array_insert_create(&this->child_sas, ARRAY_TAIL, child_sa);
}

METHOD(ike_sa_t, get_child_sa, child_sa_t*,
	private_ike_sa_t *this, protocol_id_t protocol, u_int32_t spi, bool inbound)
{
	enumerator_t *enumerator;
	child_sa_t *current, *found = NULL;

	enumerator = array_create_enumerator(this->child_sas);
	while (enumerator->enumerate(enumerator, (void**)&current))
	{
		if (current->get_spi(current, inbound) == spi &&
			current->get_protocol(current) == protocol)
		{
			found = current;
		}
	}
	enumerator->destroy(enumerator);
	return found;
}

METHOD(ike_sa_t, get_child_count, int,
	private_ike_sa_t *this)
{
	return array_count(this->child_sas);
}

/**
 * Private data of a create_child_sa_enumerator()
 */
typedef struct {
	/** implements enumerator */
	enumerator_t public;
	/** inner array enumerator */
	enumerator_t *inner;
	/** current item */
	child_sa_t *current;
} child_enumerator_t;

METHOD(enumerator_t, child_enumerate, bool,
	child_enumerator_t *this, child_sa_t **child_sa)
{
	if (this->inner->enumerate(this->inner, &this->current))
	{
		*child_sa = this->current;
		return TRUE;
	}
	return FALSE;
}

METHOD(enumerator_t, child_enumerator_destroy, void,
	child_enumerator_t *this)
{
	this->inner->destroy(this->inner);
	free(this);
}

METHOD(ike_sa_t, create_child_sa_enumerator, enumerator_t*,
	private_ike_sa_t *this)
{
	child_enumerator_t *enumerator;

	INIT(enumerator,
		.public = {
			.enumerate = (void*)_child_enumerate,
			.destroy = _child_enumerator_destroy,
		},
		.inner = array_create_enumerator(this->child_sas),
	);
	return &enumerator->public;
}

METHOD(ike_sa_t, remove_child_sa, void,
	private_ike_sa_t *this, enumerator_t *enumerator)
{
	child_enumerator_t *ce = (child_enumerator_t*)enumerator;

	array_remove_at(this->child_sas, ce->inner);
}

METHOD(ike_sa_t, rekey_child_sa, status_t,
	private_ike_sa_t *this, protocol_id_t protocol, u_int32_t spi)
{
	status_t status;

	if (this->state == IKE_PASSIVE)
	{
		return INVALID_STATE;
	}
	this->task_manager->queue_child_rekey(this->task_manager, protocol, spi);

	if (this->task_manager->initiating_type(this->task_manager) != EXCHANGE_TYPE_UNDEFINED)
	{
		DBG1(DBG_IKE, "================================ need_more");
		status = NEED_MORE;
	} else {
		DBG1(DBG_IKE, "================================ initiate");
		status = this->task_manager->initiate(this->task_manager);
	}

	return status;
}

METHOD(ike_sa_t, delete_child_sa, status_t,
	private_ike_sa_t *this, protocol_id_t protocol, u_int32_t spi, bool expired)
{
	if (this->state == IKE_PASSIVE)
	{
		return INVALID_STATE;
	}
	this->task_manager->queue_child_delete(this->task_manager,
										   protocol, spi, expired);
	return this->task_manager->initiate(this->task_manager);
}

METHOD(ike_sa_t, destroy_child_sa, status_t,
	private_ike_sa_t *this, protocol_id_t protocol, u_int32_t spi)
{
	enumerator_t *enumerator;
	child_sa_t *child_sa;
	status_t status = NOT_FOUND;

	enumerator = create_child_sa_enumerator(this);
	while (enumerator->enumerate(enumerator, (void**)&child_sa))
	{
		if (child_sa->get_protocol(child_sa) == protocol &&
			child_sa->get_spi(child_sa, TRUE) == spi)
		{
			remove_child_sa(this, enumerator);
			child_sa->destroy(child_sa);
			status = SUCCESS;
			break;
		}
	}
	enumerator->destroy(enumerator);
	return status;
}

METHOD(ike_sa_t, delete_, status_t,
	private_ike_sa_t *this)
{
	switch (this->state)
	{
		case IKE_REKEYING:
			if (this->version == IKEV1)
			{	/* SA has been reauthenticated, delete */
				charon->bus->ike_updown(charon->bus, &this->public, FALSE);
				break;
			}
			/* FALL */
		case IKE_ESTABLISHED:
			if (time_monotonic(NULL) >= this->stats[STAT_DELETE])
			{	/* IKE_SA hard lifetime hit */
				charon->bus->alert(charon->bus, ALERT_IKE_SA_EXPIRED);
			}
			this->task_manager->queue_ike_delete(this->task_manager);
			return this->task_manager->initiate(this->task_manager);
		case IKE_CREATED:
			DBG1(DBG_IKE, "deleting unestablished IKE_SA");
			break;
		case IKE_PASSIVE:
			break;
		default:
			DBG1(DBG_IKE, "destroying IKE_SA in state %N "
				"without notification", ike_sa_state_names, this->state);
			charon->bus->ike_updown(charon->bus, &this->public, FALSE);
			break;
	}
	return DESTROY_ME;
}

METHOD(ike_sa_t, rekey, status_t,
	private_ike_sa_t *this)
{
	status_t status = SUCCESS;

	if (this->state == IKE_PASSIVE)
	{
		return INVALID_STATE;
	}
	this->task_manager->queue_ike_rekey(this->task_manager);

	if (this->task_manager->initiating_type(this->task_manager) != EXCHANGE_TYPE_UNDEFINED)
	{
		DBG1(DBG_IKE, "================================ need_more");
		status = NEED_MORE;
	} else {
		DBG1(DBG_IKE, "================================ initiate");
		status = this->task_manager->initiate(this->task_manager);
	}

	return status;
}

METHOD(ike_sa_t, reauth, status_t,
	private_ike_sa_t *this)
{
	status_t status = SUCCESS;

	if (this->state == IKE_PASSIVE)
	{
		return INVALID_STATE;
	}
	if (this->state == IKE_CONNECTING)
	{
		DBG0(DBG_IKE, "reinitiating IKE_SA %s[%d]",
			 get_name(this), this->unique_id);
		reset(this);
		this->task_manager->queue_ike(this->task_manager);
		return this->task_manager->initiate(this->task_manager);
	}
	/* we can't reauthenticate as responder when we use EAP or virtual IPs.
	 * If the peer does not support RFC4478, there is no way to keep the
	 * IKE_SA up. */
	if (!has_condition(this, COND_ORIGINAL_INITIATOR))
	{
		DBG1(DBG_IKE, "initiator did not reauthenticate as requested");
		if (array_count(this->other_vips) != 0 ||
			has_condition(this, COND_XAUTH_AUTHENTICATED) ||
			has_condition(this, COND_EAP_AUTHENTICATED)
#ifdef ME
			/* as mediation server we too cannot reauth the IKE_SA */
			|| this->is_mediation_server
#endif /* ME */
			)
		{
			time_t del, now;

			del = this->stats[STAT_DELETE];
			now = time_monotonic(NULL);
			DBG1(DBG_IKE, "IKE_SA %s[%d] will timeout in %V",
				 get_name(this), this->unique_id, &now, &del);
			return FAILED;
		}
		else
		{
			DBG0(DBG_IKE, "reauthenticating IKE_SA %s[%d] actively",
				 get_name(this), this->unique_id);
		}
	}
	else
	{
		DBG0(DBG_IKE, "reauthenticating IKE_SA %s[%d]",
			 get_name(this), this->unique_id);
	}
	set_condition(this, COND_REAUTHENTICATING, TRUE);
	this->task_manager->queue_ike_reauth(this->task_manager);
	if (this->task_manager->initiating_type(this->task_manager) != EXCHANGE_TYPE_UNDEFINED)
	{
		DBG1(DBG_IKE, "================================ need_more");
		status = NEED_MORE;
	} else {
		DBG1(DBG_IKE, "================================ initiate");
		status = this->task_manager->initiate(this->task_manager);
	}

	return status;
}

/**
 * Check if tasks to create CHILD_SAs are queued in the given queue
 */
static bool is_child_queued(private_ike_sa_t *this, task_queue_t queue)
{
	enumerator_t *enumerator;
	task_t *task;
	bool found = FALSE;

	enumerator = this->task_manager->create_task_enumerator(this->task_manager,
															queue);
	while (enumerator->enumerate(enumerator, &task))
	{
		if (task->get_type(task) == TASK_CHILD_CREATE ||
			task->get_type(task) == TASK_QUICK_MODE)
		{
			found = TRUE;
			break;
		}
	}
	enumerator->destroy(enumerator);
	return found;
}

METHOD(ike_sa_t, reestablish, status_t,
	private_ike_sa_t *this)
{
	ike_sa_t *new;
	host_t *host;
	action_t action;
	enumerator_t *enumerator;
	child_sa_t *child_sa;
	child_cfg_t *child_cfg;
	bool restart = FALSE;
	status_t status = FAILED;

	if (has_condition(this, COND_REAUTHENTICATING))
	{	/* only reauthenticate if we have children */
		if (array_count(this->child_sas) == 0
#ifdef ME
			/* allow reauth of mediation connections without CHILD_SAs */
			&& !this->peer_cfg->is_mediation(this->peer_cfg)
#endif /* ME */
			)
		{
			DBG1(DBG_IKE, "unable to reauthenticate IKE_SA, no CHILD_SA "
				 "to recreate");
		}
		else
		{
			restart = TRUE;
		}
	}
	else
	{	/* check if we have children to keep up at all */
		enumerator = array_create_enumerator(this->child_sas);
		while (enumerator->enumerate(enumerator, (void**)&child_sa))
		{
			if (this->state == IKE_DELETING)
			{
				action = child_sa->get_close_action(child_sa);
			}
			else
			{
				action = child_sa->get_dpd_action(child_sa);
			}
			switch (action)
			{
				case ACTION_RESTART:
					restart = TRUE;
					break;
				case ACTION_ROUTE:
					charon->traps->install(charon->traps, this->peer_cfg,
										   child_sa->get_config(child_sa),
										   child_sa->get_reqid(child_sa));
					break;
				default:
					break;
			}
		}
		enumerator->destroy(enumerator);
		/* check if we have tasks that recreate children */
		if (!restart)
		{
			restart = is_child_queued(this, TASK_QUEUE_ACTIVE) ||
					  is_child_queued(this, TASK_QUEUE_QUEUED);
		}
#ifdef ME
		/* mediation connections have no children, keep them up anyway */
		if (this->peer_cfg->is_mediation(this->peer_cfg))
		{
			restart = TRUE;
		}
#endif /* ME */
	}
	if (!restart)
	{
		return FAILED;
	}

	/* check if we are able to reestablish this IKE_SA */
	if (!has_condition(this, COND_ORIGINAL_INITIATOR) &&
		(array_count(this->other_vips) != 0 ||
		 has_condition(this, COND_EAP_AUTHENTICATED)
#ifdef ME
		 || this->is_mediation_server
#endif /* ME */
		))
	{
		DBG1(DBG_IKE, "unable to reestablish IKE_SA due to asymmetric setup");
		return FAILED;
	}

	new = charon->ike_sa_manager->checkout_new(charon->ike_sa_manager,
											   this->version, TRUE);
	if (!new)
	{
		return FAILED;
	}
	new->set_peer_cfg(new, this->peer_cfg);
	host = this->other_host;
	new->set_other_host(new, host->clone(host));
	host = this->my_host;
	new->set_my_host(new, host->clone(host));
	/* resolve hosts but use the old addresses above as fallback */
	resolve_hosts((private_ike_sa_t*)new);
	/* if we already have a virtual IP, we reuse it */
	enumerator = array_create_enumerator(this->my_vips);
	while (enumerator->enumerate(enumerator, &host))
	{
		new->add_virtual_ip(new, TRUE, host);
	}
	enumerator->destroy(enumerator);

#ifdef ME
	if (this->peer_cfg->is_mediation(this->peer_cfg))
	{
		status = new->initiate(new, NULL, 0, NULL, NULL);
	}
	else
#endif /* ME */
	{
		/* handle existing CHILD_SAs */
		enumerator = create_child_sa_enumerator(this);
		while (enumerator->enumerate(enumerator, (void**)&child_sa))
		{
			if (has_condition(this, COND_REAUTHENTICATING))
			{
				switch (child_sa->get_state(child_sa))
				{
					case CHILD_ROUTED:
					{	/* move routed child directly */
						remove_child_sa(this, enumerator);
						new->add_child_sa(new, child_sa);
						action = ACTION_NONE;
						break;
					}
					default:
					{	/* initiate/queue all other CHILD_SAs */
						action = ACTION_RESTART;
						break;
					}
				}
			}
			else
			{	/* only restart CHILD_SAs that are configured accordingly */
				if (this->state == IKE_DELETING)
				{
					action = child_sa->get_close_action(child_sa);
				}
				else
				{
					action = child_sa->get_dpd_action(child_sa);
				}
			}
			switch (action)
			{
				case ACTION_RESTART:
					child_cfg = child_sa->get_config(child_sa);
					DBG1(DBG_IKE, "restarting CHILD_SA %s",
						 child_cfg->get_name(child_cfg));
					child_cfg->get_ref(child_cfg);
					status = new->initiate(new, child_cfg,
									child_sa->get_reqid(child_sa), NULL, NULL);
					break;
				default:
					continue;
			}
			if (status == DESTROY_ME)
			{
				break;
			}
		}
		enumerator->destroy(enumerator);
		/* adopt any active or queued CHILD-creating tasks */
		if (status != DESTROY_ME)
		{
			task_manager_t *other_tasks = ((private_ike_sa_t*)new)->task_manager;
			other_tasks->adopt_child_tasks(other_tasks, this->task_manager);
			if (new->get_state(new) == IKE_CREATED)
			{
				status = new->initiate(new, NULL, 0, NULL, NULL);
			}
		}
	}

	if (status == DESTROY_ME)
	{
		charon->ike_sa_manager->checkin_and_destroy(charon->ike_sa_manager, new);
		status = FAILED;
	}
	else
	{
		charon->bus->ike_reestablish(charon->bus, &this->public, new);
		charon->ike_sa_manager->checkin(charon->ike_sa_manager, new);
		DBG2(DBG_IKE, "=== To set reauthenticating to avoid child_updown event");
		new->set_condition(new, COND_REAUTHENTICATING, TRUE);
		DBG2(DBG_IKE, "======== new->cond_reauth: %d", new->has_condition(new, COND_REAUTHENTICATING));
		status = SUCCESS;
	}
	charon->bus->set_sa(charon->bus, &this->public);
	return status;
}

METHOD(ike_sa_t, retransmit, status_t,
	private_ike_sa_t *this, u_int32_t message_id, bool oos, u_int32_t oos_seq_num)
{
	int status;

	if (this->state == IKE_PASSIVE)
	{
		return INVALID_STATE;
	}
	this->stats[STAT_OUTBOUND] = time_monotonic(NULL);
	if (this->task_manager->retransmit(this->task_manager, message_id, oos, oos_seq_num) != SUCCESS)
	{
		/* send a proper signal to brief interested bus listeners */
		switch (this->state)
		{
			case IKE_CONNECTING:
			{
				/* retry IKE_SA_INIT/Main Mode if we have multiple keyingtries */
				u_int32_t tries = this->peer_cfg->get_keyingtries(this->peer_cfg);
				charon->bus->alert(charon->bus, ALERT_PEER_INIT_UNREACHABLE,
								   this->keyingtry);
				this->keyingtry++;
				if (tries == 0 || tries > this->keyingtry)
				{
					DBG1(DBG_IKE, "peer not responding, trying again (%d/%d)",
						 this->keyingtry + 1, tries);
					reset(this);
					resolve_hosts(this);
					this->task_manager->queue_ike(this->task_manager);
					return this->task_manager->initiate(this->task_manager);
				}
				DBG1(DBG_IKE, "establishing IKE_SA failed, peer not responding");

				if (this->status_code)
				{
					notify_wod_attach_failed(get_name(this),
											this->status_code, this->sub_error_code, this->backoff_timer_value);
				}
				break;
			}
			case IKE_DELETING:
				DBG1(DBG_IKE, "proper IKE_SA delete failed, peer not responding");
				if (has_condition(this, COND_REAUTHENTICATING))
				{
					DBG1(DBG_IKE, "delete during reauthentication failed, "
						 "trying to reestablish IKE_SA anyway");
					reestablish(this);
				}
				break;
			case IKE_REKEYING:
				DBG1(DBG_IKE, "rekeying IKE_SA failed, peer not responding");
				/* FALL */
			default:
				reestablish(this);
				break;
		}
		if (this->state != IKE_CONNECTING)
		{
			charon->bus->ike_updown(charon->bus, &this->public, FALSE);
		}
		return DESTROY_ME;
	}
	return SUCCESS;
}

METHOD(ike_sa_t, set_auth_lifetime, status_t,
	private_ike_sa_t *this, u_int32_t lifetime)
{
	u_int32_t diff, hard, soft, now;
	bool send_update;

	diff = this->peer_cfg->get_over_time(this->peer_cfg);
	if (this->peer_cfg->get_reauth_time(this->peer_cfg, TRUE) == 0) {
		DBG1(DBG_IKE, "received AUTH_LIFETIME of %ds, but did not enable re-auth", lifetime);
		return SUCCESS;
	}

	diff = min(this->peer_cfg->get_over_time(this->peer_cfg), 180);
	now = time_monotonic(NULL);
	hard = now + lifetime;
	soft = hard - diff;

	/* check if we have to send an AUTH_LIFETIME to enforce the new lifetime.
	 * We send the notify in IKE_AUTH if not yet ESTABLISHED. */
	send_update = this->state == IKE_ESTABLISHED && this->version == IKEV2 &&
				  !has_condition(this, COND_ORIGINAL_INITIATOR) &&
				  (array_count(this->other_vips) != 0 ||
				  has_condition(this, COND_EAP_AUTHENTICATED));

	if (lifetime < diff)
	{
		this->stats[STAT_REAUTH] = now;

		if (!send_update)
		{
			DBG1(DBG_IKE, "received AUTH_LIFETIME of %ds, "
				 "starting reauthentication", lifetime);
			lib->processor->queue_job(lib->processor,
					(job_t*)rekey_ike_sa_job_create(this->ike_sa_id, TRUE));
		}
	}
	else if (this->stats[STAT_REAUTH] == 0 ||
			 this->stats[STAT_REAUTH] > soft)
	{
		this->stats[STAT_REAUTH] = soft;
		if (!send_update)
		{
			DBG1(DBG_IKE, "received AUTH_LIFETIME of %ds, scheduling "
				 "reauthentication in %ds", lifetime, lifetime - diff);
			lib->scheduler->schedule_job(lib->scheduler,
						(job_t*)rekey_ike_sa_job_create(this->ike_sa_id, TRUE),
						lifetime - diff);
		}
	}
	else
	{
		DBG1(DBG_IKE, "received AUTH_LIFETIME of %ds, "
			 "reauthentication already scheduled in %ds", lifetime,
			 this->stats[STAT_REAUTH] - time_monotonic(NULL));
		send_update = FALSE;
	}
	/* give at least some seconds to reauthenticate */
	this->stats[STAT_DELETE] = max(hard, now + 10);

#ifdef USE_IKEV2
	if (send_update)
	{
		ike_auth_lifetime_t *task;

		task = ike_auth_lifetime_create(&this->public, TRUE);
		this->task_manager->queue_task(this->task_manager, &task->task);
		return this->task_manager->initiate(this->task_manager);
	}
#endif
	return SUCCESS;
}

/**
 * Check if the current combination of source and destination address is still
 * valid.
 */
static bool is_current_path_valid(private_ike_sa_t *this)
{
	bool valid = FALSE;
	host_t *src;
	src = hydra->kernel_interface->get_source_addr(hydra->kernel_interface,
											this->other_host, this->my_host);
	if (src)
	{
		if (src->ip_equals(src, this->my_host))
		{
			valid = TRUE;
		}
		src->destroy(src);
	}
	return valid;
}

/**
 * Check if we have any path avialable for this IKE SA.
 */
static bool is_any_path_valid(private_ike_sa_t *this)
{
	bool valid = FALSE;
	enumerator_t *enumerator;
	host_t *src = NULL, *addr;
	int family = AF_UNSPEC;

	switch (charon->socket->supported_families(charon->socket))
	{
		case SOCKET_FAMILY_IPV4:
			family = AF_INET;
			break;
		case SOCKET_FAMILY_IPV6:
			family = AF_INET6;
			break;
		case SOCKET_FAMILY_BOTH:
		case SOCKET_FAMILY_NONE:
			break;
	}

	DBG1(DBG_IKE, "old path is not available anymore, try to find another");
	enumerator = create_peer_address_enumerator(this);
	while (enumerator->enumerate(enumerator, &addr))
	{
		if (family != AF_UNSPEC && addr->get_family(addr) != family)
		{
			continue;
		}
		DBG1(DBG_IKE, "looking for a route to %H ...", addr);
		src = hydra->kernel_interface->get_source_addr(
										hydra->kernel_interface, addr, NULL);
		if (src)
		{
			break;
		}
	}
	enumerator->destroy(enumerator);
	if (src)
	{
		valid = TRUE;
		src->destroy(src);
	}
	return valid;
}

METHOD(ike_sa_t, roam, status_t,
	private_ike_sa_t *this, bool address)
{
	switch (this->state)
	{
		case IKE_CREATED:
		case IKE_DELETING:
		case IKE_DESTROYING:
		case IKE_PASSIVE:
			return SUCCESS;
		default:
			break;
	}

	/* keep existing path if possible */
	if (is_current_path_valid(this))
	{
		DBG2(DBG_IKE, "keeping connection path %H - %H",
			 this->my_host, this->other_host);
		set_condition(this, COND_STALE, FALSE);

		if (supports_extension(this, EXT_MOBIKE) && address)
		{	/* if any addresses changed, send an updated list */
			DBG1(DBG_IKE, "sending address list update using MOBIKE");
			this->task_manager->queue_mobike(this->task_manager, FALSE, TRUE);
			return this->task_manager->initiate(this->task_manager);
		}

		if (address == true) {
			if (this->state == IKE_CONNECTING) {
                /* no reason to send DPD during connecting state */
                DBG1(DBG_IKE, "IKE is in connecting state. Skip DPD sending");
                return SUCCESS;
            }
            /* send DPD to check if connection is valid when interface/address updated */
			DBG1(DBG_IKE, "src_ip not changed - send DPD to check if connection is valid");
			this->task_manager->queue_dpd(this->task_manager);
			return this->task_manager->initiate(this->task_manager);
		} else {
			/* ignore roam event if there is no interface/address update and
			 * packet still go out through the same network interface*/
			DBG1(DBG_IKE, "Ignore roam event because src_ip and interface for IKEv2 message not changed");
			return SUCCESS;
		}
	}

	if (!is_any_path_valid(this))
	{
		u_int32_t oos_timeout = 0;

		DBG1(DBG_IKE, "no route found to reach %H, MOBIKE update deferred",
			 this->other_host);
		set_condition(this, COND_STALE, TRUE);

		oos_timeout = this->peer_cfg->get_oos_time(this->peer_cfg);
		if (!this->oos)
		{
			job_t *job;
			set_oos(this, OOS_START);
			DBG1(DBG_IKE, "============================= oos enabled (sn=%u): %ds", this->oos_seq_num, oos_timeout);
			update_keepalive_info(this, FALSE, FALSE);
			job = (job_t*)oos_timeout_job_create(this->ike_sa_id, this->oos_seq_num, FALSE);
#ifdef STRONGSWAN_UNIT_TEST
			lib->scheduler->ut_start_timer(lib->scheduler,job, oos_timeout*1000, UT_SIMULATE_OOS_TIMER_PROPERTY);
#else
			lib->scheduler->schedule_job(lib->scheduler, job, oos_timeout);
#endif
		}
		return SUCCESS;
	}
	set_condition(this, COND_STALE, FALSE);

	/* update addresses with mobike, if supported ... */
	/* MOBIKE, when the peer did not support in notification but really support,
	 * we may skip this check */
	if (supports_extension(this, EXT_MOBIKE))
	{
		if (!has_condition(this, COND_ORIGINAL_INITIATOR))
		{	/* responder updates the peer about changed address config */
			DBG1(DBG_IKE, "sending address list update using MOBIKE, "
				 "implicitly requesting an address change");
			address = TRUE;
		}
		else
		{
			DBG1(DBG_IKE, "requesting address change using MOBIKE");
		}
		this->task_manager->queue_mobike(this->task_manager, TRUE, address);
		return this->task_manager->initiate(this->task_manager);
	}

	/* ... reauth if not */
	if (!has_condition(this, COND_ORIGINAL_INITIATOR))
	{	/* responder does not reauthenticate */
		set_condition(this, COND_STALE, TRUE);
		return SUCCESS;
	}
	if (this->peer_cfg->do_addrchg_reauth(this->peer_cfg))
	{
		DBG1(DBG_IKE, "reauthenticating IKE_SA due to address change");
		/* since our previous path is not valid anymore, try and find a new one */
		resolve_hosts(this);
		return reauth(this);
	}
	/* send DPD to check if connection is valid */
	DBG1(DBG_IKE, "src_ip changed - send DPD to check if connection is valid");
	this->task_manager->queue_dpd(this->task_manager);
	return this->task_manager->initiate(this->task_manager);
}

METHOD(ike_sa_t, add_configuration_attribute, void,
	private_ike_sa_t *this, attribute_handler_t *handler,
	configuration_attribute_type_t type, chunk_t data)
{
	attribute_entry_t entry = {
		.handler = handler,
		.type = type,
		.data = chunk_clone(data),
	};
	array_insert(this->attributes, ARRAY_TAIL, &entry);
}

METHOD(ike_sa_t, create_task_enumerator, enumerator_t*,
	private_ike_sa_t *this, task_queue_t queue)
{
	return this->task_manager->create_task_enumerator(this->task_manager, queue);
}

METHOD(ike_sa_t, flush_queue, void,
	private_ike_sa_t *this, task_queue_t queue)
{
	this->task_manager->flush_queue(this->task_manager, queue);
}

METHOD(ike_sa_t, queue_task, void,
	private_ike_sa_t *this, task_t *task)
{
	this->task_manager->queue_task(this->task_manager, task);
}

METHOD(ike_sa_t, inherit, void,
	private_ike_sa_t *this, ike_sa_t *other_public)
{
	private_ike_sa_t *other = (private_ike_sa_t*)other_public;
	child_sa_t *child_sa;
	enumerator_t *enumerator;
	attribute_entry_t entry;
	auth_cfg_t *cfg;
	host_t *vip;

	/* apply hosts and ids */
	this->my_host->destroy(this->my_host);
	this->other_host->destroy(this->other_host);
	this->my_id->destroy(this->my_id);
	this->other_id->destroy(this->other_id);
	this->my_host = other->my_host->clone(other->my_host);
	this->other_host = other->other_host->clone(other->other_host);
	this->my_id = other->my_id->clone(other->my_id);
	this->other_id = other->other_id->clone(other->other_id);

	/* apply assigned virtual IPs... */
	while (array_remove(other->my_vips, ARRAY_HEAD, &vip))
	{
		array_insert_create(&this->my_vips, ARRAY_TAIL, vip);
	}
	while (array_remove(other->other_vips, ARRAY_HEAD, &vip))
	{
		array_insert_create(&this->other_vips, ARRAY_TAIL, vip);
	}

	/* MOBIKE additional addresses */
	while (array_remove(other->peer_addresses, ARRAY_HEAD, &vip))
	{
		array_insert_create(&this->peer_addresses, ARRAY_TAIL, vip);
	}

	/* authentication information */
	enumerator = array_create_enumerator(other->my_auths);
	while (enumerator->enumerate(enumerator, &cfg))
	{
		array_insert(this->my_auths, ARRAY_TAIL, cfg->clone(cfg));
	}
	enumerator->destroy(enumerator);
	enumerator = array_create_enumerator(other->other_auths);
	while (enumerator->enumerate(enumerator, &cfg))
	{
		array_insert(this->other_auths, ARRAY_TAIL, cfg->clone(cfg));
	}
	enumerator->destroy(enumerator);

	/* ... and configuration attributes */
	while (array_remove(other->attributes, ARRAY_HEAD, &entry))
	{
		array_insert(this->attributes, ARRAY_TAIL, &entry);
	}

	/* inherit all conditions */
	this->conditions = other->conditions;
	if (this->conditions & COND_NAT_HERE)
	{
		send_keepalive(this);
	}

#ifdef ME
	if (other->is_mediation_server)
	{
		act_as_mediation_server(this);
	}
	else if (other->server_reflexive_host)
	{
		this->server_reflexive_host = other->server_reflexive_host->clone(
				other->server_reflexive_host);
	}
#endif /* ME */

	/* adopt all children */
	while (array_remove(other->child_sas, ARRAY_HEAD, &child_sa))
	{
		add_child_sa(this, child_sa);
	}

	/* move pending tasks to the new IKE_SA */
	this->task_manager->adopt_tasks(this->task_manager, other->task_manager);

	/* reauthentication timeout survives a rekeying */
	if (other->stats[STAT_REAUTH])
	{
		time_t reauth, delete, now = time_monotonic(NULL);

		this->stats[STAT_REAUTH] = other->stats[STAT_REAUTH];
		reauth = this->stats[STAT_REAUTH] - now;
		delete = reauth + this->peer_cfg->get_over_time(this->peer_cfg);
		this->stats[STAT_DELETE] = this->stats[STAT_REAUTH] + delete;
		DBG1(DBG_IKE, "rescheduling reauthentication in %ds after rekeying, "
			 "lifetime reduced to %ds", reauth, delete);
		lib->scheduler->schedule_job(lib->scheduler,
				(job_t*)rekey_ike_sa_job_create(this->ike_sa_id, TRUE), reauth);
		lib->scheduler->schedule_job(lib->scheduler,
				(job_t*)delete_ike_sa_job_create(this->ike_sa_id, TRUE), delete);
	}
}

METHOD(ike_sa_t, destroy, void,
	private_ike_sa_t *this)
{
	attribute_entry_t entry;
	child_sa_t *child_sa;
	host_t *vip;

	charon->bus->set_sa(charon->bus, &this->public);

	set_state(this, IKE_DESTROYING);
	if (this->task_manager)
	{
		this->task_manager->flush(this->task_manager);
	}

	/* remove attributes first, as we pass the IKE_SA to the handler */
	while (array_remove(this->attributes, ARRAY_TAIL, &entry))
	{
		hydra->attributes->release(hydra->attributes, entry.handler,
								   this->other_id, entry.type, entry.data);
		free(entry.data.ptr);
	}
	/* uninstall CHILD_SAs before virtual IPs, otherwise we might kill
	 * routes that the CHILD_SA tries to uninstall. */
	while (array_remove(this->child_sas, ARRAY_TAIL, &child_sa))
	{
		child_sa->destroy(child_sa);
	}
	while (array_remove(this->my_vips, ARRAY_TAIL, &vip))
	{
		hydra->kernel_interface->del_ip(hydra->kernel_interface, vip, -1, TRUE);
		vip->destroy(vip);
	}
	if (array_count(this->other_vips))
	{
		charon->bus->assign_vips(charon->bus, &this->public, FALSE);
	}
	while (array_remove(this->other_vips, ARRAY_TAIL, &vip))
	{
		if (this->peer_cfg)
		{
			linked_list_t *pools;
			identification_t *id;

			id = get_other_eap_id(this);
			pools = linked_list_create_from_enumerator(
						this->peer_cfg->create_pool_enumerator(this->peer_cfg));
			hydra->attributes->release_address(hydra->attributes, pools, vip, id);
			pools->destroy(pools);
		}
		vip->destroy(vip);
	}
	while (array_remove(this->my_dnss, ARRAY_TAIL, &vip))
	{
		vip->destroy(vip);
	}
	while (array_remove(this->my_pcscfs, ARRAY_TAIL, &vip))
	{
		vip->destroy(vip);
	}
	while (array_remove(this->my_intsubnets, ARRAY_TAIL, &vip))
	{
		vip->destroy(vip);
	}
	while (array_remove(this->my_intnetmasks, ARRAY_TAIL, &vip))
	{
		vip->destroy(vip);
	}

	/* unset SA after here to avoid usage by the listeners */
	charon->bus->set_sa(charon->bus, NULL);

	array_destroy(this->child_sas);
	DESTROY_IF(this->task_manager);
	DESTROY_IF(this->keymat);
	array_destroy(this->attributes);
	array_destroy(this->my_vips);
	array_destroy(this->other_vips);
	array_destroy(this->my_dnss);
	array_destroy(this->my_pcscfs);
	array_destroy(this->my_intsubnets);
	array_destroy(this->my_intnetmasks);
	array_destroy_offset(this->peer_addresses, offsetof(host_t, destroy));
#ifdef ME
	if (this->is_mediation_server)
	{
		charon->mediation_manager->remove(charon->mediation_manager,
										  this->ike_sa_id);
	}
	DESTROY_IF(this->server_reflexive_host);
	chunk_free(&this->connect_id);
#endif /* ME */
	free(this->nat_detection_dest.ptr);

	DESTROY_IF(this->my_host);
	DESTROY_IF(this->other_host);
	DESTROY_IF(this->my_id);
	DESTROY_IF(this->other_id);
	DESTROY_IF(this->local_host);
	DESTROY_IF(this->remote_host);

	if (IS_MDMI_SUPPORTED())
	{
		chunk_t spi_i, spi_r;
		char spi_i_str[20], spi_r_str[20];
		char tmp[40];

		spi_i = chunk_alloca(sizeof(u_int64_t));
		spi_r = chunk_alloca(sizeof(u_int64_t));
		*((u_int64_t*)spi_i.ptr) = this->ike_sa_id->get_initiator_spi(this->ike_sa_id);
		*((u_int64_t*)spi_r.ptr) = this->ike_sa_id->get_responder_spi(this->ike_sa_id);
		chunk_to_hex(spi_i, spi_i_str, 1);
		chunk_to_hex(spi_r, spi_r_str, 1);
		snprintf(tmp, sizeof(tmp), "\"%s\",\"%s\"", spi_i_str, spi_r_str);
		mdmi_notify_wod(N_IKE_DECRYPT_DEL, get_name(this), (void *)tmp);
	}

	DESTROY_IF(this->ike_cfg);
	DESTROY_IF(this->peer_cfg);
	DESTROY_IF(this->proposal);
	this->my_auth->destroy(this->my_auth);
	this->other_auth->destroy(this->other_auth);
	array_destroy_offset(this->my_auths, offsetof(auth_cfg_t, destroy));
	array_destroy_offset(this->other_auths, offsetof(auth_cfg_t, destroy));

	this->ike_sa_id->destroy(this->ike_sa_id);
	free(this);
}

/*
 * Described in header.
 */
ike_sa_t * ike_sa_create(ike_sa_id_t *ike_sa_id, bool initiator,
						 ike_version_t version)
{
	private_ike_sa_t *this;
	static refcount_t unique_id = 0;

	if (version == IKE_ANY)
	{	/* prefer IKEv2 if protocol not specified */
#ifdef USE_IKEV2
		version = IKEV2;
#else
		version = IKEV1;
#endif
	}

	INIT(this,
		.public = {
			.get_version = _get_version,
			.get_state = _get_state,
			.set_state = _set_state,
			.get_name = _get_name,
			.get_statistic = _get_statistic,
			.set_statistic = _set_statistic,
			.process_message = _process_message,
			.initiate = _initiate,
			.retry_initiate = _retry_initiate,
			.get_ike_cfg = _get_ike_cfg,
			.set_ike_cfg = _set_ike_cfg,
			.get_peer_cfg = _get_peer_cfg,
			.set_peer_cfg = _set_peer_cfg,
			.get_auth_cfg = _get_auth_cfg,
			.create_auth_cfg_enumerator = _create_auth_cfg_enumerator,
			.verify_peer_certificate = _verify_peer_certificate,
			.add_auth_cfg = _add_auth_cfg,
			.get_proposal = _get_proposal,
			.set_proposal = _set_proposal,
			.get_id = _get_id,
			.get_my_host = _get_my_host,
			.set_my_host = _set_my_host,
			.get_other_host = _get_other_host,
			.set_other_host = _set_other_host,
			.set_message_id = _set_message_id,
			.float_ports = _float_ports,
			.update_hosts = _update_hosts,
			.get_my_id = _get_my_id,
			.set_my_id = _set_my_id,
			.get_other_id = _get_other_id,
			.set_other_id = _set_other_id,
			.get_other_eap_id = _get_other_eap_id,
			.enable_extension = _enable_extension,
			.supports_extension = _supports_extension,
			.set_condition = _set_condition,
			.has_condition = _has_condition,
			.set_pending_updates = _set_pending_updates,
			.get_pending_updates = _get_pending_updates,
			.create_peer_address_enumerator = _create_peer_address_enumerator,
			.add_peer_address = _add_peer_address,
			.clear_peer_addresses = _clear_peer_addresses,
			.has_mapping_changed = _has_mapping_changed,
			.set_wdrv_keepalive = _set_wdrv_keepalive,
			.set_keepalive_job_state = _set_keepalive_job_state,
			.get_oos = _get_oos,
			.set_oos = _set_oos,
			.get_oos_seq_num = _get_oos_seq_num,
			.get_status_code = _get_status_code,
			.set_status_code = _set_status_code,
			.set_backoff_timer = _set_backoff_timer,
			.set_pcscf_need_update = _set_pcscf_need_update,
			.set_notify_device_identity = _set_notify_device_identity,
			.get_sub_error_code = _get_sub_error_code,
			.get_backoff_timer = _get_backoff_timer,
			.get_pcscf_need_update = _get_pcscf_need_update,
			.get_notify_device_identity = _get_notify_device_identity,
			.set_sub_error_code = _set_sub_error_code,
			.retransmit = _retransmit,
			.delete = _delete_,
			.destroy = _destroy,
			.send_dpd = _send_dpd,
			.update_keepalive_info = _update_keepalive_info,
			.send_keepalive = _send_keepalive,
			.get_keymat = _get_keymat,
			.add_child_sa = _add_child_sa,
			.get_child_sa = _get_child_sa,
			.get_child_count = _get_child_count,
			.create_child_sa_enumerator = _create_child_sa_enumerator,
			.remove_child_sa = _remove_child_sa,
			.rekey_child_sa = _rekey_child_sa,
			.delete_child_sa = _delete_child_sa,
			.destroy_child_sa = _destroy_child_sa,
			.rekey = _rekey,
			.reauth = _reauth,
			.reestablish = _reestablish,
			.set_auth_lifetime = _set_auth_lifetime,
			.roam = _roam,
			.inherit = _inherit,
			.generate_message = _generate_message,
			.generate_message_fragmented = _generate_message_fragmented,
			.reset = _reset,
			.get_unique_id = _get_unique_id,
			.add_virtual_ip = _add_virtual_ip,
			.get_virtual_ips = _get_virtual_ips,
			.clear_virtual_ips = _clear_virtual_ips,
			.clear_virtual_ips2 = _clear_virtual_ips2,
			.create_virtual_ip_enumerator = _create_virtual_ip_enumerator,
			.add_pcscf = _add_pcscf,
			.clear_pcscfs = _clear_pcscfs,
			.create_pcscf_enumerator = _create_pcscf_enumerator,
			.add_intnetmask = _add_intnetmask,
			.clear_intnetmasks = _clear_intnetmasks,
			.create_intnetmask_enumerator = _create_intnetmask_enumerator,
			.add_intsubnet = _add_intsubnet,
			.clear_intsubnets = _clear_intsubnets,
			.create_intsubnet_enumerator = _create_intsubnet_enumerator,
			.add_dns = _add_dns,
			.clear_dnss = _clear_dnss,
			.create_dns_enumerator = _create_dns_enumerator,
			.add_configuration_attribute = _add_configuration_attribute,
			.set_kmaddress = _set_kmaddress,
			.create_task_enumerator = _create_task_enumerator,
			.flush_queue = _flush_queue,
			.queue_task = _queue_task,
#ifdef ME
			.act_as_mediation_server = _act_as_mediation_server,
			.get_server_reflexive_host = _get_server_reflexive_host,
			.set_server_reflexive_host = _set_server_reflexive_host,
			.get_connect_id = _get_connect_id,
			.initiate_mediation = _initiate_mediation,
			.initiate_mediated = _initiate_mediated,
			.relay = _relay,
			.callback = _callback,
			.respond = _respond,
#endif /* ME */
		},
		.ike_sa_id = ike_sa_id->clone(ike_sa_id),
		.version = version,
		.my_host = host_create_any(AF_INET),
		.other_host = host_create_any(AF_INET),
		.my_id = identification_create_from_encoding(ID_ANY, chunk_empty),
		.other_id = identification_create_from_encoding(ID_ANY, chunk_empty),
		.keymat = keymat_create(version, initiator),
		.state = IKE_CREATED,
		.stats[STAT_INBOUND] = time_monotonic(NULL),
		.stats[STAT_OUTBOUND] = time_monotonic(NULL),
		.my_auth = auth_cfg_create(),
		.other_auth = auth_cfg_create(),
		.my_auths = array_create(0, 0),
		.other_auths = array_create(0, 0),
		.attributes = array_create(sizeof(attribute_entry_t), 0),
		.unique_id = ref_get(&unique_id),
		.keepalive_interval = lib->settings->get_time(lib->settings,
								"%s.keep_alive", KEEPALIVE_INTERVAL, lib->ns),
		.retry_initiate_interval = lib->settings->get_time(lib->settings,
								"%s.retry_initiate_interval", 0, lib->ns),
		.flush_auth_cfg = lib->settings->get_bool(lib->settings,
								"%s.flush_auth_cfg", FALSE, lib->ns),
		.fragment_size = lib->settings->get_int(lib->settings,
								"%s.fragment_size", 0, lib->ns),
		.read_cfg = FALSE,
		.keepalive = FALSE,
		.wdrv_keepalive = FALSE,
		.keepalive_ntfy_status = FALSE,
		.keepalive_job_state = KEEPALIVE_JOB_NONE,
		.oos = FALSE,
		.oos_seq_num = 0,
		.oos_start_time = 0,
		.status_code = 0,
		.sub_error_code = 0,
		.backoff_timer_value = 0,
		.pcscf_need_update = FALSE,
		.notify_device_identity = FALSE,
	);

	if (version == IKEV2)
	{	/* always supported with IKEv2 */
		enable_extension(this, EXT_DPD);
	}

	this->task_manager = task_manager_create(&this->public);
	this->my_host->set_port(this->my_host,
							charon->socket->get_port(charon->socket, FALSE));

	if (!this->task_manager || !this->keymat)
	{
		DBG1(DBG_IKE, "IKE version %d not supported", this->version);
		destroy(this);
		return NULL;
	}
	return &this->public;
}
