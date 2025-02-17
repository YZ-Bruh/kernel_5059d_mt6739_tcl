/*
 * Copyright (C) 2012 Tobias Brunner
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

#include "ike_auth.h"

#include <string.h>

#include <daemon.h>
#include <encoding/payloads/id_payload.h>
#include <encoding/payloads/auth_payload.h>
#include <encoding/payloads/eap_payload.h>
#include <encoding/payloads/nonce_payload.h>
#include <encoding/payloads/notify_payload.h>
#include <encoding/payloads/cert_payload.h>
#include <sa/ikev2/authenticators/eap_authenticator.h>
#include <utils/identification.h>

#include "ike_query_device_identity.h"



typedef struct private_ike_auth_t private_ike_auth_t;

/**
 * Private members of a ike_auth_t task.
 */
struct private_ike_auth_t {

	/**
	 * Public methods and task_t interface.
	 */
	ike_auth_t public;

	/**
	 * Assigned IKE_SA.
	 */
	ike_sa_t *ike_sa;

	/**
	 * Are we the initiator?
	 */
	bool initiator;

	/**
	 * Nonce chosen by us in ike_init
	 */
	chunk_t my_nonce;

	/**
	 * Nonce chosen by peer in ike_init
	 */
	chunk_t other_nonce;

	/**
	 * IKE_SA_INIT message sent by us
	 */
	packet_t *my_packet;

	/**
	 * IKE_SA_INIT message sent by peer
	 */
	packet_t *other_packet;

	/**
	 * Reserved bytes of ID payload
	 */
	char reserved[3];

	/**
	 * currently active authenticator, to authenticate us
	 */
	authenticator_t *my_auth;

	/**
	 * currently active authenticator, to authenticate peer
	 */
	authenticator_t *other_auth;

	/**
	 * peer_cfg candidates, ordered by priority
	 */
	linked_list_t *candidates;

	/**
	 * selected peer config (might change when using multiple authentications)
	 */
	peer_cfg_t *peer_cfg;

	/**
	 * have we planned an(other) authentication exchange?
	 */
	bool do_another_auth;

	/**
	 * has the peer announced another authentication exchange?
	 */
	bool expect_another_auth;

	/**
	 * should we send a AUTHENTICATION_FAILED notify?
	 */
	bool authentication_failed;

	/**
	 * received an INITIAL_CONTACT?
	 */
	bool initial_contact;
};

/**
 * check if multiple authentication extension is enabled, configuration-wise
 */
static bool multiple_auth_enabled()
{
	return lib->settings->get_bool(lib->settings,
								   "%s.multiple_authentication", TRUE, lib->ns);
}

/**
 * collect the needed information in the IKE_SA_INIT exchange from our message
 */
static status_t collect_my_init_data(private_ike_auth_t *this,
									 message_t *message)
{
	nonce_payload_t *nonce;

	/* get the nonce that was generated in ike_init */
	nonce = (nonce_payload_t*)message->get_payload(message, NONCE);
	if (nonce == NULL)
	{
		return FAILED;
	}
	this->my_nonce = nonce->get_nonce(nonce);

	/* pre-generate the message, keep a copy */
	if (this->ike_sa->generate_message(this->ike_sa, message,
									   &this->my_packet) != SUCCESS)
	{
		return FAILED;
	}
	return NEED_MORE;
}

/**
 * collect the needed information in the IKE_SA_INIT exchange from others message
 */
static status_t collect_other_init_data(private_ike_auth_t *this,
										message_t *message)
{
	/* we collect the needed information in the IKE_SA_INIT exchange */
	nonce_payload_t *nonce;

	/* get the nonce that was generated in ike_init */
	nonce = (nonce_payload_t*)message->get_payload(message, NONCE);
	if (nonce == NULL)
	{
		return FAILED;
	}
	this->other_nonce = nonce->get_nonce(nonce);

	/* keep a copy of the received packet */
	this->other_packet = message->get_packet(message);
	return NEED_MORE;
}

/**
 * Get and store reserved bytes of id_payload, required for AUTH payload
 */
static void get_reserved_id_bytes(private_ike_auth_t *this, id_payload_t *id)
{
	u_int8_t *byte;
	int i;

	for (i = 0; i < countof(this->reserved); i++)
	{
		byte = payload_get_field(&id->payload_interface, RESERVED_BYTE, i);
		if (byte)
		{
			this->reserved[i] = *byte;
		}
	}
}

/**
 * Get the next authentication configuration
 */
static auth_cfg_t *get_auth_cfg(private_ike_auth_t *this, bool local)
{
	enumerator_t *e1, *e2;
	auth_cfg_t *c1, *c2, *next = NULL;
	peer_cfg_t *config = this->ike_sa->get_peer_cfg(this->ike_sa);
	bool skip_check_id = config->skip_check_id(config);
	bool server_nocert = config->server_nocert(config);

	/* find an available config not already done */
	e1 = this->peer_cfg->create_auth_cfg_enumerator(this->peer_cfg, local);
	while (e1->enumerate(e1, &c1))
	{
		bool found = FALSE;

		e2 = this->ike_sa->create_auth_cfg_enumerator(this->ike_sa, local);
		while (e2->enumerate(e2, &c2))
		{
			if (c2->complies_v2(c2, c1, FALSE,
								skip_check_id, server_nocert))
			{
				found = TRUE;
				break;
			}
		}
		e2->destroy(e2);
		if (!found)
		{
			next = c1;
			break;
		}
	}
	e1->destroy(e1);
	return next;
}

/**
 * Move the currently active auth config to the auth configs completed
 */
