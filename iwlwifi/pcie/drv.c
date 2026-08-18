// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2005-2014, 2018-2026 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/acpi.h>

#include "fw/acpi.h"

#include "iwl-trans.h"
#include "iwl-drv.h"
#include "iwl-prph.h"
#include "gen1_2/internal.h"

#define _IS_A(cfg, _struct) __builtin_types_compatible_p(typeof(cfg),	\
							 struct _struct)
extern int _invalid_type;
#define _TRANS_CFG_CHECK(cfg)						\
	(__builtin_choose_expr(_IS_A(cfg, iwl_mac_cfg),	\
			       0, _invalid_type))
#define _ASSIGN_CFG(cfg) (_TRANS_CFG_CHECK(cfg) + (kernel_ulong_t)&(cfg))

#define IWL_PCI_DEVICE(dev, subdev, cfg) \
	.vendor = PCI_VENDOR_ID_INTEL,  .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = (subdev), \
	.driver_data = _ASSIGN_CFG(cfg)

/* Hardware specific file defines the PCI IDs table for that hardware module */
VISIBLE_IF_IWLWIFI_KUNIT const struct pci_device_id iwl_hw_card_ids[] = {

#if IS_ENABLED(CPTCFG_IWLMVM)
/* 7260 Series */
	{IWL_PCI_DEVICE(0x08B1, 0x4070, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4072, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4170, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4C60, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4C70, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4060, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x406A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4160, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4062, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4162, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4270, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4272, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4260, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x426A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4262, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4470, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4472, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4460, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x446A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4462, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4870, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x486E, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4A70, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4A6E, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4A6C, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4570, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4560, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4370, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4360, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x5070, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x5072, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x5170, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x5770, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4020, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x402A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4220, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4420, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC070, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC072, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC170, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC060, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC06A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC160, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC062, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC162, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC770, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC760, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC270, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xCC70, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xCC60, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC272, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC260, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC26A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC262, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC470, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC472, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC460, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC462, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC570, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC560, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC370, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC360, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC020, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC02A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC220, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC420, iwl7000_mac_cfg)},

/* 3160 Series */
	{IWL_PCI_DEVICE(0x08B3, 0x0070, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0072, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0170, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0172, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0060, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0062, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x0270, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x0272, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0470, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0472, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x0370, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8070, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8072, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8170, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8172, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8060, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8062, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x8270, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x8370, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x8272, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8470, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8570, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x1070, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x1170, iwl7000_mac_cfg)},

/* 3165 Series */
	{IWL_PCI_DEVICE(0x3165, 0x4010, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x4012, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3166, 0x4212, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x4410, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x4510, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x4110, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3166, 0x4310, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3166, 0x4210, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x8010, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x8110, iwl7000_mac_cfg)},

/* 3168 Series */
	{IWL_PCI_DEVICE(0x24FB, 0x2010, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FB, 0x2110, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FB, 0x2050, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FB, 0x2150, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FB, 0x0000, iwl7000_mac_cfg)},

/* 7265 Series */
	{IWL_PCI_DEVICE(0x095A, 0x5010, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5110, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5100, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5310, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5302, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5210, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5C10, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5012, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5412, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5410, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5510, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5400, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x1010, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5000, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x500A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5200, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5002, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5102, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5202, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9010, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9012, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x900A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9110, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9112, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x9210, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x9200, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9510, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x9310, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9410, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5020, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x502A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5420, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5090, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5190, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5590, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5290, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5490, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5F10, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5212, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x520A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9000, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9400, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9E10, iwl7000_mac_cfg)},

/* 8000 Series */
	{IWL_PCI_DEVICE(0x24F3, 0x0010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x10B0, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0130, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1130, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0132, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1132, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x01F0, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0012, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1012, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0250, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0150, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1150, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0x0030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0x1030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xC010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xC110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xD010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xC050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xD050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xD0B0, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xB0B0, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0x8030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0x9030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0xC030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0xD030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8130, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9130, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8132, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9132, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8150, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9150, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0004, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0044, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F5, 0x0010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F6, 0x0030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0810, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0910, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0850, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0950, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0930, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0000, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x4010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xC030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xD030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1130, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0130, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x10D0, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0150, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x9010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x8110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x8050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x8010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0810, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x9110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x8130, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0910, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0930, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0950, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0850, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1014, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x3E02, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x3E01, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1012, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0012, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0014, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x9074, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1431, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1432, iwl8000_mac_cfg)},

/* 9000 Series */
	{IWL_PCI_DEVICE(0x2526, PCI_ANY_ID, iwl9000_mac_cfg)},
	{IWL_PCI_DEVICE(0x271B, PCI_ANY_ID, iwl9000_mac_cfg)},
	{IWL_PCI_DEVICE(0x271C, PCI_ANY_ID, iwl9000_mac_cfg)},
	{IWL_PCI_DEVICE(0x30DC, PCI_ANY_ID, iwl9560_long_latency_mac_cfg)},
	{IWL_PCI_DEVICE(0x31DC, PCI_ANY_ID, iwl9560_shared_clk_mac_cfg)},
	{IWL_PCI_DEVICE(0x9DF0, PCI_ANY_ID, iwl9560_mac_cfg)},
	{IWL_PCI_DEVICE(0xA370, PCI_ANY_ID, iwl9560_mac_cfg)},

/* Qu devices */
	{IWL_PCI_DEVICE(0x02F0, PCI_ANY_ID, iwl_qu_mac_cfg)},
	{IWL_PCI_DEVICE(0x06F0, PCI_ANY_ID, iwl_qu_mac_cfg)},

	{IWL_PCI_DEVICE(0x34F0, PCI_ANY_ID, iwl_qu_medium_latency_mac_cfg)},
	{IWL_PCI_DEVICE(0x3DF0, PCI_ANY_ID, iwl_qu_medium_latency_mac_cfg)},
	{IWL_PCI_DEVICE(0x4DF0, PCI_ANY_ID, iwl_qu_medium_latency_mac_cfg)},

	{IWL_PCI_DEVICE(0x43F0, PCI_ANY_ID, iwl_qu_long_latency_mac_cfg)},
	{IWL_PCI_DEVICE(0xA0F0, PCI_ANY_ID, iwl_qu_long_latency_mac_cfg)},

	{IWL_PCI_DEVICE(0x2723, PCI_ANY_ID, iwl_ax200_mac_cfg)},

/* Ty/So devices */
	{IWL_PCI_DEVICE(0x2725, PCI_ANY_ID, iwl_ty_mac_cfg)},
	{IWL_PCI_DEVICE(0x7A70, PCI_ANY_ID, iwl_so_long_latency_imr_mac_cfg)},
	{IWL_PCI_DEVICE(0x7AF0, PCI_ANY_ID, iwl_so_mac_cfg)},
	{IWL_PCI_DEVICE(0x51F0, PCI_ANY_ID, iwl_so_long_latency_mac_cfg)},
	{IWL_PCI_DEVICE(0x51F1, PCI_ANY_ID, iwl_so_long_latency_imr_mac_cfg)},
	{IWL_PCI_DEVICE(0x54F0, PCI_ANY_ID, iwl_so_long_latency_mac_cfg)},
	{IWL_PCI_DEVICE(0x7F70, PCI_ANY_ID, iwl_so_mac_cfg)},

/* Ma devices */
	{IWL_PCI_DEVICE(0x2729, PCI_ANY_ID, iwl_ma_mac_cfg)},
	{IWL_PCI_DEVICE(0x7E40, PCI_ANY_ID, iwl_ma_mac_cfg)},
#endif /* CPTCFG_IWLMVM */
#if IS_ENABLED(CPTCFG_IWLMVM) || IS_ENABLED(CPTCFG_IWLMLD)
/* Bz devices */
	{IWL_PCI_DEVICE(0x272b, PCI_ANY_ID, iwl_gl_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0000, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0090, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0094, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0098, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x009C, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x00C0, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x00C4, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x00E0, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x00E4, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x00E8, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x00EC, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0100, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0110, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0114, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0118, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x011C, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0310, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0314, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0510, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0A10, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1671, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1672, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1771, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1772, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1791, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1792, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x4090, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x40C4, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x40E0, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x4110, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x4314, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1775, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1776, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1735, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1736, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0x7740, PCI_ANY_ID, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0x4D40, PCI_ANY_ID, iwl_bz_mac_cfg)},

/* Sc devices */
	{IWL_PCI_DEVICE(0xE440, PCI_ANY_ID, iwl_sc_mac_cfg)},
	{IWL_PCI_DEVICE(0xE340, PCI_ANY_ID, iwl_sc_mac_cfg)},
	{IWL_PCI_DEVICE(0xD340, PCI_ANY_ID, iwl_sc_mac_cfg)},
	{IWL_PCI_DEVICE(0x6E70, PCI_ANY_ID, iwl_sc_mac_cfg)},
	{IWL_PCI_DEVICE(0xD240, PCI_ANY_ID, iwl_sc_mac_cfg)},
	{IWL_PCI_DEVICE(0x9327, PCI_ANY_ID, iwl_sc_mac_cfg)},
#endif /* CPTCFG_IWLMVM || CPTCFG_IWLMLD */

	{0}
};
MODULE_DEVICE_TABLE(pci, iwl_hw_card_ids);
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_hw_card_ids);

