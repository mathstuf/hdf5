/*
 * Copyright (C) 1998 NCSA
 *                    All rights reserved.
 *
 * Programmer:  Robb Matzke <matzke@llnl.gov>
 *              Wednesday, January 21, 1998
 *
 * Purpose:	Simple selection data space I/O functions.
 */
#include <H5private.h>
#include <H5Eprivate.h>
#include <H5Sprivate.h>
#include <H5Vprivate.h>

/* Interface initialization */
#define PABLO_MASK      H5S_simp_mask
#define INTERFACE_INIT  NULL
static intn             interface_initialize_g = FALSE;

/*-------------------------------------------------------------------------
 * Function:	H5S_simp_init
 *
 * Purpose:	Generates element numbering information for the data
 *		spaces involved in a data space conversion.
 *
 * Return:	Success:	Number of elements that can be efficiently
 *				transferred at a time.
 *
 *		Failure:	Zero
 *
 * Programmer:	Robb Matzke
 *              Wednesday, January 21, 1998
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
size_t
H5S_simp_init (const struct H5O_layout_t __unused__ *layout,
	       const H5S_t *mem_space, const H5S_t *file_space,
	       size_t desired_nelmts)
{
    hsize_t	nelmts;
    int		m_ndims, f_ndims;	/*mem, file dimensionality	*/
    hsize_t	size[H5O_LAYOUT_NDIMS];	/*size of selected hyperslab	*/
    hsize_t	acc;
    int		i;
    
    FUNC_ENTER (H5S_simp_init, 0);

    /* Check args */
    assert (layout);
    assert (mem_space && H5S_SIMPLE==mem_space->extent.type);
    assert (file_space && H5S_SIMPLE==file_space->extent.type);

    /*
     * The stripmine size is such that only the slowest varying dimension can
     * be split up.  We choose the largest possible strip mine size which is
     * not larger than the desired size.
     */
    m_ndims = H5S_get_hyperslab (mem_space, NULL, size, NULL);
    for (i=m_ndims-1, acc=1; i>0; --i) acc *= size[i];
    nelmts = (desired_nelmts/acc) * acc;
    if (nelmts<=0) {
        HRETURN_ERROR (H5E_IO, H5E_UNSUPPORTED, 0,
		       "strip mine buffer is too small");
    }

    /*
     * The value chosen for mem_space must be the same as the value chosen for
     * file_space.
     */
    f_ndims = H5S_get_hyperslab (file_space, NULL, size, NULL);
    if (m_ndims!=f_ndims) {
        nelmts = H5S_select_npoints (file_space);
        if (nelmts>desired_nelmts) {
            HRETURN_ERROR (H5E_IO, H5E_UNSUPPORTED, 0,
                   "strip mining not supported across dimensionalities");
        }
        assert (nelmts==H5S_select_npoints (mem_space));
    } else {
        for (i=f_ndims-1, acc=1; i>0; --i) acc *= size[i];
        acc *= (desired_nelmts/acc);
        if (nelmts!=acc) {
            HRETURN_ERROR (H5E_IO, H5E_UNSUPPORTED, 0,
                   "unsupported strip mine size for shape change");
        }
    }
    
    assert (nelmts < MAX_SIZET);
    FUNC_LEAVE ((size_t)nelmts);
}

/*-------------------------------------------------------------------------
 * Function:	H5S_simp_fgath
 *
 * Purpose:	Gathers data points from file F and accumulates them in the
 *		type conversion buffer BUF.  The LAYOUT argument describes
 *		how the data is stored on disk and EFL describes how the data
 *		is organized in external files.  ELMT_SIZE is the size in
 *		bytes of a datum which this function treats as opaque.
 *		FILE_SPACE describes the data space of the dataset on disk
 *		and the elements that have been selected for reading (via
 *		hyperslab, etc) and NUMBERING describes how those elements
 *		are numbered (initialized by the H5S_*_init() call). This
 *		function will copy at most NELMTS elements beginning at the
 *		element numbered START.
 *
 * Return:	Success:	Number of elements copied.
 *
 *		Failure:	0
 *
 * Programmer:	Robb Matzke
 *              Wednesday, January 21, 1998
 *
 * Modifications:
 *		June 2, 1998	Albert Cheng
 *		Added xfer_mode argument
 *
 *-------------------------------------------------------------------------
 */
