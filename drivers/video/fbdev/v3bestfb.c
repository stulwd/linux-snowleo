/*
 * V3best HDMI frame buffer driver
 *
 * Author: V3best Ltd.
 *         Eric
 *
 * 2014 (c) V3best Ltd.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

/*
 * This driver was based on skeletonfb.c, Skeleton for a frame buffer device 
 * by Geert Uytterhoeven.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include "v3bestfb.h"
#include <linux/slab.h>
#include <asm/uaccess.h>

#define DRIVER_NAME	        "v3bestfb"
#define V3BEST_MAX_XRES        1024
#define V3BEST_MAX_YRES        600

#define NUM_REGS	7
#define REG_WR_CTRL	0
#define REG_WR_FBADDR	1
#define REG_WR_FBINDEX	2
#define REG_WR_HSYNC	3
#define REG_WR_HVALID	4
#define REG_WR_VSYNC	5
#define REG_WR_VVALID	6
#define REG_RD_FBINDEX	2
#define REG_WR_CTRL_ENABLE	 0x1
#define REG_WR_CTRL_DISABLE	 0x0
//#define REG_WR_CTRL_ROTATE	 0x0002
#define NUM_OF_BUFFERS 	2

#define BYTES_PER_PIXEL	4
#define BITS_PER_PIXEL	(BYTES_PER_PIXEL * 8)

#define TRANSP_SHIFT	24
#define RED_SHIFT	0
#define GREEN_SHIFT	8
#define BLUE_SHIFT	16

#define PALETTE_ENTRIES_NO	16	/* passed to fb_alloc_cmap() */

/*
 * Default v3bestfb configuration
 */
static struct v3bestfb_platform_data v3best_fb_default_pdata = {
    .xres  = V3BEST_MAX_XRES,
    .yres  = V3BEST_MAX_YRES,
    .xvirt = V3BEST_MAX_XRES,
    .yvirt = V3BEST_MAX_YRES * NUM_OF_BUFFERS,
};

static struct v3bestfb_vmode_data v3best_fb_vmode = {
    .refresh = 60,
    .h_active = V3BEST_MAX_XRES,
    .v_active = V3BEST_MAX_YRES,
    /*.hfp = 0x58,
      .hbp = 0x94,
      .vfp = 0x04,
      .vbp = 0x24,
      .hsync_len = 0x2C,
      .vsync_len = 0x05,*/
    .hfp = 48,
    .hbp = 80,
    .vfp = 3,
    .vbp = 14,
    .hsync_len = 32,
    .vsync_len = 6,

};

/*
 * Here are the default fb_fix_screeninfo and fb_var_screeninfo structures
 */
static struct fb_fix_screeninfo v3best_fb_fix = {
    .id     =		"v3best",
    .type   =		FB_TYPE_PACKED_PIXELS,
    .visual =	        FB_VISUAL_TRUECOLOR,
    .accel  =	        FB_ACCEL_NONE,
    .xpanstep =         1,
    .ypanstep =         1,
    .ywrapstep =        V3BEST_MAX_YRES 
};

static struct fb_var_screeninfo v3best_fb_var = {
    .bits_per_pixel =	BITS_PER_PIXEL,
    .red            =	{ RED_SHIFT, 8, 0 },
    .green          =	{ GREEN_SHIFT, 8, 0 },
    .blue           =   { BLUE_SHIFT, 8, 0 },
    .transp         =	{ TRANSP_SHIFT, 8, 0 },
    .activate       =	FB_ACTIVATE_NOW,
    .vmode          = FB_VMODE_NONINTERLACED,
};

struct v3bestfb_drvdata {

    struct fb_info	info;           /* FB driver info record */
    phys_addr_t	        regs_phys;      /* phys. address of the control 
                                           registers */
    void __iomem	*regs;		/* virt. address of the control
                                           registers */
    void		*fb_virt;	/* virt. address of the frame buffer */
    dma_addr_t	        fb_phys;	/* phys. address of the frame buffer */
    int		        fb_alloced;	/* Flag, was the fb memory alloced? */

    u8 		        flags;		/* features of the driver */

    u32		        reg_ctrl_default;

