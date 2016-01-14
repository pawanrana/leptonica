/*====================================================================*
 -  Copyright (C) 2001 Leptonica.  All rights reserved.
 -  This software is distributed in the hope that it will be
 -  useful, but with NO WARRANTY OF ANY KIND.
 -  No author or distributor accepts responsibility to anyone for the
 -  consequences of using this software, or for whether it serves any
 -  particular purpose or works at all, unless he or she says so in
 -  writing.  Everyone is granted permission to copy, modify and
 -  redistribute this source code, for commercial or non-commercial
 -  purposes, with the following restrictions: (1) the origin of this
 -  source code must not be misrepresented; (2) modified versions must
 -  be plainly marked as such; and (3) this notice may not be removed
 -  or altered from any source or modified source distribution.
 *====================================================================*/

/*
 *  morph.c
 *
 *     Generic binary morphological ops implemented with rasterop
 *         PIX     *pixDilate()
 *         PIX     *pixErode()
 *         PIX     *pixHMT()
 *         PIX     *pixOpen()
 *         PIX     *pixClose()
 *         PIX     *pixCloseSafe()
 *         PIX     *pixOpenGeneralized()
 *         PIX     *pixCloseGeneralized()
 *
 *     Special binary morphological (raster) ops with brick Sels
 *         PIX     *pixDilateBrick()
 *         PIX     *pixErodeBrick()
 *         PIX     *pixOpenBrick()
 *         PIX     *pixCloseBrick()
 *         PIX     *pixCloseSafeBrick()
 *
 *     Functions associated with boundary conditions
 *         void     resetMorphBoundaryCondition()
 *         l_int32  getMorphBorderPixelColor()
 *      
 *     Static helpers for arg processing
 *         static PIX     *processMorphArgs1()
 *         static PIX     *processMorphArgs2()
 *
 *  You have a number of choices for using binary morphology.
 *
 *  (1) If you are using brick Sels and know the sizes in advance,
 *      it is most convenient to use pixMorphSequence(), with
 *      the sequence string compiled in.  All intermediate
 *      images and Sels are created, used and destroyed.  You
 *      just get the result as a new Pix.  You specify separable
 *      operations explicitly, as in:
 *           "o11.1 + o1.11"
 *
 *  (2) If you are using brick Sels and may not know the sizes in
 *      advance, it is most convenient to use the pix*BrickSel()
 *      functions.  These likewise generate, use, and destroy
 *      intermediate images and Sels.  They do a separable operation
 *      if it's going to be (significantly) faster; you don't need
 *      to worry about it.  Also, you also have the option
 *      of doing the operation in-place or writing the result into
 *      an existing Pix (as well as making a new Pix for the result).
 *
 *  (3) If you are using Sels that are not bricks, you have two choices:
 *      (a) simplest: use the basic rasterop implementations (pixDilate(), ...)
 *      (b) fastest: generate the destination word accumumlation (dwa)
 *          code for your Sels and compile it with the library.
 *
 *      For an example, see flipdetect.c, which gives implementations
 *      using both the rasterop and dwa versions.  For the latter,
 *      the dwa code resides in fliphmtgen.c, and it was generated by
 *      prog/flipselgen.c.  Both the rasterop and dwa implementations
 *      are tested by prog/fliptest.c.
 *
 *  A global constant MORPH_BC is used to set the boundary conditions
 *  for rasterop-based binary morphology.  MORPH_BC, in morph.c,
 *  is set by default to ASYMMETRIC_MORPH_BC for a non-symmetric
 *  convention for boundary pixels in dilation and erosion:
 *      All pixels outside the image are assumed to be OFF
 *      for both dilation and erosion.
 *  To use a symmetric definition, see comments in pixErode()
 *  and reset MORPH_BC to SYMMETRIC_MORPH_BC, using 
 *  resetMorphBoundaryCondition().
 *
 *  Boundary artifacts are possible in closing when the non-symmetric
 *  boundary conditions are used, because foreground pixels very close
 *  to the edge can be removed.  This can be avoided by using either
 *  the symmetric boundary conditions or the function pixCloseSafe(),
 *  which adds a border before the operation and removes it afterwards.
 *
 *  The hit-miss transform (HMT) is the bit-and of 2 erosions:
 *     (erosion of the src by the hits)  &  (erosion of the bit-inverted
 *                                           src by the misses)
 *
 *  The 'generalized opening' is an HMT followed by a dilation that uses
 *  only the hits of the hit-miss Sel.
 *  The 'generalized closing' is a dilation (again, with the hits
 *  of a hit-miss Sel), followed by the HMT.
 *  Both of these 'generalized' functions are idempotent.
 *
 *  These functions are extensively tested in prog/morphtest3.c
 */