size_t
H5S_simp_fgath (H5F_t *f, const struct H5O_layout_t *layout,
		const struct H5O_compress_t *comp, const struct H5O_efl_t *efl,
		size_t elmt_size, const H5S_t *file_space,
		size_t start, size_t nelmts,
		const H5D_transfer_t xfer_mode, void *buf/*out*/)
{
    hssize_t	file_offset[H5O_LAYOUT_NDIMS];	/*offset of slab in file*/
    hsize_t	hsize[H5O_LAYOUT_NDIMS];	/*size of hyperslab	*/
    hssize_t	zero[H5O_LAYOUT_NDIMS];		/*zero			*/
    hsize_t	sample[H5O_LAYOUT_NDIMS];	/*hyperslab sampling	*/
    hsize_t	acc;				/*accumulator		*/
    intn	space_ndims;			/*dimensionality of space*/
    intn	i;				/*counters		*/

    FUNC_ENTER (H5S_simp_fgath, 0);

    /* Check args */
    assert (f);
    assert (layout);
    assert (elmt_size>0);
    assert (file_space);
    assert (nelmts>0);
    assert (buf);

    /*
     * Get hyperslab information to determine what elements are being
     * selected (there might eventually be other selection methods too).
     * We only support hyperslabs with unit sample because there's no way to
     * currently pass sample information into H5F_arr_read() much less
     * H5F_istore_read().
     */
    if ((space_ndims=H5S_get_hyperslab (file_space, file_offset,
					hsize, sample))<0) {
	HRETURN_ERROR (H5E_DATASPACE, H5E_CANTINIT, 0,
		       "unable to retrieve hyperslab parameters");
    }

    /* Check that there is no subsampling of the hyperslab */
    for (i=0; i<space_ndims; i++) {
	if (sample[i]!=1) {
	    HRETURN_ERROR (H5E_ARGS, H5E_BADVALUE, 0,
			   "hyperslab sampling is not implemented yet");
	}
    }

    /* Adjust the slowest varying dimension to take care of strip mining */
    for (i=1, acc=1; i<space_ndims; i++) acc *= hsize[i];
    assert (0==start % acc);
    assert (0==nelmts % acc);
    file_offset[0] += start / acc;
    hsize[0] = nelmts / acc;

    /* The fastest varying dimension is for the data point itself */
    file_offset[space_ndims] = 0;
    hsize[space_ndims] = elmt_size;
    HDmemset (zero, 0, layout->ndims*sizeof(*zero));

    /*
     * Gather from file.
     */
    if (H5F_arr_read (f, layout, comp, efl, hsize, hsize, zero, file_offset,
		      xfer_mode, buf/*out*/)<0) {
	HRETURN_ERROR (H5E_DATASPACE, H5E_READERROR, 0, "read error");
    }

    FUNC_LEAVE (nelmts);
}
    
