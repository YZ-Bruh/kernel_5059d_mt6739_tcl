/*
 * Copyright (C) 2007 Martin Willi
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

#include "ike_dpd.h"

#include <hydra.h>
#include <daemon.h>
#include <network/wod_channel.h>

typedef struct private_ike_dpd_t private_ike_dpd_t;

/**
 * Private members of a ike_dpd_t task.
 */
struct private_ike_dpd_t {

	/**
	 * Public methods and task_t interface.
	 */
	ike_dpd_t public;

	/**
	 * Assigned IKE_SA.
	 */
	ike_sa_t *ike_sa;
};

/**
 * update addresses of associated CHILD_SAs
 */
static void update_children(private_ike_dpd_t *this)
{
	enumerator_t *enumerator;
	child_sa_t *child_sa;
	linked_list_t *vips;
	host_t *host;

	vips = linked_list_create();

	enumerator = this->ike_sa->create_virtual_ip_enumerator(this->ike_sa, TRUE);
	while (enumerator->enumerate(enumerator, &host))
	{
		vips->insert_last(vips, host);
	}
	enumerator->destroy(enumerator);

	enumerator = this->ike_sa->create_child_sa_enumerator(this->ike_sa);
	while (enumerator->enumerate(enumerator, (void**)&child_sa))
	{
		if (child_sa->update(child_sa,
				this->ike_sa->get_my_host(this->ike_sa),
				this->ike_sa->get_other_host(this->ike_sa), vips,
				this->ike_sa->has_condition(this->ike_sa,
											COND_NAT_ANY)) == NOT_SUPPORTED)
		{
			this->ike_sa->rekey_child_sa(this->ike_sa,
					child_sa->get_protocol(child_sa),
					child_sa->get_spi(child_sa, TRUE));
		}
	}
	enumerator->destroy(enumerator);

	vips->destroy(vips);
}

METHOD(task_t, return_need_more, status_t,
	private_ike_dpd_t *this, message_t *message)
{
	return NEED_MORE;
}

METHOD(task_t, process_i, status_t,
	private_ike_dpd_t *this, message_t *message)
{
	if (this->ike_sa->get_oos(this->ike_sa))
	{
		DBG1(DBG_IKE, "*** update child_sa ***");
		update_children(this);
		this->ike_sa->set_oos(this->ike_sa, OOS_RESUME);
		DBG1(DBG_IKE, "============================= receive DPD reply, oos disabled.");
		this->ike_sa->update_keepalive_info(this->ike_sa, TRUE, TRUE);
	}
	return SUCCESS;
}

METHOD(task_t, get_type, task_type_t,
	private_ike_dpd_t *this)
{
	return TASK_IKE_DPD;
}


METHOD(task_t, migrate, void,
	private_ike_dpd_t *this, ike_sa_t *ike_sa)
{

}

METHOD(task_t, destroy, void,
	private_ike_dpd_t *this)
{
	free(this);
}

/*
 * Described in header.
 */
ike_dpd_t *ike_dpd_create(ike_sa_t *ike_sa, bool initiator)
{
	private_ike_dpd_t *this;

	INIT(this,
		.public = {
			.task = {
				.get_type = _get_type,
				.migrate = _migrate,
				.destroy = _destroy,
			},
		},
		.ike_sa = ike_sa,
	);

	if (initiator)
	{
		this->public.task.build = _return_need_more;
		this->public.task.process = _process_i;
	}
	else
	{
		this->public.task.build = (void*)return_success;
		this->public.task.process = _return_need_more;
	}

	return &this->public;
}