#include <stdio.h>
#include "allheaders.h"

    /* Global constant; initialized here; must be declared extern
     * in other files to access it directly.  However, in most
     * cases that is not necessary, because it can be reset
     * using resetMorphBoundaryCondition().  */
l_int32  MORPH_BC = ASYMMETRIC_MORPH_BC;


    /* Static helpers for arg processing */
static PIX * processMorphArgs1(PIX *pixd, PIX *pixs, SEL *sel, PIX **ppixt);
static PIX * processMorphArgs2(PIX *pixd, PIX *pixs, SEL *sel);


/*-----------------------------------------------------------------*
 *    Generic binary morphological ops implemented with rasterop   *
 *-----------------------------------------------------------------*/
/*!
 *  pixDilate()
 *
 *      Input:  pixd  (<optional>)
 *              pixs
 *              sel
 *      Return: pixd
 *
 *  Notes:
 *      (1) This dilates src using hits in Sel.
 *      (2) Three modes of usage:
 *          - pixd = NULL : result into new pixd, which is returned
 *          - pixd exists, != pixs : puts result into pixd
 *          - pixd == pixs : in-place operation; writes result back to pixs
 */
PIX *
pixDilate(PIX  *pixd,
          PIX  *pixs,
          SEL  *sel)
{
l_int32  i, j, w, h, sx, sy, cx, cy, seldata;
PIX     *pixt;

    PROCNAME("pixDilate");

    if ((pixd = processMorphArgs1(pixd, pixs, sel, &pixt)) == NULL)
	return (PIX *)ERROR_PTR("processMorphArgs1 failed", procName, pixd);

    pixGetDimensions(pixs, &w, &h, NULL);
    selGetParameters(sel, &sy, &sx, &cy, &cx);
    pixClearAll(pixd);
    for (i = 0; i < sy; i++) {
	for (j = 0; j < sx; j++) {
	    seldata = sel->data[i][j];
	    if (seldata == 1) {   /* src | dst */
		pixRasterop(pixd, j - cx, i - cy, w, h, PIX_SRC | PIX_DST,
		            pixt, 0, 0);
	    }
	}
    }

    pixDestroy(&pixt);
    return pixd;
}


/*!
 *  pixErode()
 *
 *      Input:  pixd  (<optional>)
 *              pixs
 *              sel
 *      Return: pixd
 *
 *  Notes:
 *      (1) This erodes src using hits in Sel.
 *      (2) Three modes of usage:
 *          - pixd = NULL : result into new pixd, which is returned
 *          - pixd exists, != pixs : puts result into pixd
 *          - pixd == pixs : in-place operation; writes result back to pixs
 */
PIX *
pixErode(PIX  *pixd,
         PIX  *pixs,
         SEL  *sel)
{
l_int32  i, j, w, h, sx, sy, cx, cy, seldata;
l_int32  xp, yp, xn, yn;
PIX     *pixt;

    PROCNAME("pixErode");

    if ((pixd = processMorphArgs1(pixd, pixs, sel, &pixt)) == NULL)
	return (PIX *)ERROR_PTR("processMorphArgs1 failed", procName, pixd);

    pixGetDimensions(pixs, &w, &h, NULL);
    selGetParameters(sel, &sy, &sx, &cy, &cx);
    pixSetAll(pixd);
    for (i = 0; i < sy; i++) {
	for (j = 0; j < sx; j++) {
	    seldata = sel->data[i][j];
	    if (seldata == 1) {   /* src & dst */
		    pixRasterop(pixd, cx - j, cy - i, w, h, PIX_SRC & PIX_DST,
			        pixt, 0, 0);
	    }
	}
    }

	/* Clear near edges.  We do this for the asymmetric boundary
	 * condition convention that implements erosion assuming all
	 * pixels surrounding the image are OFF.  If you use a
	 * use a symmetric b.c. convention, where the erosion is
	 * implemented assuming pixels surrounding the image
	 * are ON, these operations are omitted.  */
    if (MORPH_BC == ASYMMETRIC_MORPH_BC) {
	selFindMaxTranslations(sel, &xp, &yp, &xn, &yn);
	if (xp > 0)
	    pixRasterop(pixd, 0, 0, xp, h, PIX_CLR, NULL, 0, 0);
	if (xn > 0)
	    pixRasterop(pixd, w - xn, 0, xn, h, PIX_CLR, NULL, 0, 0);
	if (yp > 0)
	    pixRasterop(pixd, 0, 0, w, yp, PIX_CLR, NULL, 0, 0);
	if (yn > 0)
	    pixRasterop(pixd, 0, h - yn, w, yn, PIX_CLR, NULL, 0, 0);
    }

    pixDestroy(&pixt);
    return pixd;
}