static void apply_auth_cfg(private_ike_auth_t *this, bool local)
{
	auth_cfg_t *cfg;

	cfg = auth_cfg_create();
	cfg->merge(cfg, this->ike_sa->get_auth_cfg(this->ike_sa, local), local);
	this->ike_sa->add_auth_cfg(this->ike_sa, local, cfg);
}

/**
 * Check if we have should initiate another authentication round
 */
static bool do_another_auth(private_ike_auth_t *this)
{
	bool do_another = FALSE;
	enumerator_t *done, *todo;
	auth_cfg_t *done_cfg, *todo_cfg;
	peer_cfg_t *config = this->ike_sa->get_peer_cfg(this->ike_sa);
	bool skip_check_id = config->skip_check_id(config);
	bool server_nocert = config->server_nocert(config);

	if (!this->ike_sa->supports_extension(this->ike_sa, EXT_MULTIPLE_AUTH))
	{
		return FALSE;
	}

	done = this->ike_sa->create_auth_cfg_enumerator(this->ike_sa, TRUE);
	todo = this->peer_cfg->create_auth_cfg_enumerator(this->peer_cfg, TRUE);
	while (todo->enumerate(todo, &todo_cfg))
	{
		if (!done->enumerate(done, &done_cfg))
		{
			done_cfg = this->ike_sa->get_auth_cfg(this->ike_sa, TRUE);
		}
		if (!done_cfg->complies_v2(done_cfg, todo_cfg, FALSE,
								skip_check_id, server_nocert))
		{
			do_another = TRUE;
			break;
		}
	}
	done->destroy(done);
	todo->destroy(todo);
	return do_another;
}

/**
 * Get peer configuration candidates from backends
 */
static bool load_cfg_candidates(private_ike_auth_t *this)
{
	enumerator_t *enumerator;
	peer_cfg_t *peer_cfg;
	host_t *me, *other;
	identification_t *my_id, *other_id;

	me = this->ike_sa->get_my_host(this->ike_sa);
	other = this->ike_sa->get_other_host(this->ike_sa);
	my_id = this->ike_sa->get_my_id(this->ike_sa);
	other_id = this->ike_sa->get_other_id(this->ike_sa);

	DBG1(DBG_CFG, "looking for peer configs matching %H[%Y]...%H[%Y]",
		 me, my_id, other, other_id);
	enumerator = charon->backends->create_peer_cfg_enumerator(charon->backends,
											me, other, my_id, other_id, IKEV2);
	while (enumerator->enumerate(enumerator, &peer_cfg))
	{
		peer_cfg->get_ref(peer_cfg);
		if (this->peer_cfg == NULL)
		{	/* best match */
			this->peer_cfg = peer_cfg;
			this->ike_sa->set_peer_cfg(this->ike_sa, peer_cfg);
		}
		else
		{
			this->candidates->insert_last(this->candidates, peer_cfg);
		}
	}
	enumerator->destroy(enumerator);
	if (this->peer_cfg)
	{
		DBG1(DBG_CFG, "selected peer config '%s'",
			 this->peer_cfg->get_name(this->peer_cfg));
		return TRUE;
	}
	DBG1(DBG_CFG, "no matching peer config found");
	return FALSE;
}

/**
 * update the current peer candidate if necessary, using candidates
 */
static bool update_cfg_candidates(private_ike_auth_t *this, bool strict)
{
	do
	{
		if (this->peer_cfg)
		{
			char *comply_error = NULL;
			enumerator_t *e1, *e2, *tmp;
			auth_cfg_t *c1, *c2;
			peer_cfg_t *config = this->ike_sa->get_peer_cfg(this->ike_sa);
			bool skip_check_id = config->skip_check_id(config);
			bool server_nocert = config->server_nocert(config);

			e1 = this->ike_sa->create_auth_cfg_enumerator(this->ike_sa, FALSE);
			e2 = this->peer_cfg->create_auth_cfg_enumerator(this->peer_cfg, FALSE);

			if (strict)
			{	/* swap lists in strict mode: all configured rounds must be
				 * fulfilled. If !strict, we check only the rounds done so far. */
				tmp = e1;
				e1 = e2;
				e2 = tmp;
			}
			while (e1->enumerate(e1, &c1))
			{
				/* check if done authentications comply to configured ones */
				if (!e2->enumerate(e2, &c2))
				{
					comply_error = "insufficient authentication rounds";
					break;
				}
				if (!strict && !c1->complies_v2(c1, c2, TRUE,
												skip_check_id, server_nocert))
				{
					comply_error = "non-matching authentication done";
					break;
				}
				if (strict && !c2->complies_v2(c2, c1, TRUE,
												skip_check_id, server_nocert))
				{
					comply_error = "constraint checking failed";
					break;
				}
			}
			e1->destroy(e1);
			e2->destroy(e2);
			if (!comply_error)
			{
				break;
			}
			DBG1(DBG_CFG, "selected peer config '%s' inacceptable: %s",
				 this->peer_cfg->get_name(this->peer_cfg), comply_error);
			this->peer_cfg->destroy(this->peer_cfg);
		}
		if (this->candidates->remove_first(this->candidates,
										(void**)&this->peer_cfg) != SUCCESS)
		{
			DBG1(DBG_CFG, "no alternative config found");
			this->peer_cfg = NULL;
		}
		else
		{
			DBG1(DBG_CFG, "switching to peer config '%s'",
				 this->peer_cfg->get_name(this->peer_cfg));
			this->ike_sa->set_peer_cfg(this->ike_sa, this->peer_cfg);
		}
	}
	while (this->peer_cfg);

	return this->peer_cfg != NULL;
}

