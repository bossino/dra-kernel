/**
 * dwc3-omap.c - OMAP Specific Glue layer
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *	    Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/platform_data/dwc3-omap.h>
#include <linux/usb/dwc3-omap.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/extcon.h>
#include <linux/extcon/of_extcon.h>
#include <linux/regulator/consumer.h>

#include <linux/usb/otg.h>
#include <linux/usb/dwc3-omap.h>
#include <linux/usb/of.h>

/*
 * All these registers belong to OMAP's Wrapper around the
 * DesignWare USB3 Core.
 */

#define USBOTGSS_REVISION			0x0000
#define USBOTGSS_SYSCONFIG			0x0010
#define USBOTGSS_IRQ_EOI			0x0020
#define USBOTGSS_IRQSTATUS_RAW_0		0x0024
#define USBOTGSS_IRQSTATUS_0			0x0028
#define USBOTGSS_IRQENABLE_SET_0		0x002c
#define USBOTGSS_IRQENABLE_CLR_0		0x0030
#define USBOTGSS_IRQSTATUS_RAW_1		0x0034
#define USBOTGSS_IRQSTATUS_1			0x0038
#define USBOTGSS_IRQENABLE_SET_1		0x003c
#define USBOTGSS_IRQENABLE_CLR_1		0x0040
#define USBOTGSS_UTMI_OTG_CTRL			0x0080
#define USBOTGSS_UTMI_OTG_STATUS		0x0084
#define USBOTGSS_MMRAM_OFFSET			0x0100
#define USBOTGSS_FLADJ				0x0104
#define USBOTGSS_DEBUG_CFG			0x0108
#define USBOTGSS_DEBUG_DATA			0x010c

/* SYSCONFIG REGISTER */
#define USBOTGSS_SYSCONFIG_DMADISABLE		(1 << 16)

/* IRQ_EOI REGISTER */
#define USBOTGSS_IRQ_EOI_LINE_NUMBER		(1 << 0)

/* IRQS0 BITS */
#define USBOTGSS_IRQO_COREIRQ_ST		(1 << 0)

/* IRQ1 BITS */
#define USBOTGSS_IRQ1_DMADISABLECLR		(1 << 17)
#define USBOTGSS_IRQ1_OEVT			(1 << 16)
#define USBOTGSS_IRQ1_DRVVBUS_RISE		(1 << 13)
#define USBOTGSS_IRQ1_CHRGVBUS_RISE		(1 << 12)
#define USBOTGSS_IRQ1_DISCHRGVBUS_RISE		(1 << 11)
#define USBOTGSS_IRQ1_IDPULLUP_RISE		(1 << 8)
#define USBOTGSS_IRQ1_DRVVBUS_FALL		(1 << 5)
#define USBOTGSS_IRQ1_CHRGVBUS_FALL		(1 << 4)
#define USBOTGSS_IRQ1_DISCHRGVBUS_FALL		(1 << 3)
#define USBOTGSS_IRQ1_IDPULLUP_FALL		(1 << 0)

/* UTMI_OTG_CTRL REGISTER */
#define USBOTGSS_UTMI_OTG_CTRL_DRVVBUS		(1 << 5)
#define USBOTGSS_UTMI_OTG_CTRL_CHRGVBUS		(1 << 4)
#define USBOTGSS_UTMI_OTG_CTRL_DISCHRGVBUS	(1 << 3)
#define USBOTGSS_UTMI_OTG_CTRL_IDPULLUP		(1 << 0)

/* UTMI_OTG_STATUS REGISTER */
#define USBOTGSS_UTMI_OTG_STATUS_SW_MODE	(1 << 31)
#define USBOTGSS_UTMI_OTG_STATUS_POWERPRESENT	(1 << 9)
#define USBOTGSS_UTMI_OTG_STATUS_TXBITSTUFFENABLE (1 << 8)
#define USBOTGSS_UTMI_OTG_STATUS_IDDIG		(1 << 4)
#define USBOTGSS_UTMI_OTG_STATUS_SESSEND	(1 << 3)
#define USBOTGSS_UTMI_OTG_STATUS_SESSVALID	(1 << 2)
#define USBOTGSS_UTMI_OTG_STATUS_VBUSVALID	(1 << 1)

struct dwc3_omap {
	/* device lock */
	spinlock_t		lock;

	struct device		*dev;

	int			irq;
	void __iomem		*base;

	void			*context;
	u32			resource_size;