/*!
 *  pixHMT()
 *
 *      Input:  pixd  (<optional>)
 *              pixs
 *              sel
 *      Return: pixd
 *
 *  Notes:
 *      (1) The hit-miss transform erodes the src, using both hits
 *          and misses in the Sel.  It ANDs the shifted src for hits
 *          and ANDs the inverted shifted src for misses.
 *      (2) Three modes of usage:
 *          - pixd = NULL : result into new pixd, which is returned
 *          - pixd exists, != pixs : puts result into pixd
 *          - pixd == pixs : in-place operation; writes result back to pixs
 */
PIX *
pixHMT(PIX  *pixd,
       PIX  *pixs,
       SEL  *sel)
{
l_int32  i, j, w, h, sx, sy, cx, cy, firstrasterop, seldata;
l_int32  xp, yp, xn, yn;
PIX     *pixt;

    PROCNAME("pixHMT");

    if ((pixd = processMorphArgs1(pixd, pixs, sel, &pixt)) == NULL)
	return (PIX *)ERROR_PTR("processMorphArgs1 failed", procName, pixd);

    pixGetDimensions(pixs, &w, &h, NULL);
    selGetParameters(sel, &sy, &sx, &cy, &cx);
    firstrasterop = TRUE;
    for (i = 0; i < sy; i++) {
	for (j = 0; j < sx; j++) {
	    seldata = sel->data[i][j];
	    if (seldata == 1) {  /* hit */
		if (firstrasterop == TRUE) {  /* src only */
		    pixClearAll(pixd);
		    pixRasterop(pixd, cx - j, cy - i, w, h, PIX_SRC,
		                pixt, 0, 0);
		    firstrasterop = FALSE;
		}
		else {   /* src & dst */
		    pixRasterop(pixd, cx - j, cy - i, w, h, PIX_SRC & PIX_DST,
			        pixt, 0, 0);
		}
	    }
	    else if (seldata == 2) {  /* miss */
		if (firstrasterop == TRUE) {  /* ~src only */
		    pixSetAll(pixd);
		    pixRasterop(pixd, cx - j, cy - i, w, h, PIX_NOT(PIX_SRC),
			     pixt, 0, 0);
		    firstrasterop = FALSE;
		}
		else  {  /* ~src & dst */
		    pixRasterop(pixd, cx - j, cy - i, w, h,
		                PIX_NOT(PIX_SRC) & PIX_DST,
				pixt, 0, 0);
		}
	    }
	}
    }

	/* clear near edges */
    selFindMaxTranslations(sel, &xp, &yp, &xn, &yn);
    if (xp > 0)
	pixRasterop(pixd, 0, 0, xp, h, PIX_CLR, NULL, 0, 0);
    if (xn > 0)
	pixRasterop(pixd, w - xn, 0, xn, h, PIX_CLR, NULL, 0, 0);
    if (yp > 0)
	pixRasterop(pixd, 0, 0, w, yp, PIX_CLR, NULL, 0, 0);
    if (yn > 0)
	pixRasterop(pixd, 0, h - yn, w, yn, PIX_CLR, NULL, 0, 0);

    pixDestroy(&pixt);
    return pixd;
}


/*!
 *  pixOpen()
 *
 *      Input:  pixd  (<optional>)
 *              pixs
 *              sel
 *      Return: pixd
 *
 *  Notes:
 *      (1) Generic morphological opening, using hits in the Sel.
 *      (2) Three modes of usage:
 *          - pixd = NULL : result into new pixd, which is returned
 *          - pixd exists, != pixs : puts result into pixd
 *          - pixd == pixs : in-place operation; writes result back to pixs
 */
PIX *
pixOpen(PIX  *pixd,
        PIX  *pixs,
        SEL  *sel)
{
PIX  *pixt;

    PROCNAME("pixOpen");

    if ((pixd = processMorphArgs2(pixd, pixs, sel)) == NULL)
	return (PIX *)ERROR_PTR("pixd not returned", procName, pixd);

    if ((pixt = pixErode(NULL, pixs, sel)) == NULL)
	return (PIX *)ERROR_PTR("pixt not made", procName, pixd);
    pixDilate(pixd, pixt, sel);
    pixDestroy(&pixt);

    return pixd;
}
    
    
/*!
 *  pixClose()
 *
 *      Input:  pixd  (<optional>)
 *              pixs
 *              sel
 *      Return: pixd
 *
 *  Notes:
 *      (1) Generic morphological closing, using hits in the Sel.
 *      (2) This implementation is a strict dual of the opening if
 *          symmetric boundary conditions are used (see notes at top
 *          of this file).
 *      (3) Three modes of usage:
 *          - pixd = NULL : result into new pixd, which is returned
 *          - pixd exists, != pixs : puts result into pixd
 *          - pixd == pixs : in-place operation; writes result back to pixs
 */
