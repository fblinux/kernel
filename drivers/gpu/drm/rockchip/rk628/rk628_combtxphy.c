// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <linux/mfd/rk628.h>

#define REG(x)			((x) + 0x90000)

#define COMBTXPHY_CON0		REG(0x0000)
#define SW_TX_IDLE_MASK		GENMASK(29, 20)
#define SW_TX_IDLE(x)		UPDATE(x, 29, 20)
#define SW_TX_PD_MASK		GENMASK(17, 8)
#define SW_TX_PD(x)		UPDATE(x, 17, 8)
#define SW_BUS_WIDTH_MASK	GENMASK(6, 5)
#define SW_BUS_WIDTH_7BIT	UPDATE(0x3, 6, 5)
#define SW_BUS_WIDTH_8BIT	UPDATE(0x2, 6, 5)
#define SW_BUS_WIDTH_9BIT	UPDATE(0x1, 6, 5)
#define SW_BUS_WIDTH_10BIT	UPDATE(0x0, 6, 5)
#define SW_PD_PLL_MASK		BIT(4)
#define SW_PD_PLL		BIT(4)
#define SW_GVI_LVDS_EN_MASK	BIT(3)
#define SW_GVI_LVDS_EN		BIT(3)
#define SW_MIPI_DSI_EN_MASK	BIT(2)
#define SW_MIPI_DSI_EN		BIT(2)
#define SW_MODULEB_EN_MASK	BIT(1)
#define SW_MODULEB_EN		BIT(1)
#define SW_MODULEA_EN_MASK	BIT(0)
#define SW_MODULEA_EN		BIT(0)
#define COMBTXPHY_CON1		REG(0x0004)
#define COMBTXPHY_CON2		REG(0x0008)
#define COMBTXPHY_CON3		REG(0x000c)
#define COMBTXPHY_CON4		REG(0x0010)
#define COMBTXPHY_CON5		REG(0x0014)
#define SW_RATE(x)		UPDATE(x, 26, 24)
#define SW_REF_DIV(x)		UPDATE(x, 20, 16)
#define SW_PLL_FB_DIV(x)	UPDATE(x, 14, 10)
#define SW_PLL_FRAC_DIV(x)	UPDATE(x, 9, 0)
#define COMBTXPHY_CON6		REG(0x0018)
#define COMBTXPHY_CON7		REG(0x001c)
#define SW_TX_RTERM_MASK	GENMASK(22, 20)
#define SW_TX_RTERM(x)		UPDATE(x, 22, 20)
#define SW_TX_MODE_MASK		GENMASK(17, 16)
#define SW_TX_MODE(x)		UPDATE(x, 17, 16)
#define SW_TX_CTL_CON5_MASK	BIT(10)
#define SW_TX_CTL_CON5(x)	UPDATE(x, 10, 10)
#define SW_TX_CTL_CON4_MASK	GENMASK(9, 8)
#define SW_TX_CTL_CON4(x)	UPDATE(x, 9, 8)
#define COMBTXPHY_CON8		REG(0x0020)
#define COMBTXPHY_CON9		REG(0x0024)
#define SW_DSI_FSET_EN_MASK	BIT(29)
#define SW_DSI_FSET_EN		BIT(29)
#define SW_DSI_RCAL_EN_MASK	BIT(28)
#define SW_DSI_RCAL_EN		BIT(28)
#define COMBTXPHY_CON10		REG(0x0028)
#define TX9_CKDRV_EN		BIT(9)
#define TX8_CKDRV_EN		BIT(8)
#define TX7_CKDRV_EN		BIT(7)
#define TX6_CKDRV_EN		BIT(6)
#define TX5_CKDRV_EN		BIT(5)
#define TX4_CKDRV_EN		BIT(4)
#define TX3_CKDRV_EN		BIT(3)
#define TX2_CKDRV_EN		BIT(2)
#define TX1_CKDRV_EN		BIT(1)
#define TX0_CKDRV_EN		BIT(0)
#define COMBTXPHY_MAX_REGISTER	COMBTXPHY_CON10

struct rk628_combtxphy {
	struct device *dev;
	struct rk628 *parent;
	struct regmap *grf;
	struct regmap *regmap;
	struct clk *pclk;
	struct reset_control *rstc;
	enum phy_mode mode;
	unsigned int flags;

	u8 ref_div;
	u8 fb_div;
	u16 frac_div;
	u8 rate_div;
};