	u32			dma_status:1;
	u64			dma_mask;
	struct extcon_specific_cable_nb extcon_vbus_dev;
	struct extcon_specific_cable_nb extcon_id_dev;
	struct notifier_block	vbus_nb;
	struct notifier_block	id_nb;
};

struct dwc3_omap		*_omap;

static inline u32 dwc3_omap_readl(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

static inline void dwc3_omap_writel(void __iomem *base, u32 offset, u32 value)
{
	writel(value, base + offset);
}

int dwc3_omap_usbvbus_id_handler(struct device *dev,
	enum omap_dwc3_vbus_id_status status)
{
	u32			val;
	struct dwc3_omap	*omap;
	struct platform_device	*pdev;

	if (!dev)
		return -ENODEV;

	dev_dbg(omap->dev, "VBUS Connect\n");

	pdev = to_platform_device(dev);
	omap =  platform_get_drvdata(pdev);
	if (!omap)
		return -ENODEV;

	val = dwc3_omap_readl(omap->base, USBOTGSS_UTMI_OTG_STATUS);

	switch (status) {
	case OMAP_DWC3_ID_GROUND:
		dev_dbg(omap->dev, "ID GND\n");
		val &= ~(USBOTGSS_UTMI_OTG_STATUS_IDDIG
				| USBOTGSS_UTMI_OTG_STATUS_VBUSVALID
				| USBOTGSS_UTMI_OTG_STATUS_SESSEND);
		val |= USBOTGSS_UTMI_OTG_STATUS_SESSVALID
				| USBOTGSS_UTMI_OTG_STATUS_POWERPRESENT;
		break;

	case OMAP_DWC3_VBUS_VALID:
		dev_dbg(omap->dev, "VBUS Connect\n");
		val &= ~USBOTGSS_UTMI_OTG_STATUS_SESSEND;
		val |= USBOTGSS_UTMI_OTG_STATUS_IDDIG
				| USBOTGSS_UTMI_OTG_STATUS_VBUSVALID
				| USBOTGSS_UTMI_OTG_STATUS_SESSVALID
				| USBOTGSS_UTMI_OTG_STATUS_POWERPRESENT;
		break;

	case OMAP_DWC3_ID_FLOAT:
	case OMAP_DWC3_VBUS_OFF:
		dev_dbg(omap->dev, "VBUS Disconnect\n");

		val &= ~(USBOTGSS_UTMI_OTG_STATUS_SESSVALID
				| USBOTGSS_UTMI_OTG_STATUS_VBUSVALID
				| USBOTGSS_UTMI_OTG_STATUS_POWERPRESENT);
		val |= USBOTGSS_UTMI_OTG_STATUS_SESSEND
				| USBOTGSS_UTMI_OTG_STATUS_IDDIG;
		break;

	default:
		dev_dbg(omap->dev, "ID float\n");
	}

	dwc3_omap_writel(omap->base, USBOTGSS_UTMI_OTG_STATUS, val);
	return 0;
}
EXPORT_SYMBOL_GPL(dwc3_omap_usbvbus_id_handler);

int dwc3_omap_mailbox(enum omap_dwc3_vbus_id_status status)
{
	struct dwc3_omap	*omap = _omap;

	if (!omap) {
		dev_dbg(omap->dev, "not ready , deferring\n");
		return -EPROBE_DEFER;
	}
	return dwc3_omap_usbvbus_id_handler(omap->dev, status);
}
EXPORT_SYMBOL_GPL(dwc3_omap_mailbox);

static int dwc3_omap_id_notifier(struct notifier_block *nb,
	unsigned long event, void *ptr)
{
	struct dwc3_omap *omap = container_of(nb, struct dwc3_omap, id_nb);

	if (event)
		dwc3_omap_usbvbus_id_handler(omap->dev, OMAP_DWC3_ID_GROUND);
	else
		dwc3_omap_usbvbus_id_handler(omap->dev, OMAP_DWC3_VBUS_VALID);

	return NOTIFY_DONE;
}

static int dwc3_omap_vbus_notifier(struct notifier_block *nb,
	unsigned long event, void *ptr)
{
	struct dwc3_omap *omap = container_of(nb, struct dwc3_omap, vbus_nb);

	if (event)
		dwc3_omap_usbvbus_id_handler(omap->dev, OMAP_DWC3_VBUS_VALID);
	else
		dwc3_omap_usbvbus_id_handler(omap->dev, OMAP_DWC3_VBUS_OFF);

	return NOTIFY_DONE;
}

static irqreturn_t dwc3_omap_interrupt(int irq, void *_omap)
{
	struct dwc3_omap	*omap = _omap;
	u32			reg;

	spin_lock(&omap->lock);

	reg = dwc3_omap_readl(omap->base, USBOTGSS_IRQSTATUS_1);

	if (reg & USBOTGSS_IRQ1_DMADISABLECLR) {
		dev_dbg(omap->dev, "DMA Disable was Cleared\n");
		omap->dma_status = false;
	}

	if (reg & USBOTGSS_IRQ1_OEVT)
		dev_dbg(omap->dev, "OTG Event\n");

	if (reg & USBOTGSS_IRQ1_DRVVBUS_RISE)
		dev_dbg(omap->dev, "DRVVBUS Rise\n");

	if (reg & USBOTGSS_IRQ1_CHRGVBUS_RISE)
		dev_dbg(omap->dev, "CHRGVBUS Rise\n");

	if (reg & USBOTGSS_IRQ1_DISCHRGVBUS_RISE)
		dev_dbg(omap->dev, "DISCHRGVBUS Rise\n");

	if (reg & USBOTGSS_IRQ1_IDPULLUP_RISE)
		dev_dbg(omap->dev, "IDPULLUP Rise\n");

	if (reg & USBOTGSS_IRQ1_DRVVBUS_FALL)
		dev_dbg(omap->dev, "DRVVBUS Fall\n");

	if (reg & USBOTGSS_IRQ1_CHRGVBUS_FALL)
		dev_dbg(omap->dev, "CHRGVBUS Fall\n");

	if (reg & USBOTGSS_IRQ1_DISCHRGVBUS_FALL)
		dev_dbg(omap->dev, "DISCHRGVBUS Fall\n");

	if (reg & USBOTGSS_IRQ1_IDPULLUP_FALL)
		dev_dbg(omap->dev, "IDPULLUP Fall\n");

	dwc3_omap_writel(omap->base, USBOTGSS_IRQSTATUS_1, reg);

	reg = dwc3_omap_readl(omap->base, USBOTGSS_IRQSTATUS_0);
	dwc3_omap_writel(omap->base, USBOTGSS_IRQSTATUS_0, reg);

	spin_unlock(&omap->lock);

	return IRQ_HANDLED;
}

static int dwc3_omap_remove_core(struct device *dev, void *c)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static int dwc3_omap_set_dmamask(struct device *dev, void *c)
{
	dev->dma_mask = c;
	return 0;
}

static int dwc3_omap_probe(struct platform_device *pdev)
{
	struct device_node	*node = pdev->dev.of_node;

	struct dwc3_omap	*omap;
	struct resource		*res;
	struct device		*dev = &pdev->dev;
	struct extcon_dev	*edev;
	u8			mode;

	int			ret = -ENOMEM;
	int			irq;

	int			utmi_mode;

	u32			reg;

	void __iomem		*base;
	void			*context;

	if (!node) {
		dev_err(dev, "device node not found\n");
		return -EINVAL;
	}

	omap = devm_kzalloc(dev, sizeof(*omap), GFP_KERNEL);
	if (!omap) {
		dev_err(dev, "not enough memory\n");
		return -ENOMEM;
	}

	omap->dma_mask = DMA_BIT_MASK(32);
	platform_set_drvdata(pdev, omap);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "missing IRQ resource\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "missing memory base resource\n");
		return -EINVAL;
	}

	base = devm_ioremap_nocache(dev, res->start, resource_size(res));
	if (!base) {
		dev_err(dev, "ioremap failed\n");
		return -ENOMEM;
	}

	context = devm_kzalloc(dev, resource_size(res), GFP_KERNEL);
	if (!context) {
		dev_err(dev, "couldn't allocate dwc3 context memory\n");
		return -ENOMEM;
	}

	spin_lock_init(&omap->lock);

	omap->resource_size = resource_size(res);
	omap->context	= context;
	omap->dev	= dev;
	omap->irq	= irq;
	omap->base	= base;

	/*
	 * REVISIT if we ever have two instances of the wrapper, we will be
	 * in big trouble
	 */
	_omap	= omap;

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "get_sync failed with err %d\n", ret);
		goto err0;
	}

	reg = dwc3_omap_readl(omap->base, USBOTGSS_UTMI_OTG_STATUS);

	of_property_read_u32(node, "utmi-mode", &utmi_mode);

	switch (utmi_mode) {
	case DWC3_OMAP_UTMI_MODE_SW:
		reg |= USBOTGSS_UTMI_OTG_STATUS_SW_MODE;
		break;
	case DWC3_OMAP_UTMI_MODE_HW:
		reg &= ~USBOTGSS_UTMI_OTG_STATUS_SW_MODE;
		break;
	default:
		dev_dbg(dev, "UNKNOWN utmi mode %d\n", utmi_mode);
	}

	dwc3_omap_writel(omap->base, USBOTGSS_UTMI_OTG_STATUS, reg);

	/* check the DMA Status */
	reg = dwc3_omap_readl(omap->base, USBOTGSS_SYSCONFIG);
	omap->dma_status = !!(reg & USBOTGSS_SYSCONFIG_DMADISABLE);

	ret = devm_request_irq(dev, omap->irq, dwc3_omap_interrupt, IRQF_SHARED,
			"dwc3-omap", omap);
	if (ret) {
		dev_err(dev, "failed to request IRQ #%d --> %d\n",
				omap->irq, ret);
		goto err1;
	}

	/* enable all IRQs */
	reg = USBOTGSS_IRQO_COREIRQ_ST;
	dwc3_omap_writel(omap->base, USBOTGSS_IRQENABLE_SET_0, reg);

	reg = (USBOTGSS_IRQ1_OEVT |
			USBOTGSS_IRQ1_DRVVBUS_RISE |
			USBOTGSS_IRQ1_CHRGVBUS_RISE |
			USBOTGSS_IRQ1_DISCHRGVBUS_RISE |
			USBOTGSS_IRQ1_IDPULLUP_RISE |
			USBOTGSS_IRQ1_DRVVBUS_FALL |
			USBOTGSS_IRQ1_CHRGVBUS_FALL |
			USBOTGSS_IRQ1_DISCHRGVBUS_FALL |
			USBOTGSS_IRQ1_IDPULLUP_FALL);

	dwc3_omap_writel(omap->base, USBOTGSS_IRQENABLE_SET_1, reg);

	mode = of_usb_get_dr_mode(node);

	if (mode == USB_DR_MODE_OTG && of_property_read_bool(node, "extcon")) {
		edev = of_extcon_get_extcon_dev(dev, 0);
		if (IS_ERR(edev)) {
			dev_vdbg(dev, "couldn't get extcon device\n");
			ret = -EPROBE_DEFER;
			goto err2;
		}

		omap->vbus_nb.notifier_call = dwc3_omap_vbus_notifier;
		ret = extcon_register_interest(&omap->extcon_vbus_dev,
			edev->name, "USB", &omap->vbus_nb);
		if (ret < 0)
			dev_vdbg(dev, "failed to register notifier for USB\n");
		omap->id_nb.notifier_call = dwc3_omap_id_notifier;
		ret = extcon_register_interest(&omap->extcon_id_dev, edev->name,
					 "USB-HOST", &omap->id_nb);
		if (ret < 0)
			dev_vdbg(dev,
				"failed to register notifier for USB-HOST\n");

		dwc3_omap_usbvbus_id_handler(omap->dev, OMAP_DWC3_ID_GROUND);
	}

	ret = of_platform_populate(node, NULL, NULL, dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to create dwc3 core\n");
		goto err3;
	}

	device_for_each_child(&pdev->dev, &omap->dma_mask,
		dwc3_omap_set_dmamask);

	return 0;