METHOD(task_t, build_i, status_t,
	private_ike_auth_t *this, message_t *message)
{
	auth_cfg_t *cfg;

	if (message->get_exchange_type(message) == IKE_SA_INIT)
	{
		return collect_my_init_data(this, message);
	}

	if (this->peer_cfg == NULL)
	{
		this->peer_cfg = this->ike_sa->get_peer_cfg(this->ike_sa);
		this->peer_cfg->get_ref(this->peer_cfg);
	}

	if (message->get_message_id(message) == 1)
	{	/* in the first IKE_AUTH ... */
		if (this->ike_sa->supports_extension(this->ike_sa, EXT_MULTIPLE_AUTH))
		{	/* indicate support for multiple authentication */
			message->add_notify(message, FALSE, MULTIPLE_AUTH_SUPPORTED,
								chunk_empty);
		}
		/* indicate support for EAP-only authentication */
		if (this->peer_cfg->send_eap_only(this->peer_cfg))
		{
			message->add_notify(message, FALSE, EAP_ONLY_AUTHENTICATION,
								chunk_empty);
		}
		/* indicate support for pcscf restore feature */
		if (this->peer_cfg->get_pcscf_restore(this->peer_cfg))
		{
			message->add_notify(message, FALSE, PCSCF_RESELECTION_SUPPORT,
								chunk_empty);
		}
	}

	if (!this->do_another_auth && !this->my_auth)
	{	/* we have done our rounds */
		return NEED_MORE;
	}

	/* check if an authenticator is in progress */
	if (this->my_auth == NULL)
	{
		identification_t *idi, *idr = NULL;
		id_payload_t *id_payload;

		/* clean up authentication config from a previous round */
		cfg = this->ike_sa->get_auth_cfg(this->ike_sa, TRUE);
		cfg->purge(cfg, TRUE);

		/* add (optional) IDr */
		cfg = get_auth_cfg(this, FALSE);
		if (cfg)
		{
			///////////////////////////////////////////////////////////////////////
			//// modify by Yuhhua Hu to use eap_identity in IDr for TS 33.402  ////
			///////////////////////////////////////////////////////////////////////
			idr = cfg->get(cfg, AUTH_RULE_IDENTITY_APN);
			if(this->peer_cfg->get_is_emergency(this->peer_cfg))
			{
				DBG1(DBG_CFG, "enable EMERGENCY indication");
				id_payload = id_payload_create_emergency_identification(ID_RESPONDER, ID_FQDN);
				message->add_payload(message, (payload_t*)id_payload);
			}
			else if (idr)
			{
				this->ike_sa->set_other_id(this->ike_sa, idr->clone(idr));
				id_payload = id_payload_create_from_identification(ID_RESPONDER, idr);
				message->add_payload(message, (payload_t*)id_payload);
			}
			else
			{
				idr = cfg->get(cfg, AUTH_RULE_IDENTITY);
				if (!cfg->get(cfg, AUTH_RULE_IDENTITY_LOOSE) && idr &&
					!idr->contains_wildcards(idr))
				{
					this->ike_sa->set_other_id(this->ike_sa, idr->clone(idr));
					id_payload = id_payload_create_from_identification(ID_RESPONDER, idr);
					message->add_payload(message, (payload_t*)id_payload);
				}
			}
		}

		/* add IDi */
		cfg = this->ike_sa->get_auth_cfg(this->ike_sa, TRUE);
		cfg->merge(cfg, get_auth_cfg(this, TRUE), TRUE);
		idi = cfg->get(cfg, AUTH_RULE_IDENTITY);
		if (!idi || idi->get_type(idi) == ID_ANY)
		{	/* ID_ANY is invalid as IDi, use local IP address instead */
			host_t *me;

			DBG1(DBG_CFG, "no IDi configured, fall back on IP address");
			me = this->ike_sa->get_my_host(this->ike_sa);
			idi = identification_create_from_sockaddr(me->get_sockaddr(me));
			cfg->add(cfg, AUTH_RULE_IDENTITY, idi);
		}
		this->ike_sa->set_my_id(this->ike_sa, idi->clone(idi));
		id_payload = id_payload_create_from_identification(ID_INITIATOR, idi);
		get_reserved_id_bytes(this, id_payload);
		message->add_payload(message, (payload_t*)id_payload);

		if (idr && message->get_message_id(message) == 1 &&
			this->peer_cfg->get_unique_policy(this->peer_cfg) != UNIQUE_NO &&
			this->peer_cfg->get_unique_policy(this->peer_cfg) != UNIQUE_NEVER)
		{
			host_t *host;
			identification_t *id_host = NULL;
			char peer_addr[128] = {0};

			host = this->ike_sa->get_other_host(this->ike_sa);
			snprintf(peer_addr, 128, "%H", host);
			id_host = identification_create_from_string(peer_addr);

			DBG1(DBG_CFG, "====== idi: %Y, idr: %Y, host: %Y ========", idi, idr, id_host);
			if (this->peer_cfg->send_init_contact(this->peer_cfg) &&
				!charon->ike_sa_manager->has_contact(charon->ike_sa_manager,
							idi, id_host, host->get_family(host)))
			{
				message->add_notify(message, FALSE, INITIAL_CONTACT, chunk_empty);
			}
		}

		//To test
		//message->add_notify(message, FALSE, NON_FIRST_FRAGMENTS_ALSO, chunk_empty);

		/* build authentication data */
		this->my_auth = authenticator_create_builder(this->ike_sa, cfg,
							this->other_nonce, this->my_nonce,
							this->other_packet->get_data(this->other_packet),
							this->my_packet->get_data(this->my_packet),
							this->reserved);
		if (!this->my_auth)
		{
			charon->bus->alert(charon->bus, ALERT_LOCAL_AUTH_FAILED);
			return FAILED;
		}
	}
	switch (this->my_auth->build(this->my_auth, message))
	{
		case SUCCESS:
			apply_auth_cfg(this, TRUE);
			this->my_auth->destroy(this->my_auth);
			this->my_auth = NULL;
			break;
		case NEED_MORE:
			break;
		default:
			charon->bus->alert(charon->bus, ALERT_LOCAL_AUTH_FAILED);
			return FAILED;
	}

	/* check for additional authentication rounds */
	if (do_another_auth(this))
	{
		if (message->get_payload(message, AUTHENTICATION))
		{
			message->add_notify(message, FALSE, ANOTHER_AUTH_FOLLOWS, chunk_empty);
		}
	}
	else
	{
		this->do_another_auth = FALSE;
	}

	/* if need notify device_identity, add device_identity notify payload for IKE_AUT request*/
	if (this->ike_sa->get_notify_device_identity(this->ike_sa))
	{
		/* TODO: add device_identity notify payload */
		const char *imei = this->peer_cfg->get_imei(this->peer_cfg);
		size_t len = imei ? strlen(imei) : 0;;
		device_identity_payload_t device_identity_data;
		notify_payload_t *device_identity_notify;
		chunk_t data;
		if (len == 15 || len == 16)
		{
			size_t i;
			device_identity_data.identity_type = (len == 15) ? 0x1 : 0x2;
			for (i = 0; i < len; i += 2)
			{
				if (i == 14 && len == 15)
				{
					device_identity_data.identity_value[i / 2] = (imei[i] - 48) + (0xF << 4);
				}
				else
				{
					device_identity_data.identity_value[i / 2] = (imei[i] - 48) + ((imei[i+1] -48) << 4);
				}
			}
			device_identity_data.length[0] = 0;
			device_identity_data.length[1] = 9;
			data = chunk_create((u_char *) &device_identity_data, device_identity_data.length[1] + 2);
			device_identity_notify = notify_payload_create_from_protocol_and_type(NOTIFY, PROTO_NONE, DEVICE_IDENTITY);
			device_identity_notify->set_notification_data(device_identity_notify, data);
			message->add_payload(message, (payload_t*)device_identity_notify);
			DBG0(DBG_IKE, "add DEVICE_IDENTITY payload");
		}
		else
		{
			DBG0(DBG_IKE, "add DEVICE_IDENTITY payload failed: missing IMEI");
		}
		this->ike_sa->set_notify_device_identity(this->ike_sa, FALSE);
	}

	return NEED_MORE;
}

