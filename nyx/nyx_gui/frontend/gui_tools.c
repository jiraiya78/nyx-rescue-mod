/*
 * Copyright (c) 2018 naehrwert
 * Copyright (c) 2018-2026 CTCaer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <bdk.h>

#include "gui.h"
#include "gui_tools.h"
#include "gui_tools_partition_manager.h"
#include "gui_emmc_tools.h"
#include "fe_emummc_tools.h"
#include "../config.h"
#include "../hos/pkg1.h"
#include "../hos/pkg2.h"
#include "../hos/hos.h"
#include <libs/compr/blz.h>
#include <libs/fatfs/ff.h>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#include <libs/miniz/miniz.c>

lv_obj_t *ums_mbox;

extern char *emmcsn_path_impl(char *path, char *sub_dir, char *filename, sdmmc_storage_t *storage);

static lv_obj_t *_create_container(lv_obj_t *parent)
{
	static lv_style_t h_style;
	lv_style_copy(&h_style, &lv_style_transp);
	h_style.body.padding.inner = 0;
	h_style.body.padding.hor = LV_DPI - (LV_DPI / 4);
	h_style.body.padding.ver = LV_DPI / 6;

	lv_obj_t *h1 = lv_cont_create(parent, NULL);
	lv_cont_set_style(h1, &h_style);
	lv_cont_set_fit(h1, false, true);
	lv_obj_set_width(h1, (LV_HOR_RES / 9) * 4);
	lv_obj_set_click(h1, false);
	lv_cont_set_layout(h1, LV_LAYOUT_OFF);

	return h1;
}

bool get_set_autorcm_status(bool toggle)
{
	u32 sector;
	u8 corr_mod0, mod1;
	bool enabled = false;

	if (h_cfg.t210b01)
		return false;

	emmc_initialize(false);

	u8 *tempbuf = (u8 *)malloc(0x200);
	emmc_set_partition(EMMC_BOOT0);
	sdmmc_storage_read(&emmc_storage, 0x200 / EMMC_BLOCKSIZE, 1, tempbuf);

	// Get the correct RSA modulus byte masks.
	nx_emmc_get_autorcm_masks(&corr_mod0, &mod1);

	// Check if 2nd byte of modulus is correct.
	if (tempbuf[0x11] != mod1)
		goto out;

	if (tempbuf[0x10] != corr_mod0)
		enabled = true;

	// Toggle autorcm status if requested.
	if (toggle)
	{
		// Iterate BCTs.
		for (u32 i = 0; i < 4; i++)
		{
			sector = (0x200 + (0x4000 * i)) / EMMC_BLOCKSIZE; // 0x4000 bct + 0x200 offset.
			sdmmc_storage_read(&emmc_storage, sector, 1, tempbuf);

			if (!enabled)
				tempbuf[0x10] = 0;
			else
				tempbuf[0x10] = corr_mod0;
			sdmmc_storage_write(&emmc_storage, sector, 1, tempbuf);
		}
		enabled = !enabled;
	}

	// Check if RCM is patched and protect from a possible brick.
	if (enabled && h_cfg.rcm_patched && hw_get_chip_id() != GP_HIDREV_MAJOR_T210B01)
	{
		// Iterate BCTs.
		for (u32 i = 0; i < 4; i++)
		{
			sector = (0x200 + (0x4000 * i)) / EMMC_BLOCKSIZE; // 0x4000 bct + 0x200 offset.
			sdmmc_storage_read(&emmc_storage, sector, 1, tempbuf);

			// Check if 2nd byte of modulus is correct.
			if (tempbuf[0x11] != mod1)
				continue;

			// If AutoRCM is enabled, disable it.
			if (tempbuf[0x10] != corr_mod0)
			{
				tempbuf[0x10] = corr_mod0;

				sdmmc_storage_write(&emmc_storage, sector, 1, tempbuf);
			}
		}

		enabled = false;
	}

out:
	free(tempbuf);
	emmc_end();

	h_cfg.autorcm_enabled = enabled;

	return enabled;
}

static lv_res_t _create_mbox_autorcm_status(lv_obj_t *btn)
{
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char * mbox_btn_map[] = { "\251", "\222OK", "\251", "" };
	lv_obj_t * mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);

	bool enabled = get_set_autorcm_status(true);

	if (enabled)
	{
		lv_mbox_set_text(mbox,
			"AutoRCM is now #C7EA46 ENABLED!#\n\n"
			"You can now automatically enter RCM by only pressing #FF8000 POWER#.\n"
			"Use the AutoRCM button here again if you want to remove it later on.");
	}
	else
	{
		lv_mbox_set_text(mbox,
			"AutoRCM is now #FF8000 DISABLED!#\n\n"
			"The boot process is now normal and you need the #FF8000 VOL+# + #FF8000 HOME# (jig) combo to enter RCM.\n");
	}

	lv_mbox_add_btns(mbox, mbox_btn_map, nyx_mbox_action);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 5);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);

	if (enabled)
		lv_btn_set_state(btn, LV_BTN_STATE_TGL_REL);
	else
		lv_btn_set_state(btn, LV_BTN_STATE_REL);
	nyx_generic_onoff_toggle(btn);

	return LV_RES_OK;
}

static lv_res_t _create_mbox_hid(usb_ctxt_t *usbs)
{
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char *mbox_btn_map_dis[] = { "\251", "\262Close", "\251", "" };
	static const char *mbox_btn_map[] = { "\251", "\222Close", "\251", "" };
	lv_obj_t *mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);

	char *txt_buf = malloc(SZ_4K);

	s_printf(txt_buf, "#FF8000 HID Emulation#\n\n#C7EA46 Device:# ");

	if (usbs->type == USB_HID_GAMEPAD)
		strcat(txt_buf, "Gamepad");
	else
		strcat(txt_buf, "Touchpad");

	lv_mbox_set_text(mbox, txt_buf);
	free(txt_buf);

	lv_obj_t *lbl_status = lv_label_create(mbox, NULL);
	lv_label_set_recolor(lbl_status, true);
	lv_label_set_text(lbl_status, " ");
	usbs->label = (void *)lbl_status;

	lv_obj_t *lbl_tip = lv_label_create(mbox, NULL);
	lv_label_set_recolor(lbl_tip, true);
	lv_label_set_static_text(lbl_tip, "Note: To end it, press #C7EA46 L3# + #C7EA46 HOME# or remove the cable.");
	lv_obj_set_style(lbl_tip, &hint_small_style);

	lv_mbox_add_btns(mbox, mbox_btn_map_dis, nyx_mbox_action);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 5);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);

	usb_device_gadget_hid(usbs);

	lv_mbox_add_btns(mbox, mbox_btn_map, nyx_mbox_action);

	return LV_RES_OK;
}

static lv_res_t _create_mbox_ums(usb_ctxt_t *usbs)
{
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char *mbox_btn_map_dis[] = { "\251", "\262Close", "\251", "" };
	static const char *mbox_btn_map[] = { "\251", "\222Close", "\251", "" };
	lv_obj_t *mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);

	char *txt_buf = malloc(SZ_4K);

	s_printf(txt_buf, "#FF8000 USB Mass Storage#\n\n#C7EA46 Device:# ");

	if (usbs->type == MMC_SD)
	{
		switch (usbs->partition)
		{
		case 0:
			strcat(txt_buf, "SD Card");
			break;
		case EMMC_GPP + 1:
			strcat(txt_buf, "emuMMC GPP");
			break;
		case EMMC_BOOT0 + 1:
			strcat(txt_buf, "emuMMC BOOT0");
			break;
		case EMMC_BOOT1 + 1:
			strcat(txt_buf, "emuMMC BOOT1");
			break;
		}
	}
	else
	{
		switch (usbs->partition)
		{
		case EMMC_GPP + 1:
			strcat(txt_buf, "eMMC GPP");
			break;
		case EMMC_BOOT0 + 1:
			strcat(txt_buf, "eMMC BOOT0");
			break;
		case EMMC_BOOT1 + 1:
			strcat(txt_buf, "eMMC BOOT1");
			break;
		}
	}

	lv_mbox_set_text(mbox, txt_buf);
	free(txt_buf);

	lv_obj_t *lbl_status = lv_label_create(mbox, NULL);
	lv_label_set_recolor(lbl_status, true);
	lv_label_set_text(lbl_status, " ");
	usbs->label = (void *)lbl_status;

	lv_obj_t *lbl_tip = lv_label_create(mbox, NULL);
	lv_label_set_recolor(lbl_tip, true);
	if (!usbs->ro)
	{
		if (usbs->type == MMC_SD)
		{
			lv_label_set_static_text(lbl_tip,
				"Note: To end it, #C7EA46 safely eject# from inside the OS.\n"
				"       #FFDD00 DO NOT remove the cable!#");
		}
		else
		{
			lv_label_set_static_text(lbl_tip,
				"Note: To end it, #C7EA46 safely eject# from inside the OS.\n"
				"       #FFDD00 If it's not mounted, you might need to remove the cable!#");
		}
	}
	else
	{
		lv_label_set_static_text(lbl_tip,
			"Note: To end it, #C7EA46 safely eject# from inside the OS\n"
			"       or by removing the cable!#");
	}
	lv_obj_set_style(lbl_tip, &hint_small_style);

	lv_mbox_add_btns(mbox, mbox_btn_map_dis, nyx_mbox_action);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 5);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);

	// Dim backlight.
	display_backlight_brightness(20, 1000);

	usb_device_gadget_ums(usbs);

	// Restore backlight.
	display_backlight_brightness(h_cfg.backlight - 20, 1000);

	lv_mbox_add_btns(mbox, mbox_btn_map, nyx_mbox_action);

	ums_mbox = dark_bg;

	return LV_RES_OK;
}

static lv_res_t _create_mbox_ums_error(int error)
{
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char *mbox_btn_map[] = { "\251", "\222OK", "\251", "" };
	lv_obj_t * mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);

	switch (error)
	{
	case 1:
		lv_mbox_set_text(mbox, "#FF8000 USB Mass Storage#\n\n#FFFF00 Error mounting SD Card!#");
		break;
	case 2:
		lv_mbox_set_text(mbox, "#FF8000 USB Mass Storage#\n\n#FFFF00 No emuMMC found active!#");
		break;
	case 3:
		lv_mbox_set_text(mbox, "#FF8000 USB Mass Storage#\n\n#FFFF00 Active emuMMC is not partition based!#");
		break;
	}

	lv_mbox_add_btns(mbox, mbox_btn_map, nyx_mbox_action);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 5);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);

	return LV_RES_OK;
}

static void usb_gadget_set_text(void *lbl, const char *text)
{
	lv_label_set_text((lv_obj_t *)lbl, text);
	manual_system_maintenance(true);
}

static lv_res_t _action_hid_jc(lv_obj_t *btn)
{
	// Reduce BPMP, RAM and backlight and power off SDMMC1 to conserve power.
	sd_end();
	minerva_change_freq(FREQ_800);
	bpmp_clk_rate_relaxed(true);
	display_backlight_brightness(10, 1000);

	usb_ctxt_t usbs;
	usbs.type = USB_HID_GAMEPAD;
	usbs.system_maintenance = &manual_system_maintenance;
	usbs.set_text = &usb_gadget_set_text;

	_create_mbox_hid(&usbs);

	// Restore BPMP, RAM and backlight.
	minerva_change_freq(FREQ_1600);
	bpmp_clk_rate_relaxed(false);
	display_backlight_brightness(h_cfg.backlight - 20, 1000);

	return LV_RES_OK;
}

/*
static lv_res_t _action_hid_touch(lv_obj_t *btn)
{
	// Reduce BPMP, RAM and backlight and power off SDMMC1 to conserve power.
	sd_end();
	minerva_change_freq(FREQ_800);
	bpmp_clk_rate_relaxed(true);
	display_backlight_brightness(10, 1000);

	usb_ctxt_t usbs;
	usbs.type = USB_HID_TOUCHPAD;
	usbs.system_maintenance = &manual_system_maintenance;
	usbs.set_text = &usb_gadget_set_text;

	_create_mbox_hid(&usbs);

	// Restore BPMP, RAM and backlight.
	minerva_change_freq(FREQ_1600);
	bpmp_clk_rate_relaxed(false);
	display_backlight_brightness(h_cfg.backlight - 20, 1000);

	return LV_RES_OK;
}
*/

static bool usb_msc_emmc_read_only;
lv_res_t action_ums_sd(lv_obj_t *btn)
{
	usb_ctxt_t usbs;
	usbs.type = MMC_SD;
	usbs.partition = 0;
	usbs.offset = 0;
	usbs.sectors = 0;
	usbs.ro = 0;
	usbs.system_maintenance = &manual_system_maintenance;
	usbs.set_text = &usb_gadget_set_text;

	_create_mbox_ums(&usbs);

	return LV_RES_OK;
}

static lv_res_t _action_ums_emmc_boot0(lv_obj_t *btn)
{
	if (!nyx_emmc_check_battery_enough())
		return LV_RES_OK;

	usb_ctxt_t usbs;
	usbs.type = MMC_EMMC;
	usbs.partition = EMMC_BOOT0 + 1;
	usbs.offset = 0;
	usbs.sectors = 0;
	usbs.ro = usb_msc_emmc_read_only;
	usbs.system_maintenance = &manual_system_maintenance;
	usbs.set_text = &usb_gadget_set_text;

	_create_mbox_ums(&usbs);

	return LV_RES_OK;
}

static lv_res_t _action_ums_emmc_boot1(lv_obj_t *btn)
{
	if (!nyx_emmc_check_battery_enough())
		return LV_RES_OK;

	usb_ctxt_t usbs;
	usbs.type = MMC_EMMC;
	usbs.partition = EMMC_BOOT1 + 1;
	usbs.offset = 0;
	usbs.sectors = 0;
	usbs.ro = usb_msc_emmc_read_only;
	usbs.system_maintenance = &manual_system_maintenance;
	usbs.set_text = &usb_gadget_set_text;

	_create_mbox_ums(&usbs);

	return LV_RES_OK;
}

static lv_res_t _action_ums_emmc_gpp(lv_obj_t *btn)
{
	if (!nyx_emmc_check_battery_enough())
		return LV_RES_OK;

	usb_ctxt_t usbs;
	usbs.type = MMC_EMMC;
	usbs.partition = EMMC_GPP + 1;
	usbs.offset = 0;
	usbs.sectors = 0;
	usbs.ro = usb_msc_emmc_read_only;
	usbs.system_maintenance = &manual_system_maintenance;
	usbs.set_text = &usb_gadget_set_text;

	_create_mbox_ums(&usbs);

	return LV_RES_OK;
}

static lv_res_t _action_ums_emuemmc_boot0(lv_obj_t *btn)
{
	if (!nyx_emmc_check_battery_enough())
		return LV_RES_OK;

	usb_ctxt_t usbs;

	int error = sd_mount();
	if (!error)
	{
		emummc_cfg_t emu_info;
		load_emummc_cfg(&emu_info);

		error = 2;
		if (emu_info.enabled)
		{
			error = 3;
			if (emu_info.sector)
			{
				error = 0;
				usbs.offset = emu_info.sector;
			}
		}

		if (emu_info.path)
			free(emu_info.path);
		if (emu_info.nintendo_path)
			free(emu_info.nintendo_path);
	}
	sd_unmount();

	if (error)
		_create_mbox_ums_error(error);
	else
	{
		usbs.type = MMC_SD;
		usbs.partition = EMMC_BOOT0 + 1;
		usbs.sectors = 0x2000; // Forced 4MB.
		usbs.ro = usb_msc_emmc_read_only;
		usbs.system_maintenance = &manual_system_maintenance;
		usbs.set_text = &usb_gadget_set_text;
		_create_mbox_ums(&usbs);
	}

	return LV_RES_OK;
}