#define _IWL_DEV_INFO(_cfg, _name, ...) {	\
	.cfg = &_cfg,				\
	.name = _name,				\
	.device = IWL_CFG_ANY,			\
	.subdevice = IWL_CFG_ANY,		\
	.subdevice_m_h = 15,			\
	__VA_ARGS__				\
}
#define IWL_DEV_INFO(_cfg, _name, ...)		\
	_IWL_DEV_INFO(_cfg, _name, __VA_ARGS__)

#define DEVICE(n)		.device = (n)
#define SUBDEV(n)		.subdevice = (n)
#define _LOWEST_BIT(n)		(__builtin_ffs(n) - 1)
#define _BIT_ABOVE_MASK(n)	((n) + (1 << _LOWEST_BIT(n)))
#define _HIGHEST_BIT(n)		(__builtin_ffs(_BIT_ABOVE_MASK(n)) - 2)
#define _IS_POW2(n)		(((n) & ((n) - 1)) == 0)
#define _IS_CONTIG(n)		_IS_POW2(_BIT_ABOVE_MASK(n))
#define _CHECK_MASK(m)		BUILD_BUG_ON_ZERO(!_IS_CONTIG(m))
#define SUBDEV_MASKED(v, m)	.subdevice = (v) + _CHECK_MASK(m),	\
				.subdevice_m_l = _LOWEST_BIT(m),	\
				.subdevice_m_h = _HIGHEST_BIT(m)
