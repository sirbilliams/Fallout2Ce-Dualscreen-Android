#ifndef FALLOUT_SFALL_COMPAT_H_
#define FALLOUT_SFALL_COMPAT_H_

namespace fallout {

class OpcodeContext;

void sfallCompatibilityInit();
void mf_intface_hide(OpcodeContext& ctx);
void mf_intface_show(OpcodeContext& ctx);
void mf_intface_is_hidden(OpcodeContext& ctx);
void mf_get_current_inven_size(OpcodeContext& ctx);
void mf_set_dude_obj(OpcodeContext& ctx);
void mf_interface_overlay(OpcodeContext& ctx);

} // namespace fallout

#endif /* FALLOUT_SFALL_COMPAT_H_ */