PIX *
pixClose(PIX  *pixd,
         PIX  *pixs,
         SEL  *sel)
{
PIX  *pixt;

    PROCNAME("pixClose");

    if ((pixd = processMorphArgs2(pixd, pixs, sel)) == NULL)
	return (PIX *)ERROR_PTR("pixd not returned", procName, pixd);

    if ((pixt = pixDilate(NULL, pixs, sel)) == NULL)
	return (PIX *)ERROR_PTR("pixt not made", procName, pixd);
    pixErode(pixd, pixt, sel);
    pixDestroy(&pixt);

    return pixd;
}
    
    
/*!
 *  pixCloseSafe()
 *
 *      Input:  pixd  (<optional>)
 *              pixs
 *              sel
 *      Return: pixd
 *
 *  Notes:
 *      (1) Generic morphological closing, using hits in the Sel.
 *      (2) If non-symmetric boundary conditions are used, this
 *          function adds a border of OFF pixels that is of
 *          sufficient size to avoid losing pixels from the dilation,
 *          and it removes the border after the operation is finished.
 *          It thus enforces a correct extensive result for closing.
 *      (3) If symmetric b.c. are used, it is not necessary to add
 *          and remove this border.
 *      (4) Three modes of usage:
 *          - pixd = NULL : result into new pixd, which is returned
 *          - pixd exists, != pixs : puts result into pixd
 *          - pixd == pixs : in-place operation; writes result back to pixs
 */
PIX *
pixCloseSafe(PIX  *pixd,
             PIX  *pixs,
             SEL  *sel)
{
l_int32  xp, yp, xn, yn, xmax, xbord;
PIX     *pixt1, *pixt2;

    PROCNAME("pixCloseSafe");

    if (!pixs)
	return (PIX *)ERROR_PTR("pixs not defined", procName, pixd);
    if (!sel)
	return (PIX *)ERROR_PTR("sel not defined", procName, pixd);
    if (pixGetDepth(pixs) != 1)
	return (PIX *)ERROR_PTR("pixs not 1 bpp", procName, pixd);

    if (pixd) {
	if (!pixSizesEqual(pixs, pixd))
	    L_WARNING("pix src and dest sizes unequal", procName);
    }

        /* symmetric b.c. handles correctly without added pixels */
    if (MORPH_BC == SYMMETRIC_MORPH_BC)
        return pixClose(pixd, pixs, sel);

    selFindMaxTranslations(sel, &xp, &yp, &xn, &yn);
    xmax = L_MAX(xp, xn);
    xbord = 32 * ((xmax + 31) / 32);  /* full 32 bit words */

    if ((pixt1 = pixAddBorderGeneral(pixs, xbord, xbord, yp, yn, 0)) == NULL)
	return (PIX *)ERROR_PTR("pixt1 not made", procName, pixd);
    pixClose(pixt1, pixt1, sel);
    if ((pixt2 = pixRemoveBorderGeneral(pixt1, xbord, xbord, yp, yn)) == NULL)
	return (PIX *)ERROR_PTR("pixt2 not made", procName, pixd);
    pixDestroy(&pixt1);

    if (!pixd)
	return pixt2;

    pixCopy(pixd, pixt2);
    pixDestroy(&pixt2);
    return pixd;
}
    
    
/*!
 *  pixOpenGeneralized()
 *
 *      Input:  pixd  (<optional>)
 *              pixs
 *              sel
 *      Return: pixd
 *
 *  Notes:
 *      (1) Generalized morphological opening, using both hits and
 *          misses in the Sel.
 *      (2) This does a hit-miss transform, followed by a dilation
 *          using the hits.
 *      (3) Three modes of usage:
 *          - pixd = NULL : result into new pixd, which is returned
 *          - pixd exists, != pixs : puts result into pixd
 *          - pixd == pixs : in-place operation; writes result back to pixs
 */