    u32		        pseudo_palette[PALETTE_ENTRIES_NO];
    /* Fake palette of 16 colors */
};

#define to_v3bestfb_drvdata(_info) \
    container_of(_info, struct v3bestfb_drvdata, info)

static void v3best_fb_out_be32(struct v3bestfb_drvdata *drvdata, u32 offset,
        u32 val)
{
    iowrite32(val, drvdata->regs + (offset << 2));
}

static int v3best_fb_setcolreg(unsigned regno, unsigned red, 
        unsigned green, unsigned blue, 
        unsigned transp, struct fb_info *fbi)
{
    u32 *palette = fbi->pseudo_palette;

    if (regno >= PALETTE_ENTRIES_NO)
        return -EINVAL;

    if (fbi->var.grayscale) {
        /* Convert color to grayscale.
         * grayscale = 0.30*R + 0.59*G + 0.11*B */
        red = green = blue =
            (red * 77 + green * 151 + blue * 28 + 127) >> 8;
    }

    /* fbi->fix.visual is always FB_VISUAL_TRUECOLOR */

    /* We only handle 8 bits of each color. */
    red >>= 8;
    green >>= 8;
    blue >>= 8;
    palette[regno] = (red << RED_SHIFT) | (green << GREEN_SHIFT) |
        (blue << BLUE_SHIFT);

    return 0;
}

static int v3best_fb_blank(int blank_mode, struct fb_info *fbi)
{
    struct v3bestfb_drvdata *drvdata = to_v3bestfb_drvdata(fbi);

    switch (blank_mode) {
    case FB_BLANK_UNBLANK:
        /* turn on panel */
        v3best_fb_out_be32(drvdata, REG_WR_CTRL, drvdata->reg_ctrl_default);
        break;

    case FB_BLANK_NORMAL:
    case FB_BLANK_VSYNC_SUSPEND:
    case FB_BLANK_HSYNC_SUSPEND:
    case FB_BLANK_POWERDOWN:
        /* turn off panel */
        v3best_fb_out_be32(drvdata, REG_WR_CTRL, 0);
    default:
        break;

    }
    return 0; /* success */
}

/* check var to see if supported by this device.  Probably doesn't
 *  * need modifying.
 *   */
static int v3best_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
    //struct v3bestfb_drvdata *drvdata = to_v3bestfb_drvdata(fbi);

    if (var->bits_per_pixel != fbi->var.bits_per_pixel) {
        if (var->bits_per_pixel == 24)
            var->bits_per_pixel = 32;
        else
            return -EINVAL;
    }

    if (var->xres > V3BEST_MAX_XRES)
        var->xres = V3BEST_MAX_XRES;
    if (var->yres > V3BEST_MAX_YRES)
        var->yres = V3BEST_MAX_YRES;

    if (var->xres_virtual > fbi->var.xres_virtual)
        var->xres_virtual = fbi->var.xres_virtual;
    if (var->yres_virtual > fbi->var.yres_virtual)
        var->yres_virtual = fbi->var.yres_virtual;

    if (fbi->var.xres != 0)
        if ((var->xoffset + fbi->var.xres) >= fbi->var.xres_virtual)
            var->xoffset = fbi->var.xres_virtual - fbi->var.xres - 1;
    if (fbi->var.yres != 0)
        if ((var->yoffset + fbi->var.yres) >= fbi->var.yres_virtual)
            var->yoffset = fbi->var.yres_virtual - fbi->var.yres - 1;

    var->transp.offset = fbi->var.transp.offset;
    var->transp.length = fbi->var.transp.length;
    var->transp.msb_right = fbi->var.transp.msb_right;
    var->red.offset = fbi->var.red.offset;
    var->red.length = fbi->var.red.length;
    var->red.msb_right = fbi->var.red.msb_right;
    var->green.offset = fbi->var.green.offset;
    var->green.length = fbi->var.green.length;
    var->green.msb_right = fbi->var.green.msb_right;
    var->blue.offset = fbi->var.blue.offset;
    var->blue.length = fbi->var.blue.length;
    var->blue.msb_right = fbi->var.blue.msb_right;
    var->height = fbi->var.height;
    var->width = fbi->var.width;
    var->sync = fbi->var.sync;
    var->rotate = fbi->var.rotate;

    return 0;
}