METHOD(task_t, process_r, status_t,
	private_ike_auth_t *this, message_t *message)
{
	auth_cfg_t *cfg, *cand;
	id_payload_t *id_payload;
	identification_t *id;

	if (message->get_exchange_type(message) == IKE_SA_INIT)
	{
		return collect_other_init_data(this, message);
	}

	if (this->my_auth == NULL && this->do_another_auth)
	{
		/* handle (optional) IDr payload, apply proposed identity */
		id_payload = (id_payload_t*)message->get_payload(message, ID_RESPONDER);
		if (id_payload)
		{
			id = id_payload->get_identification(id_payload);
		}
		else
		{
			id = identification_create_from_encoding(ID_ANY, chunk_empty);
		}
		this->ike_sa->set_my_id(this->ike_sa, id);
	}

	if (!this->expect_another_auth)
	{
		return NEED_MORE;
	}

	if (message->get_message_id(message) == 1)
	{	/* check for extensions in the first IKE_AUTH */
		if (message->get_notify(message, MULTIPLE_AUTH_SUPPORTED))
		{
			this->ike_sa->enable_extension(this->ike_sa, EXT_MULTIPLE_AUTH);
		}
		if (message->get_notify(message, EAP_ONLY_AUTHENTICATION))
		{
			this->ike_sa->enable_extension(this->ike_sa,
										   EXT_EAP_ONLY_AUTHENTICATION);
		}
	}

	if (this->other_auth == NULL)
	{
		/* handle IDi payload */
		id_payload = (id_payload_t*)message->get_payload(message, ID_INITIATOR);
		if (!id_payload)
		{
			DBG1(DBG_IKE, "IDi payload missing");
			return FAILED;
		}
		id = id_payload->get_identification(id_payload);
		get_reserved_id_bytes(this, id_payload);
		this->ike_sa->set_other_id(this->ike_sa, id);
		cfg = this->ike_sa->get_auth_cfg(this->ike_sa, FALSE);
		cfg->add(cfg, AUTH_RULE_IDENTITY, id->clone(id));

		if (this->peer_cfg == NULL)
		{
			if (!load_cfg_candidates(this))
			{
				this->authentication_failed = TRUE;
				return NEED_MORE;
			}
		}
		if (message->get_payload(message, AUTHENTICATION) == NULL)
		{	/* before authenticating with EAP, we need a EAP config */
			cand = get_auth_cfg(this, FALSE);
			while (!cand || (
					(uintptr_t)cand->get(cand, AUTH_RULE_EAP_TYPE) == EAP_NAK &&
					(uintptr_t)cand->get(cand, AUTH_RULE_EAP_VENDOR) == 0))
			{	/* peer requested EAP, but current config does not match */
				DBG1(DBG_IKE, "peer requested EAP, config inacceptable");
				this->peer_cfg->destroy(this->peer_cfg);
				this->peer_cfg = NULL;
				if (!update_cfg_candidates(this, FALSE))
				{
					this->authentication_failed = TRUE;
					return NEED_MORE;
				}
				cand = get_auth_cfg(this, FALSE);
			}
			/* copy over the EAP specific rules for authentication */
			cfg->add(cfg, AUTH_RULE_EAP_TYPE,
					 cand->get(cand, AUTH_RULE_EAP_TYPE));
			cfg->add(cfg, AUTH_RULE_EAP_VENDOR,
					 cand->get(cand, AUTH_RULE_EAP_VENDOR));
			id = (identification_t*)cand->get(cand, AUTH_RULE_EAP_IDENTITY);
			if (id)
			{
				cfg->add(cfg, AUTH_RULE_EAP_IDENTITY, id->clone(id));
			}
			id = (identification_t*)cand->get(cand, AUTH_RULE_AAA_IDENTITY);
			if (id)
			{
				cfg->add(cfg, AUTH_RULE_AAA_IDENTITY, id->clone(id));
			}
		}

		/* verify authentication data */
		this->other_auth = authenticator_create_verifier(this->ike_sa,
							message, this->other_nonce, this->my_nonce,
							this->other_packet->get_data(this->other_packet),
							this->my_packet->get_data(this->my_packet),
							this->reserved);
		if (!this->other_auth)
		{
			this->authentication_failed = TRUE;
			return NEED_MORE;
		}
	}
	switch (this->other_auth->process(this->other_auth, message))
	{
		case SUCCESS:
			this->other_auth->destroy(this->other_auth);
			this->other_auth = NULL;
			break;
		case NEED_MORE:
			if (message->get_payload(message, AUTHENTICATION))
			{	/* AUTH verification successful, but another build() needed */
				break;
			}
			return NEED_MORE;
		default:
			this->authentication_failed = TRUE;
			return NEED_MORE;
	}

	/* If authenticated (with non-EAP) and received INITIAL_CONTACT,
	 * delete any existing IKE_SAs with that peer. */
	if (message->get_message_id(message) == 1 &&
		message->get_notify(message, INITIAL_CONTACT))
	{
		this->initial_contact = TRUE;
	}

	/* another auth round done, invoke authorize hook */
	if (!charon->bus->authorize(charon->bus, FALSE))
	{
		DBG1(DBG_IKE, "authorization hook forbids IKE_SA, cancelling");
		this->authentication_failed = TRUE;
		return NEED_MORE;
	}

	apply_auth_cfg(this, FALSE);

	if (!update_cfg_candidates(this, FALSE))
	{
		this->authentication_failed = TRUE;
		return NEED_MORE;
	}

	if (message->get_notify(message, ANOTHER_AUTH_FOLLOWS) == NULL)
	{
		this->expect_another_auth = FALSE;
		if (!update_cfg_candidates(this, TRUE))
		{
			this->authentication_failed = TRUE;
			return NEED_MORE;
		}
	}
	return NEED_MORE;
}