/*-------------------------------------------------------------------------
 * Function:	H5S_simp_mscat
 *
 * Purpose:	Scatters data points from the type conversion buffer
 *		TCONV_BUF to the application buffer BUF.  Each element is
 *		ELMT_SIZE bytes and they are organized in application memory
 *		according to MEM_SPACE.  The NUMBERING information together
 *		with START and NELMTS describe how the elements stored in
 *		TCONV_BUF are globally numbered.
 *
 * Return:	Success:	SUCCEED
 *
 *		Failure:	FAIL
 *
 * Programmer:	Robb Matzke
 *              Wednesday, January 21, 1998
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5S_simp_mscat (const void *tconv_buf, size_t elmt_size,
		const H5S_t *mem_space,
		size_t start, size_t nelmts, void *buf/*out*/)
{
    hssize_t	mem_offset[H5O_LAYOUT_NDIMS];	/*slab offset in app buf*/
    hsize_t	mem_size[H5O_LAYOUT_NDIMS];	/*total size of app buf	*/
    hsize_t	hsize[H5O_LAYOUT_NDIMS];	/*size of hyperslab	*/
    hssize_t	zero[H5O_LAYOUT_NDIMS];		/*zero			*/
    hsize_t	sample[H5O_LAYOUT_NDIMS];	/*hyperslab sampling	*/
    hsize_t	acc;				/*accumulator		*/
    intn	space_ndims;			/*dimensionality of space*/
    intn	i;				/*counters		*/

    FUNC_ENTER (H5S_simp_mscat, FAIL);

    /* Check args */
    assert (tconv_buf);
    assert (elmt_size>0);
    assert (mem_space && H5S_SIMPLE==mem_space->extent.type);
    assert (nelmts>0);
    assert (buf);

    /*
     * Retrieve hyperslab information to determine what elements are being
     * selected (there might be other selection methods in the future).  We
     * only handle hyperslabs with unit sample because there's currently no
     * way to pass sample information to H5V_hyper_copy().
     */
    if ((space_ndims=H5S_get_hyperslab (mem_space, mem_offset, hsize,
					sample))<0) {
	HRETURN_ERROR (H5E_DATASPACE, H5E_CANTINIT, FAIL,
		       "unable to retrieve hyperslab parameters");
    }

    /* Check that there is no subsampling of the hyperslab */
    for (i=0; i<space_ndims; i++) {
	if (sample[i]!=1) {
	    HRETURN_ERROR (H5E_ARGS, H5E_BADVALUE, FAIL,
			   "hyperslab sampling is not implemented yet");
	}
    }
    if (H5S_extent_dims (mem_space, mem_size, NULL)<0) {
	HRETURN_ERROR (H5E_DATASPACE, H5E_CANTINIT, FAIL,
		       "unable to retrieve data space dimensions");
    }

    /* Adjust the slowest varying dimension to take care of strip mining */
    for (i=1, acc=1; i<space_ndims; i++) acc *= hsize[i];
    assert (0==start % acc);
    assert (0==nelmts % acc);
    mem_offset[0] += start / acc;
    hsize[0] = nelmts / acc;

    /* The fastest varying dimension is for the data point itself */
    mem_offset[space_ndims] = 0;
    mem_size[space_ndims] = elmt_size;
    hsize[space_ndims] = elmt_size;
    HDmemset (zero, 0, (space_ndims+1)*sizeof(*zero));

    /*
     * Scatter from conversion buffer to application memory.
     */
    if (H5V_hyper_copy (space_ndims+1, hsize, mem_size, mem_offset, buf,
			hsize, zero, tconv_buf)<0) {
	HRETURN_ERROR (H5E_DATASPACE, H5E_CANTINIT, FAIL,
		       "unable to scatter data to memory");
    }

    FUNC_LEAVE (SUCCEED);
}