static int v3best_fb_set_par(struct fb_info *fbi)
{
    //struct v3bestfb_drvdata *drvdata = to_v3bestfb_drvdata(fbi);

    //int rc;

    if ((fbi->var.xres > V3BEST_MAX_XRES) ||
            (fbi->var.yres > V3BEST_MAX_YRES)) {
        return -EINVAL;
    }
    pr_info("video mode: %dx%d-%d\n",
            fbi->var.xres, fbi->var.yres, fbi->var.bits_per_pixel);

    return 0;
}


/* Pan the display if device supports it. */
static int v3best_fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
    struct v3bestfb_drvdata *drvdata = to_v3bestfb_drvdata(fbi);

    if (!var || !drvdata) {
        return -EINVAL;
    }
    /*if (var->xoffset - fbi->var.xoffset) {
    // No support for X panning for now! 
    return -EINVAL;
    }
    */
    if (fbi->var.yoffset == var->yoffset) {
        return 0;
    }
    /* check for negative values */
    if (var->yoffset < 0) {
        //var->yoffset += var->yres;
        var->yoffset = 0;
    }
    if (var->vmode & FB_VMODE_YWRAP) {
        if (var->yoffset > fbi->var.yres_virtual) {
            return -EINVAL;
        }
    } else {
        if (var->yoffset + var->yres > fbi->var.yres_virtual) {
            // if smaller then physical layer video memory allow panning 
            return -EINVAL;
        }
    }
    fbi->var.yoffset = var->yoffset;
    // fbi->fix.smem_start = fbi->fix.smem_start + var->xres * 
    //     BYTES_PER_PIXEL * var->yoffset;
    if (var->vmode & FB_VMODE_YWRAP)
        fbi->var.vmode |= FB_VMODE_YWRAP;
    else
        fbi->var.vmode &= ~FB_VMODE_YWRAP;

    v3best_fb_out_be32(drvdata, REG_WR_FBADDR, drvdata->info.fix.smem_start + 
            drvdata->info.var.xres *  BYTES_PER_PIXEL * var->yoffset);

    //  pr_info("v3bestfb: pan display!\n");

    return 0;
}

static struct fb_ops v3bestfb_ops =
{
    .owner			= THIS_MODULE,
    .fb_setcolreg		= v3best_fb_setcolreg,
    .fb_blank		        = v3best_fb_blank,
    //    .fb_check_var               = v3best_fb_check_var,
    //  .fb_set_par                 = v3best_fb_set_par,
    .fb_pan_display             = v3best_fb_pan_display,
    .fb_fillrect		= cfb_fillrect,
    .fb_copyarea		= cfb_copyarea,
    .fb_imageblit		= cfb_imageblit,
};

#ifdef CONFIG_PM
/*
 * Power management hooks.  Note that we won't be called from IRQ context,
 * unlike the blank functions above, so we may sleep.
 */
static int v3bestfb_suspend(struct platform_device *dev, pm_message_t state)
{
    struct v3bestfb_drvdata *drvdata = platform_get_drvdata(dev);

    //drvdata->reg_ctrl_default = REG_WR_CTRL_DISABLE;
    drvdata->reg_ctrl_default = REG_WR_CTRL_ENABLE;
    v3best_fb_out_be32(drvdata, REG_WR_CTRL,
            drvdata->reg_ctrl_default);
    return 0;
}

static int v3bestfb_resume(struct platform_device *dev)
{
    struct v3bestfb_drvdata *drvdata = platform_get_drvdata(dev);

    drvdata->reg_ctrl_default = REG_WR_CTRL_ENABLE;
    v3best_fb_out_be32(drvdata, REG_WR_CTRL,
            drvdata->reg_ctrl_default);
    return 0;
}
#else
#define v3bestfb_suspend	NULL
#define v3bestfb_resume		NULL
#endif

static int v3bestfb_init(struct device *dev,
        struct v3bestfb_drvdata *drvdata,
        unsigned long physaddr,
        struct v3bestfb_platform_data *pdata)
{
    int rc;
    int fbsize = pdata->xvirt * pdata->yvirt * BYTES_PER_PIXEL;