#define RF_TYPE(n)		.match_rf_type = 1,			\
				.rf_type = IWL_CFG_RF_TYPE_##n
#define DISCRETE		.match_discrete = 1,			\
				.discrete = 1
#define INTEGRATED		.match_discrete = 1,			\
				.discrete = 0
#define RF_ID(n)		.match_rf_id = 1,			\
				.rf_id = IWL_CFG_RF_ID_##n
#define NO_CDB			.match_cdb = 1, .cdb = 0
#define CDB			.match_cdb = 1, .cdb = 1
#define BW_NOT_LIMITED		.match_bw_limit = 1, .bw_limit = 0
#define BW_LIMITED		.match_bw_limit = 1, .bw_limit = 1

VISIBLE_IF_IWLWIFI_KUNIT const struct iwl_dev_info iwl_dev_info_table[] = {

#if IS_ENABLED(CPTCFG_IWLMVM)
/* 7260 Series */
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B1)), // unlisted ones fall through to here
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x4060)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x406A)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x4160)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B1), SUBDEV(0x4062)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B1), SUBDEV(0x4162)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x4460)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x446A)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B1), SUBDEV(0x4462)),
	IWL_DEV_INFO(iwl7260_high_temp_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B1), SUBDEV(0x4A70)),
	IWL_DEV_INFO(iwl7260_high_temp_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B1), SUBDEV(0x4A6E)),
	IWL_DEV_INFO(iwl7260_high_temp_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B1), SUBDEV(0x4A6C)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x4560)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x4020)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x402A)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x4420)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC060)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC06A)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC160)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B1), SUBDEV(0xC062)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B1), SUBDEV(0xC162)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC760)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC460)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B1), SUBDEV(0xC462)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC560)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC360)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC020)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC02A)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC420)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B2), SUBDEV(0x4270)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B2), SUBDEV(0x4272)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B2), SUBDEV(0x4260)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B2), SUBDEV(0x426A)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B2), SUBDEV(0x4262)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B2), SUBDEV(0x4370)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B2), SUBDEV(0x4360)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B2), SUBDEV(0x4220)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B2), SUBDEV(0xC270)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B2), SUBDEV(0xC272)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B2), SUBDEV(0xC260)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B2), SUBDEV(0xC26A)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B2), SUBDEV(0xC262)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B2), SUBDEV(0xC370)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B2), SUBDEV(0xC220)),