METHOD(task_t, build_r, status_t,
	private_ike_auth_t *this, message_t *message)
{
	auth_cfg_t *cfg;

	if (message->get_exchange_type(message) == IKE_SA_INIT)
	{
		if (multiple_auth_enabled())
		{
			message->add_notify(message, FALSE, MULTIPLE_AUTH_SUPPORTED,
								chunk_empty);
		}
		return collect_my_init_data(this, message);
	}

	if (this->authentication_failed || this->peer_cfg == NULL)
	{
		goto peer_auth_failed;
	}

	if (this->my_auth == NULL && this->do_another_auth)
	{
		identification_t *id, *id_cfg;
		id_payload_t *id_payload;

		/* add IDr */
		cfg = this->ike_sa->get_auth_cfg(this->ike_sa, TRUE);
		cfg->purge(cfg, TRUE);
		cfg->merge(cfg, get_auth_cfg(this, TRUE), TRUE);

		id_cfg = cfg->get(cfg, AUTH_RULE_IDENTITY);
		id = this->ike_sa->get_my_id(this->ike_sa);
		if (id->get_type(id) == ID_ANY)
		{	/* no IDr received, apply configured ID */
			if (!id_cfg || id_cfg->contains_wildcards(id_cfg))
			{	/* no ID configured, use local IP address */
				host_t *me;

				DBG1(DBG_CFG, "no IDr configured, fall back on IP address");
				me = this->ike_sa->get_my_host(this->ike_sa);
				id_cfg = identification_create_from_sockaddr(
														me->get_sockaddr(me));
				cfg->add(cfg, AUTH_RULE_IDENTITY, id_cfg);
			}
			this->ike_sa->set_my_id(this->ike_sa, id_cfg->clone(id_cfg));
			id = id_cfg;
		}
		else
		{	/* IDr received, check if it matches configuration */
			if (id_cfg && !id->matches(id, id_cfg))
			{
				DBG1(DBG_CFG, "received IDr %Y, but require %Y", id, id_cfg);
				goto peer_auth_failed;
			}
		}

		id_payload = id_payload_create_from_identification(ID_RESPONDER, id);
		get_reserved_id_bytes(this, id_payload);
		message->add_payload(message, (payload_t*)id_payload);

		if (this->initial_contact)
		{
			charon->ike_sa_manager->check_uniqueness(charon->ike_sa_manager,
													 this->ike_sa, TRUE);
			this->initial_contact = FALSE;
		}

		if ((uintptr_t)cfg->get(cfg, AUTH_RULE_AUTH_CLASS) == AUTH_CLASS_EAP)
		{	/* EAP-only authentication */
			if (!this->ike_sa->supports_extension(this->ike_sa,
												  EXT_EAP_ONLY_AUTHENTICATION))
			{
				DBG1(DBG_IKE, "configured EAP-only authentication, but peer "
					 "does not support it");
				goto peer_auth_failed;
			}
		}
		else
		{
			/* build authentication data */
			this->my_auth = authenticator_create_builder(this->ike_sa, cfg,
								this->other_nonce, this->my_nonce,
								this->other_packet->get_data(this->other_packet),
								this->my_packet->get_data(this->my_packet),
								this->reserved);
			if (!this->my_auth)
			{
				goto local_auth_failed;
			}
		}
	}

	if (this->other_auth)
	{
		switch (this->other_auth->build(this->other_auth, message))
		{
			case SUCCESS:
				this->other_auth->destroy(this->other_auth);
				this->other_auth = NULL;
				break;
			case NEED_MORE:
				break;
			default:
				if (message->get_payload(message, EXTENSIBLE_AUTHENTICATION))
				{	/* skip AUTHENTICATION_FAILED if we have EAP_FAILURE */
					goto peer_auth_failed_no_notify;
				}
				goto peer_auth_failed;
		}
	}
	if (this->my_auth)
	{
		switch (this->my_auth->build(this->my_auth, message))
		{
			case SUCCESS:
				apply_auth_cfg(this, TRUE);
				this->my_auth->destroy(this->my_auth);
				this->my_auth = NULL;
				break;
			case NEED_MORE:
				break;
			default:
				goto local_auth_failed;
		}
	}

	/* check for additional authentication rounds */
	if (do_another_auth(this))
	{
		message->add_notify(message, FALSE, ANOTHER_AUTH_FOLLOWS, chunk_empty);
	}
	else
	{
		this->do_another_auth = FALSE;
	}
	if (!this->do_another_auth && !this->expect_another_auth)
	{
		if (charon->ike_sa_manager->check_uniqueness(charon->ike_sa_manager,
													 this->ike_sa, FALSE))
		{
			DBG1(DBG_IKE, "cancelling IKE_SA setup due to uniqueness policy");
			charon->bus->alert(charon->bus, ALERT_UNIQUE_KEEP);
			message->add_notify(message, TRUE, AUTHENTICATION_FAILED,
								chunk_empty);
			return FAILED;
		}
		if (!charon->bus->authorize(charon->bus, TRUE))
		{
			DBG1(DBG_IKE, "final authorization hook forbids IKE_SA, cancelling");
			goto peer_auth_failed;
		}
		DBG0(DBG_IKE, "IKE_SA %s[%d] established between %H[%Y]...%H[%Y]",
			 this->ike_sa->get_name(this->ike_sa),
			 this->ike_sa->get_unique_id(this->ike_sa),
			 this->ike_sa->get_my_host(this->ike_sa),
			 this->ike_sa->get_my_id(this->ike_sa),
			 this->ike_sa->get_other_host(this->ike_sa),
			 this->ike_sa->get_other_id(this->ike_sa));
		this->ike_sa->set_state(this->ike_sa, IKE_ESTABLISHED);
		charon->bus->ike_updown(charon->bus, this->ike_sa, TRUE);
		return SUCCESS;
	}
	return NEED_MORE;

peer_auth_failed:
	message->add_notify(message, TRUE, AUTHENTICATION_FAILED, chunk_empty);
peer_auth_failed_no_notify:
	charon->bus->alert(charon->bus, ALERT_PEER_AUTH_FAILED);
	return FAILED;
local_auth_failed:
	message->add_notify(message, TRUE, AUTHENTICATION_FAILED, chunk_empty);
	charon->bus->alert(charon->bus, ALERT_LOCAL_AUTH_FAILED);
	return FAILED;
}