    if (!request_mem_region(physaddr, NUM_REGS*4, DRIVER_NAME)) {
        dev_err(dev, "Couldn't lock memory region at 0x%08lX\n",
                physaddr);
        rc = -ENODEV;
        goto err_region;
    }

    drvdata->regs_phys = physaddr;
    drvdata->regs = ioremap(physaddr, NUM_REGS*4);
    if (!drvdata->regs) {
        dev_err(dev, "Couldn't lock memory region at 0x%08lX\n",
                physaddr);
        rc = -ENODEV;
        goto err_map;
    }

    /* Allocate the framebuffer memory */
    if (pdata->fb_phys) {
        drvdata->fb_phys = pdata->fb_phys;
        // drvdata->fb_virt = ioremap_wc(pdata->fb_phys, 64*1024*1024);
        drvdata->fb_virt = ioremap(pdata->fb_phys, fbsize);
    } else {
        drvdata->fb_alloced = 1;
        //drvdata->fb_virt = dma_alloc_coherent(dev, PAGE_ALIGN(fbsize),
        //        &drvdata->fb_phys, GFP_KERNEL);
        drvdata->fb_virt = dma_alloc_writecombine(dev, PAGE_ALIGN(fbsize),
                &drvdata->fb_phys, GFP_KERNEL);
    }

    if (!drvdata->fb_virt) {
        dev_err(dev, "Could not allocate frame buffer memory\n");
        rc = -ENOMEM;
        goto err_fbmem;
    }

    /* Clear (turn to black) the framebuffer */
    //memset_io((void __iomem *)drvdata->fb_virt, 0, fbsize);
    //memcpy((void __iomem *)drvdata->fb_virt, bg_data, fbsize);

    /* Tell the hardware where the frame buffer is */
    v3best_fb_out_be32(drvdata, REG_WR_FBADDR, drvdata->fb_phys);

    /*v3best_fb_out_be32(drvdata, REG_WR_HSYNC, 0x002c0058);
      v3best_fb_out_be32(drvdata, REG_WR_HVALID, 0x07800094);
      v3best_fb_out_be32(drvdata, REG_WR_VSYNC, 0x00050004);
      v3best_fb_out_be32(drvdata, REG_WR_VVALID, 0x04380024);*/
    v3best_fb_out_be32(drvdata, REG_WR_HSYNC, (v3best_fb_vmode.hsync_len<<16)|
            v3best_fb_vmode.hfp);
    v3best_fb_out_be32(drvdata, REG_WR_HVALID, (v3best_fb_vmode.h_active<<16)|
            v3best_fb_vmode.hbp);
    v3best_fb_out_be32(drvdata, REG_WR_VSYNC, (v3best_fb_vmode.vsync_len<<16)|
            v3best_fb_vmode.vfp);
    v3best_fb_out_be32(drvdata, REG_WR_VVALID, (v3best_fb_vmode.v_active)<<16|
            v3best_fb_vmode.vbp);


    /* Turn on the display */
    drvdata->reg_ctrl_default = REG_WR_CTRL_ENABLE;
    v3best_fb_out_be32(drvdata, REG_WR_CTRL,
            drvdata->reg_ctrl_default);

    /* Fill struct fb_info */
    drvdata->info.device = dev;
    drvdata->info.screen_base = (void __iomem *)drvdata->fb_virt;
    drvdata->info.fbops = &v3bestfb_ops;
    drvdata->info.fix = v3best_fb_fix;
    drvdata->info.fix.smem_start = drvdata->fb_phys;
    drvdata->info.fix.smem_len = fbsize;
    drvdata->info.fix.line_length = pdata->xvirt * BYTES_PER_PIXEL;

    drvdata->info.pseudo_palette = drvdata->pseudo_palette;
    drvdata->info.flags = FBINFO_DEFAULT;
    drvdata->info.var = v3best_fb_var;
    drvdata->info.var.height = pdata->screen_height_mm;
    drvdata->info.var.width = pdata->screen_width_mm;
    drvdata->info.var.xres = pdata->xres;
    drvdata->info.var.yres = pdata->yres;
    drvdata->info.var.xres_virtual = pdata->xvirt;
    drvdata->info.var.yres_virtual = pdata->yvirt;