static int rk628_combtxphy_dsi_power_on(struct rk628_combtxphy *combtxphy)
{
	u32 val;
	int ret;

	regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON0,
			   SW_BUS_WIDTH_MASK | SW_GVI_LVDS_EN_MASK |
			   SW_MIPI_DSI_EN_MASK,
			   SW_BUS_WIDTH_8BIT | SW_MIPI_DSI_EN);

	if (combtxphy->flags & COMBTXPHY_MODULEA_EN)
		regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON0,
				   SW_MODULEA_EN_MASK, SW_MODULEA_EN);
	if (combtxphy->flags & COMBTXPHY_MODULEB_EN)
		regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON0,
				   SW_MODULEB_EN_MASK, SW_MODULEB_EN);

	regmap_write(combtxphy->regmap, COMBTXPHY_CON5,
		     SW_REF_DIV(combtxphy->ref_div - 1) |
		     SW_PLL_FB_DIV(combtxphy->fb_div) |
		     SW_PLL_FRAC_DIV(combtxphy->frac_div) |
		     SW_RATE(combtxphy->rate_div / 2));

	regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON0,
			   SW_PD_PLL, 0);

	ret = regmap_read_poll_timeout(combtxphy->grf, GRF_DPHY0_STATUS,
				       val, val & DPHY_PHYLOCK, 0, 1000);
	if (ret < 0) {
		dev_err(combtxphy->dev, "phy is not lock\n");
		return ret;
	}

	regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON9,
			   SW_DSI_FSET_EN_MASK | SW_DSI_RCAL_EN_MASK,
			   SW_DSI_FSET_EN | SW_DSI_RCAL_EN);

	usleep_range(200, 400);

	return 0;
}

static int rk628_combtxphy_lvds_power_on(struct rk628_combtxphy *combtxphy)
{
	u32 val;
	int ret;

	regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON7,
			   SW_TX_MODE_MASK, SW_TX_MODE(3));
	regmap_write(combtxphy->regmap, COMBTXPHY_CON10,
		     TX7_CKDRV_EN | TX2_CKDRV_EN);
	regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON0,
			   SW_BUS_WIDTH_MASK | SW_GVI_LVDS_EN_MASK |
			   SW_MIPI_DSI_EN_MASK,
			   SW_BUS_WIDTH_7BIT | SW_GVI_LVDS_EN);

	if (combtxphy->flags & COMBTXPHY_MODULEA_EN)
		regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON0,
				   SW_MODULEA_EN_MASK, SW_MODULEA_EN);
	if (combtxphy->flags & COMBTXPHY_MODULEB_EN)
		regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON0,
				   SW_MODULEB_EN_MASK, SW_MODULEB_EN);

	regmap_write(combtxphy->regmap, COMBTXPHY_CON5,
		     SW_REF_DIV(combtxphy->ref_div - 1) |
		     SW_PLL_FB_DIV(combtxphy->fb_div) |
		     SW_PLL_FRAC_DIV(combtxphy->frac_div) |
		     SW_RATE(combtxphy->rate_div / 2));
	regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON0,
			   SW_PD_PLL | SW_TX_PD_MASK, 0);

	ret = regmap_read_poll_timeout(combtxphy->grf, GRF_DPHY0_STATUS,
				       val, val & DPHY_PHYLOCK, 0, 1000);
	if (ret < 0) {
		dev_info(combtxphy->dev, "phy is not lock\n");
		return ret;
	}

	usleep_range(100, 200);
	regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON0,
			   SW_TX_IDLE_MASK, 0);

	return 0;
}

static int rk628_combtxphy_gvi_power_on(struct rk628_combtxphy *combtxphy)
{
	regmap_write(combtxphy->regmap, COMBTXPHY_CON5,
		     SW_REF_DIV(combtxphy->ref_div - 1) |
		     SW_PLL_FB_DIV(combtxphy->fb_div) |
		     SW_PLL_FRAC_DIV(combtxphy->frac_div) |
		     SW_RATE(combtxphy->rate_div / 2));
	regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON0,
			   SW_BUS_WIDTH_MASK | SW_GVI_LVDS_EN_MASK |
			   SW_MIPI_DSI_EN_MASK |
			   SW_MODULEB_EN_MASK | SW_MODULEA_EN_MASK,
			   SW_BUS_WIDTH_10BIT | SW_GVI_LVDS_EN |
			   SW_MODULEB_EN | SW_MODULEA_EN);

	regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON0,
			   SW_PD_PLL | SW_TX_PD_MASK, 0);
	usleep_range(100, 200);
	regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON0,
			   SW_TX_IDLE_MASK, 0);

	return 0;
}