static lv_res_t _action_ums_emuemmc_boot1(lv_obj_t *btn)
{
	if (!nyx_emmc_check_battery_enough())
		return LV_RES_OK;

	usb_ctxt_t usbs;

	int error = sd_mount();
	if (!error)
	{
		emummc_cfg_t emu_info;
		load_emummc_cfg(&emu_info);

		error = 2;
		if (emu_info.enabled)
		{
			error = 3;
			if (emu_info.sector)
			{
				error = 0;
				usbs.offset = emu_info.sector + 0x2000;
			}
		}

		if (emu_info.path)
			free(emu_info.path);
		if (emu_info.nintendo_path)
			free(emu_info.nintendo_path);
	}
	sd_unmount();

	if (error)
		_create_mbox_ums_error(error);
	else
	{
		usbs.type = MMC_SD;
		usbs.partition = EMMC_BOOT1 + 1;
		usbs.sectors = 0x2000; // Forced 4MB.
		usbs.ro = usb_msc_emmc_read_only;
		usbs.system_maintenance = &manual_system_maintenance;
		usbs.set_text = &usb_gadget_set_text;
		_create_mbox_ums(&usbs);
	}

	return LV_RES_OK;
}

static lv_res_t _action_ums_emuemmc_gpp(lv_obj_t *btn)
{
	if (!nyx_emmc_check_battery_enough())
		return LV_RES_OK;

	usb_ctxt_t usbs;

	int error = sd_mount();
	if (!error)
	{
		emummc_cfg_t emu_info;
		load_emummc_cfg(&emu_info);

		error = 2;
		if (emu_info.enabled)
		{
			error = 3;
			if (emu_info.sector)
			{
				error = 1;
				usbs.offset = emu_info.sector + 0x4000;

				u8 *gpt = malloc(SD_BLOCKSIZE);
				if (!sdmmc_storage_read(&sd_storage, usbs.offset + 1, 1, gpt))
				{
					if (!memcmp(gpt, "EFI PART", 8))
					{
						error = 0;
						usbs.sectors = *(u32 *)(gpt + 0x20) + 1; // Backup LBA + 1.
					}
				}
			}
		}

		if (emu_info.path)
			free(emu_info.path);
		if (emu_info.nintendo_path)
			free(emu_info.nintendo_path);
	}
	sd_unmount();

	if (error)
		_create_mbox_ums_error(error);
	else
	{
		usbs.type = MMC_SD;
		usbs.partition = EMMC_GPP + 1;
		usbs.ro = usb_msc_emmc_read_only;
		usbs.system_maintenance = &manual_system_maintenance;
		usbs.set_text = &usb_gadget_set_text;
		_create_mbox_ums(&usbs);
	}

	return LV_RES_OK;
}

void nyx_run_ums(void *param)
{
	u32 *cfg = (u32 *)param;

	u8 type = (*cfg) >> 24;
	*cfg = *cfg & (~NYX_CFG_EXTRA);

	// Disable read only flag.
	usb_msc_emmc_read_only = false;

	switch (type)
	{
	case NYX_UMS_SD_CARD:
		action_ums_sd(NULL);
		break;
	case NYX_UMS_EMMC_BOOT0:
		_action_ums_emmc_boot0(NULL);
		break;
	case NYX_UMS_EMMC_BOOT1:
		_action_ums_emmc_boot1(NULL);
		break;
	case NYX_UMS_EMMC_GPP:
		_action_ums_emmc_gpp(NULL);
		break;
	case NYX_UMS_EMUMMC_BOOT0:
		_action_ums_emuemmc_boot0(NULL);
		break;
	case NYX_UMS_EMUMMC_BOOT1:
		_action_ums_emuemmc_boot1(NULL);
		break;
	case NYX_UMS_EMUMMC_GPP:
		_action_ums_emuemmc_gpp(NULL);
		break;
	}
}

static lv_res_t _emmc_read_only_toggle(lv_obj_t *btn)
{
	nyx_generic_onoff_toggle(btn);

	usb_msc_emmc_read_only = lv_btn_get_state(btn) & LV_BTN_STATE_TGL_REL ? 1 : 0;

	return LV_RES_OK;
}

static lv_res_t _create_window_usb_tools(lv_obj_t *parent)
{
	lv_obj_t *win = nyx_create_standard_window(SYMBOL_USB" USB Tools", NULL);

	static lv_style_t h_style;
	lv_style_copy(&h_style, &lv_style_transp);
	h_style.body.padding.inner = 0;
	h_style.body.padding.hor = LV_DPI - (LV_DPI / 4);
	h_style.body.padding.ver = LV_DPI / 9;

	// Create USB Mass Storage container.
	lv_obj_t *h1 = lv_cont_create(win, NULL);
	lv_cont_set_style(h1, &h_style);
	lv_cont_set_fit(h1, false, true);
	lv_obj_set_width(h1, (LV_HOR_RES / 9) * 5);
	lv_obj_set_click(h1, false);
	lv_cont_set_layout(h1, LV_LAYOUT_OFF);

	lv_obj_t *label_sep = lv_label_create(h1, NULL);
	lv_label_set_static_text(label_sep, "");

	lv_obj_t *label_txt = lv_label_create(h1, NULL);
	lv_label_set_static_text(label_txt, "USB Mass Storage");
	lv_obj_set_style(label_txt, lv_theme_get_current()->label.prim);
	lv_obj_align(label_txt, label_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, -LV_DPI * 3 / 10);

	lv_obj_t *line_sep = lv_line_create(h1, NULL);
	static const lv_point_t line_pp[] = { {0, 0}, { LV_HOR_RES - (LV_DPI - (LV_DPI / 4)) * 2, 0} };
	lv_line_set_points(line_sep, line_pp, 2);
	lv_line_set_style(line_sep, lv_theme_get_current()->line.decor);
	lv_obj_align(line_sep, label_txt, LV_ALIGN_OUT_BOTTOM_LEFT, -(LV_DPI / 4), LV_DPI / 8);

	// Create SD UMS button.
	lv_obj_t *btn1 = lv_btn_create(h1, NULL);
	lv_obj_t *label_btn = lv_label_create(btn1, NULL);
	lv_btn_set_fit(btn1, true, true);
	lv_label_set_static_text(label_btn, SYMBOL_SD"  SD Card");

	lv_obj_align(btn1, line_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, LV_DPI / 4);
	lv_btn_set_action(btn1, LV_BTN_ACTION_CLICK, action_ums_sd);

	lv_obj_t *label_txt2 = lv_label_create(h1, NULL);
	lv_label_set_recolor(label_txt2, true);
	lv_label_set_static_text(label_txt2,
		"Allows you to mount the SD Card to a PC/Phone.\n"
		"#C7EA46 All operating systems are supported. Access is# #FF8000 Read/Write.#");

	lv_obj_set_style(label_txt2, &hint_small_style);
	lv_obj_align(label_txt2, btn1, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);

	// Create RAW GPP button.
	lv_obj_t *btn_gpp = lv_btn_create(h1, btn1);
	label_btn = lv_label_create(btn_gpp, NULL);
	lv_label_set_static_text(label_btn, SYMBOL_CHIP"  eMMC RAW GPP");
	lv_obj_align(btn_gpp, label_txt2, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 2);
	lv_btn_set_action(btn_gpp, LV_BTN_ACTION_CLICK, _action_ums_emmc_gpp);

	// Create BOOT0 button.
	lv_obj_t *btn_boot0 = lv_btn_create(h1, btn1);
	label_btn = lv_label_create(btn_boot0, NULL);
	lv_label_set_static_text(label_btn, "BOOT0");
	lv_obj_align(btn_boot0, btn_gpp, LV_ALIGN_OUT_RIGHT_MID, LV_DPI / 10, 0);
	lv_btn_set_action(btn_boot0, LV_BTN_ACTION_CLICK, _action_ums_emmc_boot0);

	// Create BOOT1 button.
	lv_obj_t *btn_boot1 = lv_btn_create(h1, btn1);
	label_btn = lv_label_create(btn_boot1, NULL);
	lv_label_set_static_text(label_btn, "BOOT1");
	lv_obj_align(btn_boot1, btn_boot0, LV_ALIGN_OUT_RIGHT_MID, LV_DPI / 10, 0);
	lv_btn_set_action(btn_boot1, LV_BTN_ACTION_CLICK, _action_ums_emmc_boot1);

	// Create emuMMC RAW GPP button.
	lv_obj_t *btn_emu_gpp = lv_btn_create(h1, btn1);
	label_btn = lv_label_create(btn_emu_gpp, NULL);
	lv_label_set_static_text(label_btn, SYMBOL_MODULES_ALT"  emu RAW GPP");
	lv_obj_align(btn_emu_gpp, btn_gpp, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 2);
	lv_btn_set_action(btn_emu_gpp, LV_BTN_ACTION_CLICK, _action_ums_emuemmc_gpp);

	// Create emuMMC BOOT0 button.
	lv_obj_t *btn_emu_boot0 = lv_btn_create(h1, btn1);
	label_btn = lv_label_create(btn_emu_boot0, NULL);
	lv_label_set_static_text(label_btn, "BOOT0");
	lv_obj_align(btn_emu_boot0, btn_boot0, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 2);
	lv_btn_set_action(btn_emu_boot0, LV_BTN_ACTION_CLICK, _action_ums_emuemmc_boot0);

	// Create emuMMC BOOT1 button.
	lv_obj_t *btn_emu_boot1 = lv_btn_create(h1, btn1);
	label_btn = lv_label_create(btn_emu_boot1, NULL);
	lv_label_set_static_text(label_btn, "BOOT1");
	lv_obj_align(btn_emu_boot1, btn_boot1, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 2);
	lv_btn_set_action(btn_emu_boot1, LV_BTN_ACTION_CLICK, _action_ums_emuemmc_boot1);

	label_txt2 = lv_label_create(h1, NULL);
	lv_label_set_recolor(label_txt2, true);
	lv_label_set_static_text(label_txt2,
		"Allows you to mount the eMMC/emuMMC.\n"
		"#C7EA46 Default access is# #FF8000 read-only.#");
	lv_obj_set_style(label_txt2, &hint_small_style);
	lv_obj_align(label_txt2, btn_emu_gpp, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);

	lv_obj_t *h_write = lv_cont_create(win, NULL);
	lv_cont_set_style(h_write, &h_style);
	lv_cont_set_fit(h_write, false, true);
	lv_obj_set_width(h_write, (LV_HOR_RES / 9) * 2);
	lv_obj_set_click(h_write, false);
	lv_cont_set_layout(h_write, LV_LAYOUT_OFF);
	lv_obj_align(h_write, label_txt2, LV_ALIGN_OUT_RIGHT_MID, LV_DPI / 10, 0);

	// Create read/write access button.
	lv_obj_t *btn_write_access = lv_btn_create(h_write, NULL);
	nyx_create_onoff_button(lv_theme_get_current(), h_write,
		btn_write_access, SYMBOL_EDIT" Read-Only", _emmc_read_only_toggle, false);
	if (!n_cfg.ums_emmc_rw)
		lv_btn_set_state(btn_write_access, LV_BTN_STATE_TGL_REL);
	_emmc_read_only_toggle(btn_write_access);

	// Create USB Input Devices container.
	lv_obj_t *h2 = lv_cont_create(win, NULL);
	lv_cont_set_style(h2, &h_style);
	lv_cont_set_fit(h2, false, true);
	lv_obj_set_width(h2, (LV_HOR_RES / 9) * 3);
	lv_obj_set_click(h2, false);
	lv_cont_set_layout(h2, LV_LAYOUT_OFF);
	lv_obj_align(h2, h1, LV_ALIGN_OUT_RIGHT_TOP, LV_DPI * 17 / 29, 0);

	label_sep = lv_label_create(h2, NULL);
	lv_label_set_static_text(label_sep, "");

	lv_obj_t *label_txt3 = lv_label_create(h2, NULL);
	lv_label_set_static_text(label_txt3, "USB Input Devices");
	lv_obj_set_style(label_txt3, lv_theme_get_current()->label.prim);
	lv_obj_align(label_txt3, label_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, -LV_DPI * 4 / 21);

	line_sep = lv_line_create(h2, line_sep);
	lv_obj_align(line_sep, label_txt3, LV_ALIGN_OUT_BOTTOM_LEFT, -(LV_DPI / 4), LV_DPI / 8);

	// Create Gamepad button.
	lv_obj_t *btn3 = lv_btn_create(h2, NULL);
	label_btn = lv_label_create(btn3, NULL);
	lv_btn_set_fit(btn3, true, true);
	lv_label_set_static_text(label_btn, SYMBOL_CIRCUIT"  Gamepad");
	lv_obj_align(btn3, line_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, LV_DPI / 4);
	lv_btn_set_action(btn3, LV_BTN_ACTION_CLICK, _action_hid_jc);

	lv_obj_t *label_txt4 = lv_label_create(h2, NULL);
	lv_label_set_recolor(label_txt4, true);
	lv_label_set_static_text(label_txt4,
		"Plug-in the Joy-Con and convert the device\n"
		"into a gamepad for PC or Phone.\n"
		"#C7EA46 Needs both Joy-Con in order to function.#");

	lv_obj_set_style(label_txt4, &hint_small_style);
	lv_obj_align(label_txt4, btn3, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);
/*
	// Create Touchpad button.
	lv_obj_t *btn4 = lv_btn_create(h2, btn1);
	label_btn = lv_label_create(btn4, NULL);
	lv_label_set_static_text(label_btn, SYMBOL_KEYBOARD"  Touchpad");
	lv_obj_align(btn4, label_txt4, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 2);
	lv_btn_set_action(btn4, LV_BTN_ACTION_CLICK, _action_hid_touch);
	lv_btn_set_state(btn4, LV_BTN_STATE_INA);

	label_txt4 = lv_label_create(h2, NULL);
	lv_label_set_recolor(label_txt4, true);
	lv_label_set_static_text(label_txt4,
		"Control the PC via the device\'s touchscreen.\n"
		"#C7EA46 Two fingers tap acts like a# #FF8000 Right click##C7EA46 .#\n");
	lv_obj_set_style(label_txt4, &hint_small_style);
	lv_obj_align(label_txt4, btn4, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);
*/
	return LV_RES_OK;
}

static int _fix_attributes(lv_obj_t *lb_val, char *path, u32 *total)
{
	FRESULT res;
	DIR dir;
	u32 dirLength = 0;
	static FILINFO fno;

	// Open directory.
	res = f_opendir(&dir, path);
	if (res != FR_OK)
		return res;

	dirLength = strlen(path);

	// Hard limit path to 1024 characters. Do not result to error.
	if (dirLength > 1024)
	{
		total[2]++;
		goto out;
	}

	for (;;)
	{
		// Clear file or folder path.
		path[dirLength] = 0;

		// Read a directory item.
		res = f_readdir(&dir, &fno);

		// Break on error or end of dir.
		if (res != FR_OK || fno.fname[0] == 0)
			break;

		// Set new directory or file.
		memcpy(&path[dirLength], "/", 1);
		strcpy(&path[dirLength + 1], fno.fname);

		// Is it a directory?
		if (fno.fattrib & AM_DIR)
		{
			// Check if it's a HOS single file folder.
			strcat(path, "/00");
			bool is_hos_special = !f_stat(path, NULL);
			path[strlen(path) - 3] = 0;

			// Set archive bit to HOS single file folders.
			if (is_hos_special)
			{
				if (!(fno.fattrib & AM_ARC))
				{
					if (!f_chmod(path, AM_ARC, AM_ARC))
						total[0]++;
					else
						total[3]++;
				}
			}
			else if (fno.fattrib & AM_ARC) // If not, clear the archive bit.
			{
				if (!f_chmod(path, 0, AM_ARC))
					total[1]++;
				else
					total[3]++;
			}

			lv_label_set_text(lb_val, path);
			manual_system_maintenance(true);

			// Enter the directory.
			res = _fix_attributes(lb_val, path, total);
			if (res != FR_OK)
				break;
		}
	}

out:
	f_closedir(&dir);

	return res;
}