/*-------------------------------------------------------------------------
 * Function:	H5S_simp_mgath
 *
 * Purpose:	Gathers dataset elements from application memory BUF and
 *		copies them into the data type conversion buffer TCONV_BUF.
 *		Each element is ELMT_SIZE bytes and arranged in application
 *		memory according to MEM_SPACE.  The elements selected from
 *		BUF by MEM_SPACE are numbered according to NUMBERING and the
 *		caller is requesting that at most NELMTS be gathered
 *		beginning with number START.  The elements are packed into
 *		TCONV_BUF in order of their NUMBERING.
 *
 * Return:	Success:	Number of elements copied.
 *
 *		Failure:	0
 *
 * Programmer:	Robb Matzke
 *              Wednesday, January 21, 1998
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
size_t
H5S_simp_mgath (const void *buf, size_t elmt_size,
		const H5S_t *mem_space,
		size_t start, size_t nelmts, void *tconv_buf/*out*/)
{
    hssize_t	mem_offset[H5O_LAYOUT_NDIMS];	/*slab offset in app buf*/
    hsize_t	mem_size[H5O_LAYOUT_NDIMS];	/*total size of app buf	*/
    hsize_t	hsize[H5O_LAYOUT_NDIMS];	/*size of hyperslab	*/
    hssize_t	zero[H5O_LAYOUT_NDIMS];		/*zero			*/
    hsize_t	sample[H5O_LAYOUT_NDIMS];	/*hyperslab sampling	*/
    hsize_t	acc;				/*accumulator		*/
    intn	space_ndims;			/*dimensionality of space*/
    intn	i;				/*counters		*/

    FUNC_ENTER (H5S_simp_mgath, 0);

    /* Check args */
    assert (buf);
    assert (elmt_size>0);
    assert (mem_space && H5S_SIMPLE==mem_space->extent.type);
    assert (nelmts>0);
    assert (tconv_buf);

    /*
     * Retrieve hyperslab information to determine what elements are being
     * selected (there might be other selection methods in the future).  We
     * only handle hyperslabs with unit sample because there's currently no
     * way to pass sample information to H5V_hyper_copy().
     */
    if ((space_ndims=H5S_get_hyperslab (mem_space, mem_offset, hsize,
					sample))<0) {
	HRETURN_ERROR (H5E_DATASPACE, H5E_CANTINIT, 0,
		       "unable to retrieve hyperslab parameters");
    }

    /* Check that there is no subsampling of the hyperslab */
    for (i=0; i<space_ndims; i++) {
	if (sample[i]!=1) {
	    HRETURN_ERROR (H5E_ARGS, H5E_BADVALUE, 0,
			   "hyperslab sampling is not implemented yet");
	}
    }
    if (H5S_extent_dims (mem_space, mem_size, NULL)<0) {
	HRETURN_ERROR (H5E_DATASPACE, H5E_CANTINIT, 0,
		       "unable to retrieve data space dimensions");
    }

    /* Adjust the slowest varying dimension to account for strip mining */
    for (i=1, acc=1; i<space_ndims; i++) acc *= hsize[i];
    assert (0==start % acc);
    assert (0==nelmts % acc);
    mem_offset[0] += start / acc;
    hsize[0] = nelmts / acc;
    
    /* The fastest varying dimension is for the data point itself */
    mem_offset[space_ndims] = 0;
    mem_size[space_ndims] = elmt_size;
    hsize[space_ndims] = elmt_size;
    HDmemset (zero, 0, (space_ndims+1)*sizeof(*zero));

    /*
     * Scatter from conversion buffer to application memory.
     */
    if (H5V_hyper_copy (space_ndims+1, hsize, hsize, zero, tconv_buf,
			mem_size, mem_offset, buf)<0) {
	HRETURN_ERROR (H5E_DATASPACE, H5E_CANTINIT, 0,
		       "unable to scatter data to memory");
    }

    FUNC_LEAVE (nelmts);
}