/* 3160 Series */
	IWL_DEV_INFO(iwl3160_cfg, iwl3160_2ac_name,
		     DEVICE(0x08B3)),

	IWL_DEV_INFO(iwl3160_cfg, iwl3160_n_name,
		     DEVICE(0x08B3), SUBDEV_MASKED(0x62, 0xFF)),
	IWL_DEV_INFO(iwl3160_cfg, iwl3160_2n_name,
		     DEVICE(0x08B3), SUBDEV_MASKED(0x60, 0xFF)),

	IWL_DEV_INFO(iwl3160_cfg, iwl3160_2ac_name,
		     DEVICE(0x08B4)),

/* 3165 Series */
	IWL_DEV_INFO(iwl3165_2ac_cfg, iwl3165_2ac_name,
		     DEVICE(0x3165)),
	IWL_DEV_INFO(iwl3165_2ac_cfg, iwl3165_2ac_name,
		     DEVICE(0x3166)),

/* 3168 Series */
	IWL_DEV_INFO(iwl3168_2ac_cfg, iwl3168_2ac_name,
		     DEVICE(0x24FB)),

/* 7265 Series */
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2ac_name,
		     DEVICE(0x095A)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5000)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x500A)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_n_name,
		     DEVICE(0x095A), SUBDEV(0x5002)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_n_name,
		     DEVICE(0x095A), SUBDEV(0x5102)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5020)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x502A)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5090)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5190)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5100)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5400)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5420)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5490)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5C10)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5590)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x9000)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x900A)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x9400)),

	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2ac_name,
		     DEVICE(0x095B)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095B), SUBDEV(0x520A)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_n_name,
		     DEVICE(0x095B), SUBDEV(0x5302)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095B), SUBDEV(0x5200)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_n_name,
		     DEVICE(0x095B), SUBDEV(0x5202)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095B), SUBDEV(0x9200)),

/* 8000 Series */
	IWL_DEV_INFO(iwl8260_cfg, iwl8260_2ac_name,
		     DEVICE(0x24F3)),
	IWL_DEV_INFO(iwl8260_cfg, iwl8260_2n_name,
		     DEVICE(0x24F3), SUBDEV(0x0004)),
	IWL_DEV_INFO(iwl8260_cfg, iwl8260_2n_name,
		     DEVICE(0x24F3), SUBDEV(0x0044)),
	IWL_DEV_INFO(iwl8260_cfg, iwl8260_2ac_name,
		     DEVICE(0x24F4)),
	IWL_DEV_INFO(iwl8260_cfg, iwl4165_2ac_name,
		     DEVICE(0x24F5)),
	IWL_DEV_INFO(iwl8260_cfg, iwl4165_2ac_name,
		     DEVICE(0x24F6)),
	IWL_DEV_INFO(iwl8265_cfg, iwl8265_2ac_name,
		     DEVICE(0x24FD)),
	IWL_DEV_INFO(iwl8265_cfg, iwl8275_2ac_name,
		     DEVICE(0x24FD), SUBDEV(0x3E02)),
	IWL_DEV_INFO(iwl8265_cfg, iwl8275_2ac_name,
		     DEVICE(0x24FD), SUBDEV(0x3E01)),
	IWL_DEV_INFO(iwl8265_cfg, iwl8275_2ac_name,
		     DEVICE(0x24FD), SUBDEV(0x1012)),
	IWL_DEV_INFO(iwl8265_cfg, iwl8275_2ac_name,
		     DEVICE(0x24FD), SUBDEV(0x0012)),
	IWL_DEV_INFO(iwl8265_cfg, iwl_killer_1435i_name,
		     DEVICE(0x24FD), SUBDEV(0x1431)),
	IWL_DEV_INFO(iwl8265_cfg, iwl_killer_1434_kix_name,
		     DEVICE(0x24FD), SUBDEV(0x1432)),