static lv_res_t _create_window_unset_abit_tool(lv_obj_t *btn)
{
	lv_obj_t *win = nyx_create_standard_window(SYMBOL_COPY" Fix Archive Bit (All folders)", NULL);

	// Disable buttons.
	nyx_window_toggle_buttons(win, true);

	lv_obj_t *desc = lv_cont_create(win, NULL);
	lv_obj_set_size(desc, LV_HOR_RES * 10 / 11, LV_VER_RES - (LV_DPI * 11 / 7) * 4);

	lv_obj_t * lb_desc = lv_label_create(desc, NULL);
	lv_label_set_long_mode(lb_desc, LV_LABEL_LONG_BREAK);
	lv_label_set_recolor(lb_desc, true);

	if (sd_mount())
	{
		lv_label_set_text(lb_desc, "#FFDD00 Failed to init SD!#");
		lv_obj_set_width(lb_desc, lv_obj_get_width(desc));
	}
	else
	{
		lv_label_set_text(lb_desc, "#00DDFF Traversing all SD card files!#\nThis may take some time...");
		lv_obj_set_width(lb_desc, lv_obj_get_width(desc));

		lv_obj_t *val = lv_cont_create(win, NULL);
		lv_obj_set_size(val, LV_HOR_RES * 10 / 11, LV_VER_RES - (LV_DPI * 11 / 7) * 4);

		lv_obj_t * lb_val = lv_label_create(val, lb_desc);

		char *path = malloc(0x1000);
		path[0] = 0;

		lv_label_set_text(lb_val, "");
		lv_obj_set_width(lb_val, lv_obj_get_width(val));
		lv_obj_align(val, desc, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);

		u32 total[4] = { 0 };
		_fix_attributes(lb_val, path, total);

		sd_unmount();

		lv_obj_t *desc2 = lv_cont_create(win, NULL);
		lv_obj_set_size(desc2, LV_HOR_RES * 10 / 11, LV_VER_RES - (LV_DPI * 11 / 7) * 4);
		lv_obj_t * lb_desc2 = lv_label_create(desc2, lb_desc);

		char *txt_buf = (char *)malloc(0x500);

		if (!total[0] && !total[1])
			s_printf(txt_buf, "#96FF00 Done! No change was needed.#");
		else
			s_printf(txt_buf, "#96FF00 Done! Archive bits fixed:# #FF8000 %d unset and %d set!#", total[1], total[0]);

		// Check errors.
		if (total[2] || total[3])
		{
			s_printf(txt_buf, "\n\n#FFDD00 Errors: folder accesses: %d, arc bit fixes: %d!#\n"
					          "#FFDD00 Filesystem should be checked for errors.#",
					          total[2], total[3]);
		}

		lv_label_set_text(lb_desc2, txt_buf);
		lv_obj_set_width(lb_desc2, lv_obj_get_width(desc2));
		lv_obj_align(desc2, val, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 0);

		free(path);
	}

	// Enable buttons.
	nyx_window_toggle_buttons(win, false);

	return LV_RES_OK;
}

static lv_res_t _create_mbox_fix_touchscreen(lv_obj_t *btn)
{
	int res = 1;
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char *mbox_btn_map[] = { "\251", "\222OK", "\251", "" };
	lv_obj_t * mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);

	char *txt_buf = malloc(SZ_16K);
	strcpy(txt_buf, "#FF8000 Don't touch the screen!#\n\nThe tuning process will start in ");
	u32 text_idx = strlen(txt_buf);
	lv_mbox_set_text(mbox, txt_buf);

	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);

	lv_mbox_set_text(mbox,
		"#FFDD00 Warning: Only run this if you really have issues!#\n\n"
		"Press #FF8000 POWER# to Continue.\nPress #FF8000 VOL# to abort.");
	manual_system_maintenance(true);

	if (!(btn_wait() & BTN_POWER))
		goto out;

	manual_system_maintenance(true);
	lv_mbox_set_text(mbox, txt_buf);

	u32 seconds = 5;
	while (seconds)
	{
		s_printf(txt_buf + text_idx, "%d seconds...", seconds);
		lv_mbox_set_text(mbox, txt_buf);
		manual_system_maintenance(true);
		msleep(1000);
		seconds--;
	}

	u8 err[2];
	if (touch_panel_ito_test(err))
		goto ito_failed;

	if (!err[0] && !err[1])
	{
		res = touch_execute_autotune();
		if (!res)
			goto out;
	}
	else
	{
		touch_sense_enable();

		s_printf(txt_buf, "#FFFF00 ITO Test: ");
		switch (err[0])
		{
		case ITO_FORCE_OPEN:
			strcat(txt_buf, "Force Open");
			break;
		case ITO_SENSE_OPEN:
			strcat(txt_buf, "Sense Open");
			break;
		case ITO_FORCE_SHRT_GND:
			strcat(txt_buf, "Force Short to GND");
			break;
		case ITO_SENSE_SHRT_GND:
			strcat(txt_buf, "Sense Short to GND");
			break;
		case ITO_FORCE_SHRT_VCM:
			strcat(txt_buf, "Force Short to VDD");
			break;
		case ITO_SENSE_SHRT_VCM:
			strcat(txt_buf, "Sense Short to VDD");
			break;
		case ITO_FORCE_SHRT_FORCE:
			strcat(txt_buf, "Force Short to Force");
			break;
		case ITO_SENSE_SHRT_SENSE:
			strcat(txt_buf, "Sense Short to Sense");
			break;
		case ITO_F2E_SENSE:
			strcat(txt_buf, "Force Short to Sense");
			break;
		case ITO_FPC_FORCE_OPEN:
			strcat(txt_buf, "FPC Force Open");
			break;
		case ITO_FPC_SENSE_OPEN:
			strcat(txt_buf, "FPC Sense Open");
			break;
		default:
			strcat(txt_buf, "Unknown");
			break;

		}
		s_printf(txt_buf + strlen(txt_buf), " (%d), Chn: %d#\n\n", err[0], err[1]);
		strcat(txt_buf, "#FFFF00 The touchscreen calibration failed!");
		lv_mbox_set_text(mbox, txt_buf);
		goto out2;
	}

ito_failed:
	touch_sense_enable();

out:
	if (!res)
		lv_mbox_set_text(mbox, "#C7EA46 The touchscreen calibration finished!");
	else
		lv_mbox_set_text(mbox, "#FFFF00 The touchscreen calibration failed!");

out2:
	lv_mbox_add_btns(mbox, mbox_btn_map, nyx_mbox_action);

	free(txt_buf);

	return LV_RES_OK;
}

static lv_res_t _create_window_dump_pk12_tool(lv_obj_t *btn)
{
	lv_obj_t *win = nyx_create_standard_window(SYMBOL_MODULES" Dump package1/2", NULL);

	// Disable buttons.
	nyx_window_toggle_buttons(win, true);

	lv_obj_t *desc = lv_cont_create(win, NULL);
	lv_obj_set_size(desc, LV_HOR_RES * 10 / 11, LV_VER_RES - (LV_DPI * 12 / 7));

	lv_obj_t *lb_desc = lv_label_create(desc, NULL);
	lv_obj_set_style(lb_desc, &monospace_text);
	lv_label_set_long_mode(lb_desc, LV_LABEL_LONG_BREAK);
	lv_label_set_recolor(lb_desc, true);
	lv_obj_set_width(lb_desc, lv_obj_get_width(desc) / 2);

	lv_obj_t *lb_desc2 = lv_label_create(desc, NULL);
	lv_obj_set_style(lb_desc2, &monospace_text);
	lv_label_set_long_mode(lb_desc2, LV_LABEL_LONG_BREAK);
	lv_label_set_recolor(lb_desc2, true);
	lv_obj_set_width(lb_desc2, lv_obj_get_width(desc) / 2);
	lv_label_set_text(lb_desc2, " ");

	lv_obj_align(lb_desc2, lb_desc, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);

	if (sd_mount())
	{
		lv_label_set_text(lb_desc, "#FFDD00 Failed to init SD!#");

		goto out_end;
	}

	char path[128];

	u8 mkey = 0;
	u8 *pkg1 = (u8 *)zalloc(SZ_256K);
	u8 *warmboot = (u8 *)zalloc(SZ_256K);
	u8 *secmon = (u8 *)zalloc(SZ_256K);
	u8 *loader = (u8 *)zalloc(SZ_256K);
	u8 *pkg2   = (u8 *)zalloc(SZ_8M);

	char *txt_buf  = (char *)malloc(SZ_16K);

	if (emmc_initialize(false))
	{
		lv_label_set_text(lb_desc, "#FFDD00 Failed to init eMMC!#");

		goto out_free;
	}

	char *bct_paths[2] = {
		"/pkg/main",
		"/pkg/safe"
	};

	char *pkg1_paths[2] = {
		"/pkg/main/pkg1",
		"/pkg/safe/pkg1"
	};

	char *pkg2_partitions[2] = {
		"BCPKG2-1-Normal-Main",
		"BCPKG2-3-SafeMode-Main"
	};

	char *pkg2_paths[2] = {
		"/pkg/main/pkg2",
		"/pkg/safe/pkg2"
	};

	char *pkg2ini_paths[2] = {
		"/pkg/main/pkg2/ini",
		"/pkg/safe/pkg2/ini"
	};

	// Create main directories.
	emmcsn_path_impl(path, "/pkg", "", &emmc_storage);
	emmcsn_path_impl(path, "/pkg/main", "", &emmc_storage);
	emmcsn_path_impl(path, "/pkg/safe", "", &emmc_storage);

	// Parse eMMC GPT.
	emmc_set_partition(EMMC_GPP);
	LIST_INIT(gpt);
	emmc_gpt_parse(&gpt);

	lv_obj_t *lb_log = lb_desc;
	for (u32 idx = 0; idx < 2; idx++)
	{
		if (idx)
			lb_log = lb_desc2;

		// Read package1.
		char *build_date = malloc(32);
		u32   pk1_offset = h_cfg.t210b01 ? sizeof(bl_hdr_t210b01_t) : 0; // Skip T210B01 OEM header.
		emmc_set_partition(!idx ? EMMC_BOOT0 : EMMC_BOOT1);
		sdmmc_storage_read(&emmc_storage,
			!idx ? PKG1_BOOTLOADER_MAIN_OFFSET : PKG1_BOOTLOADER_SAFE_OFFSET, PKG1_BOOTLOADER_SIZE / EMMC_BLOCKSIZE, pkg1);

		const pkg1_id_t *pkg1_id = pkg1_identify(pkg1 + pk1_offset, build_date);

		s_printf(txt_buf, "#00DDFF Found %s pkg1 ('%s')#\n\n", !idx ? "Main" : "Safe", build_date);
		lv_label_set_text(lb_log, txt_buf);
		manual_system_maintenance(true);
		free(build_date);

		// Dump package1 in its encrypted state.
		emmcsn_path_impl(path, pkg1_paths[idx], "pkg1_enc.bin", &emmc_storage);
		bool res = sd_save_to_file(pkg1, PKG1_BOOTLOADER_SIZE, path);

		// Exit if unknown.
		if (!pkg1_id)
		{
			strcat(txt_buf, "#FFDD00 Unknown pkg1 version!#");
			lv_label_set_text(lb_log, txt_buf);
			manual_system_maintenance(true);

			if (!res)
			{
				strcat(txt_buf, "\nEncrypted pkg1 extracted to pkg1_enc.bin");
				lv_label_set_text(lb_log, txt_buf);
				manual_system_maintenance(true);
			}

			goto out;
		}

		mkey = pkg1_id->mkey;

		tsec_ctxt_t tsec_ctxt = {0};
		tsec_ctxt.fw = (void *)(pkg1 + pkg1_id->tsec_off);
		tsec_ctxt.pkg1 = (void *)pkg1;
		tsec_ctxt.pkg11_off = pkg1_id->pkg11_off;

		// Read the correct eks for older HOS versions.
		const u32 eks_size = sizeof(pkg1_eks_t);
		pkg1_eks_t *eks = (pkg1_eks_t *)malloc(eks_size);
		emmc_set_partition(EMMC_BOOT0);
		sdmmc_storage_read(&emmc_storage, PKG1_HOS_EKS_OFFSET + (mkey * eks_size) / EMMC_BLOCKSIZE,
										  eks_size / EMMC_BLOCKSIZE, eks);

		// Generate keys.
		hos_keygen(eks, mkey, &tsec_ctxt);
		free(eks);

		// Decrypt.
		if (h_cfg.t210b01 || mkey <= HOS_MKEY_VER_600)
		{
			if (!pkg1_decrypt(pkg1_id, pkg1))
			{
				strcat(txt_buf, "#FFDD00 Pkg1 decryption failed!#\n");
				if (h_cfg.t210b01)
					strcat(txt_buf, "#FFDD00 Is BEK missing?#\n");
				lv_label_set_text(lb_log, txt_buf);
				goto out;
			}
		}

		// Dump the BCTs from blocks 2/3 (backup) which are normally valid.
		static const u32 BCT_SIZE = 0x2800;
		static const u32 BLK_SIZE = SZ_16K / EMMC_BLOCKSIZE;
		u8 *bct = (u8 *)zalloc(BCT_SIZE);
		sdmmc_storage_read(&emmc_storage, BLK_SIZE * 2 + BLK_SIZE * idx, BCT_SIZE / EMMC_BLOCKSIZE, bct);
		emmcsn_path_impl(path, bct_paths[idx], "bct.bin", &emmc_storage);
		if (sd_save_to_file(bct, 0x2800, path))
			goto out;
		if (h_cfg.t210b01)
		{
			se_aes_iv_clear(13);
			se_aes_crypt_cbc(13, DECRYPT, bct + 0x480, bct + 0x480, BCT_SIZE - 0x480);
			emmcsn_path_impl(path, bct_paths[idx], "bct_decr.bin", &emmc_storage);
			if (sd_save_to_file(bct, 0x2800, path))
				goto out;
		}

		// Dump package1.1 contents.
		if (h_cfg.t210b01 || mkey <= HOS_MKEY_VER_620)
		{
			pkg1_unpack(warmboot, secmon, loader, pkg1_id, pkg1 + pk1_offset);
			pk11_hdr_t *hdr_pk11 = (pk11_hdr_t *)(pkg1 + pk1_offset + pkg1_id->pkg11_off + 0x20);

			// Display info.
			s_printf(txt_buf + strlen(txt_buf),
				"#C7EA46 NX Bootloader size:  #0x%05X\n"
				"#C7EA46 Secure monitor size: #0x%05X\n"
				"#C7EA46 Warmboot size:       #0x%05X\n\n",
				hdr_pk11->ldr_size, hdr_pk11->sm_size, hdr_pk11->wb_size);

			lv_label_set_text(lb_log, txt_buf);
			manual_system_maintenance(true);

			// Dump package1.1.
			emmcsn_path_impl(path, pkg1_paths[idx], "pkg1_decr.bin", &emmc_storage);
			if (sd_save_to_file(pkg1, SZ_256K, path))
				goto out;
			strcat(txt_buf, "Package1 extracted to pkg1_decr.bin\n");
			lv_label_set_text(lb_log, txt_buf);
			manual_system_maintenance(true);

			// Dump nxbootloader.
			emmcsn_path_impl(path, pkg1_paths[idx], "nxloader.bin", &emmc_storage);
			if (sd_save_to_file(loader, hdr_pk11->ldr_size, path))
				goto out;
			strcat(txt_buf, "NX Bootloader extracted to nxloader.bin\n");
			lv_label_set_text(lb_log, txt_buf);
			manual_system_maintenance(true);

			// Dump secmon.
			emmcsn_path_impl(path, pkg1_paths[idx], "secmon.bin", &emmc_storage);
			if (sd_save_to_file(secmon, hdr_pk11->sm_size, path))
				goto out;
			strcat(txt_buf, "Secure Monitor extracted to secmon.bin\n");
			lv_label_set_text(lb_log, txt_buf);
			manual_system_maintenance(true);

			// Dump warmboot.
			emmcsn_path_impl(path, pkg1_paths[idx], "warmboot.bin", &emmc_storage);
			if (sd_save_to_file(warmboot, hdr_pk11->wb_size, path))
				goto out;
			// If T210B01, save a copy of decrypted warmboot binary also.
			if (h_cfg.t210b01)
			{

				se_aes_iv_clear(13);
				se_aes_crypt_cbc(13, DECRYPT, warmboot + 0x330, warmboot + 0x330, hdr_pk11->wb_size - 0x330);
				emmcsn_path_impl(path, pkg1_paths[idx], "warmboot_dec.bin", &emmc_storage);
				if (sd_save_to_file(warmboot, hdr_pk11->wb_size, path))
					goto out;
			}
			strcat(txt_buf, "Warmboot extracted to warmboot.bin\n\n");
			lv_label_set_text(lb_log, txt_buf);
			manual_system_maintenance(true);
		}

		// Find and dump package2 partition.
		emmc_set_partition(EMMC_GPP);
		emmc_part_t *pkg2_part = emmc_part_find(&gpt, pkg2_partitions[idx]);
		if (!pkg2_part)
			goto out;

		// Read in package2 header and get package2 real size.
		static const u32 PKG2_OFFSET = 0x4000;
		u8 *tmp = (u8 *)malloc(EMMC_BLOCKSIZE);
		emmc_part_read(pkg2_part, PKG2_OFFSET / EMMC_BLOCKSIZE, 1, tmp);
		u32 *hdr_pkg2_raw = (u32 *)(tmp + 0x100);
		u32 pkg2_size = hdr_pkg2_raw[0] ^ hdr_pkg2_raw[2] ^ hdr_pkg2_raw[3];
		free(tmp);

		// Read in package2.
		u32 pkg2_size_aligned = ALIGN(pkg2_size, EMMC_BLOCKSIZE);
		emmc_part_read(pkg2_part, PKG2_OFFSET / EMMC_BLOCKSIZE, pkg2_size_aligned / EMMC_BLOCKSIZE, pkg2);

		// Dump encrypted package2.
		emmcsn_path_impl(path, pkg2_paths[idx], "pkg2_encr.bin", &emmc_storage);
		res = sd_save_to_file(pkg2, pkg2_size, path);

		// Decrypt package2 and parse KIP1 blobs in INI1 section.
		pkg2_hdr_t *pkg2_hdr = pkg2_decrypt(pkg2, mkey);
		if (!pkg2_hdr)
		{
			strcat(txt_buf, "#FFDD00 Pkg2 decryption failed!#");
			lv_label_set_text(lb_log, txt_buf);
			manual_system_maintenance(true);

			if (!res)
			{
				strcat(txt_buf, "\npkg2 encrypted extracted to pkg2_encr.bin\n");
				lv_label_set_text(lb_log, txt_buf);
				manual_system_maintenance(true);
			}

			// Clear EKS slot, in case something went wrong with tsec keygen.
			hos_eks_clear(mkey);

			goto out;
		}

		// Display info.
		s_printf(txt_buf + strlen(txt_buf),
			"#C7EA46 Kernel size:   #0x%06X\n"
			"#C7EA46 INI1 size:     #0x%06X\n\n",
			pkg2_hdr->sec_size[PKG2_SEC_KERNEL], pkg2_hdr->sec_size[PKG2_SEC_INI1]);

		lv_label_set_text(lb_log, txt_buf);
		manual_system_maintenance(true);

		// Dump pkg2.1.
		emmcsn_path_impl(path, pkg2_paths[idx], "pkg2_decr.bin", &emmc_storage);
		if (sd_save_to_file(pkg2, pkg2_hdr->sec_size[PKG2_SEC_KERNEL] + pkg2_hdr->sec_size[PKG2_SEC_INI1], path))
			goto out;
		strcat(txt_buf, "Package2 extracted to pkg2_decr.bin\n");
		lv_label_set_text(lb_log, txt_buf);
		manual_system_maintenance(true);

		// Dump kernel.
		emmcsn_path_impl(path, pkg2_paths[idx], "kernel.bin", &emmc_storage);
		if (sd_save_to_file(pkg2_hdr->data, pkg2_hdr->sec_size[PKG2_SEC_KERNEL], path))
			goto out;
		strcat(txt_buf, "Kernel extracted to kernel.bin\n");
		lv_label_set_text(lb_log, txt_buf);
		manual_system_maintenance(true);

		// Dump INI1.
		u32 ini1_off  = pkg2_hdr->sec_size[PKG2_SEC_KERNEL];
		u32 ini1_size = pkg2_hdr->sec_size[PKG2_SEC_INI1];
		if (!ini1_size)
		{
			pkg2_get_newkern_info(pkg2_hdr->data);
			ini1_off  = pkg2_newkern_ini1_start;
			ini1_size = pkg2_newkern_ini1_end - pkg2_newkern_ini1_start;
		}

		if (!ini1_off)
		{
			strcat(txt_buf, "#FFDD00 Failed to dump INI1 and kips!#\n");
			goto out;
		}

		pkg2_ini1_t *ini1 = (pkg2_ini1_t *)(pkg2_hdr->data + ini1_off);
		emmcsn_path_impl(path, pkg2_paths[idx], "ini1.bin", &emmc_storage);
		if (sd_save_to_file(ini1, ini1_size, path))
			goto out;

		strcat(txt_buf, "INI1 extracted to ini1.bin\n");
		lv_label_set_text(lb_log, txt_buf);
		manual_system_maintenance(true);

		char filename[32];
		u8 *ptr = (u8 *)ini1;
		ptr += sizeof(pkg2_ini1_t);

		// Dump all kips.
		for (u32 i = 0; i < ini1->num_procs; i++)
		{
			pkg2_kip1_t *kip1 = (pkg2_kip1_t *)ptr;
			u32 kip1_size = pkg2_calc_kip1_size(kip1);
			char *kip_name = kip1->name;

			// Check if FS supports exFAT.
			if (!strcmp("FS", kip_name))
			{
				u8 *ro_data = malloc(SZ_4M);
				u32 offset      = (kip1->flags & BIT(KIP_TEXT)) ? kip1->sections[KIP_TEXT].size_comp :
																  kip1->sections[KIP_TEXT].size_decomp;
				u32 size_comp   = kip1->sections[KIP_RODATA].size_comp;
				u32 size_decomp = kip1->sections[KIP_RODATA].size_decomp;
				if (kip1->flags & BIT(KIP_RODATA))
					blz_uncompress_srcdest(&kip1->data[offset], size_comp, ro_data, size_decomp);
				else
					memcpy(ro_data, &kip1->data[offset], size_decomp);

				for (u32 i = 0; i < 0x100; i+= sizeof(u32))
				{
					// Check size and name of nss matches.
					if (*(u32 *)&ro_data[i] == 8 && !memcmp("fs.exfat", &ro_data[i + 4], 8))
					{
						kip_name = "FS_exfat";
						break;
					}
				}

				free(ro_data);
			}

			s_printf(filename, "%s.kip1", kip_name);
			emmcsn_path_impl(path, pkg2ini_paths[idx], filename, &emmc_storage);
			if (sd_save_to_file(kip1, kip1_size, path))
				goto out;

			s_printf(txt_buf + strlen(txt_buf), "- Extracted %s.kip1\n", kip_name);
			lv_label_set_text(lb_log, txt_buf);
			manual_system_maintenance(true);

			ptr += kip1_size;
		}
	}

out:
	emmc_gpt_free(&gpt);
out_free:
	free(pkg1);
	free(secmon);
	free(warmboot);
	free(loader);
	free(pkg2);
	free(txt_buf);
	emmc_end();
	sd_unmount();

	if (mkey >= HOS_MKEY_VER_620)
		se_aes_key_clear(8);
out_end:
	// Enable buttons.
	nyx_window_toggle_buttons(win, false);

	return LV_RES_OK;
}

