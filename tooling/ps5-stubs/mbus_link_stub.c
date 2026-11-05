/*
 * Porpoise - device bus (libSceMbus) link stub, Move test builds only.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host-link declarations so the title imports these from the system's
 * libSceMbus (a PS5 title can't look functions up at run time); never
 * packaged or executed. Signatures as libSceMove calls them.
 */

#include <stdint.h>

int32_t sceMbusInit(void) { return -1; }
int32_t sceMbusResolveByUserId(int32_t type, int32_t user, int32_t index)
{
    (void)type; (void)user; (void)index;
    return -1;
}
int32_t sceMbusAddHandle(int32_t type, int32_t user, int32_t handle)
{
    (void)type; (void)user; (void)handle;
    return -1;
}
int32_t sceMbusRemoveHandle(int32_t type, int32_t handle)
{
    (void)type; (void)handle;
    return -1;
}