/**
 * Send an INFORMATIONAL message with an AUTH_FAILED before closing IKE_SA
 */
static void send_auth_failed_informational(private_ike_auth_t *this,
										   message_t *reply)
{
	message_t *message;
	packet_t *packet;
	host_t *host;

	message = message_create(IKEV2_MAJOR_VERSION, IKEV2_MINOR_VERSION);
	message->set_message_id(message, reply->get_message_id(reply) + 1);
	host = this->ike_sa->get_my_host(this->ike_sa);
	message->set_source(message, host->clone(host));
	host = this->ike_sa->get_other_host(this->ike_sa);
	message->set_destination(message, host->clone(host));
	message->set_exchange_type(message, INFORMATIONAL);
	message->add_notify(message, FALSE, AUTHENTICATION_FAILED, chunk_empty);

	if (this->ike_sa->generate_message(this->ike_sa, message,
									   &packet) == SUCCESS)
	{
		charon->sender->send(charon->sender, packet);
	}
	message->destroy(message);
}

METHOD(task_t, process_i, status_t,
	private_ike_auth_t *this, message_t *message)
{
	enumerator_t *enumerator;
	payload_t *payload;
	auth_cfg_t *cfg;
	bool mutual_eap = FALSE;
	bool is_error = FALSE;
	peer_cfg_t *peer_cfg;
	ike_cfg_t *ike_cfg;

	if (message->get_exchange_type(message) == IKE_SA_INIT)
	{
		if (message->get_notify(message, MULTIPLE_AUTH_SUPPORTED) &&
			multiple_auth_enabled())
		{
			this->ike_sa->enable_extension(this->ike_sa, EXT_MULTIPLE_AUTH);
		}
		return collect_other_init_data(this, message);
	}

	enumerator = message->create_payload_enumerator(message);
	while (enumerator->enumerate(enumerator, &payload))
	{
		if (payload->get_type(payload) == NOTIFY)
		{
			notify_payload_t *notify = (notify_payload_t*)payload;
			notify_type_t type = notify->get_notify_type(notify);

			switch (type)
			{
				case NO_PROPOSAL_CHOSEN:
				case SINGLE_PAIR_REQUIRED:
				case NO_ADDITIONAL_SAS:
				case INTERNAL_ADDRESS_FAILURE:
				case FAILED_CP_REQUIRED:
				case TS_UNACCEPTABLE:
				case INVALID_SELECTORS:
					/* these are errors, but are not critical as only the
					 * CHILD_SA won't get build, but IKE_SA establishes anyway */
					break;
				case MOBIKE_SUPPORTED:
				case ADDITIONAL_IP4_ADDRESS:
				case ADDITIONAL_IP6_ADDRESS:
					/* handled in ike_mobike task */
					break;
				case AUTH_LIFETIME:
					/* handled in ike_auth_lifetime task */
					break;
				case ME_ENDPOINT:
					/* handled in ike_me task */
					break;
					/* get the backoff_timer from NOTIFY*/
				case BACKOFF_TIMER:
					{
						chunk_t backoff_timer ;
						backoff_timer = notify->get_notification_data(notify);
						DBG1(DBG_IKE, "received BACKOFF_TIMER notify %d",
						 type);
						if(backoff_timer.len == 2)
						{
							uint8_t backoff_timer_value;
							backoff_timer_value = *((uint8_t *)backoff_timer.ptr+1);
							if(backoff_timer_value > 0)
							{
								this->ike_sa->set_backoff_timer(this->ike_sa, backoff_timer_value);
								DBG1(DBG_IKE, "received BACKOFF_TIMER notify , backoff_timer value :%d ",backoff_timer_value);
							}
							else
							{
								DBG1(DBG_IKE, "received BACKOFF_TIMER notify , invalid backoff_timer value");
							}
						}
						else
						{
							DBG1(DBG_IKE, "received BACKOFF_TIMER notify error data length");
						}
						break;
					}
				/* get the device_identity notify*/
				case DEVICE_IDENTITY:
					{
						chunk_t device_identity,spi_data;
						uint8_t device_identity_type;
						peer_cfg = this->ike_sa->get_peer_cfg(this->ike_sa);
						if(peer_cfg)
						{
							if(peer_cfg->get_device_identity(peer_cfg))
							{
								DBG1(DBG_IKE, "enable device_identity,need check the device_identity payload");
								spi_data = notify->get_spi_data(notify);
								if(!spi_data.len)
								{
									DBG1(DBG_IKE, "valid SPI data for device_identity notify");
									device_identity = notify->get_notification_data(notify);
									device_identity_type = *((uint8_t *)device_identity.ptr +2);
									if(device_identity_type == 1 || device_identity_type == 2)
									{
										DBG1(DBG_IKE, "valid device_identify type,need notify");
										this->ike_sa->set_notify_device_identity(this->ike_sa, TRUE);
									}
									else
									{
										DBG1(DBG_IKE, "invalid device_identify type,not need notify");
									}
								}
								else
								{
									DBG1(DBG_IKE, "invalid SPI data for device_identity notify");
								}
							}
							else
							{
								DBG1(DBG_IKE, "dsiable device_identity,not need check the device_identity payload");
							}
						}
					}
					break;
				default:
				{
					if (type <= 16383)
					{
						DBG1(DBG_IKE, "received %N notify error",
						notify_type_names, type);
						if (this->ike_sa->get_status_code(this->ike_sa) == PDN_STATUS_OK)
						{
							this->ike_sa->set_status_code(this->ike_sa, PDN_STATUS_UNABLE_MAKE_IPSEC_TUNNEL);
							this->ike_sa->set_sub_error_code(this->ike_sa, type);
						}
						is_error = TRUE;
//						enumerator->destroy(enumerator);
//						charon->bus->alert(charon->bus, ALERT_LOCAL_AUTH_FAILED);
//						return FAILED;
					}
					DBG2(DBG_IKE, "received %N notify",
						notify_type_names, type);
					break;
				}
			}
		}
	}
	enumerator->destroy(enumerator);

	if(is_error)
	{
		charon->bus->alert(charon->bus, ALERT_LOCAL_AUTH_FAILED);
		return FAILED;
	}

	if (this->expect_another_auth)
	{
		if (this->other_auth == NULL)
		{
			id_payload_t *id_payload;
			cert_payload_t *cert_payload;
			identification_t *id;

			peer_cfg = this->ike_sa->get_peer_cfg(this->ike_sa);
			ike_cfg = this->ike_sa->get_ike_cfg(this->ike_sa);
			if(!ike_cfg || !peer_cfg)
			{
				DBG1(DBG_IKE, "config missing");
				goto peer_auth_failed;
			}

			if(ike_cfg->send_certreq(ike_cfg) && !peer_cfg->server_nocert(peer_cfg))
			{
				/* handle CERTIFICATE payload */
				cert_payload = (cert_payload_t*)message->get_payload(message,
															 CERTIFICATE);
				if (!cert_payload)
				{
					DBG1(DBG_IKE, "CERTIFICATE payload missing");
					goto peer_auth_failed;
				}
			}

			/* handle IDr payload */
			id_payload = (id_payload_t*)message->get_payload(message,
															 ID_RESPONDER);
			if (!id_payload)
			{
				DBG1(DBG_IKE, "IDr payload missing");
				goto peer_auth_failed;
			}
			id = id_payload->get_identification(id_payload);
			get_reserved_id_bytes(this, id_payload);
			this->ike_sa->set_other_id(this->ike_sa, id);
			cfg = this->ike_sa->get_auth_cfg(this->ike_sa, FALSE);
			cfg->add(cfg, AUTH_RULE_IDENTITY, id->clone(id));

			if (message->get_payload(message, AUTHENTICATION))
			{
				/* verify authentication data */
				this->other_auth = authenticator_create_verifier(this->ike_sa,
								message, this->other_nonce, this->my_nonce,
								this->other_packet->get_data(this->other_packet),
								this->my_packet->get_data(this->my_packet),
								this->reserved);
				if (!this->other_auth)
				{
					goto peer_auth_failed;
				}
			}
			else
			{
				/* responder omitted AUTH payload, indicating EAP-only */
				mutual_eap = TRUE;
			}
		}
		if (this->other_auth)
		{
			switch (this->other_auth->process(this->other_auth, message))
			{
				case SUCCESS:
					break;
				case NEED_MORE:
					return NEED_MORE;
				default:
					goto peer_auth_failed;
			}
			this->other_auth->destroy(this->other_auth);
			this->other_auth = NULL;
		}
		/* another auth round done, invoke authorize hook */
		if (!charon->bus->authorize(charon->bus, FALSE))
		{
			DBG1(DBG_IKE, "authorization forbids IKE_SA, cancelling");
			goto peer_auth_failed;
		}

		if (!mutual_eap)
		{
			apply_auth_cfg(this, FALSE);
		}
	}

	if (this->my_auth)
	{
		switch (this->my_auth->process(this->my_auth, message))
		{
			case SUCCESS:
				apply_auth_cfg(this, TRUE);
				if (this->my_auth->is_mutual(this->my_auth))
				{
					apply_auth_cfg(this, FALSE);
				}
				this->my_auth->destroy(this->my_auth);
				this->my_auth = NULL;
				this->do_another_auth = do_another_auth(this);
				break;
			case NEED_MORE:
				break;
			default:
				charon->bus->alert(charon->bus, ALERT_LOCAL_AUTH_FAILED);
				send_auth_failed_informational(this, message);
				if (this->ike_sa->get_status_code(this->ike_sa) == PDN_STATUS_OK)
				{
					this->ike_sa->set_status_code(this->ike_sa, PDN_STATUS_UNABLE_MAKE_IPSEC_TUNNEL);
				}
				return FAILED;
		}
	}
	if (mutual_eap)
	{
		if (!this->my_auth || !this->my_auth->is_mutual(this->my_auth))
		{
			DBG1(DBG_IKE, "do not allow non-mutual EAP-only authentication");
			goto peer_auth_failed;
		}
		DBG1(DBG_IKE, "allow mutual EAP-only authentication");
	}

	if (message->get_notify(message, ANOTHER_AUTH_FOLLOWS) == NULL)
	{
		this->expect_another_auth = FALSE;
	}
	if (!this->expect_another_auth && !this->do_another_auth && !this->my_auth)
	{
		if (!update_cfg_candidates(this, TRUE))
		{
			goto peer_auth_failed;
		}
		if (!charon->bus->authorize(charon->bus, TRUE))
		{
			DBG1(DBG_IKE, "final authorization hook forbids IKE_SA, "
					      "cancelling");
			goto peer_auth_failed;
		}
		DBG0(DBG_IKE, "IKE_SA %s[%d] established between %H[%Y]...%H[%Y]",
			 this->ike_sa->get_name(this->ike_sa),
			 this->ike_sa->get_unique_id(this->ike_sa),
			 this->ike_sa->get_my_host(this->ike_sa),
			 this->ike_sa->get_my_id(this->ike_sa),
			 this->ike_sa->get_other_host(this->ike_sa),
			 this->ike_sa->get_other_id(this->ike_sa));
		this->ike_sa->set_state(this->ike_sa, IKE_ESTABLISHED);
		charon->bus->ike_updown(charon->bus, this->ike_sa, TRUE);
		return SUCCESS;
	}
	return NEED_MORE;

peer_auth_failed:
	if (this->ike_sa->get_status_code(this->ike_sa) == PDN_STATUS_OK)
	{
		this->ike_sa->set_status_code(this->ike_sa, PDN_STATUS_UNABLE_MAKE_IPSEC_TUNNEL);
	}
	charon->bus->alert(charon->bus, ALERT_PEER_AUTH_FAILED);
	send_auth_failed_informational(this, message);
	return FAILED;
}