/* JF1 RF */
	IWL_DEV_INFO(iwl_rf_jf, iwl9461_160_name,
		     RF_TYPE(JF1)),
	IWL_DEV_INFO(iwl_rf_jf_80mhz, iwl9461_name,
		     RF_TYPE(JF1), BW_LIMITED),
	IWL_DEV_INFO(iwl_rf_jf, iwl9462_160_name,
		     RF_TYPE(JF1), RF_ID(JF1_DIV)),
	IWL_DEV_INFO(iwl_rf_jf_80mhz, iwl9462_name,
		     RF_TYPE(JF1), RF_ID(JF1_DIV), BW_LIMITED),
/* JF2 RF */
	IWL_DEV_INFO(iwl_rf_jf, iwl9260_160_name,
		     RF_TYPE(JF2)),
	IWL_DEV_INFO(iwl_rf_jf_80mhz, iwl9260_name,
		     RF_TYPE(JF2), BW_LIMITED),
	IWL_DEV_INFO(iwl_rf_jf, iwl9560_160_name,
		     RF_TYPE(JF2), RF_ID(JF)),
	IWL_DEV_INFO(iwl_rf_jf_80mhz, iwl9560_name,
		     RF_TYPE(JF2), RF_ID(JF), BW_LIMITED),

/* HR RF */
	IWL_DEV_INFO(iwl_rf_hr, iwl_ax201_name, RF_TYPE(HR2)),
	IWL_DEV_INFO(iwl_rf_hr_80mhz, iwl_ax101_name, RF_TYPE(HR1)),
	IWL_DEV_INFO(iwl_rf_hr_80mhz, iwl_ax203_name, RF_TYPE(HR2), BW_LIMITED),
	IWL_DEV_INFO(iwl_rf_hr, iwl_ax200_name, DEVICE(0x2723)),

/* GF RF */
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax211_name, RF_TYPE(GF)),
	IWL_DEV_INFO(iwl_rf_ot, iwl_ax231_name, RF_TYPE(GF), SUBDEV(0x0294)),
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax411_name, RF_TYPE(GF), CDB),
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax210_name, DEVICE(0x2725)),

/* Killer CRFs */
	IWL_DEV_INFO(iwl_rf_jf, iwl9260_killer_1550_name, SUBDEV(0x1550)),
	IWL_DEV_INFO(iwl_rf_jf, iwl9560_killer_1550s_name, SUBDEV(0x1551)),
	IWL_DEV_INFO(iwl_rf_jf, iwl9560_killer_1550i_name, SUBDEV(0x1552)),

	IWL_DEV_INFO(iwl_rf_hr, iwl_ax201_killer_1650s_name, SUBDEV(0x1651)),
	IWL_DEV_INFO(iwl_rf_hr, iwl_ax201_killer_1650i_name, SUBDEV(0x1652)),

	IWL_DEV_INFO(iwl_rf_gf, iwl_ax211_killer_1675s_name, SUBDEV(0x1671)),
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax211_killer_1675i_name, SUBDEV(0x1672)),
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax210_killer_1675w_name, SUBDEV(0x1673)),
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax210_killer_1675x_name, SUBDEV(0x1674)),
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax411_killer_1690s_name, SUBDEV(0x1691)),
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax411_killer_1690i_name, SUBDEV(0x1692)),

/* Killer discrete */
	IWL_DEV_INFO(iwl_rf_hr, iwl_ax200_killer_1650w_name,
		     DEVICE(0x2723), SUBDEV(0x1653)),
	IWL_DEV_INFO(iwl_rf_hr, iwl_ax200_killer_1650x_name,
		     DEVICE(0x2723), SUBDEV(0x1654)),