static void _create_tab_tools_emmc_sd_usb(lv_theme_t *th, lv_obj_t *parent)
{
	lv_page_set_scrl_layout(parent, LV_LAYOUT_PRETTY);

	// Create Backup & Restore container.
	lv_obj_t *h1 = _create_container(parent);

	lv_obj_t *label_sep = lv_label_create(h1, NULL);
	lv_label_set_static_text(label_sep, "");

	lv_obj_t *label_txt = lv_label_create(h1, NULL);
	lv_label_set_static_text(label_txt, "Backup & Restore");
	lv_obj_set_style(label_txt, th->label.prim);
	lv_obj_align(label_txt, label_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, -LV_DPI * 3 / 10);

	lv_obj_t *line_sep = lv_line_create(h1, NULL);
	static const lv_point_t line_pp[] = { {0, 0}, { LV_HOR_RES - (LV_DPI - (LV_DPI / 4)) * 2, 0} };
	lv_line_set_points(line_sep, line_pp, 2);
	lv_line_set_style(line_sep, th->line.decor);
	lv_obj_align(line_sep, label_txt, LV_ALIGN_OUT_BOTTOM_LEFT, -(LV_DPI / 4), LV_DPI / 8);

	// Create Backup eMMC button.
	lv_obj_t *btn = lv_btn_create(h1, NULL);
	if (hekate_bg)
	{
		lv_btn_set_style(btn, LV_BTN_STYLE_REL, &btn_transp_rel);
		lv_btn_set_style(btn, LV_BTN_STYLE_PR, &btn_transp_pr);
	}
	lv_obj_t *label_btn = lv_label_create(btn, NULL);
	lv_btn_set_fit(btn, true, true);
	lv_label_set_static_text(label_btn, SYMBOL_UPLOAD"  Backup eMMC");
	lv_obj_align(btn, line_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, LV_DPI / 4);
	lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, create_window_backup_restore_tool);

	lv_obj_t *label_txt2 = lv_label_create(h1, NULL);
	lv_label_set_recolor(label_txt2, true);
	lv_label_set_static_text(label_txt2,
		"Allows you to backup eMMC/emuMMC partitions individually\n"
		"or as a whole raw image to the SD card.\n"
		"#C7EA46 Supports SD cards from# #FF8000 4GB# #C7EA46 and up. #"
		"#FF8000 FAT32# #C7EA46 and ##FF8000 exFAT##C7EA46 .#");
	lv_obj_set_style(label_txt2, &hint_small_style);
	lv_obj_align(label_txt2, btn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);

	// Create Restore eMMC button.
	lv_obj_t *btn2 = lv_btn_create(h1, btn);
	label_btn = lv_label_create(btn2, NULL);
	lv_label_set_static_text(label_btn, SYMBOL_DOWNLOAD"  Restore eMMC");
	lv_obj_align(btn2, label_txt2, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 2);
	lv_btn_set_action(btn2, LV_BTN_ACTION_CLICK, create_window_backup_restore_tool);

	label_txt2 = lv_label_create(h1, NULL);
	lv_label_set_recolor(label_txt2, true);
	lv_label_set_static_text(label_txt2,
		"Allows you to restore eMMC/emuMMC partitions individually\n"
		"or as a whole raw image from the SD card.\n"
		"#C7EA46 Supports SD cards from# #FF8000 4GB# #C7EA46 and up. #"
		"#FF8000 FAT32# #C7EA46 and ##FF8000 exFAT##C7EA46 .#");
	lv_obj_set_style(label_txt2, &hint_small_style);
	lv_obj_align(label_txt2, btn2, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);

	// Create Misc container.
	lv_obj_t *h2 = _create_container(parent);
	lv_obj_align(h2, h1, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);

	label_sep = lv_label_create(h2, NULL);
	lv_label_set_static_text(label_sep, "");

	lv_obj_t *label_txt3 = lv_label_create(h2, NULL);
	lv_label_set_static_text(label_txt3, "SD Partitions & USB");
	lv_obj_set_style(label_txt3, th->label.prim);
	lv_obj_align(label_txt3, label_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, -LV_DPI * 3 / 10);

	line_sep = lv_line_create(h2, line_sep);
	lv_obj_align(line_sep, label_txt3, LV_ALIGN_OUT_BOTTOM_LEFT, -(LV_DPI / 4), LV_DPI / 8);

	// Create Partition SD Card button.
	lv_obj_t *btn3 = lv_btn_create(h2, NULL);
	if (hekate_bg)
	{
		lv_btn_set_style(btn3, LV_BTN_STYLE_REL, &btn_transp_rel);
		lv_btn_set_style(btn3, LV_BTN_STYLE_PR, &btn_transp_pr);
	}
	label_btn = lv_label_create(btn3, NULL);
	lv_btn_set_fit(btn3, true, true);
	lv_label_set_static_text(label_btn, SYMBOL_SD"  Partition SD Card");
	lv_obj_align(btn3, line_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, LV_DPI / 4);
	lv_btn_set_action(btn3, LV_BTN_ACTION_CLICK, create_window_sd_partition_manager);
	lv_btn_set_action(btn3, LV_BTN_ACTION_LONG_PR, create_window_emmc_partition_manager);

	lv_obj_t *label_txt4 = lv_label_create(h2, NULL);
	lv_label_set_recolor(label_txt4, true);
	lv_label_set_static_text(label_txt4,
		"Allows you to partition the SD Card for using it with #C7EA46 emuMMC#,\n"
		"#C7EA46 Android# and #C7EA46 Linux#. You can also flash Linux and Android.\n");
	lv_obj_set_style(label_txt4, &hint_small_style);
	lv_obj_align(label_txt4, btn3, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);

	// Create USB Tools button.
	lv_obj_t *btn4 = lv_btn_create(h2, btn3);
	label_btn = lv_label_create(btn4, NULL);
	lv_label_set_static_text(label_btn, SYMBOL_USB"  USB Tools");
	lv_obj_align(btn4, label_txt4, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 2);
	lv_btn_set_action(btn4, LV_BTN_ACTION_CLICK, _create_window_usb_tools);

	label_txt4 = lv_label_create(h2, NULL);
	lv_label_set_recolor(label_txt4, true);
	lv_label_set_static_text(label_txt4,
		"#C7EA46 USB mass storage#, #C7EA46 gamepad# and other USB tools.\n"
		"Mass storage can mount SD, eMMC and emuMMC. The\n"
		"gamepad transforms the Switch into an input device.#");
	lv_obj_set_style(label_txt4, &hint_small_style);
	lv_obj_align(label_txt4, btn4, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);
}