METHOD(task_t, get_type, task_type_t,
	private_ike_auth_t *this)
{
	return TASK_IKE_AUTH;
}

METHOD(task_t, migrate, void,
	private_ike_auth_t *this, ike_sa_t *ike_sa)
{
	chunk_free(&this->my_nonce);
	chunk_free(&this->other_nonce);
	DESTROY_IF(this->my_packet);
	DESTROY_IF(this->other_packet);
	DESTROY_IF(this->peer_cfg);
	DESTROY_IF(this->my_auth);
	DESTROY_IF(this->other_auth);
	this->candidates->destroy_offset(this->candidates, offsetof(peer_cfg_t, destroy));

	this->my_packet = NULL;
	this->other_packet = NULL;
	this->ike_sa = ike_sa;
	this->peer_cfg = NULL;
	this->my_auth = NULL;
	this->other_auth = NULL;
	this->do_another_auth = TRUE;
	this->expect_another_auth = TRUE;
	this->authentication_failed = FALSE;
	this->candidates = linked_list_create();
}

METHOD(task_t, destroy, void,
	private_ike_auth_t *this)
{
	chunk_free(&this->my_nonce);
	chunk_free(&this->other_nonce);
	DESTROY_IF(this->my_packet);
	DESTROY_IF(this->other_packet);
	DESTROY_IF(this->my_auth);
	DESTROY_IF(this->other_auth);
	DESTROY_IF(this->peer_cfg);
	this->candidates->destroy_offset(this->candidates, offsetof(peer_cfg_t, destroy));
	free(this);
}

/*
 * Described in header.
 */
ike_auth_t *ike_auth_create(ike_sa_t *ike_sa, bool initiator)
{
	private_ike_auth_t *this;

	INIT(this,
		.public = {
			.task = {
				.get_type = _get_type,
				.migrate = _migrate,
				.build = _build_r,
				.process = _process_r,
				.destroy = _destroy,
			},
		},
		.ike_sa = ike_sa,
		.initiator = initiator,
		.candidates = linked_list_create(),
		.do_another_auth = TRUE,
		.expect_another_auth = TRUE,
	);
	if (initiator)
	{
		this->public.task.build = _build_i;
		this->public.task.process = _process_i;
	}
	return &this->public;
}