#endif /* CPTCFG_IWLMVM */
#if IS_ENABLED(CPTCFG_IWLMLD)
/* FM RF */
	IWL_DEV_INFO(iwl_rf_fm, iwl_be201_name, RF_TYPE(FM)),
	IWL_DEV_INFO(iwl_rf_fm, iwl_be401_name, RF_TYPE(FM), CDB),
	IWL_DEV_INFO(iwl_rf_fm, iwl_be200_name, RF_TYPE(FM),
		     DEVICE(0x272B), DISCRETE),
	IWL_DEV_INFO(iwl_rf_fm_160mhz, iwl_be202_name,
		     RF_TYPE(FM), BW_LIMITED),

/* Killer CRFs */
	IWL_DEV_INFO(iwl_rf_fm, iwl_killer_be1750s_name, SUBDEV(0x1771)),
	IWL_DEV_INFO(iwl_rf_fm, iwl_killer_be1750i_name, SUBDEV(0x1772)),
	IWL_DEV_INFO(iwl_rf_fm, iwl_killer_be1790s_name, SUBDEV(0x1791)),
	IWL_DEV_INFO(iwl_rf_fm, iwl_killer_be1790i_name, SUBDEV(0x1792)),
	IWL_DEV_INFO(iwl_rf_fm_160mhz, iwl_killer_be1730x_name, SUBDEV(0x1730)),
	IWL_DEV_INFO(iwl_rf_fm_160mhz, iwl_killer_be1730x_name, SUBDEV(0x1731)),

/* Killer discrete */
	IWL_DEV_INFO(iwl_rf_fm, iwl_killer_be1750w_name,
		     DEVICE(0x272B), SUBDEV(0x1773)),
	IWL_DEV_INFO(iwl_rf_fm, iwl_killer_be1750x_name,
		     DEVICE(0x272B), SUBDEV(0x1774)),

/* WH RF */
	IWL_DEV_INFO(iwl_rf_wh, iwl_be211_name, RF_TYPE(WH)),
	IWL_DEV_INFO(iwl_rf_wh_non_eht, iwl_ax221_name, RF_TYPE(WH),
		     SUBDEV(0x0514)),
	IWL_DEV_INFO(iwl_rf_wh_non_eht, iwl_ax221_name, RF_TYPE(WH),
		     SUBDEV(0x4514)),
	IWL_DEV_INFO(iwl_rf_wh_160mhz, iwl_be213_name, RF_TYPE(WH), BW_LIMITED),

/* PE RF */
	IWL_DEV_INFO(iwl_rf_pe, iwl_bn201_name, RF_TYPE(PE)),
	IWL_DEV_INFO(iwl_rf_pe_no_uhr, iwl_be223_name, RF_TYPE(PE),
		     SUBDEV_MASKED(0x0524, 0xFFF)),
	IWL_DEV_INFO(iwl_rf_pe, iwl_bn203_name, RF_TYPE(PE),
		     SUBDEV_MASKED(0x0324, 0xFFF)),

/* Killer */
	IWL_DEV_INFO(iwl_rf_wh, iwl_killer_be1775s_name, SUBDEV(0x1776)),
	IWL_DEV_INFO(iwl_rf_wh, iwl_killer_be1775i_name, SUBDEV(0x1775)),
	IWL_DEV_INFO(iwl_rf_wh_160mhz, iwl_killer_be1735x_name, SUBDEV(0x1735)),
	IWL_DEV_INFO(iwl_rf_wh_160mhz, iwl_killer_be1735x_name, SUBDEV(0x1736)),

	IWL_DEV_INFO(iwl_rf_pe, iwl_killer_bn1850w2_name, SUBDEV(0x1851)),
	IWL_DEV_INFO(iwl_rf_pe, iwl_killer_bn1850i_name, SUBDEV(0x1852)),
#endif /* CPTCFG_IWLMLD */
};
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_dev_info_table);

/* PCI registers */
#define PCI_CFG_RETRY_TIMEOUT	0x041