static void _create_tab_tools_arc_rcm_pkg12(lv_theme_t *th, lv_obj_t *parent)
{
	lv_page_set_scrl_layout(parent, LV_LAYOUT_PRETTY);

	// Create Misc container.
	lv_obj_t *h1 = _create_container(parent);

	lv_obj_t *label_sep = lv_label_create(h1, NULL);
	lv_label_set_static_text(label_sep, "");

	lv_obj_t *label_txt = lv_label_create(h1, NULL);
	lv_label_set_static_text(label_txt, "Misc");
	lv_obj_set_style(label_txt, th->label.prim);
	lv_obj_align(label_txt, label_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, -LV_DPI * 3 / 10);

	lv_obj_t *line_sep = lv_line_create(h1, NULL);
	static const lv_point_t line_pp[] = { {0, 0}, { LV_HOR_RES - (LV_DPI - (LV_DPI / 4)) * 2, 0} };
	lv_line_set_points(line_sep, line_pp, 2);
	lv_line_set_style(line_sep, th->line.decor);
	lv_obj_align(line_sep, label_txt, LV_ALIGN_OUT_BOTTOM_LEFT, -(LV_DPI / 4), LV_DPI / 8);

	// Create fix archive bit button.
	lv_obj_t *btn = lv_btn_create(h1, NULL);
	if (hekate_bg)
	{
		lv_btn_set_style(btn, LV_BTN_STYLE_REL, &btn_transp_rel);
		lv_btn_set_style(btn, LV_BTN_STYLE_PR, &btn_transp_pr);
	}
	lv_obj_t *label_btn = lv_label_create(btn, NULL);
	lv_btn_set_fit(btn, true, true);
	lv_label_set_static_text(label_btn, SYMBOL_DIRECTORY"  Fix Archive Bit");
	lv_obj_align(btn, line_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, LV_DPI / 4);
	lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, _create_window_unset_abit_tool);

	lv_obj_t *label_txt2 = lv_label_create(h1, NULL);
	lv_label_set_recolor(label_txt2, true);
	lv_label_set_static_text(label_txt2,
		"Allows you to fix the archive bit for all folders including the\n"
		"root and emuMMC \'Nintendo\' folders.\n"
		"#C7EA46 It sets the archive bit to folders named with ##FF8000 .[ext]#\n"
		"#FF8000 Use that option when you have corruption messages.#");
	lv_obj_set_style(label_txt2, &hint_small_style);
	lv_obj_align(label_txt2, btn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);

	// Create Fix touch calibration button.
	lv_obj_t *btn2 = lv_btn_create(h1, btn);
	label_btn = lv_label_create(btn2, NULL);
	lv_label_set_static_text(label_btn, SYMBOL_KEYBOARD"  Calibrate Touchscreen");
	lv_obj_align(btn2, label_txt2, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 2);
	lv_btn_set_action(btn2, LV_BTN_ACTION_CLICK, _create_mbox_fix_touchscreen);

	label_txt2 = lv_label_create(h1, NULL);
	lv_label_set_recolor(label_txt2, true);
	lv_label_set_static_text(label_txt2,
		"Allows you to calibrate the touchscreen module.\n"
		"#FF8000 This can fix any issues with touchscreen in Nyx and HOS.#");
	lv_obj_set_style(label_txt2, &hint_small_style);
	lv_obj_align(label_txt2, btn2, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);

	// Create Others container.
	lv_obj_t *h2 = _create_container(parent);
	lv_obj_align(h2, h1, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);

	label_sep = lv_label_create(h2, NULL);
	lv_label_set_static_text(label_sep, "");

	lv_obj_t *label_txt3 = lv_label_create(h2, NULL);
	lv_label_set_static_text(label_txt3, "Others");
	lv_obj_set_style(label_txt3, th->label.prim);
	lv_obj_align(label_txt3, label_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, -LV_DPI * 3 / 10);

	line_sep = lv_line_create(h2, line_sep);
	lv_obj_align(line_sep, label_txt3, LV_ALIGN_OUT_BOTTOM_LEFT, -(LV_DPI / 4), LV_DPI / 8);

	// Create AutoRCM On/Off button.
	lv_obj_t *btn3 = lv_btn_create(h2, NULL);
	if (hekate_bg)
	{
		lv_btn_set_style(btn3, LV_BTN_STYLE_REL,     &btn_transp_rel);
		lv_btn_set_style(btn3, LV_BTN_STYLE_PR,      &btn_transp_pr);
		lv_btn_set_style(btn3, LV_BTN_STYLE_TGL_REL, &btn_transp_tgl_rel);
		lv_btn_set_style(btn3, LV_BTN_STYLE_TGL_PR,  &btn_transp_tgl_pr);
		lv_btn_set_style(btn3, LV_BTN_STYLE_INA,     &btn_transp_ina);
	}
	label_btn = lv_label_create(btn3, NULL);
	lv_btn_set_fit(btn3, true, true);
	lv_label_set_recolor(label_btn, true);
	lv_label_set_text(label_btn, SYMBOL_REFRESH"  AutoRCM #00FFC9   ON #");
	lv_obj_align(btn3, line_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, LV_DPI / 4);
	lv_btn_set_action(btn3, LV_BTN_ACTION_CLICK, _create_mbox_autorcm_status);

	// Set default state for AutoRCM and lock it out if patched unit.
	if (get_set_autorcm_status(false))
		lv_btn_set_state(btn3, LV_BTN_STATE_TGL_REL);
	else
		lv_btn_set_state(btn3, LV_BTN_STATE_REL);
	nyx_generic_onoff_toggle(btn3);

	if (h_cfg.rcm_patched)
	{
		lv_obj_set_click(btn3, false);
		lv_btn_set_state(btn3, LV_BTN_STATE_INA);
	}
	autorcm_btn = btn3;

	char *txt_buf = (char *)malloc(SZ_4K);

	s_printf(txt_buf,
		"Allows you to enter RCM without using #C7EA46 VOL+# & #C7EA46 HOME# (jig).\n"
		"#FF8000 It can restore all versions of AutoRCM whenever requested.#\n"
		"#FF3C28 This corrupts the BCT and you can't boot without a custom#\n"
		"#FF3C28 bootloader.#");

	if (h_cfg.rcm_patched)
		strcat(txt_buf, " #FF8000 This is disabled because this unit is patched!#");

	lv_obj_t *label_txt4 = lv_label_create(h2, NULL);
	lv_label_set_recolor(label_txt4, true);
	lv_label_set_text(label_txt4, txt_buf);
	free(txt_buf);

	lv_obj_set_style(label_txt4, &hint_small_style);
	lv_obj_align(label_txt4, btn3, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);

	label_sep = lv_label_create(h2, NULL);
	lv_label_set_static_text(label_sep, "");
	lv_obj_align(label_sep, label_txt4, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI * 11 / 7);

	// Create Dump Package1/2 button.
	lv_obj_t *btn4 = lv_btn_create(h2, btn);
	label_btn = lv_label_create(btn4, NULL);
	lv_label_set_static_text(label_btn, SYMBOL_MODULES"  Dump Package1/2");
	lv_obj_align(btn4, label_txt4, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 2);
	lv_btn_set_action(btn4, LV_BTN_ACTION_CLICK, _create_window_dump_pk12_tool);

	label_txt2 = lv_label_create(h2, NULL);
	lv_label_set_recolor(label_txt2, true);
	lv_label_set_static_text(label_txt2,
		"Allows you to dump and decrypt pkg1 and pkg2 and further\n"
		"split it up into their individual parts. It also dumps the kip1.");
	lv_obj_set_style(label_txt2, &hint_small_style);
	lv_obj_align(label_txt2, btn4, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);
}

// ====================================
// START CUSTOM NYX
// ====================================

static FRESULT local_dir_nuke(const char *path)
{
    FRESULT res;
    DIR dir;
    static FILINFO fno;
    
    // Self-contained local stack buffer
    char buf[512];

    res = f_opendir(&dir, path);
    if (res == FR_OK) {
        for (;;) {
            res = f_readdir(&dir, &fno);
            if (res != FR_OK || fno.fname[0] == 0) break; // Error or end of dir
            
            if (fno.fname[0] == '.') continue;

            // --- REPLACE s_printf WITH PURE STRING COPY/CONCATENATION ---
            // Safely manually construct: path + "/" + filename
            strcpy(buf, path);
            strcat(buf, "/");
            strcat(buf, fno.fname);
            // ------------------------------------------------------------

            if (fno.fattrib & AM_DIR) {
                res = local_dir_nuke(buf);
            } else {
                res = f_unlink(buf);
            }
            if (res != FR_OK) break;
        }
        f_closedir(&dir);
    }
    
    // If directory was emptied or if it was just a file, delete it
    if (res == FR_OK || res == FR_NO_PATH) {
        res = f_unlink(path);
    }
    return res;
}

// ==========================================
// TIER 1: QUICK FIX
// ==========================================
static lv_res_t _run_system_rescue_action(lv_obj_t *btn)
{
	// 1. Array of known problematic Title IDs (Themes, Sy-modules, Overlays, shells)
	const char *target_folders[] = {
		"atmosphere/contents/0100000000001000", // Home Menu Theme
		"atmosphere/contents/0100000000001001", // User Profile Page Theme
		"atmosphere/contents/0100000000001007", // System Settings Customizer
		"atmosphere/contents/0100000000001013", // Lock Screen Layouts
		"atmosphere/contents/420000000000000B", // Outdated sys-patch (Wi-Fi Crash)
		"atmosphere/contents/420000000007E51A", // Outdated Tesla Menu Overlay
		"atmosphere/contents/010000000000000D"  // Corrupt uLaunch Shell Replacement
	};

	// Human-readable names mapped to match the target array for the text log output
	const char *target_names[] = {
		"Home Menu Theme",
		"User Page Customizer",
		"Settings Customizer",
		"Lock Screen Layout",
		"sys-patch Sysmodule",
		"Tesla Overlay Menu",
		"uLaunch Custom Shell"
	};

	int found_count = 0;
	char summary_buffer[512];
	memset(summary_buffer, 0, sizeof(summary_buffer));

	// Start building our text report string
	strcat(summary_buffer, "#FF8000 System Rescue Complete!#\n\n");


	FILINFO fno;
	int total_items = sizeof(target_folders) / sizeof(target_folders[0]);

	if (sd_mount())
	{
		
	// 2. Scan the SD Card filesystem using FatFS
	for (int i = 0; i < total_items; i++)
	{
		// f_stat checks if the target folder exists on the SD card partition
		if (f_stat(target_folders[i], &fno) == FR_OK)
		{
			// Target folder located! Completely erase it using Hekate's recursive directory purge tool
			if (local_dir_nuke(target_folders[i]) == FR_OK)
			{
				char line[64];
				snprintf(line, sizeof(line), "#00FF00 [Purged]# %s\n", target_names[i]);
				strcat(summary_buffer, line);
				found_count++;
			}
		}
	}

	// 3. Automated Error Log Maintenance Check
	// If erpt_reports accumulates thousands of telemetry crash files, it throttles performance
	if (f_stat("atmosphere/erpt_reports", &fno) == FR_OK)
	{
		local_dir_nuke("atmosphere/erpt_reports");
		f_mkdir("atmosphere/erpt_reports"); // Recreate the folder in a clean, pristine slate
		strcat(summary_buffer, "#00FF00 [Cleaned]# Telemetry Error Logs\n");
		found_count++;
	}

	// 4. Handle final message output based on results
	if (found_count == 0)
	{
		memset(summary_buffer, 0, sizeof(summary_buffer));
		strcat(summary_buffer, 
			"#00FFFF System Healthy!#\n\n"
			"No problematic theme folders or outdated custom layout & sysmodules were detected on your SD card.");
	}
	else
	{
		strcat(summary_buffer, "\n\n#FFFF00 Safe to reboot!# Your Atmosphere boot chain components are clear.");
	}
	} //end sd_mount
	
	sd_unmount();

	// 5. Render the result window onto the screen
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char *mbox_btn_map[] = { "\251", "\222OK", "\251", "" };
	lv_obj_t *mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_mbox_set_text(mbox, summary_buffer);

	lv_mbox_add_btns(mbox, mbox_btn_map, nyx_mbox_action);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6); // Slighly wider window layout to comfortably fit log rows
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);

	return LV_RES_OK;
}

// ==========================================
// TIER 2: ADVANCED REPAIR ACTION
// ==========================================
static lv_res_t _run_tier2_advanced_action(lv_obj_t *btn)
{
	char summary_buffer[512];
	memset(summary_buffer, 0, sizeof(summary_buffer));
	strcat(summary_buffer, "#FF8000 Tier 2 Advanced Repair Complete#\n\n");

	FILINFO fno;
	int changes = 0;

	if (sd_mount())
	{
	// 1. Purge problematic reboot payload that causes infinite warmboot crash loops
	if (f_stat("atmosphere/reboot_payload.bin", &fno) == FR_OK)
	{
		if (f_unlink("atmosphere/reboot_payload.bin") == FR_OK)
		{
			strcat(summary_buffer, "#00FF00 [Removed]# Corrupt Warmboot Payload\n");
			changes++;
		}
	}

	// 2. Wipe bad configuration overrides that break low-level boot arguments
	if (f_stat("atmosphere/config/system_settings.ini", &fno) == FR_OK)
	{
		if (f_unlink("atmosphere/config/system_settings.ini") == FR_OK)
		{
			strcat(summary_buffer, "#00FF00 [Reset]# Global System Settings Configuration\n");
			changes++;
		}
	}

	// 3. Clear corrupted cheat code override indexes that cause game engine panics
	if (f_stat("atmosphere/cheat_vm_logs", &fno) == FR_OK)
	{
		local_dir_nuke("atmosphere/cheat_vm_logs");
		strcat(summary_buffer, "#00FF00 [Scrubbed]# Global Cheat Engine Logs\n");
		changes++;
	}

	if (changes == 0)
	{
		memset(summary_buffer, 0, sizeof(summary_buffer));
		strcat(summary_buffer, "#00FFFF System Clean!#\n\nNo deep system corruptions or warmboot payload conflicts were detected.");
	}
	else
	{
		strcat(summary_buffer, "\n#FFFF00 Notice:# System settings reset to safe default fallback values.");
	}
	}
	sd_unmount();
	
	// Render Status Box Window
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char *mbox_btn_map[] = { "\251", "\222OK", "\251", "" };
	lv_obj_t *mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_mbox_set_text(mbox, summary_buffer);
	lv_mbox_add_btns(mbox, mbox_btn_map, nyx_mbox_action);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);

	return LV_RES_OK;
}


// ==========================================
// MINIMAL ZIP PARSER + TINFL DEFLATE
// No external library needed. Handles DEFLATE (method 8) and
// Stored (method 0) entries. Sufficient for Atmosphere/hekate ZIPs.
// ==========================================

// ZIP local file header (comes before each file's data in the archive).
#pragma pack(push, 1)
typedef struct {
	u32 sig;         // 0x04034b50
	u16 version;
	u16 flags;
	u16 method;      // 0=stored, 8=deflate
	u16 mod_time;
	u16 mod_date;
	u32 crc32;
	u32 comp_size;
	u32 uncomp_size;
	u16 fname_len;
	u16 extra_len;
} zip_local_hdr_t;
#pragma pack(pop)

#define ZIP_LOCAL_SIG  0x04034b50
#define ZIP_DATA_DESC_SIG 0x08074b50

// Ensure all parent directories for 'path' exist, creating them as needed.
static void _zip_mkdir_p(const char *path)
{
	char tmp[FF_MAX_LFN];
	strncpy(tmp, path, sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = '\0';

	// Strip trailing slash if present (directory entries).
	size_t len = strlen(tmp);
	if (len > 0 && tmp[len - 1] == '/')
	{
		tmp[len - 1] = '\0';
		// For directory entries, create all components including the final one.
		for (char *p = tmp + 1; *p; p++)
		{
			if (*p == '/')
			{
				*p = '\0';
				f_mkdir(tmp);
				*p = '/';
			}
		}
		f_mkdir(tmp); // Create the final directory component.
	}
	else
	{
		// For file paths, only create parent directories — NOT the final component.
		for (char *p = tmp + 1; *p; p++)
		{
			if (*p == '/')
			{
				*p = '\0';
				f_mkdir(tmp);
				*p = '/';
			}
		}
		// Do NOT call f_mkdir(tmp) here — tmp is a file, not a directory.
	}
}


// Returns decompressed size on success, -1 on error.
// in_buf/in_size: compressed DEFLATE stream (raw, no zlib header).
// out_buf must be pre-allocated to out_size bytes.
static int _tinfl_decompress(const u8 *in_buf, u32 in_size, u8 *out_buf, u32 out_size)
{
    size_t result = tinfl_decompress_mem_to_mem(out_buf, (size_t)out_size,
                                                in_buf,  (size_t)in_size, 0);
    // 0 flags = raw deflate (no zlib wrapper), which is what ZIP method 8 uses
    return (result == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) ? -1 : (int)result;
}

// Scan ZIP archive in 'zip_buf' for an entry whose filename matches 'fname_prefix'.
// Decompresses it into a freshly malloc'd buffer; caller must free().
// Returns decompressed size, or 0 on failure.
// If out_entry_name is non-NULL, copies the entry's filename there (max 64 chars).
static u32 _zip_extract_entry(const u8 *zip_buf, u32 zip_size,
	const char *fname_prefix, u8 **out_data, char *out_entry_name)
{
	const u8 *p = zip_buf;
	const u8 *end = zip_buf + zip_size;
	size_t prefix_len = strlen(fname_prefix);

	while (p + sizeof(zip_local_hdr_t) <= end)
	{
		const zip_local_hdr_t *hdr = (const zip_local_hdr_t *)p;
		if (hdr->sig != ZIP_LOCAL_SIG)
			break;

		u16 fname_len = hdr->fname_len;
		u16 extra_len = hdr->extra_len;
		const char *fname = (const char *)(p + sizeof(zip_local_hdr_t));
		const u8 *data = (const u8 *)(fname + fname_len + extra_len);

		u32 comp_size   = hdr->comp_size;
		u32 uncomp_size = hdr->uncomp_size;

		// Handle data descriptor flag safely.
		if ((hdr->flags & 0x8) && comp_size == 0 && uncomp_size == 0)
		{
			p = data;
			continue;
		}

		// Match prefix anywhere in the filename path, case-insensitive.
		// e.g. "hekate_ctcaer_" matches "hekate_ctcaer_6.2.2/hekate_ctcaer_6.2.2.bin"
		bool match = false;
		if (fname_len >= prefix_len)
		{
			// Check every position in fname for the prefix.
			for (int i = 0; i <= (int)(fname_len - prefix_len); i++)
			{
				if (strncasecmp(fname + i, fname_prefix, prefix_len) == 0)
				{
					match = true;
					break;
				}
			}
		}

		if (match && uncomp_size > 0)
		{
			if (out_entry_name)
			{
				int copy_len = fname_len < 63 ? fname_len : 63;
				memcpy(out_entry_name, fname, copy_len);
				out_entry_name[copy_len] = '\0';
			}

			u8 *out = malloc(uncomp_size);
			if (!out) return 0;

			int result = -1;
			if (hdr->method == 0)
			{
				memcpy(out, data, uncomp_size);
				result = (int)uncomp_size;
			}
			else if (hdr->method == 8)
			{
				result = _tinfl_decompress(data, comp_size, out, uncomp_size);
			}

			if (result < 0) { free(out); return 0; }
			*out_data = out;
			return (u32)result;
		}

		// Advance to next entry.
		if (data + comp_size > end) break;
		p = data + comp_size;

		// Skip data descriptor if present.
		if (hdr->flags & 0x8)
		{
			if (p + 16 <= end)
			{
				u32 sig = p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);
				if (sig == ZIP_DATA_DESC_SIG) p += 16;
			}
		}
	}
	return 0;
}