PIX *
pixOpenGeneralized(PIX  *pixd,
                   PIX  *pixs,
                   SEL  *sel)
{
PIX  *pixt;

    PROCNAME("pixOpenGeneralized");

    if ((pixd = processMorphArgs2(pixd, pixs, sel)) == NULL)
	return (PIX *)ERROR_PTR("pixd not returned", procName, pixd);

    if ((pixt = pixHMT(NULL, pixs, sel)) == NULL)
	return (PIX *)ERROR_PTR("pixt not made", procName, pixd);
    pixDilate(pixd, pixt, sel);
    pixDestroy(&pixt);

    return pixd;
}
    
    
/*!
 *  pixCloseGeneralized()
 *
 *      Input:  pixd  (<optional>)
 *              pixs
 *              sel
 *      Return: pixd
 *
 *  Notes:
 *      (1) Generalized morphological closing, using both hits and
 *          misses in the Sel.
 *      (2) This does a dilation using the hits, followed by a
 *          hit-miss transform.
 *      (3) This operation is a dual of the generalized opening.
 *      (4) Three modes of usage:
 *          - pixd = NULL : result into new pixd, which is returned
 *          - pixd exists, != pixs : puts result into pixd
 *          - pixd == pixs : in-place operation; writes result back to pixs
 */
PIX *
pixCloseGeneralized(PIX  *pixd,
                    PIX  *pixs,
                    SEL  *sel)
{
PIX  *pixt;

    PROCNAME("pixCloseGeneralized");

    if ((pixd = processMorphArgs2(pixd, pixs, sel)) == NULL)
	return (PIX *)ERROR_PTR("pixd not returned", procName, pixd);

    if ((pixt = pixDilate(NULL, pixs, sel)) == NULL)
	return (PIX *)ERROR_PTR("pixt not made", procName, pixd);
    pixHMT(pixd, pixt, sel);
    pixDestroy(&pixt);

    return pixd;
}


/*-----------------------------------------------------------------*
 *         Special binary morphological ops with brick Sels        *
 *-----------------------------------------------------------------*/
/*!
 *  pixDilateBrick()
 *
 *      Input:  pixd  (<optional>)
 *              pixs
 *              hsize (width of brick Sel)
 *              vsize (height of brick Sel)
 *      Return: pixd
 *
 *  Notes:
 *      (1) Sel is a brick with all elements being hits
 *      (2) The origin is at (x, y) = (hsize/2, vsize/2)
 *      (3) Do separably if both hsize and vsize are > 1.
 *      (4) Three modes of usage:
 *          - pixd = NULL : result into new pixd, which is returned
 *          - pixd exists, != pixs : puts result into pixd
 *          - pixd == pixs : in-place operation; writes result back to pixs
 */
PIX *
pixDilateBrick(PIX     *pixd,
               PIX     *pixs,
               l_int32  hsize,
               l_int32  vsize)
{
PIX  *pixt;
SEL  *sel, *selh, *selv;

    PROCNAME("pixDilateBrick");

    if (!pixs)
	return (PIX *)ERROR_PTR("pixs not defined", procName, pixd);
    if (pixGetDepth(pixs) != 1)
	return (PIX *)ERROR_PTR("pixs not 1 bpp", procName, pixd);
    if (hsize < 1 || vsize < 1)
	return (PIX *)ERROR_PTR("hsize and vsize not >= 1", procName, pixd);

    if (hsize == 1 && vsize == 1)
        return pixCopy(pixd, pixs);
    if (hsize == 1 || vsize == 1) {  /* no intermediate result */
        sel = selCreateBrick(vsize, hsize, vsize / 2, hsize / 2, SEL_HIT);
        pixd = pixDilate(pixd, pixs, sel);
        selDestroy(&sel);
    }
    else {
        selh = selCreateBrick(1, hsize, 0, hsize / 2, SEL_HIT);
        selv = selCreateBrick(vsize, 1, vsize / 2, 0, SEL_HIT);
	pixt = pixDilate(NULL, pixs, selh);
	pixd = pixDilate(pixd, pixt, selv);
	pixDestroy(&pixt);
	selDestroy(&selh);
	selDestroy(&selv);
    }

    return pixd;
}


/*!
 *  pixErodeBrick()
 *
 *      Input:  pixd  (<optional>)
 *              pixs
 *              hsize (width of brick Sel)
 *              vsize (height of brick Sel)
 *      Return: pixd
 *
 *  Notes:
 *      (1) Sel is a brick with all elements being hits
 *      (2) The origin is at (x, y) = (hsize/2, vsize/2)
 *      (3) Do separably if both hsize and vsize are > 1.
 *      (4) Three modes of usage:
 *          - pixd = NULL : result into new pixd, which is returned
 *          - pixd exists, != pixs : puts result into pixd
 *          - pixd == pixs : in-place operation; writes result back to pixs
 */
