#include <stdio.h>
#include "acl.h"


int
init(void)
{

	acl_symbol_register("milter_stage", MS_CONNECT | MS_HELO |
		MS_ENVFROM | MS_ENVRCPT | MS_HEADER | MS_EOH | MS_BODY | MS_EOM,
		NULL);

	acl_symbol_register("milter_stagename", MS_CONNECT | MS_HELO |
		MS_ENVFROM | MS_ENVRCPT | MS_HEADER | MS_EOH | MS_BODY | MS_EOM,
		NULL);

	acl_symbol_register("milter_received", MS_CONNECT | MS_HELO |
		MS_ENVFROM | MS_ENVRCPT | MS_HEADER | MS_EOH | MS_BODY | MS_EOM,
		NULL);

	acl_symbol_register("milter_hostaddr", MS_CONNECT | MS_HELO |
		MS_ENVFROM | MS_ENVRCPT | MS_HEADER | MS_EOH | MS_BODY | MS_EOM,
		NULL);

	acl_symbol_register("milter_hostname", MS_CONNECT | MS_HELO |
		MS_ENVFROM | MS_ENVRCPT | MS_HEADER | MS_EOH | MS_BODY | MS_EOM,
		NULL);

	acl_symbol_register("milter_helo", MS_HELO | MS_ENVFROM | MS_ENVRCPT |
		MS_HEADER | MS_EOH | MS_BODY | MS_EOM, NULL);

	acl_symbol_register("milter_envfrom", MS_ENVFROM | MS_ENVRCPT |
		MS_HEADER | MS_EOH | MS_BODY | MS_EOM, NULL);

	acl_symbol_register("milter_envrcpt", MS_ENVRCPT | MS_HEADER | MS_EOH |
		MS_BODY | MS_EOM, NULL);

	acl_symbol_register("milter_recipient_list", MS_EOH | MS_BODY | MS_EOM,
		NULL);

	acl_symbol_register("milter_message", MS_EOM, NULL);

	acl_symbol_register("milter_message_size", MS_EOM, NULL);

	return 0;
}