    drvdata->info.var.xoffset = 0;
    drvdata->info.var.yoffset = 0;

    /* Allocate a colour map */
    rc = fb_alloc_cmap(&drvdata->info.cmap, PALETTE_ENTRIES_NO, 0);
    if (rc) {
        dev_err(dev, "Fail to allocate colormap (%d entries)\n",
                PALETTE_ENTRIES_NO);
        goto err_cmap;
    }

    /* Register new frame buffer */
    rc = register_framebuffer(&drvdata->info);
    if (rc) {
        dev_err(dev, "Could not register frame buffer\n");
        goto err_regfb;
    }

    /* Put a banner in the log (for DEBUG) */
    dev_dbg(dev, "regs: phys=%lx, virt=%p\n", physaddr,
            drvdata->regs);
    /* Put a banner in the log (for DEBUG) */
    dev_dbg(dev, "fb: phys=%llx, virt=%p, size=%x\n",
            (unsigned long long)drvdata->fb_phys, drvdata->fb_virt, fbsize);

    return 0;	/* success */

err_regfb:
    fb_dealloc_cmap(&drvdata->info.cmap);

err_cmap:
    if (drvdata->fb_alloced)
        dma_free_coherent(dev, PAGE_ALIGN(fbsize), drvdata->fb_virt,
                drvdata->fb_phys);
    else
        iounmap(drvdata->fb_virt);

    /* Turn off the display */
    v3best_fb_out_be32(drvdata, REG_WR_CTRL, 0);

err_fbmem:
    iounmap(drvdata->regs);

err_map:
    release_mem_region(physaddr, NUM_REGS*4);

err_region:
    kfree(drvdata);
    dev_set_drvdata(dev, NULL);

    return rc;
}

static int v3bestfb_release(struct device *dev)
{
    struct v3bestfb_drvdata *drvdata = dev_get_drvdata(dev);

#if !defined(CONFIG_FRAMEBUFFER_CONSOLE) && defined(CONFIG_LOGO)
    v3best_fb_blank(VESA_POWERDOWN, &drvdata->info);
#endif

    unregister_framebuffer(&drvdata->info);

    fb_dealloc_cmap(&drvdata->info.cmap);

    if (drvdata->fb_alloced)
        dma_free_coherent(dev, PAGE_ALIGN(drvdata->info.fix.smem_len),
                drvdata->fb_virt, drvdata->fb_phys);
    else
        iounmap(drvdata->fb_virt);

    /* Turn off the display */
    v3best_fb_out_be32(drvdata, REG_WR_CTRL, 0);

    /* Release the resources, as allocated based on interface */
    iounmap(drvdata->regs);
    release_mem_region(drvdata->regs_phys, NUM_REGS*4);

    kfree(drvdata);
    dev_set_drvdata(dev, NULL);

    return 0;
}

/* 
 * OF bus binding
 */