const struct iwl_dev_info *
iwl_pci_find_dev_info(u16 device, u16 subsystem_device, u16 rf_type, u8 cdb,
		      u8 rf_id, u8 bw_limit, bool discrete)
{
	int i;

	if (ARRAY_SIZE(iwl_dev_info_table) == 0)
		return NULL;

	for (i = (int)(ARRAY_SIZE(iwl_dev_info_table) - 1); i >= 0; i--) {
		const struct iwl_dev_info *dev_info = &iwl_dev_info_table[i];
		u16 subdevice_mask;

		if (dev_info->device != (u16)IWL_CFG_ANY &&
		    dev_info->device != device)
			continue;

		subdevice_mask = GENMASK(dev_info->subdevice_m_h,
					 dev_info->subdevice_m_l);

		if (dev_info->subdevice != (u16)IWL_CFG_ANY &&
		    dev_info->subdevice != (subsystem_device & subdevice_mask))
			continue;

		if (dev_info->match_rf_type && dev_info->rf_type != rf_type)
			continue;

		if (dev_info->match_cdb && dev_info->cdb != cdb)
			continue;

		if (dev_info->match_rf_id && dev_info->rf_id != rf_id)
			continue;

		if (dev_info->match_bw_limit && dev_info->bw_limit != bw_limit)
			continue;

		if (dev_info->match_discrete && dev_info->discrete != discrete)
			continue;

		return dev_info;
	}

	return NULL;
}
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_pci_find_dev_info);

static int iwl_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const struct iwl_mac_cfg *mac_cfg = (void *)ent->driver_data;
	u8 __iomem *hw_base;
	u32 bar0, hw_rev;
	int ret;

	/* reassign our BAR 0 if invalid due to possible runtime PM races */
	pci_read_config_dword(pdev, PCI_BASE_ADDRESS_0, &bar0);
	if (bar0 == PCI_BASE_ADDRESS_MEM_TYPE_64) {
		ret = pci_assign_resource(pdev, 0);
		if (ret)
			return ret;
	}

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = pcim_request_all_regions(pdev, DRV_NAME);
	if (ret) {
		dev_err(&pdev->dev, "Requesting all PCI BARs failed.\n");
		return ret;
	}

	hw_base = pcim_iomap(pdev, 0, 0);
	if (!hw_base) {
		dev_err(&pdev->dev, "Failed to map BAR 0.\n");
		return -ENOMEM;
	}

	/* We can't use iwl_read32 because trans wasn't allocated */
	hw_rev = readl(hw_base + CSR_HW_REV);
	if (hw_rev == 0xffffffff) {
		dev_err(&pdev->dev, "HW_REV=0xFFFFFFFF, PCI issues?\n");
		return -EIO;
	}

	return iwl_pci_gen1_2_probe(pdev, ent, mac_cfg, hw_base, hw_rev);
}

static void iwl_pci_remove(struct pci_dev *pdev)
{
	struct iwl_trans *trans = pci_get_drvdata(pdev);

	if (!trans)
		return;

	iwl_pcie_gen1_2_remove(trans);
}

#ifdef CONFIG_PM_SLEEP

static int iwl_pci_suspend(struct device *device)
{
	/* Before you put code here, think about WoWLAN. You cannot check here
	 * whether WoWLAN is enabled or not, and your code will run even if
	 * WoWLAN is enabled - don't kill the NIC, someone may need it in Sx.
	 */

	return 0;
}

