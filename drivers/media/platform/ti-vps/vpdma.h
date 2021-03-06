/*
 * Copyright (c) 2013 Texas Instruments Inc.
 *
 * David Griego, <dagriego@biglakesoftware.com>
 * Dale Farnsworth, <dale@farnsworth.org>
 * Archit Taneja, <archit@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef __TI_VPDMA_H_
#define __TI_VPDMA_H_

#include "vpdma_priv.h"

/*
 * A vpdma_buf tracks the size, DMA address and mapping status of each
 * driver DMA area.
 */
struct vpdma_buf {
	void			*addr;
	dma_addr_t		dma_addr;
	size_t			size;
	bool			mapped;
};

struct vpdma_desc_list {
	struct vpdma_buf buf;
	void *next, *current_desc;
	int type;
};

struct vpdma_data {
	void __iomem		*base;

	struct platform_device	*pdev;

	/* tells whether vpdma firmware is loaded or not */
	bool ready;
};

enum vpdma_data_format_type {
	VPDMA_DATA_FMT_TYPE_YUV,
	VPDMA_DATA_FMT_TYPE_RGB,
	VPDMA_DATA_FMT_TYPE_MISC,
};

struct vpdma_data_format {
	enum vpdma_data_format_type type;
	int data_type;
	u8 depth;
};

#define VPDMA_DESC_ALIGN		16	/* 16-byte descriptor alignment */

/* line stride should be 16 byte aligned */
#define VPDMA_STRIDE_ALIGN		16

#define VPDMA_DTD_DESC_SIZE		32	/* 8 words */
#define VPDMA_CFD_CTD_DESC_SIZE		16	/* 4 words */

#define VPDMA_LIST_TYPE_NORMAL		0
#define VPDMA_LIST_TYPE_SELF_MODIFYING	1
#define VPDMA_LIST_TYPE_DOORBELL	2

enum vpdma_yuv_formats {
	VPDMA_DATA_FMT_Y444 = 0,
	VPDMA_DATA_FMT_Y422,
	VPDMA_DATA_FMT_Y420,
	VPDMA_DATA_FMT_C444,
	VPDMA_DATA_FMT_C422,
	VPDMA_DATA_FMT_C420,
	VPDMA_DATA_FMT_YC422,
	VPDMA_DATA_FMT_YC444,
	VPDMA_DATA_FMT_CY422,
};

enum vpdma_rgb_formats {
	VPDMA_DATA_FMT_RGB565 = 0,
	VPDMA_DATA_FMT_ARGB16_1555,
	VPDMA_DATA_FMT_ARGB16,
	VPDMA_DATA_FMT_RGBA16_5551,
	VPDMA_DATA_FMT_RGBA16,
	VPDMA_DATA_FMT_ARGB24,
	VPDMA_DATA_FMT_RGB24,
	VPDMA_DATA_FMT_ARGB32,
	VPDMA_DATA_FMT_RGBA24,
	VPDMA_DATA_FMT_RGBA32,
	VPDMA_DATA_FMT_BGR565,
	VPDMA_DATA_FMT_ABGR16_1555,
	VPDMA_DATA_FMT_ABGR16,
	VPDMA_DATA_FMT_BGRA16_5551,
	VPDMA_DATA_FMT_BGRA16,
	VPDMA_DATA_FMT_ABGR24,
	VPDMA_DATA_FMT_BGR24,
	VPDMA_DATA_FMT_ABGR32,
	VPDMA_DATA_FMT_BGRA24,
	VPDMA_DATA_FMT_BGRA32,
};

enum vpdma_misc_formats {
	VPDMA_DATA_FMT_MV = 0,
};

extern const struct vpdma_data_format vpdma_yuv_fmts[];
extern const struct vpdma_data_format vpdma_rgb_fmts[];
extern const struct vpdma_data_format vpdma_misc_fmts[];

enum vpdma_frame_start_event {
	VPDMA_FSEVENT_HDMI_FID = 0,
	VPDMA_FSEVENT_DVO2_FID,
	VPDMA_FSEVENT_HDCOMP_FID,
	VPDMA_FSEVENT_SD_FID,
	VPDMA_FSEVENT_LM_FID0,
	VPDMA_FSEVENT_LM_FID1,
	VPDMA_FSEVENT_LM_FID2,
	VPDMA_FSEVENT_CHANNEL_ACTIVE,
};

/* max width configurations */
enum vpdma_max_width {
	MAX_OUT_WIDTH_UNLIMITED = 0,
	MAX_OUT_WIDTH_REG1,
	MAX_OUT_WIDTH_REG2,
	MAX_OUT_WIDTH_REG3,
	MAX_OUT_WIDTH_352,
	MAX_OUT_WIDTH_768,
	MAX_OUT_WIDTH_1280,
	MAX_OUT_WIDTH_1920,
};

/* max height configurations */
enum vpdma_max_height {
	MAX_OUT_HEIGHT_UNLIMITED = 0,
	MAX_OUT_HEIGHT_REG1,
	MAX_OUT_HEIGHT_REG2,
	MAX_OUT_HEIGHT_REG3,
	MAX_OUT_HEIGHT_288,
	MAX_OUT_HEIGHT_576,
	MAX_OUT_HEIGHT_720,
	MAX_OUT_HEIGHT_1080,
};

