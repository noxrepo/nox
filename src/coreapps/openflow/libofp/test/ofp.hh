#ifndef __NOX_OF_H
#define __NOX_OF_H

#include "openflow/openflow.h"
#include "ofpbuf.h"
#include "ofp-errors.h"
#include "ofp-msgs.h"
#include "ofp-util.h"
#include "ofp-actions.h"

enum ofp_controller_role
{
    OFPCR_ROLE_NOCHANGE = OFPCR12_ROLE_NOCHANGE,    /* Don't change current role. */
    OFPCR_ROLE_EQUAL = OFPCR12_ROLE_EQUAL,       /* Default role, full access. */
    OFPCR_ROLE_MASTER = OFPCR12_ROLE_EQUAL,      /* Full access, at most one master. */
    OFPCR_ROLE_SLAVE = OFPCR12_ROLE_EQUAL,       /* Read-only access. */
};

typedef ofp11_port ofl_port;

namespace vigil
{
namespace openflow
{
namespace v13
{
    const unsigned int OFP_MAX_MSG_BYTES = 64 * 1024;
    const unsigned int OFP_HEADER_BYTES = 8;
    const unsigned int OFP_HELLO_BYTES = 8;
    const unsigned int OFP_SWITCH_CONFIG_BYTES = 12;
} // namespace v13
} // namespace openflow
} // namespace vigil

#endif