PIX *
pixErodeBrick(PIX     *pixd,
              PIX     *pixs,
              l_int32  hsize,
              l_int32  vsize)
{
PIX  *pixt;
SEL  *sel, *selh, *selv;

    PROCNAME("pixErodeBrick");

    if (!pixs)
	return (PIX *)ERROR_PTR("pixs not defined", procName, pixd);
    if (pixGetDepth(pixs) != 1)
	return (PIX *)ERROR_PTR("pixs not 1 bpp", procName, pixd);
    if (hsize < 1 || vsize < 1)
	return (PIX *)ERROR_PTR("hsize and vsize not >= 1", procName, pixd);

    if (hsize == 1 && vsize == 1)
        return pixCopy(pixd, pixs);
    if (hsize == 1 || vsize == 1) {  /* no intermediate result */
        sel = selCreateBrick(vsize, hsize, vsize / 2, hsize / 2, SEL_HIT);
	pixd = pixErode(pixd, pixs, sel);
	selDestroy(&sel);
    }
    else {
        selh = selCreateBrick(1, hsize, 0, hsize / 2, SEL_HIT);
        selv = selCreateBrick(vsize, 1, vsize / 2, 0, SEL_HIT);
	pixt = pixErode(NULL, pixs, selh);
	pixd = pixErode(pixd, pixt, selv);
	pixDestroy(&pixt);
	selDestroy(&selh);
	selDestroy(&selv);
    }

    return pixd;
}


/*!
 *  pixOpenBrick()
 *
 *      Input:  pixd  (<optional>)
 *              pixs
 *              hsize (width of brick Sel)
 *              vsize (height of brick Sel)
 *      Return: pixd
 *
 *  Notes:
 *      (1) Sel is a brick with all elements being hits
 *      (2) The origin is at (x, y) = (hsize/2, vsize/2)
 *      (3) Do separably if both hsize and vsize are > 1.
 *      (4) Three modes of usage:
 *          - pixd = NULL : result into new pixd, which is returned
 *          - pixd exists, != pixs : puts result into pixd
 *          - pixd == pixs : in-place operation; writes result back to pixs
 */
PIX *
pixOpenBrick(PIX     *pixd,
             PIX     *pixs,
             l_int32  hsize,
             l_int32  vsize)
{
PIX  *pixt;
SEL  *sel, *selh, *selv;

    PROCNAME("pixOpenBrick");

    if (!pixs)
	return (PIX *)ERROR_PTR("pixs not defined", procName, pixd);
    if (pixGetDepth(pixs) != 1)
	return (PIX *)ERROR_PTR("pixs not 1 bpp", procName, pixd);
    if (hsize < 1 || vsize < 1)
	return (PIX *)ERROR_PTR("hsize and vsize not >= 1", procName, pixd);

    if (hsize == 1 && vsize == 1)
        return pixCopy(pixd, pixs);
    if (hsize == 1 || vsize == 1) {  /* no intermediate result */
        sel = selCreateBrick(vsize, hsize, vsize / 2, hsize / 2, SEL_HIT);
        pixd = pixOpen(pixd, pixs, sel);
        selDestroy(&sel);
    }
    else {  /* do separably */
        selh = selCreateBrick(1, hsize, 0, hsize / 2, SEL_HIT);
        selv = selCreateBrick(vsize, 1, vsize / 2, 0, SEL_HIT);
	pixt = pixErode(NULL, pixs, selh);
	pixd = pixErode(pixd, pixt, selv);
	pixDilate(pixt, pixd, selh);
	pixDilate(pixd, pixt, selv);
	pixDestroy(&pixt);
	selDestroy(&selh);
	selDestroy(&selv);
    }

    return pixd;
}


/*!
 *  pixCloseBrick()
 *
 *      Input:  pixd  (<optional>)
 *              pixs
 *              hsize (width of brick Sel)
 *              vsize (height of brick Sel)
 *      Return: pixd
 *
 *  Notes:
 *      (1) Sel is a brick with all elements being hits
 *      (2) The origin is at (x, y) = (hsize/2, vsize/2)
 *      (3) Do separably if both hsize and vsize are > 1.
 *      (4) Three modes of usage:
 *          - pixd = NULL : result into new pixd, which is returned
 *          - pixd exists, != pixs : puts result into pixd
 *          - pixd == pixs : in-place operation; writes result back to pixs
 */