// Walk a ZIP archive and extract all entries whose names start with 'path_prefix'
// to 'dest_root' on the SD card. Skips directory entries (names ending in '/').
// Returns count of files successfully written, or -1 on fatal error.
// Walk a ZIP archive and extract all entries whose names start with 'path_prefix'
// to the SD card. Directory entries are created. Zero-byte files are created empty.
// Logs to FIL *log_fp if non-NULL. Returns count of files successfully written.
static int _zip_extract_folder(const u8 *zip_buf, u32 zip_size,
	const char *path_prefix, const char *dest_root, FIL *log_fp)
{
	const u8 *p = zip_buf;
	const u8 *end = zip_buf + zip_size;
	int count = 0;
	size_t prefix_len = strlen(path_prefix);
	bool dest_empty = (dest_root == NULL || dest_root[0] == '\0');

	#define ZIP_LOG(fmt, ...) \
		do { \
			if (log_fp) { \
				char _zl[256]; \
				int _zn = snprintf(_zl, sizeof(_zl), fmt, ##__VA_ARGS__); \
				UINT _zw; \
				f_write(log_fp, _zl, _zn, &_zw); \
			} \
		} while(0)

	while (p + sizeof(zip_local_hdr_t) <= end)
	{
		const zip_local_hdr_t *hdr = (const zip_local_hdr_t *)p;
		if (hdr->sig != ZIP_LOCAL_SIG) break;

		u16 fname_len = hdr->fname_len;
		u16 extra_len = hdr->extra_len;

		// Copy fname into null-terminated buffer — not null-terminated in ZIP.
		char fname[FF_MAX_LFN];
		u16 safe_len = fname_len < (FF_MAX_LFN - 1) ? fname_len : (FF_MAX_LFN - 1);
		memcpy(fname, p + sizeof(zip_local_hdr_t), safe_len);
		fname[safe_len] = '\0';

		const u8 *data  = p + sizeof(zip_local_hdr_t) + fname_len + extra_len;
		u32 comp_size   = hdr->comp_size;
		u32 uncomp_size = hdr->uncomp_size;

		// Safe skip when both sizes are truly unknown.
		if ((hdr->flags & 0x8) && comp_size == 0 && uncomp_size == 0)
		{
			ZIP_LOG("SKIP (data descriptor): %s\n", fname);
			p = data;
			continue;
		}

		bool is_dir     = (safe_len > 0 && fname[safe_len - 1] == '/');
		bool name_match = (safe_len >= prefix_len &&
		                   strncasecmp(fname, path_prefix, prefix_len) == 0);

		ZIP_LOG("%s [m=%d cs=%lu us=%lu elen=%u]: %s\n",
			name_match ? ">" : " ",
			hdr->method, (unsigned long)comp_size,
			(unsigned long)uncomp_size, (unsigned int)extra_len, fname);

		if (name_match)
		{
			// Keep full fname path when no dest_root given.
			char out_path[FF_MAX_LFN];
			if (dest_empty)
				snprintf(out_path, sizeof(out_path), "%s", fname);
			else
				snprintf(out_path, sizeof(out_path), "%s/%s", dest_root, fname);

			if (is_dir)
			{
				_zip_mkdir_p(out_path); // handles trailing slash internally
				ZIP_LOG("  MKDIR: %s\n", out_path);
			}
			else
			{
				_zip_mkdir_p(out_path); // creates parent dirs only

				if (uncomp_size == 0)
				{
					// Zero-byte file (e.g. boot2.flag) — create empty.
					FIL fp; UINT bw;
					if (f_open(&fp, out_path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
					{
						f_close(&fp);
						count++;
						ZIP_LOG("  TOUCH: %s\n", out_path);
					}
					else
						ZIP_LOG("#FF0000 WRITE FAIL (empty): %s#\n", out_path);
				}
				else
				{
					u8 *out = malloc(uncomp_size);
					if (!out)
					{
						ZIP_LOG("#FF0000 NOMEM: %s#\n", out_path);
						if (data + comp_size > end) break;
						p = data + comp_size;
						continue;
					}

					int result = -1;
					if (hdr->method == 0)
					{
						memcpy(out, data, uncomp_size);
						result = (int)uncomp_size;
					}
					else if (hdr->method == 8)
					{
						result = _tinfl_decompress(data, comp_size, out, uncomp_size);
					}
					else
						ZIP_LOG("#FFFF00 UNSUPPORTED method %d: %s#\n", hdr->method, out_path);

					if (result > 0)
					{
						FIL fp; UINT written;
						if (f_open(&fp, out_path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
						{
							f_write(&fp, out, (UINT)result, &written);
							f_close(&fp);
							count++;
							ZIP_LOG("#00FF00 OK: %s (%d bytes)#\n", out_path, result);
						}
						else
							ZIP_LOG("#FF0000 WRITE FAIL: %s#\n", out_path);
					}
					else if (result < 0)
						ZIP_LOG("#FF0000 DECOMP FAIL: %s#\n", out_path);

					free(out);
				}
			}
		}

		if (data + comp_size > end) break;
		p = data + comp_size;

		if (hdr->flags & 0x8)
		{
			if (p + 16 <= end)
			{
				u32 sig = p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);
				if (sig == ZIP_DATA_DESC_SIG) p += 16;
			}
		}
	}

	#undef ZIP_LOG
	return count;
}

// Find a filename matching 'prefix*' inside 'dir', copy result to 'out' (max out_size).
static bool _find_file_by_prefix(const char *dir, const char *prefix, char *out, int out_size)
{
	DIR dp;
	FILINFO fno;
	if (f_opendir(&dp, dir) != FR_OK) return false;
	bool found = false;
	while (f_readdir(&dp, &fno) == FR_OK && fno.fname[0])
	{
		if (strncasecmp(fno.fname, prefix, strlen(prefix)) == 0)
		{
			// Must end in .zip
			size_t len = strlen(fno.fname);
			if (len < 4 || strncasecmp(fno.fname + len - 4, ".zip", 4) != 0)
				continue;

			// Guard against truncation: dir + '/' + fname must fit in out_size.
			if ((int)(strlen(dir) + 1 + len + 1) <= out_size)
				snprintf(out, out_size, "%s/%s", dir, fno.fname);
			else
				continue; // Name too long for output buffer — skip.
			found = true;
			break;
		}
	}
	f_closedir(&dp);
	return found;
}

// Load a whole file from SD into a malloc'd buffer. Returns size or 0.
static u32 _sd_load_file(const char *path, u8 **out_buf)
{
	FIL fp;
	if (f_open(&fp, path, FA_READ) != FR_OK) return 0;
	u32 size = f_size(&fp);
	if (!size) { f_close(&fp); return 0; }
	u8 *buf = malloc(size);
	if (!buf) { f_close(&fp); return 0; }
	UINT br;
	f_read(&fp, buf, size, &br);
	f_close(&fp);
	if (br != size) { free(buf); return 0; }
	*out_buf = buf;
	return size;
}

// ==========================================
// ATMOSPHERE DATED BACKUP HELPER
// Renames "atmosphere" -> "atmosphere_YYYYMMDD"
// Appends "_01", "_02" etc. if that name already exists.
// ==========================================
static FRESULT _safe_backup_atmosphere(void)
{
	FRESULT res;
	char new_path[64];
	FILINFO fno;

	rtc_time_t time;
	max77620_rtc_get_time_adjusted(&time);

	snprintf(new_path, sizeof(new_path), "atmosphere_%04d%02d%02d",
		time.year, time.month, time.day);

	if (f_stat(new_path, &fno) == FR_OK)
	{
		for (int i = 1; i < 100; i++)
		{
			snprintf(new_path, sizeof(new_path), "atmosphere_%04d%02d%02d_%02d",
				time.year, time.month, time.day, i);
			if (f_stat(new_path, &fno) != FR_OK)
				break;
		}
	}

	res = f_rename("atmosphere", new_path);
	return res;
}



// ==========================================
// TIER 3: REINSTALL ATMOSPHERE + HEKATE
// Scans /bootloader/rescue/ for:
//   atmosphere*.zip  -> extracts to SD root (atmosphere/, switch/, hbmenu.nro, fusee.bin)
//   hekate*.zip      -> extracts hekate*.bin -> payload.bin + bootloader/update.bin
// ==========================================
static lv_res_t _run_tier3_reinstall_action(lv_obj_t *btn)
{
	// Create progress message box immediately so user sees activity.
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	lv_obj_t *mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_mbox_set_text(mbox, "#FF8000 Step 3: Reinstall CFW#\n\nStarting...");
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);

	// Live status label inside the mbox.
	lv_obj_t *lbl_status = lv_label_create(mbox, NULL);
	lv_label_set_recolor(lbl_status, true);
	lv_label_set_text(lbl_status, "#C7EA46 Mounting SD...#");
	manual_system_maintenance(true);

	char *summary_buffer = malloc(4096);
	if (!summary_buffer) return LV_RES_OK;
	memset(summary_buffer, 0, 4096);
	const char *rescue_dir = "bootloader/rescue";
	FILINFO fno;

	if (sd_mount())
	{
		lv_label_set_text(lbl_status, "#FF0000 Failed to mount SD card!#");
		manual_system_maintenance(true);
		strcat(summary_buffer, "#FF0000 Failed to mount SD card!#");
		goto _show_result;
	}

	// Open log file.
	FIL log_fp;
	bool log_open = (f_open(&log_fp, "bootloader/rescue/rescue_log.txt", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);

	#define RESCUE_LOG(fmt, ...) \
			do { \
					if (log_open) { \
							char _rl[256]; \
							int _rn = snprintf(_rl, sizeof(_rl), fmt, ##__VA_ARGS__); \
							UINT _rw; \
							f_write(&log_fp, _rl, _rn, &_rw); \
					} \
			} while(0)

	if (f_stat(rescue_dir, &fno) != FR_OK)
	{
		strcat(summary_buffer,
			"#FF0000 Rescue folder not found!#\n\n"
			"Please create and populate:\n"
			"#FFFF00 /bootloader/rescue/#\n\n"
			"Drop atmosphere*.zip, fusee.bin and hekate*.zip there.");
		goto _show_result;
	}

// ---- Atmosphere ZIP ----
	{
		RESCUE_LOG("=== Atmosphere ZIP ===\n");

		char atmo_zip_path[FF_MAX_LFN];
		if (!_find_file_by_prefix(rescue_dir, "atmosphere", atmo_zip_path, sizeof(atmo_zip_path)))
		{
			RESCUE_LOG("ERROR: atmosphere*.zip not found in rescue dir\n");
			strcat(summary_buffer, "#FFFF00 No atmosphere*.zip found in rescue folder.#\n\n");
		}
		else
		{
			RESCUE_LOG("Found: %s\n", atmo_zip_path);

			lv_label_set_text(lbl_status, "#C7EA46 Loading atmosphere ZIP...#");
			manual_system_maintenance(true);

			u8 *zip_buf = NULL;
			u32 zip_size = _sd_load_file(atmo_zip_path, &zip_buf);
			RESCUE_LOG("ZIP load size: %lu\n", (unsigned long)zip_size);

			if (!zip_buf || !zip_size)
			{
				RESCUE_LOG("ERROR: failed to load atmosphere ZIP\n");
				strcat(summary_buffer, "#FF0000 Failed to load atmosphere ZIP!#\n\n");
			}
			else
			{
				lv_label_set_text(lbl_status, "#C7EA46 Backing up atmosphere/...#");
				manual_system_maintenance(true);

				FRESULT bres = _safe_backup_atmosphere();
				RESCUE_LOG("Backup result: %d\n", (int)bres);
				if (bres == FR_OK)
					strcat(summary_buffer, "#00FF00 atmosphere/ backed up.#\n");
				else
					strcat(summary_buffer, "#FFFF00 atmosphere/ backup skipped.#\n");

				lv_label_set_text(lbl_status, "#C7EA46 Extracting atmosphere ZIP...#");
				manual_system_maintenance(true);

				RESCUE_LOG("--- Extracting atmosphere ZIP entries ---\n");
				int n = _zip_extract_folder(zip_buf, zip_size, "", "", &log_fp);
				RESCUE_LOG("--- Extraction done: %d files ---\n", n);

				if (n > 0)
				{
					char nbuf[64];
					snprintf(nbuf, sizeof(nbuf), "#00FF00 Atmosphere: %d files extracted.#\n", n);
					strcat(summary_buffer, nbuf);
				}
				else
					strcat(summary_buffer, "#FF0000 Atmosphere extraction failed!#\n");

				lv_label_set_text(lbl_status, "#C7EA46 Copying fusee.bin...#");
				manual_system_maintenance(true);

				char fusee_path[FF_MAX_LFN];
				snprintf(fusee_path, sizeof(fusee_path), "%s/fusee.bin", rescue_dir);
				FILINFO fusee_fno;
				if (f_stat(fusee_path, &fusee_fno) == FR_OK)
				{
					RESCUE_LOG("fusee.bin found in rescue, copying...\n");
					u8 *fusee_buf = NULL;
					u32 fusee_size = _sd_load_file(fusee_path, &fusee_buf);
					if (fusee_buf && fusee_size)
					{
						FIL fp; UINT bw;
						if (f_open(&fp, "fusee.bin", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
						{
							f_write(&fp, fusee_buf, fusee_size, &bw);
							f_close(&fp);
							RESCUE_LOG("fusee.bin written: %lu bytes\n", (unsigned long)fusee_size);
							strcat(summary_buffer, "#00FF00 fusee.bin updated.#\n");
						}
						else
							RESCUE_LOG("ERROR: could not open fusee.bin for write\n");
						free(fusee_buf);
					}
				}
				else
					RESCUE_LOG("fusee.bin not in rescue dir, skipping\n");

				free(zip_buf);
			}
		}
	}

	// ---- Hekate ZIP ----
	{
		RESCUE_LOG("=== Hekate ZIP ===\n");

		char hekate_zip_path[FF_MAX_LFN];
		if (!_find_file_by_prefix(rescue_dir, "hekate", hekate_zip_path, sizeof(hekate_zip_path)))
		{
			RESCUE_LOG("ERROR: hekate*.zip not found in rescue dir\n");
			strcat(summary_buffer, "#FFFF00 No hekate*.zip found - skipping hekate update.#\n");
		}
		else
		{
			RESCUE_LOG("Found: %s\n", hekate_zip_path);

			lv_label_set_text(lbl_status, "#C7EA46 Loading hekate ZIP...#");
			manual_system_maintenance(true);

			u8 *zip_buf = NULL;
			u32 zip_size = _sd_load_file(hekate_zip_path, &zip_buf);
			RESCUE_LOG("ZIP load size: %lu\n", (unsigned long)zip_size);

			if (!zip_buf || !zip_size)
			{
				RESCUE_LOG("ERROR: failed to load hekate ZIP\n");
				strcat(summary_buffer, "#FF0000 Failed to load hekate ZIP!#\n");
			}
			else
			{
				lv_label_set_text(lbl_status, "#C7EA46 Extracting hekate...#");
				manual_system_maintenance(true);

				char entry_name[64] = {0};
				u8 *bin_data = NULL;
				u32 bin_size = _zip_extract_entry(zip_buf, zip_size, "hekate_ctcaer_", &bin_data, entry_name);
				RESCUE_LOG("hekate bin entry: '%s' size: %lu\n", entry_name, (unsigned long)bin_size);

				if (!bin_data || !bin_size)
				{
					RESCUE_LOG("ERROR: hekate_ctcaer_*.bin not found in ZIP\n");
					strcat(summary_buffer, "#FF0000 hekate_ctcaer_*.bin not found in ZIP!#\n");
				}
				else
				{
					FIL fp; UINT bw; bool ok = true;

					lv_label_set_text(lbl_status, "#C7EA46 Writing payload.bin...#");
					manual_system_maintenance(true);

					if (f_open(&fp, "payload.bin", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
					{
						f_write(&fp, bin_data, bin_size, &bw);
						f_close(&fp);
						RESCUE_LOG("payload.bin written: %lu bytes\n", (unsigned long)bin_size);
						strcat(summary_buffer, "#00FF00 payload.bin updated.#\n");
					}
					else
					{
						RESCUE_LOG("ERROR: could not write payload.bin\n");
						strcat(summary_buffer, "#FF0000 Could not write payload.bin!#\n");
						ok = false;
					}

					if (ok)
					{
						lv_label_set_text(lbl_status, "#C7EA46 Writing bootloader/update.bin...#");
						manual_system_maintenance(true);

						if (f_open(&fp, "bootloader/update.bin", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
						{
							f_write(&fp, bin_data, bin_size, &bw);
							f_close(&fp);
							RESCUE_LOG("bootloader/update.bin written\n");
							strcat(summary_buffer, "#00FF00 bootloader/update.bin staged.#\n");
						}
						else
						{
							RESCUE_LOG("WARN: could not write bootloader/update.bin\n");
							strcat(summary_buffer, "#FFFF00 Could not write update.bin.#\n");
						}
					}

					lv_label_set_text(lbl_status, "#C7EA46 Extracting bootloader/ support files...#");
					manual_system_maintenance(true);

					RESCUE_LOG("--- Extracting hekate bootloader/ entries ---\n");
					int n = _zip_extract_folder(zip_buf, zip_size, "bootloader/", "", &log_fp);
					RESCUE_LOG("--- Extraction done: %d files ---\n", n);

					if (n > 0)
					{
						char nbuf[64];
						snprintf(nbuf, sizeof(nbuf), "#00FF00 Hekate: %d support files extracted.#\n", n);
						strcat(summary_buffer, nbuf);
					}

					free(bin_data);
				}
				free(zip_buf);
			}
		}
	}

	RESCUE_LOG("=== All done ===\n");
	strcat(summary_buffer, "\n#C7EA46 Reboot to apply hekate update.#");
	lv_label_set_text(lbl_status, "#00FF00 Done!#");
	manual_system_maintenance(true);

_show_result:
	#undef RESCUE_LOG
	if (log_open) { f_close(&log_fp); log_open = false; }
	sd_unmount();

	// Update the existing mbox in place — do NOT delete dark_bg and recreate.
	// nyx_mbox_action traverses btnm→mbox→dark_bg which must stay intact.
	lv_obj_del(lbl_status);
	lv_mbox_set_text(mbox, summary_buffer);
	static const char *mbox_btn_map[] = { "\251", "\222OK", "\251", "" };
	lv_mbox_add_btns(mbox, mbox_btn_map, nyx_mbox_action);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);

	return LV_RES_OK;
}

// ==========================================
// CONFIRMATION MBOX DISPATCHER
// Shows a Yes/No mbox. On "Yes", calls _pending_confirm_action.
// ==========================================
static lv_res_t (*_pending_confirm_action)(lv_obj_t *) = NULL;

static lv_res_t _confirm_mbox_cb(lv_obj_t *btnm, const char *txt)
{
	lv_obj_t *dark_bg = lv_obj_get_parent(lv_obj_get_parent(btnm));
	lv_obj_del(dark_bg);

	if (txt && strcmp(txt, "Yes") == 0 && _pending_confirm_action)
	{
		lv_res_t res = _pending_confirm_action(NULL);
		_pending_confirm_action = NULL;
		return res;
	}
	_pending_confirm_action = NULL;
	return LV_RES_OK;
}

static void _show_confirm_mbox(const char *msg, lv_res_t (*action)(lv_obj_t *))
{
	_pending_confirm_action = action;

	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char *confirm_btns[] = { "\222Yes", "\222No", "" };
	lv_obj_t *mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_mbox_set_text(mbox, msg);
	lv_mbox_add_btns(mbox, confirm_btns, _confirm_mbox_cb);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);
}


// Confirm launchers for each step.
static lv_res_t _confirm_step1(lv_obj_t *btn)
{
	_show_confirm_mbox(
		"#FF8000 Step 1: Fast Purge#\n\n"
		"This will delete theme/overlay/module folders known\n"
		"to cause boot panics and clear erpt_reports.\n\n"
		"#C7EA46 Proceed?#",
		_run_system_rescue_action);
	return LV_RES_OK;
}

static lv_res_t _confirm_step2(lv_obj_t *btn)
{
	_show_confirm_mbox(
		"#FF8000 Step 2: Advanced System Reset#\n\n"
		"This will remove reboot_payload.bin,\n"
		"delete system_settings.ini and clear cheat_vm_logs.\n\n"
		"#C7EA46 Proceed?#",
		_run_tier2_advanced_action);
	return LV_RES_OK;
}

static lv_res_t _confirm_step3(lv_obj_t *btn)
{
	// Scan for required files first and build a status message.
	const char *rescue_dir = "bootloader/rescue";
	char msg[768];
	memset(msg, 0, sizeof(msg));

	strcat(msg, "#FF8000 Step 3: Reinstall Custom Firmware#\n\n");

	if (sd_mount())
		{
			strcat(msg, "#FF0000 Failed to mount SD card!#");
			// show error box and return
			lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
			lv_obj_set_style(dark_bg, &mbox_darken);
			lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);
			static const char *ok_btns[] = { "\251", "\222OK", "\251", "" };
			lv_obj_t *mbox = lv_mbox_create(dark_bg, NULL);
			lv_mbox_set_recolor_text(mbox, true);
			lv_mbox_set_text(mbox, msg);
			lv_mbox_add_btns(mbox, ok_btns, nyx_mbox_action);
			lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);
			lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
			lv_obj_set_top(mbox, true);
			return LV_RES_OK;
		}

	bool atmo_found = false, hekate_found = false;

	// Check for atmosphere*.zip.
	{
		char tmp[FF_MAX_LFN];
		if (_find_file_by_prefix(rescue_dir, "atmosphere", tmp, sizeof(tmp)))
		{
			strcat(msg, "#00FF00 [FOUND]# atmosphere*.zip\n");
			atmo_found = true;
		}
		else
			strcat(msg, "#FF0000 [MISSING]# atmosphere*.zip\n");

		// Check fusee.bin as a standalone file in rescue dir.
		char fusee_path[FF_MAX_LFN];
		snprintf(fusee_path, sizeof(fusee_path), "%s/fusee.bin", rescue_dir);
		FILINFO fusee_fno;
		if (f_stat(fusee_path, &fusee_fno) == FR_OK)
			strcat(msg, "#00FF00 [FOUND]# fusee.bin\n");
		else
			strcat(msg, "#FFFF00 [MISSING]# fusee.bin (optional)\n");
	}

	// Check for hekate*.zip.
	{
		char tmp[FF_MAX_LFN];
		if (_find_file_by_prefix(rescue_dir, "hekate", tmp, sizeof(tmp)))
		{
			strcat(msg, "#00FF00 [FOUND]# hekate*.zip\n");
			hekate_found = true;
		}
		else
			strcat(msg, "#FF0000 [MISSING]# hekate*.zip\n");
	}

	strcat(msg, "\n");

	if (!atmo_found && !hekate_found)
	{
		strcat(msg,
			"#FF0000 No rescue ZIPs found!#\n"
			"Place files in #FFFF00 /bootloader/rescue/#\n"
			"and try again.");

		// Show as info only — no confirm needed.
		lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
		lv_obj_set_style(dark_bg, &mbox_darken);
		lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);
		static const char *ok_btns[] = { "\251", "\222OK", "\251", "" };
		lv_obj_t *mbox = lv_mbox_create(dark_bg, NULL);
		lv_mbox_set_recolor_text(mbox, true);
		lv_mbox_set_text(mbox, msg);
		lv_mbox_add_btns(mbox, ok_btns, nyx_mbox_action);
		lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);
		lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
		lv_obj_set_top(mbox, true);
		return LV_RES_OK;
	}
	
sd_unmount();

	strcat(msg, "#C7EA46 Confirm to start repair process?#");
	_show_confirm_mbox(msg, _run_tier3_reinstall_action);
	return LV_RES_OK;
}

// ==========================================
// OVERLAY TOOLS INSTALLER
// Extracts in sequence from bootloader/rescue/:
//   nx-ovlloader.zip  -> SD root
//   ovlmenu.zip       -> SD root
//   sys-patch*.zip    -> SD root
// ==========================================
static lv_res_t _run_install_overlays_action(lv_obj_t *btn)
{
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	lv_obj_t *mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_mbox_set_text(mbox, "#FF8000 Overlay Tools Install#\n\nStarting...");
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);

	lv_obj_t *lbl_status = lv_label_create(mbox, NULL);
	lv_label_set_recolor(lbl_status, true);
	lv_label_set_text(lbl_status, "#C7EA46 Mounting SD...#");
	manual_system_maintenance(true);

	char *summary_buffer = malloc(2048);
	if (!summary_buffer) return LV_RES_OK;
	memset(summary_buffer, 0, 2048);
	strcat(summary_buffer, "#FF8000 Overlay Tools Install#\n\n");
	const char *rescue_dir = "bootloader/rescue";

	// Declare ALL variables before any goto.
	FIL log_fp;
	bool log_open = false;

	#define OVL_LOG(fmt, ...) \
		do { \
			if (log_open) { \
				char _ol[256]; \
				int _on = snprintf(_ol, sizeof(_ol), fmt, ##__VA_ARGS__); \
				UINT _ow; \
				f_write(&log_fp, _ol, _on, &_ow); \
			} \
		} while(0)

	if (sd_mount())
	{
		lv_label_set_text(lbl_status, "#FF0000 Failed to mount SD card!#");
		manual_system_maintenance(true);
		strcat(summary_buffer, "#FF0000 Failed to mount SD card!#");
		goto _ovl_show_result;
	}

	log_open = (f_open(&log_fp, "bootloader/rescue/overlay_log.txt",
	                    FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);

	const char *pkgs[][2] = {
		{ "nx-ovlloader", "nx-ovlloader" },
		{ "ovlmenu",      "ovlmenu"      },
		{ "sys-patch",    "sys-patch"    },
	};

	for (int i = 0; i < 3; i++)
	{
		OVL_LOG("=== %s ===\n", pkgs[i][1]);

		char status_msg[64];
		snprintf(status_msg, sizeof(status_msg), "#C7EA46 Looking for %s...#", pkgs[i][1]);
		lv_label_set_text(lbl_status, status_msg);
		manual_system_maintenance(true);

		char zip_path[FF_MAX_LFN];
		if (!_find_file_by_prefix(rescue_dir, pkgs[i][0], zip_path, sizeof(zip_path)))
		{
			OVL_LOG("ERROR: %s*.zip not found in rescue dir\n", pkgs[i][0]);
			char line[96];
			snprintf(line, sizeof(line),
				"#FF0000 [MISSING]# %s*.zip in /bootloader/rescue/\n", pkgs[i][1]);
			strcat(summary_buffer, line);
			continue;
		}

		OVL_LOG("Found: %s\n", zip_path);

		snprintf(status_msg, sizeof(status_msg), "#C7EA46 Loading %s...#", pkgs[i][1]);
		lv_label_set_text(lbl_status, status_msg);
		manual_system_maintenance(true);

		u8 *zip_buf = NULL;
		u32 zip_size = _sd_load_file(zip_path, &zip_buf);
		OVL_LOG("ZIP load size: %lu\n", (unsigned long)zip_size);

		if (!zip_buf || !zip_size)
		{
			OVL_LOG("ERROR: failed to load %s\n", zip_path);
			char line[96];
			snprintf(line, sizeof(line), "#FF0000 [LOAD FAIL]# %s\n", pkgs[i][1]);
			strcat(summary_buffer, line);
			continue;
		}

		snprintf(status_msg, sizeof(status_msg), "#C7EA46 Extracting %s...#", pkgs[i][1]);
		lv_label_set_text(lbl_status, status_msg);
		manual_system_maintenance(true);

		OVL_LOG("--- Extracting %s entries ---\n", pkgs[i][1]);
		int n = _zip_extract_folder(zip_buf, zip_size, "", "",
		                            log_open ? &log_fp : NULL); // safe guard
		OVL_LOG("--- Extraction done: %d files ---\n", n);
		free(zip_buf);

		char line[96];
		if (n > 0)
			snprintf(line, sizeof(line),
				"#00FF00 [OK]# %s — %d files extracted.\n", pkgs[i][1], n);
		else
			snprintf(line, sizeof(line),
				"#FF0000 [FAIL]# %s extraction failed.\n", pkgs[i][1]);
		strcat(summary_buffer, line);
	}

	OVL_LOG("=== All done ===\n");
	lv_label_set_text(lbl_status, "#00FF00 Done!#");
	manual_system_maintenance(true);
	strcat(summary_buffer,
		"\n#C7EA46 If games still can't be played:#\n"
		"1. Reboot into Atmosphere.\n"
		"2. Open Tesla Menu with L+Dpad Down+R3 (Right Joystick).\n"
		"3. Enable sys-patch from the overlay list.\n");

_ovl_show_result:
	#undef OVL_LOG
	if (log_open) { f_close(&log_fp); log_open = false; }
	sd_unmount();

	// Update the existing mbox in place — do NOT delete dark_bg and recreate.
	lv_obj_del(lbl_status);
	lv_mbox_set_text(mbox, summary_buffer);
	static const char *mbox_btn_map[] = { "\251", "\222OK", "\251", "" };
	lv_mbox_add_btns(mbox, mbox_btn_map, nyx_mbox_action);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);

	return LV_RES_OK;
}

static lv_res_t _confirm_install_overlays(lv_obj_t *btn)
{
	_show_confirm_mbox(
		"#FF8000 Install Overlay Tools#\n\n"
		"Extracts in order from #FFFF00 /bootloader/rescue/#:\n"
		"  nx-ovlloader.zip\n"
		"  ovlmenu.zip\n"
		"  sys-patch*.zip\n\n"
		"Download these from GitHub first:\n"
		"github.com/WerWolv/nx-ovlloader\n"
		"github.com/WerWolv/Tesla-Menu\n"
		"github.com/impeeza/sys-patch\n\n"
		"#C7EA46 Proceed?#",
		_run_install_overlays_action);
	return LV_RES_OK;
}

// ==========================================
// SCROLLABLE HELP / INSTRUCTIONS WINDOW
// ==========================================
static lv_res_t _create_window_rescue_help(lv_obj_t *btn)
{
	lv_obj_t *win = nyx_create_standard_window(SYMBOL_LIST" System Rescue — Instructions", NULL);

	lv_obj_t *lbl = lv_label_create(win, NULL);
	lv_label_set_long_mode(lbl, LV_LABEL_LONG_BREAK);
	lv_label_set_recolor(lbl, true);
	lv_obj_set_width(lbl, LV_HOR_RES * 10 / 11);
	lv_label_set_static_text(lbl,
		"#FF8000 SYSTEM RESCUE TOOL — FULL GUIDE#\n"
		"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n"

		"#C7EA46 STEP 1 — Fast Purge# (try this first)\n"
		"Removes known problematic content that causes boot panics:\n"
		"  • Custom Home Menu Themes\n"
		"  • User Profile Page Themes\n"
		"  • System Settings Customizer\n"
		"  • Lock Screen Layout mods\n"
		"  • Outdated sys-patch (causes Wi-Fi crash)\n"
		"  • Outdated Tesla Menu Overlay\n"
		"  • Corrupt uLaunch Shell replacement\n"
		"  • Clears atmosphere/erpt_reports (FAT 16k file limit slowdown)\n\n"

		"#FF8000 Go to STEP 3 if your issue started after an Auto Firmware Update.#\n\n"

		"ExFAT Archive Bit issue (common on V1/V2 + Mac users):\n"
		"If you transferred files from macOS or had an unexpected shutdown on an exFAT card, Archive Bits may be corrupted. "
		"Atmosphere refuses to boot with generic error codes.\n\n"
		"Fix: Hekate -> Tools -> Arch Bit RCM Touch.\n\n"

		"#FF8000 CHEAT auto-enable crash#: download #C7EA46 NX Fix Cheat# from "
		"the homebrew app store and disable auto-enable on launch.\n\n"

		"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
		"#C7EA46 STEP 2 — Advanced System Reset#\n"
		"Use if Step 1 did not resolve the issue.\n"
		"  • Removes reboot_payload.bin (warmboot crash loop)\n"
		"  • Deletes atmosphere/config/system_settings.ini\n"
		"  • Scrubs atmosphere/cheat_vm_logs (bloated logs break cheat manager system \n"
		"    module on boot)\n\n"

		"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
		"#C7EA46 STEP 3 — Reinstall Custom Firmware#\n"
		"Use after Auto Firmware Update broke your CFW, or if Steps 1 and 2 did not help.\n\n"
		"Before running Step 3, download and place in #FFFF00 /bootloader/rescue/# on your SD card:\n\n"
		"  atmosphere*.zip & fusee.bin\n"
		"    github.com/Atmosphere-NX/Atmosphere/releases\n\n"
		"  hekate*.zip\n"
		"    github.com/CTCaer/hekate/releases\n\n"
		"Step 3 will:\n"
		"  1. Back up your current atmosphere/ folder (dated copy)\n"
		"  2. Extract Atmosphere to SD root (atmosphere/, switch/, hbmenu.nro & fusee.bin)\n"
		"  3. Extract hekate*.zip to SD root and rename hekate_ctcaer_*.bin -> payload.bin\n"
		"     and stage bootloader/update.bin for self-update\n\n"
		"#FF8000 After Step 3, games may not work yet!#\n"
		"You must also reinstall overlay tools (see below).\n\n"
		"#FF8000 Note:# A new hekate update will replace nyx.bin and remove this rescue tool. Keep a backup copy of your custom nyx.bin and restore it to bootloader/sys/ after updating.\n\n"

		"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
		"#C7EA46 INSTALL OVERLAY SYS-PATCH TOOLS# (after Step 3)\n"
		"Required for games to work.\n\n"
		"Download and place in #FFFF00 /bootloader/rescue/#:\n\n"
		"  nx-ovlloader.zip\n"
		"    github.com/WerWolv/nx-ovlloader/releases\n\n"
		"  ovlmenu.zip\n"
		"    github.com/WerWolv/Tesla-Menu/releases\n\n"
		"  sys-patch*.zip\n"
		"    github.com/impeeza/sys-patch/releases\n\n"
		"Then press #C7EA46 Install Overlay Tools# button.\n"
		"Extraction order: ovlloader first, then Tesla Menu (ovlmenu.zip), then sys-patch.\n\n"
		"After reboot into Atmosphere, game should automatically works. If not,:\n"
		"  • Hold R while launching any game -> hbmenu opens\n"
		"  • Press L+Dpad Down+R3 (Right Joystick) to open Tesla Menu\n"
		"  • Enable sys-patch from the overlay list\n"
	);

	return LV_RES_OK;
}

static lv_res_t _create_window_step1_help(lv_obj_t *btn)
{
	lv_obj_t *win = nyx_create_standard_window(SYMBOL_LIST" Step 1 - Information", NULL);

	lv_obj_t *lbl = lv_label_create(win, NULL);
	lv_label_set_long_mode(lbl, LV_LABEL_LONG_BREAK);
	lv_label_set_recolor(lbl, true);
	lv_obj_set_width(lbl, LV_HOR_RES * 10 / 11);
	lv_label_set_static_text(lbl,
		"Clears possible cause of boot panic: #FF8000 CUSTOM THEME#, User\n"
		"Profile Page Theme, System Settings Customizer, Lock\n"
		"Screen Layouts, Outdated sys-patch (Wi-Fi Crash), Outdated\n"
		"Tesla Menu Overlay, Corrupt uLaunch Shell Replacement.\n"
		"It then clear FAT 16k files limit in\n"
		"atmosphere/erpt_reports (causing slow down).\n\n"

		"#FF8000 Go to STEP 3 if your issue start after Auto Firmware Update.#\n\n"

		"If your crash is due to #C7EA46 cheat auto-enable# on game launch,\n"
		"download #C7EA46 NX Fix Cheat# from homebrew store and disable\n"
		"auto-enable cheat on launch."
	);

	return LV_RES_OK;
}

static void _create_system_rescue_menu(lv_theme_t *th, lv_obj_t *parent)
{
	// LV_LAYOUT_PRETTY auto-places h1 with correct page margins.
	// h2 is aligned to h1 AFTER h2 is fully built so LVGL has correct sizes.
	lv_page_set_scrl_layout(parent, LV_LAYOUT_OFF);

	// ---- Column 1: Step 1 & 2 ----
	lv_obj_t *h1 = _create_container(parent);

	lv_obj_t *label_sep = lv_label_create(h1, NULL);
	lv_label_set_static_text(label_sep, "");

	lv_obj_t *label_txt = lv_label_create(h1, NULL);
	lv_label_set_static_text(label_txt, "Step 1 & 2 - Quick Fix & Quick Repair");
	lv_obj_set_style(label_txt, th->label.prim);
	lv_obj_align(label_txt, label_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, -LV_DPI * 3 / 10);

	lv_obj_t *line_sep = lv_line_create(h1, NULL);
	static const lv_point_t line_pp[] = { {0, 0}, { LV_HOR_RES - (LV_DPI - (LV_DPI / 4)) * 2, 0} };
	lv_line_set_points(line_sep, line_pp, 2);
	lv_line_set_style(line_sep, th->line.decor);
	lv_obj_align(line_sep, label_txt, LV_ALIGN_OUT_BOTTOM_LEFT, -(LV_DPI / 4), LV_DPI / 8);

	// Help button — opens full scrollable instruction window.
	lv_obj_t *btn_instruction = lv_btn_create(h1, NULL);
	/* if (hekate_bg)
	{
		lv_btn_set_style(btn_instruction, LV_BTN_STYLE_REL, &btn_transp_rel);
		lv_btn_set_style(btn_instruction, LV_BTN_STYLE_PR, &btn_transp_pr);
	} */
	lv_obj_t *label_btn_help = lv_label_create(btn_instruction, NULL);
	lv_btn_set_fit(btn_instruction, true, true);
	lv_label_set_static_text(label_btn_help, SYMBOL_LIST"  Full Instructions");
	lv_obj_align(btn_instruction, line_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, LV_DPI / 4);
	lv_btn_set_action(btn_instruction, LV_BTN_ACTION_CLICK, _create_window_rescue_help);
	
	lv_obj_t *btn_step1 = lv_btn_create(h1, NULL);
	lv_obj_t *label_btn_step1 = lv_label_create(btn_step1, NULL);
	lv_btn_set_fit(btn_step1, true, true);
	lv_label_set_static_text(label_btn_step1, SYMBOL_REFRESH"  Step 1: Fast Purge");
	lv_obj_align(btn_step1, btn_instruction, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 4);
	lv_btn_set_action(btn_step1, LV_BTN_ACTION_CLICK, _confirm_step1);

	// NEW: Information / Help Button to replace the messy raw label layout block
	lv_obj_t *btn_help_step1 = lv_btn_create(h1, NULL);
	if (hekate_bg)
	{
		lv_btn_set_style(btn_help_step1, LV_BTN_STYLE_REL, &btn_transp_rel);
		lv_btn_set_style(btn_help_step1, LV_BTN_STYLE_PR, &btn_transp_pr);
	}
	lv_obj_t *label_btn_help_step1 = lv_label_create(btn_help_step1, NULL);
	lv_btn_set_fit(btn_help_step1, true, true);
	lv_label_set_static_text(label_btn_help_step1, SYMBOL_INFO"");
	// Positions the help button cleanly right underneath the execute button
	lv_obj_align(btn_help_step1, btn_step1, LV_ALIGN_OUT_RIGHT_TOP, LV_DPI / 6, 0);
	// Maps the window builder to execution on click event
	lv_btn_set_action(btn_help_step1, LV_BTN_ACTION_CLICK, _create_window_step1_help);

	// Step 2 button.
	lv_obj_t *btn_step2 = lv_btn_create(h1, NULL);
	/* if (hekate_bg)
	{
		lv_btn_set_style(btn_step2, LV_BTN_STYLE_REL, &btn_transp_rel);
		lv_btn_set_style(btn_step2, LV_BTN_STYLE_PR, &btn_transp_pr);
	} */
	lv_obj_t *label_btn_step2 = lv_label_create(btn_step2, NULL);
	lv_btn_set_fit(btn_step2, true, true);
	lv_label_set_static_text(label_btn_step2, SYMBOL_WARNING"  Step 2: Advanced System Reset");
	lv_obj_align(btn_step2, btn_step1, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 4);
	lv_btn_set_action(btn_step2, LV_BTN_ACTION_CLICK, _confirm_step2);

	lv_obj_t *label_inst_step2 = lv_label_create(h1, NULL);
	lv_label_set_recolor(label_inst_step2, true);
	lv_label_set_static_text(label_inst_step2,
		"Remove #C7EA46 reboot_payload.bin# (causing warmboot loop\n"
		"& become unbootable).\n"
		"Delete #C7EA46 atmosphere/config/system_settings.ini#\n"
		"Scrubbing old bloated #C7EA46 atmosphere/cheat_vm_logs#");
	lv_obj_set_style(label_inst_step2, &hint_small_style);
	lv_obj_align(label_inst_step2, btn_step2, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);
	
	// ---- Column 2: Step 3 + Overlay Tools ----
	// Create h2 and populate it fully before aligning, so h1's height is
	// finalised and LV_ALIGN_OUT_RIGHT_TOP places h2 at the correct y=0.
	lv_obj_t *h2 = _create_container(parent);

	lv_obj_t *label_sep2 = lv_label_create(h2, NULL);
	lv_label_set_static_text(label_sep2, "");

	lv_obj_t *label_header_step3 = lv_label_create(h2, NULL);
	lv_label_set_static_text(label_header_step3, "Step 3 - Advanced Repair");
	lv_obj_set_style(label_header_step3, th->label.prim);
	lv_obj_align(label_header_step3, label_sep2, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, -LV_DPI * 3 / 10);

	lv_obj_t *line_sep2 = lv_line_create(h2, line_sep);
	lv_obj_align(line_sep2, label_header_step3, LV_ALIGN_OUT_BOTTOM_LEFT, -(LV_DPI / 4), LV_DPI / 8);

	// Step 3 button.
	lv_obj_t *btn_step3 = lv_btn_create(h2, NULL);
	lv_obj_t *label_btn_step3 = lv_label_create(btn_step3, NULL);
	lv_btn_set_fit(btn_step3, true, true);
	lv_label_set_static_text(label_btn_step3, SYMBOL_DRIVE"  Step 3: Reinstall CFW");
	lv_obj_align(btn_step3, line_sep2, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, LV_DPI / 4);
	lv_btn_set_action(btn_step3, LV_BTN_ACTION_CLICK, _confirm_step3);

	lv_obj_t *label_inst_step3 = lv_label_create(h2, NULL);
	lv_label_set_recolor(label_inst_step3, true);
	lv_label_set_static_text(label_inst_step3,
		"Purges active CFW binaries and prepares extraction\n"
		"from #FF8000 /bootloader/rescue/# staging directory.\n\n"
		"New hekate update will remove this nyx.bin rescue tool.\n"
		"Replace it manually with custom nyx.bin to bootloader/sys/.");
	lv_obj_set_style(label_inst_step3, &hint_small_style);
	lv_obj_align(label_inst_step3, btn_step3, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);

	// Install Overlay Tools button.
	lv_obj_t *btn_overlay = lv_btn_create(h2, NULL);
	lv_obj_t *label_btn_overlay = lv_label_create(btn_overlay, NULL);
	lv_btn_set_fit(btn_overlay, true, true);
	lv_label_set_static_text(label_btn_overlay, SYMBOL_MODULES"  Install Overlay Tools");
	lv_obj_align(btn_overlay, label_inst_step3, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 2);
	lv_btn_set_action(btn_overlay, LV_BTN_ACTION_CLICK, _confirm_install_overlays);

	lv_obj_t *label_inst_overlay = lv_label_create(h2, NULL);
	lv_label_set_recolor(label_inst_overlay, true);
	lv_label_set_static_text(label_inst_overlay,
		"Read notes in Full Instruction button.");
	lv_obj_set_style(label_inst_overlay, &hint_small_style);
	lv_obj_align(label_inst_overlay, btn_overlay, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);
	
	
	// Pin h2 to the top-right of h1 now that both containers are fully built.
	// FIX 3: Force absolute alignment positioning relative to the scrollable container base
	lv_obj_set_pos(h1, LV_DPI / 4, 0);
	lv_obj_set_pos(h2, LV_HOR_RES / 2, 0);
	// FIX 4: Explicitly force total vertical canvas height constraints to enable page scrolling mechanics
	lv_page_set_scrl_height(parent, 540);
}

// ====================================
// END CUSTOM NYX
// ====================================

void create_tab_tools(lv_theme_t *th, lv_obj_t *parent)
{
	lv_obj_t *tv = lv_tabview_create(parent, NULL);

	lv_obj_set_size(tv, LV_HOR_RES, 572);

	static lv_style_t tabview_style;
	lv_style_copy(&tabview_style, th->tabview.btn.rel);
	tabview_style.body.padding.ver = LV_DPI / 8;

	lv_tabview_set_style(tv, LV_TABVIEW_STYLE_BTN_REL, &tabview_style);
	if (hekate_bg)
	{
		lv_tabview_set_style(tv, LV_TABVIEW_STYLE_BTN_PR, &tabview_btn_pr);
		lv_tabview_set_style(tv, LV_TABVIEW_STYLE_BTN_TGL_PR, &tabview_btn_tgl_pr);
	}

	lv_tabview_set_sliding(tv, false);
	lv_tabview_set_btns_pos(tv, LV_TABVIEW_BTNS_POS_BOTTOM);

	lv_obj_t *tab1= lv_tabview_add_tab(tv, "eMMC "SYMBOL_DOT" SD Partitions "SYMBOL_DOT" USB");
	lv_obj_t *tab2 = lv_tabview_add_tab(tv, "Arch bit "SYMBOL_DOT" RCM "SYMBOL_DOT" Touch "SYMBOL_DOT" Pkg1/2");
	
	// CUSTOM NYX RESCUE TAB
	lv_obj_t *tab3 = lv_tabview_add_tab(tv, "System Rescue");

	lv_obj_t *line_sep = lv_line_create(tv, NULL);
	static const lv_point_t line_pp[] = { {0, 0}, { 0, LV_DPI / 4} };
	lv_line_set_points(line_sep, line_pp, 2);
	lv_line_set_style(line_sep, lv_theme_get_current()->line.decor);
	lv_obj_align(line_sep, tv, LV_ALIGN_IN_BOTTOM_MID, -1, -LV_DPI * 2 / 12);

	_create_tab_tools_emmc_sd_usb(th, tab1);
	_create_tab_tools_arc_rcm_pkg12(th, tab2);
	
	// CUSTOM NYX RESCUE TAB
	_create_system_rescue_menu(th, tab3);
	
	lv_tabview_set_tab_act(tv, 0, false);
}