/*-------------------------------------------------------------------------
 * Function:	H5S_simp_fscat
 *
 * Purpose:	Scatters dataset elements from the type conversion buffer BUF
 *		to the file F where the data points are arranged according to
 *		the file data space FILE_SPACE and stored according to
 *		LAYOUT and EFL. Each element is ELMT_SIZE bytes and has a
 *		unique number according to NUMBERING.  The caller is
 *		requesting that NELMTS elements are coppied beginning with
 *		element number START.
 *
 * Return:	Success:	SUCCEED
 *
 *		Failure:	FAIL
 *
 * Programmer:	Robb Matzke
 *              Wednesday, January 21, 1998
 *
 * Modifications:
 *		June 2, 1998	Albert Cheng
 *		Added xfer_mode argument
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5S_simp_fscat (H5F_t *f, const struct H5O_layout_t *layout,
		const struct H5O_compress_t *comp, const struct H5O_efl_t *efl,
		size_t elmt_size, const H5S_t *file_space,
		size_t start, size_t nelmts,
		const H5D_transfer_t xfer_mode, const void *buf)
{
    hssize_t	file_offset[H5O_LAYOUT_NDIMS];	/*offset of hyperslab	*/
    hsize_t	hsize[H5O_LAYOUT_NDIMS];	/*size of hyperslab	*/
    hssize_t	zero[H5O_LAYOUT_NDIMS];		/*zero vector		*/
    hsize_t	sample[H5O_LAYOUT_NDIMS];	/*hyperslab sampling	*/
    hsize_t	acc;				/*accumulator		*/
    intn	space_ndims;			/*space dimensionality	*/
    intn	i;				/*counters		*/

    FUNC_ENTER (H5S_simp_fscat, FAIL);

    /* Check args */
    assert (f);
    assert (layout);
    assert (elmt_size>0);
    assert (file_space);
    assert (nelmts>0);
    assert (buf);

    /*
     * Get hyperslab information to determine what elements are being
     * selected (there might eventually be other selection methods too).
     * We only support hyperslabs with unit sample because there's no way to
     * currently pass sample information into H5F_arr_read() much less
     * H5F_istore_read().
     */
    if ((space_ndims=H5S_get_hyperslab (file_space, file_offset, hsize,
					sample))<0) {
	HRETURN_ERROR (H5E_DATASPACE, H5E_CANTINIT, FAIL,
		       "unable to retrieve hyperslab parameters");
    }

    /* Check that there is no subsampling of the hyperslab */
    for (i=0; i<space_ndims; i++) {
	if (sample[i]!=1) {
	    HRETURN_ERROR (H5E_ARGS, H5E_BADVALUE, FAIL,
			   "hyperslab sampling is not implemented yet");
	}
    }

    /* Adjust the slowest varying dimension to account for strip mining */
    for (i=1, acc=1; i<space_ndims; i++) acc *= hsize[i];
    assert (0==start % acc);
    assert (0==nelmts % acc);
    file_offset[0] += start / acc;
    hsize[0] = nelmts / acc;
    
    /* The fastest varying dimension is for the data point itself */
    file_offset[space_ndims] = 0;
    hsize[space_ndims] = elmt_size;
    HDmemset (zero, 0, layout->ndims*sizeof(*zero));

    /*
     * Scatter to file.
     */
    if (H5F_arr_write (f, layout, comp, efl, hsize, hsize, zero,
		       file_offset, xfer_mode, buf)<0) {
	HRETURN_ERROR (H5E_DATASPACE, H5E_WRITEERROR, FAIL, "write error");
    }

    FUNC_LEAVE (SUCCEED);
}