static int _iwl_pci_resume(struct device *device, bool restore)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct iwl_trans *trans = pci_get_drvdata(pdev);
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	bool device_was_powered_off = false;

	/* Before you put code here, think about WoWLAN. You cannot check here
	 * whether WoWLAN is enabled or not, and your code will run even if
	 * WoWLAN is enabled - the NIC may be alive.
	 */

	/*
	 * We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	pci_write_config_byte(pdev, PCI_CFG_RETRY_TIMEOUT, 0x00);

	if (!trans->op_mode)
		return 0;

	if (test_bit(STATUS_DEVICE_ENABLED, &trans->status)) {
		/*
		 * Scratch value was altered, this means the device was powered
		 * off, we need to reset it completely.
		 * Note: MAC (bits 0:7) will be cleared upon suspend even with
		 * wowlan, but not bits [15:8]. So if we have bits set in lower
		 * word, assume the device is alive.
		 * Alternatively, if the scratch value is 0xFFFFFFFF, then we
		 * no longer have access to the device and consider it powered
		 * off.
		 * For older devices, just try silently to grab the NIC.
		 */
		if (trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_BZ) {
			u32 scratch = iwl_read32(trans, CSR_FUNC_SCRATCH);

			if (!(scratch & CSR_FUNC_SCRATCH_POWER_OFF_MASK) ||
			    scratch == ~0U) {
				IWL_DEBUG_WOWLAN(trans,
						 "Scratch 0x%08x indicates device was powered off\n",
						 scratch);
				device_was_powered_off = true;
			}
		} else {
			/*
			 * bh are re-enabled by iwl_trans_pcie_release_nic_access,
			 * so re-enable them if _iwl_trans_pcie_grab_nic_access
			 * fails.
			 */
			local_bh_disable();
			if (_iwl_trans_pcie_grab_nic_access(trans, true)) {
				iwl_trans_pcie_release_nic_access(trans);
			} else {
				device_was_powered_off = true;
				local_bh_enable();
			}
		}
	}

	if (restore || device_was_powered_off) {
		trans->state = IWL_TRANS_NO_FW;
		/* Hope for the best here ... If one of those steps fails we
		 * won't really know how to recover.
		 */
		iwl_pcie_prepare_card_hw(trans);
		iwl_trans_activate_nic(trans);
		iwl_op_mode_device_powered_off(trans->op_mode);
	}

	/* In WOWLAN, let iwl_trans_pcie_d3_resume do the rest of the work */
	if (test_bit(STATUS_DEVICE_ENABLED, &trans->status))
		return 0;

	/* reconfigure the MSI-X mapping to get the correct IRQ for rfkill */
	iwl_pcie_conf_msix_hw(trans_pcie);

	/*
	 * Enable rfkill interrupt (in order to keep track of the rfkill
	 * status). Must be locked to avoid processing a possible rfkill
	 * interrupt while in iwl_pcie_check_hw_rf_kill().
	 */
	mutex_lock(&trans_pcie->mutex);
	iwl_enable_rfkill_int(trans);
	iwl_pcie_check_hw_rf_kill(trans);
	mutex_unlock(&trans_pcie->mutex);

	return 0;
}

static int iwl_pci_restore(struct device *device)
{
	return _iwl_pci_resume(device, true);
}

static int iwl_pci_resume(struct device *device)
{
	return _iwl_pci_resume(device, false);
}

static const struct dev_pm_ops iwl_dev_pm_ops = {
	.suspend = pm_sleep_ptr(iwl_pci_suspend),
	.resume = pm_sleep_ptr(iwl_pci_resume),
	.freeze = pm_sleep_ptr(iwl_pci_suspend),
	.thaw = pm_sleep_ptr(iwl_pci_resume),
	.poweroff = pm_sleep_ptr(iwl_pci_suspend),
	.restore = pm_sleep_ptr(iwl_pci_restore),
};

#define IWL_PM_OPS	(&iwl_dev_pm_ops)

#else /* CONFIG_PM_SLEEP */

#define IWL_PM_OPS	NULL

#endif /* CONFIG_PM_SLEEP */

static void iwl_pci_dump(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct iwl_trans *trans = pci_get_drvdata(pdev);

	iwl_op_mode_dump(trans->op_mode);
}

static struct pci_driver iwl_pci_driver = {
	.name = DRV_NAME,
	.id_table = iwl_hw_card_ids,
	.probe = iwl_pci_probe,
	.remove = iwl_pci_remove,
	.driver.pm = IWL_PM_OPS,
	.driver.coredump = iwl_pci_dump,
};

int __must_check iwl_pci_register_driver(void)
{
	int ret;
	ret = pci_register_driver(&iwl_pci_driver);
	if (ret)
		pr_err("Unable to initialize PCI module\n");

	return ret;
}

void iwl_pci_unregister_driver(void)
{
	pci_unregister_driver(&iwl_pci_driver);
}