static int rk628_combtxphy_power_on(struct phy *phy)
{
	struct rk628_combtxphy *combtxphy = phy_get_drvdata(phy);

	clk_prepare_enable(combtxphy->pclk);
	reset_control_assert(combtxphy->rstc);
	udelay(10);
	reset_control_deassert(combtxphy->rstc);
	udelay(10);

	regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON0,
			   SW_TX_IDLE_MASK | SW_TX_PD_MASK | SW_PD_PLL_MASK,
			   SW_TX_IDLE(0x3ff) | SW_TX_PD(0x3ff) | SW_PD_PLL);

	switch (combtxphy->mode) {
	case PHY_MODE_VIDEO_MIPI:
		regmap_update_bits(combtxphy->grf, GRF_POST_PROC_CON,
				   SW_TXPHY_REFCLK_SEL_MASK,
				   SW_TXPHY_REFCLK_SEL(0));
		return rk628_combtxphy_dsi_power_on(combtxphy);
	case PHY_MODE_VIDEO_LVDS:
		regmap_update_bits(combtxphy->grf, GRF_POST_PROC_CON,
				   SW_TXPHY_REFCLK_SEL_MASK,
				   SW_TXPHY_REFCLK_SEL(1));
		return rk628_combtxphy_lvds_power_on(combtxphy);
	case PHY_MODE_GVI:
		regmap_update_bits(combtxphy->grf, GRF_POST_PROC_CON,
				   SW_TXPHY_REFCLK_SEL_MASK,
				   SW_TXPHY_REFCLK_SEL(0));
		return rk628_combtxphy_gvi_power_on(combtxphy);
	default:
		return -EINVAL;
	}

	return 0;
}

static int rk628_combtxphy_power_off(struct phy *phy)
{
	struct rk628_combtxphy *combtxphy = phy_get_drvdata(phy);

	regmap_update_bits(combtxphy->regmap, COMBTXPHY_CON0,
			   SW_TX_IDLE_MASK | SW_TX_PD_MASK | SW_PD_PLL_MASK |
			   SW_MODULEB_EN_MASK | SW_MODULEA_EN_MASK,
			   SW_TX_IDLE(0x3ff) | SW_TX_PD(0x3ff) | SW_PD_PLL);

	clk_disable_unprepare(combtxphy->pclk);

	return 0;
}

static int rk628_combtxphy_set_mode(struct phy *phy, enum phy_mode mode)
{
	struct rk628_combtxphy *combtxphy = phy_get_drvdata(phy);
	unsigned int fvco, frac_rate, fin = 24;

	switch (mode) {
	case PHY_MODE_VIDEO_MIPI:
	{
		int bus_width = phy_get_bus_width(phy);
		unsigned int fhsc = bus_width >> 8;
		unsigned int flags = bus_width & 0xff;

		fhsc = fin * (fhsc / fin);

		if (fhsc < 80 || fhsc > 1500)
			return -EINVAL;
		else if (fhsc < 375)
			combtxphy->rate_div = 4;
		else if (fhsc < 750)
			combtxphy->rate_div = 2;
		else
			combtxphy->rate_div = 1;

		combtxphy->flags = flags;

		fvco = fhsc * 2 * combtxphy->rate_div;
		combtxphy->ref_div = 1;
		combtxphy->fb_div = fvco / 8 / fin;
		frac_rate = fvco - (fin * 8 * combtxphy->fb_div);

		if (frac_rate) {
			frac_rate <<= 10;
			frac_rate /= fin * 8;
			combtxphy->frac_div = frac_rate;
		} else {
			combtxphy->frac_div = 0;
		}

		fvco = fin * (1024 * combtxphy->fb_div + combtxphy->frac_div);
		fvco *= 8;
		fvco = DIV_ROUND_UP(fvco, 1024 * combtxphy->ref_div);
		fhsc = fvco / 2 / combtxphy->rate_div;
		phy_set_bus_width(phy, fhsc);
		break;
	}
	case PHY_MODE_VIDEO_LVDS:
	{
		int bus_width = phy_get_bus_width(phy);
		unsigned int flags = bus_width & 0xff;
		unsigned int rate = (bus_width >> 8) * 7;

		combtxphy->flags = flags;
		combtxphy->ref_div = 1;
		combtxphy->fb_div = 14;
		combtxphy->frac_div = 0;

		if (rate < 500)
			combtxphy->rate_div = 4;
		else if (rate < 1000)
			combtxphy->rate_div = 2;
		else
			combtxphy->rate_div = 1;
		break;
	}
	case PHY_MODE_GVI:
	{
		unsigned int fhsc = phy_get_bus_width(phy) & 0xfff;

		if (fhsc < 500 || fhsc > 4000)
			return -EINVAL;
		else if (fhsc < 1000)
			combtxphy->rate_div = 4;
		else if (fhsc < 2000)
			combtxphy->rate_div = 2;
		else
			combtxphy->rate_div = 1;
		fvco = fhsc * combtxphy->rate_div;

		combtxphy->ref_div = 1;
		combtxphy->fb_div = fvco / 8 / fin;
		frac_rate = fvco - (fin * 8 * combtxphy->fb_div);

		if (frac_rate) {
			frac_rate <<= 10;
			frac_rate /= fin * 8;
			combtxphy->frac_div = frac_rate;
		} else {
			combtxphy->frac_div = 0;
		}

		fvco = fin * (1024 * combtxphy->fb_div + combtxphy->frac_div);
		fvco *= 8;
		fvco /= 1024 * combtxphy->ref_div;
		fhsc = fvco / combtxphy->rate_div;
		phy_set_bus_width(phy, fhsc);

		break;
	}
	default:
		return -EINVAL;
	}

	combtxphy->mode = mode;

	return 0;
}