/*-------------------------------------------------------------------------
 * Function:	H5S_simp_read
 *
 * Purpose:	Reads a dataset from file F directly into application memory
 *		BUF performing data space conversion in a single step from
 *		FILE_SPACE to MEM_SPACE. The dataset is stored in the file
 *		according to the LAYOUT and EFL (external file list) and data
 *		point in the file is ELMT_SIZE bytes.
 *
 * Return:	Success:	SUCCEED
 *
 *		Failure:	FAIL
 *
 * Programmer:	Robb Matzke
 *              Thursday, March 12, 1998
 *
 * Modifications:
 *		June 2, 1998	Albert Cheng
 *		Added xfer_mode argument
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5S_simp_read (H5F_t *f, const struct H5O_layout_t *layout,
	       const struct H5O_compress_t *comp, const struct H5O_efl_t *efl,
	       size_t elmt_size, const H5S_t *file_space,
	       const H5S_t *mem_space, const H5D_transfer_t xfer_mode,
	       void *buf/*out*/)
{
    hssize_t	file_offset[H5O_LAYOUT_NDIMS];
    hsize_t	hslab_size[H5O_LAYOUT_NDIMS];
    hssize_t	mem_offset[H5O_LAYOUT_NDIMS];
    hsize_t	mem_size[H5O_LAYOUT_NDIMS];
    int		i;

    FUNC_ENTER (H5S_simp_read, FAIL);

#ifndef NDEBUG
    assert (file_space->extent.type==mem_space->extent.type);
    assert (file_space->extent.u.simple.rank==mem_space->extent.u.simple.rank);
    for (i=0; i<file_space->extent.u.simple.rank; i++) {
#ifdef FIXME
	if (file_space->hslab_def && mem_space->hslab_def) {
	    assert (1==file_space->h.stride[i]);
	    assert (1==mem_space->h.stride[i]);
	    assert (file_space->h.count[i]==mem_space->h.count[i]);
	} else if (file_space->hslab_def) {
	    assert (1==file_space->h.stride[i]);
	    assert (file_space->h.count[i]==mem_space->u.simple.size[i]);
	} else if (mem_space->hslab_def) {
	    assert (1==mem_space->h.stride[i]);
	    assert (file_space->u.simple.size[i]==mem_space->h.count[i]);
	} else {
	    assert (file_space->u.simple.size[i]==
		    mem_space->u.simple.size[i]);
	}
#endif
    }
#endif
	

    /*
     * Calculate size of hyperslab and offset of hyperslab into file and
     * memory.
     */
    switch(file_space->select.type) {
        case H5S_SEL_NONE:      /* no selection defined */
            HRETURN_ERROR (H5E_DATASPACE, H5E_BADVALUE, FAIL,
			   "selection not defined");

        case H5S_SEL_POINTS:        /* point sequence selection defined */
        case H5S_SEL_HYPERSLABS:    /* hyperslab selection defined */
            HRETURN_ERROR (H5E_DATASPACE, H5E_UNSUPPORTED, FAIL,
			   "selection type not supprted currently");

        case H5S_SEL_ALL:           /* entire dataspace selection */
            for (i=0; i<file_space->extent.u.simple.rank; i++)
                hslab_size[i] = file_space->extent.u.simple.size[i];
            break;
    } /* end switch */

    switch(mem_space->select.type) {
        case H5S_SEL_NONE:      /* no selection defined */
            HRETURN_ERROR (H5E_DATASPACE, H5E_BADVALUE, FAIL,
			   "selection not defined");

        case H5S_SEL_POINTS:        /* point sequence selection defined */
        case H5S_SEL_HYPERSLABS:    /* hyperslab selection defined */
            HRETURN_ERROR (H5E_DATASPACE, H5E_UNSUPPORTED, FAIL,
			   "selection type not supprted currently");

        case H5S_SEL_ALL:           /* entire dataspace selection */
            for (i=0; i<mem_space->extent.u.simple.rank; i++)
                mem_size[i] = mem_space->extent.u.simple.size[i];
            break;
    } /* end switch */

    switch(file_space->select.type) {
        case H5S_SEL_NONE:      /* no selection defined */
            HRETURN_ERROR (H5E_DATASPACE, H5E_BADVALUE, FAIL,
			   "selection not defined");

        case H5S_SEL_POINTS:        /* point sequence selection defined */
        case H5S_SEL_HYPERSLABS:    /* hyperslab selection defined */
#ifdef LATER
            for (i=0; i<file_space->u.simple.rank; i++) 
                file_offset[i] = file_space->h.start[i];
#endif
            HRETURN_ERROR (H5E_DATASPACE, H5E_UNSUPPORTED, FAIL,
			   "selection type not supprted currently");

        case H5S_SEL_ALL:           /* entire dataspace selection */
            for (i=0; i<file_space->extent.u.simple.rank; i++) 
                file_offset[i] = 0;
            break;
    } /* end switch */

    switch(mem_space->select.type) {
        case H5S_SEL_NONE:      /* no selection defined */
            HRETURN_ERROR (H5E_DATASPACE, H5E_BADVALUE, FAIL,
			   "selection not defined");

        case H5S_SEL_POINTS:        /* point sequence selection defined */
        case H5S_SEL_HYPERSLABS:    /* hyperslab selection defined */
#ifdef LATER
            for (i=0; i<mem_space->u.simple.rank; i++) 
                mem_offset[i] = mem_space->h.start[i];
#endif
            HRETURN_ERROR (H5E_DATASPACE, H5E_UNSUPPORTED, FAIL,
			   "selection type not supprted currently");

        case H5S_SEL_ALL:           /* entire dataspace selection */
            for (i=0; i<mem_space->extent.u.simple.rank; i++)
                mem_offset[i] = 0;
            break;
    } /* end switch */

    hslab_size[file_space->extent.u.simple.rank] = elmt_size;
    mem_size[file_space->extent.u.simple.rank] = elmt_size;
    file_offset[file_space->extent.u.simple.rank] = 0;
    mem_offset[file_space->extent.u.simple.rank] = 0;
    
    /* Read the hyperslab */
    if (H5F_arr_read (f, layout, comp, efl, hslab_size,
		      mem_size, mem_offset, file_offset, xfer_mode, buf)<0) {
	HRETURN_ERROR (H5E_IO, H5E_READERROR, FAIL, "unable to read dataset");
    }

    FUNC_LEAVE (SUCCEED);
}