err3:
	if (omap->extcon_vbus_dev.edev)
		extcon_unregister_interest(&omap->extcon_vbus_dev);
	if (omap->extcon_id_dev.edev)
		extcon_unregister_interest(&omap->extcon_id_dev);
err2:
	dwc3_omap_writel(omap->base, USBOTGSS_IRQENABLE_SET_1, 0);
err1:
	pm_runtime_put_sync(dev);
err0:
	pm_runtime_disable(dev);
	return ret;
}

static int dwc3_omap_remove(struct platform_device *pdev)
{
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	device_for_each_child(&pdev->dev, NULL, dwc3_omap_remove_core);

	return 0;
}

static const struct of_device_id of_dwc3_match[] = {
	{
		.compatible =	"ti,dwc3"
	},
	{ },
};
MODULE_DEVICE_TABLE(of, of_dwc3_match);

static struct platform_driver dwc3_omap_driver = {
	.probe		= dwc3_omap_probe,
	.remove		= dwc3_omap_remove,
	.driver		= {
		.name	= "omap-dwc3",
		.of_match_table	= of_dwc3_match,
	},
};

module_platform_driver(dwc3_omap_driver);

MODULE_ALIAS("platform:omap-dwc3");
MODULE_AUTHOR("Felipe Balbi <balbi@ti.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("DesignWare USB3 OMAP Glue Layer");