PIX *
pixCloseBrick(PIX     *pixd,
              PIX     *pixs,
              l_int32  hsize,
              l_int32  vsize)
{
PIX  *pixt;
SEL  *sel, *selh, *selv;

    PROCNAME("pixCloseBrick");

    if (!pixs)
	return (PIX *)ERROR_PTR("pixs not defined", procName, pixd);
    if (pixGetDepth(pixs) != 1)
	return (PIX *)ERROR_PTR("pixs not 1 bpp", procName, pixd);
    if (hsize < 1 || vsize < 1)
	return (PIX *)ERROR_PTR("hsize and vsize not >= 1", procName, pixd);

    if (hsize == 1 && vsize == 1)
        return pixCopy(pixd, pixs);
    if (hsize == 1 || vsize == 1) {  /* no intermediate result */
        sel = selCreateBrick(vsize, hsize, vsize / 2, hsize / 2, SEL_HIT);
        pixd = pixClose(pixd, pixs, sel);
        selDestroy(&sel);
    }
    else {  /* do separably */
        selh = selCreateBrick(1, hsize, 0, hsize / 2, SEL_HIT);
        selv = selCreateBrick(vsize, 1, vsize / 2, 0, SEL_HIT);
	pixt = pixDilate(NULL, pixs, selh);
	pixd = pixDilate(pixd, pixt, selv);
	pixErode(pixt, pixd, selh);
	pixErode(pixd, pixt, selv);
	pixDestroy(&pixt);
	selDestroy(&selh);
	selDestroy(&selv);
    }

    return pixd;
}


/*!
 *  pixCloseSafeBrick()
 *
 *      Input:  pixd  (<optional>)
 *              pixs
 *              hsize (width of brick Sel)
 *              vsize (height of brick Sel)
 *      Return: pixd
 *
 *  Notes:
 *      (1) Sel is a brick with all elements being hits
 *      (2) The origin is at (x, y) = (hsize/2, vsize/2)
 *      (3) Do separably if both hsize and vsize are > 1.
 *      (4) Safe version: add border of sufficient size and remove at end
 *      (5) Three modes of usage:
 *          - pixd = NULL : result into new pixd, which is returned
 *          - pixd exists, != pixs : puts result into pixd
 *          - pixd == pixs : in-place operation; writes result back to pixs
 */
PIX *
pixCloseSafeBrick(PIX     *pixd,
                  PIX     *pixs,
                  l_int32  hsize,
                  l_int32  vsize)
{
l_int32  maxtrans, bordsize;
PIX     *pixsb, *pixt, *pixdb;
SEL     *sel, *selh, *selv;

    PROCNAME("pixCloseSafeBrick");

    if (!pixs)
	return (PIX *)ERROR_PTR("pixs not defined", procName, pixd);
    if (pixGetDepth(pixs) != 1)
	return (PIX *)ERROR_PTR("pixs not 1 bpp", procName, pixd);
    if (hsize < 1 || vsize < 1)
	return (PIX *)ERROR_PTR("hsize and vsize not >= 1", procName, pixd);

    if (hsize == 1 && vsize == 1)
        return pixCopy(pixd, pixs);

        /* symmetric b.c. handles correctly without added pixels */
    if (MORPH_BC == SYMMETRIC_MORPH_BC)
        return pixCloseBrick(pixd, pixs, hsize, vsize);

    maxtrans = L_MAX(hsize / 2, vsize / 2);
    bordsize = 32 * ((maxtrans + 31) / 32);  /* full 32 bit words */
    pixsb = pixAddBorder(pixs, bordsize, 0);

    if (hsize == 1 || vsize == 1) {  /* no intermediate result */
        sel = selCreateBrick(vsize, hsize, vsize / 2, hsize / 2, SEL_HIT);
        pixdb = pixClose(NULL, pixsb, sel);
        selDestroy(&sel);
    }
    else {  /* do separably */
        selh = selCreateBrick(1, hsize, 0, hsize / 2, SEL_HIT);
        selv = selCreateBrick(vsize, 1, vsize / 2, 0, SEL_HIT);
	pixt = pixDilate(NULL, pixsb, selh);
	pixdb = pixDilate(NULL, pixt, selv);
	pixErode(pixt, pixdb, selh);
	pixErode(pixdb, pixt, selv);
	pixDestroy(&pixt);
	selDestroy(&selh);
	selDestroy(&selv);
    }

    pixt = pixRemoveBorder(pixdb, bordsize);
    pixDestroy(&pixsb);
    pixDestroy(&pixdb);

    if (!pixd)
        pixd = pixt;
    else {
        pixCopy(pixd, pixt);
	pixDestroy(&pixt);
    }

    return pixd;
}


