// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Loongson Technology Corporation Limited.
 */

#include <linux/edac.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include "edac_module.h"

enum ecc_index {
	ECC_SET = 0,
	ECC_RESERVED,
	ECC_COUNT,
	ECC_CS_COUNT,
	ECC_CODE,
	ECC_ADDR,
	ECC_DATA0,
	ECC_DATA1,
	ECC_DATA2,
	ECC_DATA3,
};

struct loongson_edac_pvt {
	u64 *ecc_base;
	int last_ce_count;
};

static int read_ecc(struct mem_ctl_info *mci)
{
	struct loongson_edac_pvt *pvt = mci->pvt_info;
	u64 ecc;
	int cs;

	if (!pvt->ecc_base)
		return pvt->last_ce_count;

	ecc = pvt->ecc_base[ECC_CS_COUNT];
	/* cs0 -- cs3 */
	cs = ecc & 0xff;
	cs += (ecc >> 8) & 0xff;
	cs += (ecc >> 16) & 0xff;
	cs += (ecc >> 24) & 0xff;

	return cs;
}

static void edac_check(struct mem_ctl_info *mci)
{
	struct loongson_edac_pvt *pvt = mci->pvt_info;
	int new, add;

	new = read_ecc(mci);
	add = new - pvt->last_ce_count;
	pvt->last_ce_count = new;
	if (add <= 0)
		return;

	edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, add,
			     0, 0, 0, 0, 0, -1, "error", "");
	edac_mc_printk(mci, KERN_INFO, "add: %d", add);
}

static int get_dimm_config(struct mem_ctl_info *mci)
{
	struct dimm_info *dimm;
	u32 size, npages;

	/* size not used */
	size = -1;
	npages = MiB_TO_PAGES(size);

	dimm = edac_get_dimm(mci, 0, 0, 0);
	dimm->nr_pages = npages;
	snprintf(dimm->label, sizeof(dimm->label),
		 "MC#%uChannel#%u_DIMM#%u", mci->mc_idx, 0, 0);
	dimm->grain = 8;

	return 0;
}

static void pvt_init(struct mem_ctl_info *mci, u64 *vbase)
{
	struct loongson_edac_pvt *pvt = mci->pvt_info;

	pvt->ecc_base = vbase;
	pvt->last_ce_count = read_ecc(mci);
}

static int edac_probe(struct platform_device *pdev)
{
	struct edac_mc_layer layers[2];
	struct loongson_edac_pvt *pvt;
	struct mem_ctl_info *mci;
	u64 *vbase;
	int ret;

	vbase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(vbase))
		return PTR_ERR(vbase);

	/* allocate a new MC control structure */
	layers[0].type = EDAC_MC_LAYER_CHANNEL;
	layers[0].size = 1;
	layers[0].is_virt_csrow = false;
	layers[1].type = EDAC_MC_LAYER_SLOT;
	layers[1].size = 1;
	layers[1].is_virt_csrow = true;
	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers, sizeof(*pvt));
	if (mci == NULL)
		return -ENOMEM;

	mci->mc_idx = edac_device_alloc_index();
	mci->mtype_cap = MEM_FLAG_RDDR4;
	mci->edac_ctl_cap = EDAC_FLAG_NONE;
	mci->edac_cap = EDAC_FLAG_NONE;
	mci->mod_name = "loongson_edac.c";
	mci->ctl_name = "loongson_edac_ctl";
	mci->dev_name = "loongson_edac_dev";
	mci->ctl_page_to_phys = NULL;
	mci->pdev = &pdev->dev;
	mci->error_desc.grain = 8;
	/* Set the function pointer to an actual operation function */
	mci->edac_check = edac_check;

	pvt_init(mci, vbase);
	get_dimm_config(mci);

	ret = edac_mc_add_mc(mci);
	if (ret) {
		edac_dbg(0, "MC: failed edac_mc_add_mc()\n");
		edac_mc_free(mci);
		return ret;
	}
	edac_op_state = EDAC_OPSTATE_POLL;

	return 0;
}

static void edac_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = edac_mc_del_mc(&pdev->dev);

	if (mci)
		edac_mc_free(mci);
}

static const struct of_device_id loongson_edac_of_match[] = {
	{ .compatible = "loongson,ls3a5000-mc-edac", },
	{}
};
MODULE_DEVICE_TABLE(of, loongson_edac_of_match);

static struct platform_driver loongson_edac_driver = {
	.probe		= edac_probe,
	.remove		= edac_remove,
	.driver		= {
		.name	= "loongson-mc-edac",
		.of_match_table = loongson_edac_of_match,
	},
};
module_platform_driver(loongson_edac_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zhao Qunqin <zhaoqunqin@loongson.cn>");
MODULE_DESCRIPTION("EDAC driver for loongson memory controller");