static const struct phy_ops rk628_combtxphy_ops = {
	.set_mode = rk628_combtxphy_set_mode,
	.power_on = rk628_combtxphy_power_on,
	.power_off = rk628_combtxphy_power_off,
	.owner = THIS_MODULE,
};

static const struct regmap_range rk628_combtxphy_readable_ranges[] = {
	regmap_reg_range(COMBTXPHY_CON0, COMBTXPHY_CON10),
};

static const struct regmap_access_table rk628_combtxphy_readable_table = {
	.yes_ranges     = rk628_combtxphy_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_combtxphy_readable_ranges),
};

static const struct regmap_config rk628_combtxphy_regmap_cfg = {
	.name = "combtxphy",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.cache_type = REGCACHE_RBTREE,
	.max_register = COMBTXPHY_MAX_REGISTER,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.rd_table = &rk628_combtxphy_readable_table,
};

static int rk628_combtxphy_probe(struct platform_device *pdev)
{
	struct rk628 *rk628 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk628_combtxphy *combtxphy;
	struct phy_provider *phy_provider;
	struct phy *phy;
	int ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	combtxphy = devm_kzalloc(dev, sizeof(*combtxphy), GFP_KERNEL);
	if (!combtxphy)
		return -ENOMEM;

	combtxphy->dev = dev;
	combtxphy->parent = rk628;
	combtxphy->grf = rk628->grf;
	platform_set_drvdata(pdev, combtxphy);

	combtxphy->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(combtxphy->pclk))
		return PTR_ERR(combtxphy->pclk);

	combtxphy->rstc = of_reset_control_get(dev->of_node, NULL);
	if (IS_ERR(combtxphy->rstc)) {
		ret = PTR_ERR(combtxphy->rstc);
		dev_err(dev, "failed to get reset control: %d\n", ret);
		return ret;
	}

	combtxphy->regmap = devm_regmap_init_i2c(rk628->client,
						 &rk628_combtxphy_regmap_cfg);
	if (IS_ERR(combtxphy->regmap)) {
		ret = PTR_ERR(combtxphy->regmap);
		dev_err(dev, "failed to allocate register map: %d\n", ret);
		return ret;
	}

	phy = devm_phy_create(dev, NULL, &rk628_combtxphy_ops);
	if (IS_ERR(phy)) {
		ret = PTR_ERR(phy);
		dev_err(dev, "failed to create phy: %d\n", ret);
		return ret;
	}

	phy_set_drvdata(phy, combtxphy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		ret = PTR_ERR(phy_provider);
		dev_err(dev, "failed to register phy provider: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id rk628_combtxphy_of_match[] = {
	{ .compatible = "rockchip,rk628-combtxphy", },
	{}
};
MODULE_DEVICE_TABLE(of, rk628_combtxphy_of_match);

static struct platform_driver rk628_combtxphy_driver = {
	.driver = {
		.name = "rk628-combtxphy",
		.of_match_table	= of_match_ptr(rk628_combtxphy_of_match),
	},
	.probe = rk628_combtxphy_probe,
};
module_platform_driver(rk628_combtxphy_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK628 GVI/LVDS/MIPI Combo TX PHY driver");
MODULE_LICENSE("GPL v2");