/*-------------------------------------------------------------------------
 * Function:	H5S_simp_write
 *
 * Purpose:	Write a dataset from application memory BUF directly into
 *		file F performing data space conversion in a single step from
 *		MEM_SPACE to FILE_SPACE. The dataset is stored in the file
 *		according to the LAYOUT and EFL (external file list) and data
 *		point in the file is ELMT_SIZE bytes.
 *
 * Return:	Success:	SUCCEED
 *
 *		Failure:	FAIL
 *
 * Programmer:	Robb Matzke
 *              Thursday, March 12, 1998
 *
 * Modifications:
 *		June 2, 1998	Albert Cheng
 *		Added xfer_mode argument
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5S_simp_write (H5F_t *f, const struct H5O_layout_t *layout,
		const struct H5O_compress_t *comp, const struct H5O_efl_t *efl,
		size_t elmt_size, const H5S_t *file_space,
		const H5S_t *mem_space, const H5D_transfer_t xfer_mode,
		const void *buf)
{
    hssize_t	file_offset[H5O_LAYOUT_NDIMS];
    hsize_t	hslab_size[H5O_LAYOUT_NDIMS];
    hssize_t	mem_offset[H5O_LAYOUT_NDIMS];
    hsize_t	mem_size[H5O_LAYOUT_NDIMS];
    int		i;

    FUNC_ENTER (H5S_simp_write, FAIL);

#ifndef NDEBUG
    assert (file_space->extent.type==mem_space->extent.type);
    assert (file_space->extent.u.simple.rank==mem_space->extent.u.simple.rank);
#ifdef LATER
    for (i=0; i<file_space->u.simple.rank; i++) {
	if (file_space->hslab_def && mem_space->hslab_def) {
	    assert (1==file_space->h.stride[i]);
	    assert (1==mem_space->h.stride[i]);
	    assert (file_space->h.count[i]==mem_space->h.count[i]);
	} else if (file_space->hslab_def) {
	    assert (1==file_space->h.stride[i]);
	    assert (file_space->h.count[i]==mem_space->u.simple.size[i]);
	} else if (mem_space->hslab_def) {
	    assert (1==mem_space->h.stride[i]);
	    assert (file_space->u.simple.size[i]==mem_space->h.count[i]);
	} else {
	    assert (file_space->u.simple.size[i]==
		    mem_space->u.simple.size[i]);
	}
    }
#endif
#endif
	

    /*
     * Calculate size of hyperslab and offset of hyperslab into file and
     * memory.
     */
    switch(file_space->select.type) {
        case H5S_SEL_NONE:      /* no selection defined */
            HRETURN_ERROR (H5E_DATASPACE, H5E_BADVALUE, FAIL,
			   "selection not defined");

        case H5S_SEL_POINTS:        /* point sequence selection defined */
        case H5S_SEL_HYPERSLABS:    /* hyperslab selection defined */
            HRETURN_ERROR (H5E_DATASPACE, H5E_UNSUPPORTED, FAIL,
			   "selection type not supprted currently");

        case H5S_SEL_ALL:           /* entire dataspace selection */
            for (i=0; i<file_space->extent.u.simple.rank; i++)
                hslab_size[i] = file_space->extent.u.simple.size[i];
            break;
    } /* end switch */

    switch(mem_space->select.type) {
        case H5S_SEL_NONE:      /* no selection defined */
            HRETURN_ERROR (H5E_DATASPACE, H5E_BADVALUE, FAIL,
			   "selection not defined");

        case H5S_SEL_POINTS:        /* point sequence selection defined */
        case H5S_SEL_HYPERSLABS:    /* hyperslab selection defined */
            HRETURN_ERROR (H5E_DATASPACE, H5E_UNSUPPORTED, FAIL,
			   "selection type not supprted currently");

        case H5S_SEL_ALL:           /* entire dataspace selection */
            for (i=0; i<mem_space->extent.u.simple.rank; i++)
                mem_size[i] = mem_space->extent.u.simple.size[i];
            break;
    } /* end switch */

    switch(file_space->select.type) {
        case H5S_SEL_NONE:      /* no selection defined */
            HRETURN_ERROR (H5E_DATASPACE, H5E_BADVALUE, FAIL,
			   "selection not defined");

        case H5S_SEL_POINTS:        /* point sequence selection defined */
        case H5S_SEL_HYPERSLABS:    /* hyperslab selection defined */
#ifdef LATER
            for (i=0; i<file_space->u.simple.rank; i++) 
                file_offset[i] = file_space->h.start[i];
#endif
            HRETURN_ERROR (H5E_DATASPACE, H5E_UNSUPPORTED, FAIL,
			   "selection type not supprted currently");

        case H5S_SEL_ALL:           /* entire dataspace selection */
            for (i=0; i<file_space->extent.u.simple.rank; i++) 
                file_offset[i] = 0;
            break;
    } /* end switch */

    switch(mem_space->select.type) {
        case H5S_SEL_NONE:      /* no selection defined */
            HRETURN_ERROR (H5E_DATASPACE, H5E_BADVALUE, FAIL,
			   "selection not defined");

        case H5S_SEL_POINTS:        /* point sequence selection defined */
        case H5S_SEL_HYPERSLABS:    /* hyperslab selection defined */
#ifdef LATER
            for (i=0; i<mem_space->u.simple.rank; i++) 
                mem_offset[i] = mem_space->h.start[i];
#endif
            HRETURN_ERROR (H5E_DATASPACE, H5E_UNSUPPORTED, FAIL,
			   "selection type not supprted currently");

        case H5S_SEL_ALL:           /* entire dataspace selection */
            for (i=0; i<mem_space->extent.u.simple.rank; i++)
                mem_offset[i] = 0;
            break;
    } /* end switch */

    hslab_size[file_space->extent.u.simple.rank] = elmt_size;
    mem_size[file_space->extent.u.simple.rank] = elmt_size;
    file_offset[file_space->extent.u.simple.rank] = 0;
    mem_offset[file_space->extent.u.simple.rank] = 0;
    
    /* Write the hyperslab */
    if (H5F_arr_write (f, layout, comp, efl, hslab_size,
		       mem_size, mem_offset, file_offset, xfer_mode, buf)<0) {
	HRETURN_ERROR (H5E_IO, H5E_WRITEERROR, FAIL,
		       "unable to write dataset");
    }

    FUNC_LEAVE (SUCCEED);
}

