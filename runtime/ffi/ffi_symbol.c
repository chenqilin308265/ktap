/*
 * ffi_symbol.c - ktapvm kernel module ffi symbol submodule
 *
 * Copyright (C) 2012-2016, Huawei Technologies.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */


#include "../../include/ktap_types.h"
#include "../../include/ktap_ffi.h"
#include "../ktap.h"
#include "../kp_vm.h"
#include "../kp_obj.h"
#include "../kp_str.h"
#include "../kp_tab.h"

static inline csymbol *get_csym_arr(ktap_state_t *ks)
{
	return G(ks)->ffis.csym_arr;
}

static inline int get_csym_nr(ktap_state_t *ks)
{
	return G(ks)->ffis.csym_nr;
}

static inline void set_csym_arr(ktap_state_t *ks, csymbol *csym)
{
	G(ks)->ffis.csym_arr = csym;
}

static inline void set_csym_nr(ktap_state_t *ks, int nr)
{
	G(ks)->ffis.csym_nr = nr;
}


static inline ktap_tab_t *get_ffi_ctable(ktap_state_t *ks)
{
	return G(ks)->ffis.ctable;
}

static int setup_ffi_ctable(ktap_state_t *ks)
{
	ktap_val_t ffi_name, ffi_lib_name, ffi_mt;
	const ktap_val_t *gt, *ffit;
	ktap_tab_t *registry;

	gt = kp_tab_getint(hvalue(&G(ks)->registry), KTAP_RIDX_GLOBALS);

	G(ks)->ffis.ctable = kp_tab_new(ks, 0, 512);
	if (!G(ks)->ffis.ctable)
		return -1;

	/* get global["ffi"] */
	set_string(&ffi_name, kp_str_new(ks, "ffi"));
	registry = hvalue(gt);
	ffit = kp_tab_get(ks, registry, &ffi_name);
	/* insert ffi C table to ffi table */
	set_table(&ffi_mt, get_ffi_ctable(ks));
	set_string(&ffi_lib_name, kp_str_new(ks, "C"));
	registry = hvalue(ffit);
	kp_tab_setvalue(ks, registry, &ffi_lib_name, &ffi_mt);

	return 0;
}

inline csymbol *ffi_get_csym_by_id(ktap_state_t *ks, int id)
{
	return &(get_csym_arr(ks)[id]);
}

csymbol_id ffi_get_csym_id(ktap_state_t *ks, char *name)
{
	int i;

	for (i = 0; i < get_csym_nr(ks); i++) {
		if (!strcmp(name, csym_name(ffi_get_csym_by_id(ks, i)))) {
			return i;
		}
	}

	kp_error(ks, "Cannot find csymbol with name %s\n", name);
	return 0;
}

static void add_ffi_func_to_ctable(ktap_state_t *ks, csymbol_id id)
{
	ktap_val_t func_name, fv;
	ktap_cdata_t *cd;
	csymbol *cs;

	/* push cdata to ctable */
	set_cdata(&fv, kp_obj_newobject(ks, KTAP_TYPE_CDATA, sizeof(ktap_cdata_t),
					NULL));
	cd = cdvalue(&fv);
	cd_set_csym_id(cd, id);

	cs = id_to_csym(ks, id);
	set_string(&func_name, kp_str_new(ks, csym_name(cs)));
	kp_tab_setvalue(ks, get_ffi_ctable(ks), &func_name, &fv);
}

static int setup_ffi_symbol_table(ktap_state_t *ks)
{
	int i;
	csymbol *cs;

	if (setup_ffi_ctable(ks))
		return -1;

	/* push all functions to ctable */
	for (i = 0; i < get_csym_nr(ks); i++) {
		cs = &get_csym_arr(ks)[i];
		switch (cs->type) {
		case FFI_FUNC:
			kp_verbose_printf(ks, "[%d] loading C function %s\n",
					i, csym_name(cs));
			add_ffi_func_to_ctable(ks, i);
			kp_verbose_printf(ks, "%s loaded\n", csym_name(cs));
			break;
		case FFI_STRUCT:
		case FFI_UNION:
			break;
		default:
			break;
		}
	}

	return 0;
}

void ffi_free_symbols(ktap_state_t *ks)
{
	int i;
	csymbol_id *arg_ids;
	csymbol *cs;

	if (!get_csym_arr(ks))
		return;

	for (i = 0; i < get_csym_nr(ks); i++) {
		cs = &get_csym_arr(ks)[i];
		switch (csym_type(cs)) {
		case FFI_FUNC:
			arg_ids = csym_func_arg_ids(cs);
			if (arg_ids)
				kp_free(ks, arg_ids);
			break;
		case FFI_STRUCT:
		case FFI_UNION:
			/*@TODO finish this  20.11 2013 (houqp)*/
			break;
		default:
			break;
		}
	}

	kp_free(ks, get_csym_arr(ks));
}

int ffi_set_csym_arr(ktap_state_t *ks, int cs_nr, csymbol *new_arr)
{
	set_csym_nr(ks, cs_nr);
	set_csym_arr(ks, new_arr);
	return setup_ffi_symbol_table(ks);
}