static int v3bestfb_of_probe(struct platform_device *op)
{
    const u32 *prop;
    struct v3bestfb_platform_data pdata;
    struct resource res;
    int size, rc;
    struct v3bestfb_drvdata *drvdata;

    /* Copy with the default pdata (not a ptr reference!) */
    pdata = v3best_fb_default_pdata;

    /* Allocate the driver data region */
    drvdata = kzalloc(sizeof(*drvdata), GFP_KERNEL);
    if (!drvdata) {
        dev_err(&op->dev, "Couldn't allocate device private record\n");
        return -ENOMEM;
    }

    rc = of_address_to_resource(op->dev.of_node, 0, &res);
    if (rc) {
        dev_err(&op->dev, "invalid address\n");
        goto err;
    }

    //	prop = of_get_property(op->dev.of_node, "phys-size", &size);
    //	if ((prop) && (size >= sizeof(u32)*2)) {
    //	pdata.screen_width_mm = prop[0];
    //	pdata.screen_width_mm = prop[0];
    prop = of_get_property(op->dev.of_node, "v3best,xres", &size);
    if (!prop) {
        pr_err("Error getting xres\n");
        return -EINVAL;
    }
    pdata.xres = be32_to_cpup(prop);

    prop = of_get_property(op->dev.of_node, "v3best,yres", &size);
    if (!prop) {
        pr_err("Error getting yres\n");
        return -EINVAL;
    }
    pdata.yres = be32_to_cpup(prop);

    pdata.screen_width_mm = 0;
    pdata.screen_height_mm = 0;

    prop = of_get_property(op->dev.of_node, "v3best,xvirt", &size);
    if (!prop) {
        pr_err("Error getting xvirt\n");
        return -EINVAL;
    }
    pdata.xvirt = be32_to_cpup(prop);

    prop = of_get_property(op->dev.of_node, "v3best,yvirt", &size);
    if (!prop) {
        pr_err("Error getting yvirt\n");
        return -EINVAL;
    }
    pdata.yvirt = be32_to_cpup(prop);


    prop = of_get_property(op->dev.of_node, "v3best,h_active", &size);
    if (!prop) {
        pr_err("Error getting h_active\n");
        return -EINVAL;
    }
    v3best_fb_vmode.h_active = be32_to_cpup(prop);

    prop = of_get_property(op->dev.of_node, "v3best,v_active", &size);
    if (!prop) {
        pr_err("Error getting v_active\n");
        return -EINVAL;
    }
    v3best_fb_vmode.v_active = be32_to_cpup(prop);

    prop = of_get_property(op->dev.of_node, "v3best,hbp", &size);
    if (!prop) {
        pr_err("Error getting hbp\n");
        return -EINVAL;
    }
    v3best_fb_vmode.hbp = be32_to_cpup(prop);

    prop = of_get_property(op->dev.of_node, "v3best,hfp", &size);
    if (!prop) {
        pr_err("Error getting hfp\n");
        return -EINVAL;
    }
    v3best_fb_vmode.hfp = be32_to_cpup(prop);

    prop = of_get_property(op->dev.of_node, "v3best,vbp", &size);
    if (!prop) {
        pr_err("Error getting vbp\n");
        return -EINVAL;
    }
    v3best_fb_vmode.vbp = be32_to_cpup(prop);

    prop = of_get_property(op->dev.of_node, "v3best,vfp", &size);
    if (!prop) {
        pr_err("Error getting vfp\n");
        return -EINVAL;
    }
    v3best_fb_vmode.vfp = be32_to_cpup(prop);

    prop = of_get_property(op->dev.of_node, "v3best,hsync_len", &size);
    if (!prop) {
        pr_err("Error getting hsync_len\n");
        return -EINVAL;
    }
    v3best_fb_vmode.hsync_len = be32_to_cpup(prop);

    prop = of_get_property(op->dev.of_node, "v3best,vsync_len", &size);
    if (!prop) {
        pr_err("Error getting vsync_len\n");
        return -EINVAL;
    }
    v3best_fb_vmode.vsync_len = be32_to_cpup(prop);

    prop = of_get_property(op->dev.of_node, "v3best,fbphy", &size);
    if (!prop) {
        pr_warn("Error getting fbphy\n");
    } else {
        pdata.fb_phys = be32_to_cpup(prop);
    }

    dev_set_drvdata(&op->dev, drvdata);
    return v3bestfb_init(&op->dev, drvdata, res.start, &pdata);

err:
    kfree(drvdata);
    return -ENODEV;
}

static int v3bestfb_of_remove(struct platform_device *op)
{
    return v3bestfb_release(&op->dev);
}

/* Match table for of_platform binding */
static struct of_device_id v3bestfb_of_match[] = {
    { .compatible = "v3best,hdmi-out-1.00.a", },
    {},
};
MODULE_DEVICE_TABLE(of, v3bestfb_of_match);

static struct platform_driver v3bestfb_of_driver = {
    .probe     = v3bestfb_of_probe,
    .remove    = v3bestfb_of_remove,
    .suspend   = v3bestfb_suspend,
    .resume    = v3bestfb_resume,
    .driver    = {
        .name           = DRIVER_NAME,
        .owner          = THIS_MODULE,
        .of_match_table = v3bestfb_of_match,
    },
};

module_platform_driver(v3bestfb_of_driver);

MODULE_AUTHOR("V3best Ltd. Eric");
MODULE_DESCRIPTION("V3best HDMI frame buffer driver");
MODULE_LICENSE("GPL");