#define VIP_CHAN_VIP2_OFFSET		70
#define VIP_CHAN_MULT_PORTB_OFFSET	16
#define VIP_CHAN_YUV_PORTB_OFFSET	2
#define VIP_CHAN_RGB_PORTB_OFFSET	1

/* flags for VPDMA data descriptors */
#define VPDMA_DATA_ODD_LINE_SKIP	(1 << 0)
#define VPDMA_DATA_EVEN_LINE_SKIP	(1 << 1)
#define VPDMA_DATA_FRAME_1D		(1 << 2)
#define VPDMA_DATA_MODE_TILED		(1 << 3)

/*
 * client identifiers used for configuration descriptors
 */
#define CFD_MMR_CLIENT		0
#define CFD_SC_CLIENT		4
#define CFD_SC_CLIENT2		8

/* Address data block header format */
struct vpdma_adb_hdr {
	u32			offset;
	u32			nwords;
	u32			reserved0;
	u32			reserved1;
};

/* helpers for creating ADB headers for config descriptors MMRs as client */
#define ADB_ADDR(dma_buf, str, fld)	((dma_buf)->addr + offsetof(str, fld))
#define MMR_ADB_ADDR(buf, str, fld)	ADB_ADDR(&(buf), struct str, fld)

#define VPDMA_SET_MMR_ADB_HDR(buf, str, hdr, regs, offset_a)	\
	do {							\
		struct vpdma_adb_hdr *h;			\
		struct str *adb = NULL;				\
		h = MMR_ADB_ADDR(buf, str, hdr);		\
		h->offset = (offset_a);				\
		h->nwords = sizeof(adb->regs) >> 2;		\
	} while (0)

/* vpdma descriptor buffer allocation and management */
int vpdma_buf_alloc(struct vpdma_buf *buf, size_t size);
void vpdma_buf_free(struct vpdma_buf *buf);
void vpdma_buf_map(struct vpdma_data *vpdma, struct vpdma_buf *buf);
void vpdma_buf_unmap(struct vpdma_data *vpdma, struct vpdma_buf *buf);

/* vpdma descriptor list funcs */
int vpdma_create_desc_list(struct vpdma_desc_list *list, size_t size, int type);
void vpdma_reset_desc_list(struct vpdma_desc_list *list);
void vpdma_free_desc_list(struct vpdma_desc_list *list);

/* helpers for creating vpdma descriptors */
void vpdma_add_cfd_block(struct vpdma_desc_list *list, int client,
		struct vpdma_buf *blk, u32 dest_offset);
void vpdma_add_cfd_adb(struct vpdma_desc_list *list, int client,
		struct vpdma_buf *adb);
void vpdma_add_sync_on_channel_ctd(struct vpdma_desc_list *list, int channel);
int vpdma_add_out_dtd(struct vpdma_desc_list *list, int width,
		const struct vpdma_data_format *fmt, dma_addr_t dma_addr,
		enum vpdma_max_width max_w, enum vpdma_max_height max_h,
		int channel, u32 flags);
void vpdma_add_in_dtd(struct vpdma_desc_list *list, int width,
		const struct v4l2_rect *c_rect,
		const struct vpdma_data_format *fmt, dma_addr_t dma_addr,
		int channel, int field, u32 flags, int frame_width,
		int frame_height, int start_h, int start_v);
int vpdma_list_busy(struct vpdma_data *vpdma, int list_num);
int vpdma_create_desc_list(struct vpdma_desc_list *list, size_t size, int type);
void vpdma_reset_desc_list(struct vpdma_desc_list *list);
void vpdma_free_desc_list(struct vpdma_desc_list *list);
int vpdma_submit_descs(struct vpdma_data *vpdma,
	struct vpdma_desc_list *list, int list_num);
void vpdma_update_dma_addr(struct vpdma_data *vpdma,
	struct vpdma_desc_list *list, dma_addr_t dma_addr,
	struct vpdma_dtd *write_dtd, int drop);

void vpdma_enable_channel_3_irq(struct vpdma_data *vpdma, bool enable);
void vpdma_set_max_size(struct vpdma_data *vpdma, int reg_addr,
		u32 width, u32 height);


void vpdma_enable_list_complete_irq(struct vpdma_data *vpdma, int irq_num,
		int list_num, bool enable);
void vpdma_clear_list_stat(struct vpdma_data *vpdma, int irq_num);

void vpdma_set_bg_color(struct vpdma_data *vpdma,
		struct vpdma_data_format *fmt, u32 color);
void vpdma_set_line_mode(struct vpdma_data *vpdma, int line_mode, int client);
void vpdma_set_frame_start_event(struct vpdma_data *data,
		enum vpdma_frame_start_event fs_event, int client);

void vpdma_dump_regs(struct vpdma_data *vpdma);

/* initialize vpdma, passed with VPE's platform device pointer */
int vpdma_init(struct platform_device *pdev, struct vpdma_data **pvpdma);

#endif