/*-----------------------------------------------------------------*
 *           Functions associated with boundary conditions         *
 *-----------------------------------------------------------------*/
/*!
 *  resetMorphBoundaryCondition()
 *
 *      Input:  bc (SYMMETRIC_MORPH_BC, ASYMMETRIC_MORPH_BC)
 *      Return: void
 */
void
resetMorphBoundaryCondition(l_int32  bc)
{
    PROCNAME("resetMorphBoundaryCondition");

    if (bc != SYMMETRIC_MORPH_BC && bc != ASYMMETRIC_MORPH_BC) {
        L_WARNING("invalid bc; using asymmetric", procName);
        bc = ASYMMETRIC_MORPH_BC;
    }
    MORPH_BC = bc;
    return;
}


/*!
 *  getMorphBorderPixelColor()
 *
 *      Input:  type (MORPH_DILATION, MORPH_EROSION) 
 *              depth (of pix)
 *      Return: color of border pixels for this operation
 */
l_uint32
getMorphBorderPixelColor(l_int32  type,
                         l_int32  depth)
{
    PROCNAME("getMorphBorderPixelColor");

    if (type != MORPH_DILATION && type != MORPH_EROSION)
        return ERROR_INT("invalid type", procName, 0);
    if (depth != 1 && depth != 2 && depth != 4 && depth != 8 &&
        depth != 16 && depth != 32)
        return ERROR_INT("invalid depth", procName, 0);

    if (MORPH_BC == ASYMMETRIC_MORPH_BC || type == MORPH_DILATION)
        return 0;

        /* symmetric & erosion */
    if (depth < 32)
        return ((1 << depth) - 1);
    if (depth == 32)
        return 0xffffff00;
}


/*-----------------------------------------------------------------*
 *               Static helpers for arg processing                 *
 *-----------------------------------------------------------------*/
/*!
 *  processMorphArgs1()
 *
 *      Input:  pixd (<optional>
 *              pixs
 *              sel
 *              &pixt (<returned>)
 *      Return: pixd, or null on error.
 *
 *  Notes:
 *      (1) This is used for generic erosion, dilation and HMT.
 */
static PIX *
processMorphArgs1(PIX   *pixd,
                  PIX   *pixs,
                  SEL   *sel,
                  PIX  **ppixt)
{
l_int32  sx, sy;

    PROCNAME("processMorphArgs1");

    if (!pixs)
	return (PIX *)ERROR_PTR("pixs not defined", procName, pixd);
    if (!sel)
	return (PIX *)ERROR_PTR("sel not defined", procName, pixd);
    if (pixGetDepth(pixs) != 1)
	return (PIX *)ERROR_PTR("pixs not 1 bpp", procName, pixd);

    selGetParameters(sel, &sx, &sy, NULL, NULL);
    if (sx == 0 || sy == 0)
	return (PIX *)ERROR_PTR("sel of size 0", procName, pixd);

    if (!pixd) {
	if ((pixd = pixCreateTemplate(pixs)) == NULL)
	    return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
	*ppixt = pixClone(pixs);
    }
    else {
	if (!pixSizesEqual(pixs, pixd))
	    return (PIX *)ERROR_PTR("pix sizes unequal", procName, pixd);
	if (pixd == pixs) {
	    if ((*ppixt = pixCopy(NULL, pixs)) == NULL)
		return (PIX *)ERROR_PTR("pixt not made", procName, pixd);
	}
	else
	    *ppixt = pixClone(pixs);
    }
    return pixd;
}


/*!
 *  processMorphArgs2()
 *
 *  This is used for generic openings and closings.
 */
static PIX *
processMorphArgs2(PIX   *pixd,
                  PIX   *pixs,
                  SEL   *sel)
{
l_int32  sx, sy;

    PROCNAME("processMorphArgs2");

    if (!pixs)
	return (PIX *)ERROR_PTR("pixs not defined", procName, pixd);
    if (!sel)
	return (PIX *)ERROR_PTR("sel not defined", procName, pixd);
    if (pixGetDepth(pixs) != 1)
	return (PIX *)ERROR_PTR("pixs not 1 bpp", procName, pixd);

    selGetParameters(sel, &sx, &sy, NULL, NULL);
    if (sx == 0 || sy == 0)
	return (PIX *)ERROR_PTR("sel of size 0", procName, pixd);

    if (!pixd) {
	if ((pixd = pixCreateTemplate(pixs)) == NULL)
	    return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    }
    else {
	if (!pixSizesEqual(pixs, pixd))
	    return (PIX *)ERROR_PTR("pix sizes unequal", procName, pixd);
    }

    return pixd;
}
