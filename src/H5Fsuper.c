/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have          *
 * access to either file, you may request a copy from help@hdfgroup.org.     *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/****************/
/* Module Setup */
/****************/

#define H5F_PACKAGE		/*suppress error about including H5Fpkg	  */

/* Interface initialization */
#define H5_INTERFACE_INIT_FUNC	H5F_init_super_interface


/***********/
/* Headers */
/***********/
#include "H5private.h"		/* Generic Functions			*/
#include "H5Eprivate.h"		/* Error handling		  	*/
#include "H5Fpkg.h"             /* File access				*/
#include "H5FDprivate.h"	/* File drivers				*/
#include "H5Iprivate.h"		/* IDs			  		*/
#include "H5Pprivate.h"		/* Property lists			*/
#include "H5SMprivate.h"        /* Shared Object Header Messages        */


/****************/
/* Local Macros */
/****************/

/* Superblock sizes for various versions */
#define H5F_SIZEOF_CHKSUM 4     /* Checksum size in the file */

/* Fixed-size portion at the beginning of all superblocks */
#define H5F_SUPERBLOCK_FIXED_SIZE ( H5F_SIGNATURE_LEN                   \
        + 1) /* superblock version */

/* Macros for computing variable-size superblock size */
#define H5F_SUPERBLOCK_VARLEN_SIZE_COMMON                               \
        (2  /* freespace, and root group versions */			\
        + 1 /* reserved */                                              \
        + 3 /* shared header vers, size of address, size of lengths */  \
        + 1 /* reserved */                                              \
        + 4 /* group leaf k, group internal k */                        \
        + 4) /* consistency flags */
#define H5F_SUPERBLOCK_VARLEN_SIZE_V0(f)                                \
        ( H5F_SUPERBLOCK_VARLEN_SIZE_COMMON /* Common variable-length info */ \
        + H5F_SIZEOF_ADDR(f) /* base address */                         \
        + H5F_SIZEOF_ADDR(f) /* <unused> */				\
        + H5F_SIZEOF_ADDR(f) /* EOF address */                          \
        + H5F_SIZEOF_ADDR(f) /* driver block address */                 \
        + H5G_SIZEOF_ENTRY(f)) /* root group ptr */
#define H5F_SUPERBLOCK_VARLEN_SIZE_V1(f)                                \
        ( H5F_SUPERBLOCK_VARLEN_SIZE_COMMON /* Common variable-length info */ \
        + 2 /* indexed B-tree internal k */                             \
        + 2 /* reserved */                                              \
        + H5F_SIZEOF_ADDR(f) /* base address */                         \
        + H5F_SIZEOF_ADDR(f) /* <unused> */				\
        + H5F_SIZEOF_ADDR(f) /* EOF address */                          \
        + H5F_SIZEOF_ADDR(f) /* driver block address */                 \
        + H5G_SIZEOF_ENTRY(f)) /* root group ptr */
#define H5F_SUPERBLOCK_VARLEN_SIZE_V2(f)                                \
        ( H5F_SUPERBLOCK_VARLEN_SIZE_COMMON /* Common variable-length info */ \
        + 2 /* indexed B-tree internal k */                             \
        + H5F_SIZEOF_ADDR(f) /* base address */                         \
        + H5F_SIZEOF_ADDR(f) /* superblock extension address */         \
        + H5F_SIZEOF_ADDR(f) /* EOF address */                          \
        + H5F_SIZEOF_ADDR(f) /* driver block address */                 \
        + H5G_SIZEOF_ENTRY(f) /* root group ptr */                      \
        + H5F_SIZEOF_CHKSUM) /* superblock + driver info block checksum (keep this last) */
#define H5F_SUPERBLOCK_VARLEN_SIZE(v, f) (				\
        (v == 0 ? H5F_SUPERBLOCK_VARLEN_SIZE_V0(f) : 0)			\
        + (v == 1 ? H5F_SUPERBLOCK_VARLEN_SIZE_V1(f) : 0)               \
        + (v == 2 ? H5F_SUPERBLOCK_VARLEN_SIZE_V2(f) : 0))

/* Total size of superblock, depends on superblock version */
#define H5F_SUPERBLOCK_SIZE(v, f) ( H5F_SUPERBLOCK_FIXED_SIZE           \
        + H5F_SUPERBLOCK_VARLEN_SIZE(v, f))

/* Driver info block macros */
#define H5F_DRVINFOBLOCK_HDR_SIZE 16

/* Maximum size of super-block buffers */
#define H5F_MAX_SUPERBLOCK_SIZE  134
#define H5F_MAX_DRVINFOBLOCK_SIZE  1024


/******************/
/* Local Typedefs */
/******************/


/********************/
/* Package Typedefs */
/********************/


/********************/
/* Local Prototypes */
/********************/


/*********************/
/* Package Variables */
/*********************/


/*****************************/
/* Library Private Variables */
/*****************************/


/*******************/
/* Local Variables */
/*******************/


/*--------------------------------------------------------------------------
NAME
   H5F_init_super_interface -- Initialize interface-specific information
USAGE
    herr_t H5F_init_super_interface()

RETURNS
    Non-negative on success/Negative on failure
DESCRIPTION
    Initializes any interface-specific data or routines.  (Just calls
    H5F_init() currently).

--------------------------------------------------------------------------*/
static herr_t
H5F_init_super_interface(void)
{
    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5F_init_super_interface)

    FUNC_LEAVE_NOAPI(H5F_init())
} /* H5F_init_super_interface() */


/*-------------------------------------------------------------------------
 * Function:	H5F_locate_signature
 *
 * Purpose:	Finds the HDF5 superblock signature in a file.	The signature
 *		can appear at address 0, or any power of two beginning with
 *		512.
 *
 * Return:	Success:	The absolute format address of the signature.
 *
 *		Failure:	HADDR_UNDEF
 *
 * Programmer:	Robb Matzke
 *		Friday, November  7, 1997
 *
 *-------------------------------------------------------------------------
 */
haddr_t
H5F_locate_signature(H5FD_t *file, hid_t dxpl_id)
{
    haddr_t	    addr, eoa;
    uint8_t	    buf[H5F_SIGNATURE_LEN];
    unsigned	    n, maxpow;
    haddr_t         ret_value;       /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5F_locate_signature)

    /* Find the least N such that 2^N is larger than the file size */
    if(HADDR_UNDEF == (addr = H5FD_get_eof(file)) || HADDR_UNDEF == (eoa = H5FD_get_eoa(file, H5FD_MEM_SUPER)))
	HGOTO_ERROR(H5E_IO, H5E_CANTINIT, HADDR_UNDEF, "unable to obtain EOF/EOA value")
    for(maxpow = 0; addr; maxpow++)
        addr >>= 1;
    maxpow = MAX(maxpow, 9);

    /*
     * Search for the file signature at format address zero followed by
     * powers of two larger than 9.
     */
    for(n = 8; n < maxpow; n++) {
	addr = (8 == n) ? 0 : (haddr_t)1 << n;
	if(H5FD_set_eoa(file, H5FD_MEM_SUPER, addr+H5F_SIGNATURE_LEN) < 0)
	    HGOTO_ERROR(H5E_IO, H5E_CANTINIT, HADDR_UNDEF, "unable to set EOA value for file signature")
	if(H5FD_read(file, H5FD_MEM_SUPER, dxpl_id, addr, (size_t)H5F_SIGNATURE_LEN, buf) < 0)
	    HGOTO_ERROR(H5E_IO, H5E_CANTINIT, HADDR_UNDEF, "unable to read file signature")
	if(!HDmemcmp(buf, H5F_SIGNATURE, (size_t)H5F_SIGNATURE_LEN))
            break;
    } /* end for */

    /*
     * If the signature was not found then reset the EOA value and return
     * failure.
     */
    if(n >= maxpow) {
	(void)H5FD_set_eoa(file, H5FD_MEM_SUPER, eoa); /* Ignore return value */
	HGOTO_ERROR(H5E_IO, H5E_CANTINIT, HADDR_UNDEF, "unable to find a valid file signature")
    } /* end if */

    /* Set return value */
    ret_value = addr;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5F_locate_signature() */


/*-------------------------------------------------------------------------
 * Function:    H5F_read_superblock
 *
 * Purpose:     Reads the superblock from the file or from the BUF. If
 *              ADDR is a valid address, then it reads it from the file.
 *              If not, then BUF must be non-NULL for it to read from the
 *              BUF.
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  Bill Wendling
 *              wendling@ncsa.uiuc.edu
 *              Sept 12, 2003
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5F_read_superblock(H5F_t *f, hid_t dxpl_id, H5G_loc_t *root_loc)
{
    haddr_t             stored_eoa;         /*relative end-of-addr in file  */
    haddr_t             eof;                /*end of file address           */
    size_t              sizeof_addr;        /* Size of offsets in the file (in bytes) */
    size_t              sizeof_size;        /* Size of lengths in the file (in bytes) */
    const size_t        fixed_size = H5F_SUPERBLOCK_FIXED_SIZE; /*fixed sizeof superblock   */
    unsigned            sym_leaf_k;         /* Symbol table leaf node's 'K' value */
    size_t              variable_size;      /*variable sizeof superblock    */
    unsigned            btree_k[H5B_NUM_BTREE_ID];  /* B-tree internal node 'K' values */
    H5F_file_t         *shared = NULL;      /* shared part of `file'        */
    H5FD_t             *lf = NULL;          /* file driver part of `shared' */
    uint8_t            *p;                  /* Temporary pointer into encoding buffer */
    unsigned            super_vers;         /* Superblock version          */
    unsigned            freespace_vers;     /* Freespace info version       */
    unsigned            obj_dir_vers;       /* Object header info version   */
    unsigned            share_head_vers;    /* Shared header info version   */
    uint8_t             buf[H5F_MAX_SUPERBLOCK_SIZE + H5F_MAX_DRVINFOBLOCK_SIZE];     /* Buffer for superblock & driver info block */
    H5P_genplist_t     *c_plist;            /* File creation property list  */
    herr_t              ret_value = SUCCEED;

    FUNC_ENTER_NOAPI(H5F_read_superblock, FAIL)

    /* Short cuts */
    shared = f->shared;
    lf = shared->lf;

    /* Get the shared file creation property list */
    if(NULL == (c_plist = H5I_object(shared->fcpl_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "can't get property list")

    /* Find the superblock */
    if(HADDR_UNDEF == (shared->super_addr = H5F_locate_signature(lf, dxpl_id)))
        HGOTO_ERROR(H5E_FILE, H5E_NOTHDF5, FAIL, "unable to find file signature")

    /* Read fixed-size portion of the superblock */
    p = buf;
    if(H5FD_set_eoa(lf, H5FD_MEM_SUPER, shared->super_addr + fixed_size) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, FAIL, "set end of space allocation request failed")
    if(H5FD_read(lf, H5FD_MEM_SUPER, dxpl_id, shared->super_addr, fixed_size, p) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_READERROR, FAIL, "unable to read superblock")

    /* Skip over signature (already checked when locating the superblock) */
    p += H5F_SIGNATURE_LEN;

    /* Superblock version */
    super_vers = *p++;
    if(super_vers > HDF5_SUPERBLOCK_VERSION_LATEST)
        HGOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "bad superblock version number")
    if(H5P_set(c_plist, H5F_CRT_SUPER_VERS_NAME, &super_vers) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "unable to set superblock version")

    /* Sanity check */
    HDassert(((size_t)(p - buf)) == fixed_size);

    /* Determine the size of the variable-length part of the superblock */
    variable_size = H5F_SUPERBLOCK_VARLEN_SIZE(super_vers, f);
    HDassert(variable_size > 0);
    HDassert(fixed_size + variable_size <= sizeof(buf));

    /* Read in variable-sized portion of superblock */
    if(H5FD_set_eoa(lf, H5FD_MEM_SUPER, shared->super_addr + fixed_size + variable_size) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, FAIL, "set end of space allocation request failed")
    if(H5FD_read(lf, H5FD_MEM_SUPER, dxpl_id, shared->super_addr + fixed_size, variable_size, p) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "unable to read superblock")

    /* Freespace version */
    freespace_vers = *p++;
    if(HDF5_FREESPACE_VERSION != freespace_vers)
        HGOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "bad free space version number")
    if(H5P_set(c_plist, H5F_CRT_FREESPACE_VERS_NAME, &freespace_vers) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "unable to set free space version")

    /* Root group version number */
    obj_dir_vers = *p++;
    if(HDF5_OBJECTDIR_VERSION != obj_dir_vers)
        HGOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "bad object directory version number")
    if(H5P_set(c_plist, H5F_CRT_OBJ_DIR_VERS_NAME, &obj_dir_vers) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "unable to set object directory version")

    /* Skip over reserved byte */
    p++;

    /* Shared header version number */
    share_head_vers = *p++;
    if(HDF5_SHAREDHEADER_VERSION != share_head_vers)
        HGOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "bad shared-header format version number")
    if(H5P_set(c_plist, H5F_CRT_SHARE_HEAD_VERS_NAME, &share_head_vers) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "unable to set shared-header format version")

    /* Size of file addresses */
    sizeof_addr = *p++;
    if(sizeof_addr != 2 && sizeof_addr != 4 &&
            sizeof_addr != 8 && sizeof_addr != 16 && sizeof_addr != 32)
        HGOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "bad byte number in an address")
    if(H5P_set(c_plist, H5F_CRT_ADDR_BYTE_NUM_NAME,&sizeof_addr) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "unable to set byte number in an address")
    shared->sizeof_addr = sizeof_addr;  /* Keep a local copy also */

    /* Size of file sizes */
    sizeof_size = *p++;
    if(sizeof_size != 2 && sizeof_size != 4 &&
            sizeof_size != 8 && sizeof_size != 16 && sizeof_size != 32)
        HGOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "bad byte number for object size")
    if(H5P_set(c_plist, H5F_CRT_OBJ_BYTE_NUM_NAME, &sizeof_size) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "unable to set byte number for object size")
    shared->sizeof_size = sizeof_size;  /* Keep a local copy also */

    /* Skip over reserved byte */
    p++;

    /* Various B-tree sizes */
    UINT16DECODE(p, sym_leaf_k);
    if(sym_leaf_k == 0)
        HGOTO_ERROR(H5E_FILE, H5E_BADRANGE, FAIL, "bad symbol table leaf node 1/2 rank")
    if(H5P_set(c_plist, H5F_CRT_SYM_LEAF_NAME, &sym_leaf_k) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "unable to set rank for symbol table leaf nodes")
    shared->sym_leaf_k = sym_leaf_k;    /* Keep a local copy also */

    /* Need 'get' call to set other array values */
    if(H5P_get(c_plist, H5F_CRT_BTREE_RANK_NAME, btree_k) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "unable to get rank for btree internal nodes")
    UINT16DECODE(p, btree_k[H5B_SNODE_ID]);
    if(btree_k[H5B_SNODE_ID] == 0)
        HGOTO_ERROR(H5E_FILE, H5E_BADRANGE, FAIL, "bad 1/2 rank for btree internal nodes")
    /*
     * Delay setting the value in the property list until we've checked
     * for the indexed storage B-tree internal 'K' value later.
     */

    /* File consistency flags. Not really used yet */
    UINT32DECODE(p, shared->consist_flags);

    /*
     * If the superblock version # is greater than 0, read in the indexed
     * storage B-tree internal 'K' value
     */
    if(super_vers > HDF5_SUPERBLOCK_VERSION_DEF) {
        UINT16DECODE(p, btree_k[H5B_ISTORE_ID]);
        /* Reserved bytes are present only in version 1 */
        if(super_vers == HDF5_SUPERBLOCK_VERSION_1)
            p += 2;   /* reserved */
    } /* end if */
    else
        btree_k[H5B_ISTORE_ID] = HDF5_BTREE_ISTORE_IK_DEF;

    /* Set the B-tree internal node values, etc */
    if(H5P_set(c_plist, H5F_CRT_BTREE_RANK_NAME, btree_k) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "unable to set rank for btree internal nodes")
    HDmemcpy(shared->btree_k, btree_k, sizeof(unsigned) * (size_t)H5B_NUM_BTREE_ID);    /* Keep a local copy also */

    /* Remainder of "variable-sized" portion of superblock */
    H5F_addr_decode(f, (const uint8_t **)&p, &shared->base_addr/*out*/);
    H5F_addr_decode(f, (const uint8_t **)&p, &shared->extension_addr/*out*/);
    H5F_addr_decode(f, (const uint8_t **)&p, &stored_eoa/*out*/);
    H5F_addr_decode(f, (const uint8_t **)&p, &shared->driver_addr/*out*/);
    if(H5G_obj_ent_decode(f, (const uint8_t **)&p, root_loc->oloc/*out*/) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "unable to read root symbol entry")

    /*
     * Check if superblock address is different from base address and
     * adjust base address and "end of address" address if so.
     */
    if(!H5F_addr_eq(shared->super_addr, shared->base_addr)) {
        /* Check if the superblock moved earlier in the file */
        if(H5F_addr_lt(shared->super_addr, shared->base_addr))
            stored_eoa -= (shared->base_addr - shared->super_addr);
        else
            /* The superblock moved later in the file */
            stored_eoa += (shared->super_addr - shared->base_addr);

        shared->base_addr = shared->super_addr;
    } /* end if */

    /* This step is for h5repart tool only. If user wants to change file driver
     *  from family to sec2 while using h5repart, set the driver address to
     *  undefined to let the library ignore the family driver information saved
     *  in the superblock.
     */
    if(shared->fam_to_sec2)
        shared->driver_addr = HADDR_UNDEF;

    /* Decode the optional driver information block */
    if(H5F_addr_defined(shared->driver_addr)) {
        char drv_name[9];       /* Name of driver */
        unsigned drv_vers;      /* Version of driver info block */
        haddr_t drv_addr = shared->base_addr + shared->driver_addr;
        size_t drv_fixed_size;  /* Amount of fixed size driver info to read */
        size_t drv_variable_size; /* Size of variable-length portion of driver info block, in bytes */

        /* Account for extra data read in for superblock version > 1 */
        /* (extra room is for the trailing checksum on the superblock + driver info block) */
        if(super_vers >= HDF5_SUPERBLOCK_VERSION_2)
            drv_fixed_size = (size_t)H5F_DRVINFOBLOCK_HDR_SIZE - H5F_SIZEOF_CHKSUM;
        else
            drv_fixed_size = (size_t)H5F_DRVINFOBLOCK_HDR_SIZE;

        /* Read in fixed-sized portion of driver info block */
        if(H5FD_set_eoa(lf, H5FD_MEM_SUPER, drv_addr + H5F_DRVINFOBLOCK_HDR_SIZE) < 0)
            HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, FAIL, "set end of space allocation request failed")
        if(H5FD_read(lf, H5FD_MEM_SUPER, dxpl_id, drv_addr, drv_fixed_size, p) < 0)
            HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "unable to read driver information block")

        /* Version number */
        drv_vers = *p++;
        if(drv_vers > HDF5_DRIVERINFO_VERSION_LATEST)
            HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "bad driver information block version number")

        p += 3; /* reserved bytes */

        /* Driver info size */
        UINT32DECODE(p, drv_variable_size);

        /* Account for extra data to read for superblock version > 1 */
        /* (extra room is for the trailing checksum on the superblock + driver info block) */
        if(super_vers >= HDF5_SUPERBLOCK_VERSION_2)
            drv_variable_size += H5F_SIZEOF_CHKSUM;

        /* Sanity check */
        HDassert(fixed_size + variable_size + drv_fixed_size + drv_variable_size <= sizeof(buf));

        /* Driver name and/or version */
        HDstrncpy(drv_name, (const char *)p, (size_t)8);
        drv_name[8] = '\0';
        p += 8; /* advance past name/version */

        /* Read in variable-sized portion of driver info block */
        if(H5FD_set_eoa(lf, H5FD_MEM_SUPER, drv_addr + H5F_DRVINFOBLOCK_HDR_SIZE + drv_variable_size) < 0)
            HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, FAIL, "set end of space allocation request failed")
        if(H5FD_read(lf, H5FD_MEM_SUPER, dxpl_id, drv_addr + H5F_DRVINFOBLOCK_HDR_SIZE, drv_variable_size, p) < 0)
            HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "unable to read file driver information")

        /* Decode driver information */
        /* Check if driver matches driver information saved. Unfortunately, we can't push this
         * function to each specific driver because we're checking if the driver is correct.
         */
        if(!HDstrncmp(drv_name, "NCSAfami", (size_t)8) && HDstrcmp(lf->cls->name, "family"))
            HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "family driver should be used")
        if(!HDstrncmp(drv_name, "NCSAmult", (size_t)8) && HDstrcmp(lf->cls->name, "multi"))
            HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "multi driver should be used")

        /* Decode the driver-specific driver info block information */
        if(H5FD_sb_decode(lf, drv_name, p) < 0)
            HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "unable to decode driver information")

        /* Advance past variable length portion of driver info block */
        p += drv_variable_size;
    } /* end if */

    /* Compute checksum for superblock versions > 1 */
    if(super_vers >= HDF5_SUPERBLOCK_VERSION_2) {
        uint32_t computed_chksum;       /* Computed checksum  */
        uint32_t read_chksum;           /* Checksum read from file  */

        /* Compute checksum on superblock + driver info block */
        computed_chksum = H5_checksum_metadata(buf, (size_t)(p - buf), 0);

        /* Decode checksum */
        UINT32DECODE(p, read_chksum);

        if(read_chksum != computed_chksum)
            HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "bad checksum on driver information block")
    } /* end if */

    /*
     * The user-defined data is the area of the file before the base
     * address.
     */
    if(H5P_set(c_plist, H5F_CRT_USER_BLOCK_NAME, &shared->base_addr) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "unable to set user block size")

    /*
     * Make sure that the data is not truncated. One case where this is
     * possible is if the first file of a family of files was opened
     * individually.
     */
    if(HADDR_UNDEF == (eof = H5FD_get_eof(lf)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "unable to determine file size")
    if(eof < stored_eoa)
        HGOTO_ERROR(H5E_FILE, H5E_TRUNCATED, FAIL, "truncated file")

    /*
     * Tell the file driver how much address space has already been
     * allocated so that it knows how to allocate additional memory.
     */
    if(H5FD_set_eoa(lf, H5FD_MEM_SUPER, stored_eoa) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "unable to set end-of-address marker for file")

    /* Read the file's superblock extension, if there is one. */
    if(H5F_addr_defined(shared->extension_addr)) {
        H5O_loc_t ext_loc;              /* "Object location" for superblock extension */

        /* Sanity check - superblock extension should only be defined for
         *      superblock version >= 2.
         */
        HDassert(super_vers >= HDF5_SUPERBLOCK_VERSION_2);

        /* Set up "fake" object location for superblock extension */
        H5O_loc_reset(&ext_loc);
        ext_loc.file = f;
        ext_loc.addr = shared->extension_addr;

        /* Open the superblock extension */
        if(H5O_open(&ext_loc) < 0)
            HGOTO_ERROR(H5E_OHDR, H5E_CANTOPENFILE, FAIL, "unable to open superblock extension")

        /* Read in the shared OH message information if there is any */
        if(H5SM_get_info(&ext_loc, c_plist, dxpl_id) < 0)
            HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "unable to read SOHM table information")

        /* Close the extension.  Bump the version number to avoid closing the
         * file (since this will be the only open object).
         */
        f->nopen_objs++;
        if(H5O_close(&ext_loc) < 0)
            HGOTO_ERROR(H5E_OHDR, H5E_CANTOPENFILE, FAIL, "unable to close superblock extension")
        f->nopen_objs--;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5F_read_superblock() */


/*-------------------------------------------------------------------------
 * Function:    H5F_init_superblock
 *
 * Purpose:     Allocates the superblock for the file and initializes
 *              information about the superblock in memory.  Does not write
 *              any superblock information to the file.
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  Quincey Koziol
 *              koziol@ncsa.uiuc.edu
 *              Sept 15, 2003
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5F_init_superblock(H5F_t *f, H5O_loc_t *ext_loc, hid_t dxpl_id)
{
    hsize_t         userblock_size = 0;         /* Size of userblock, in bytes */
    size_t          superblock_size;            /* Size of superblock, in bytes     */
    size_t          driver_size;                /* Size of driver info block (bytes)*/
    unsigned        super_vers;                 /* Superblock version              */
    haddr_t         addr;                       /* Address of superblock            */
    H5P_genplist_t *plist;                      /* Property list                    */
    herr_t          ret_value = SUCCEED;

    FUNC_ENTER_NOAPI(H5F_init_superblock, FAIL)

    /* Get the shared file creation property list */
    if(NULL == (plist = H5I_object(f->shared->fcpl_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a property list")

    /*
     * The superblock starts immediately after the user-defined
     * header, which we have already insured is a proper size. The
     * base address is set to the same thing as the superblock for
     * now.
     */
    if(H5P_get(plist, H5F_CRT_USER_BLOCK_NAME, &userblock_size) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTGET, FAIL, "unable to get user block size")
    f->shared->super_addr = userblock_size;
    f->shared->base_addr = f->shared->super_addr;
    f->shared->consist_flags = 0x03;

    /* Grab superblock version from property list */
    if(H5P_get(plist, H5F_CRT_SUPER_VERS_NAME, &super_vers) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "unable to get superblock version")

    /* Compute the size of the superblock */
    superblock_size = H5F_SUPERBLOCK_SIZE(super_vers, f);

    /* Compute the size of the driver information block */
    H5_ASSIGN_OVERFLOW(driver_size, H5FD_sb_size(f->shared->lf), hsize_t, size_t);
    if(driver_size > 0) {
        driver_size += H5F_DRVINFOBLOCK_HDR_SIZE;

        /*
         * The file driver information block begins immediately after the
         * superblock. (relative to base address in file)
         */
        f->shared->driver_addr = superblock_size;
    } /* end if */

    /*
     * Allocate space for the userblock, superblock, driver info
     * block, and shared object header message table. We do it with
     * one allocation request because the userblock and superblock
     * need to be at the beginning of the file and only the first
     * allocation request is required to return memory at format 
     * address zero.
     */
    H5_CHECK_OVERFLOW(f->shared->base_addr, haddr_t, hsize_t);
    addr = H5FD_alloc(f->shared->lf, H5FD_MEM_SUPER, dxpl_id,
                      ((hsize_t)f->shared->base_addr + superblock_size + driver_size));
    if(HADDR_UNDEF == addr)
        HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, FAIL, "unable to allocate file space for userblock and/or superblock")
    if(0 != addr)
        HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, FAIL, "file driver failed to allocate userblock and/or superblock at address zero")

    /* Create the superblock extension for "extra" superblock data, if necessary.
     * (Currently, the extension is only needed if Shared Object Header
     *  Messages are enabled)
     */
    if(f->shared->sohm_nindexes > 0) {
        /* The superblock extension isn't actually a group, but the
         * default group creation list should work fine.
         * If we don't supply a size for the object header, HDF5 will
         * allocate H5O_MIN_SIZE by default.  This is currently
         * big enough to hold the biggest possible extension, but should
         * be tuned if more information is added to the superblock
         * extension.
         */
        H5O_loc_reset(ext_loc);
        if(H5O_create(f, dxpl_id, 0, H5P_GROUP_CREATE_DEFAULT, ext_loc) < 0)
            HGOTO_ERROR(H5E_OHDR, H5E_CANTCREATE, FAIL, "unable to create superblock extension")

        /* Record this address */
        f->shared->extension_addr = ext_loc->addr;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5F_init_superblock() */


/*-------------------------------------------------------------------------
 * Function:    H5F_write_superblock
 *
 * Purpose:     Writes (and optionally allocates) the superblock for the file.
 *              If BUF is non-NULL, then write the serialized superblock
 *              information into it. It should be a buffer of size
 *              H5F_SUPERBLOCK_SIZE + H5F_DRVINFOBLOCK_SIZE
 *              or larger.
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  Bill Wendling
 *              wendling@ncsa.uiuc.edu
 *              Sept 12, 2003
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5F_write_superblock(H5F_t *f, hid_t dxpl_id)
{
    uint8_t         buf[H5F_MAX_SUPERBLOCK_SIZE + H5F_MAX_DRVINFOBLOCK_SIZE];  /* Superblock & driver info blockencoding buffer */
    uint8_t        *p = NULL;                   /* Ptr into encoding buffers        */
    uint32_t        chksum = 0;                 /* Checksum temporary variable      */
    size_t          superblock_size;            /* Size of superblock, in bytes     */
    size_t          driver_size;                /* Size of driver info block (bytes)*/
    unsigned        super_vers;                 /* Superblock version              */
    unsigned        freespace_vers;             /* Freespace info version           */
    unsigned        obj_dir_vers;               /* Object header info version       */
    unsigned        share_head_vers;            /* Shared header info version       */
    H5P_genplist_t *plist;                      /* Property list                    */
    herr_t          ret_value = SUCCEED;

    FUNC_ENTER_NOAPI(H5F_write_superblock, FAIL)

    /* Get the shared file creation property list */
    if(NULL == (plist = H5I_object(f->shared->fcpl_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a property list")

    /* Grab values from property list */
    if(H5P_get(plist, H5F_CRT_SUPER_VERS_NAME, &super_vers) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "unable to get superblock version")
    if(H5P_get(plist, H5F_CRT_FREESPACE_VERS_NAME, &freespace_vers) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "unable to get free space version")
    if(H5P_get(plist, H5F_CRT_OBJ_DIR_VERS_NAME, &obj_dir_vers) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "unable to get object directory version")
    if(H5P_get(plist, H5F_CRT_SHARE_HEAD_VERS_NAME, &share_head_vers) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "unable to get shared-header format version")

    /* Encode the file superblock */
    p = buf;
    HDmemcpy(p, H5F_SIGNATURE, (size_t)H5F_SIGNATURE_LEN);
    p += H5F_SIGNATURE_LEN;
    *p++ = (uint8_t)super_vers;
    *p++ = (uint8_t)freespace_vers;
    *p++ = (uint8_t)obj_dir_vers;
    *p++ = 0;   /* reserved*/

    *p++ = (uint8_t)share_head_vers;
    HDassert(H5F_SIZEOF_ADDR(f) <= 255);
    *p++ = (uint8_t)H5F_SIZEOF_ADDR(f);
    HDassert(H5F_SIZEOF_SIZE(f) <= 255);
    *p++ = (uint8_t)H5F_SIZEOF_SIZE(f);
    *p++ = 0;   /* reserved */

    UINT16ENCODE(p, f->shared->sym_leaf_k);
    UINT16ENCODE(p, f->shared->btree_k[H5B_SNODE_ID]);
    UINT32ENCODE(p, f->shared->consist_flags);

    /*
     * Versions of the superblock >0 have the indexed storage B-tree
     * internal 'K' value stored
     */
    if(super_vers > HDF5_SUPERBLOCK_VERSION_DEF) {
        UINT16ENCODE(p, f->shared->btree_k[H5B_ISTORE_ID]);
        if(super_vers < HDF5_SUPERBLOCK_VERSION_2) {
            *p++ = 0;   /*reserved */
            *p++ = 0;   /*reserved */
        } /* end if */
    } /* end if */

    H5F_addr_encode(f, &p, f->shared->base_addr);
    H5F_addr_encode(f, &p, f->shared->extension_addr);
    H5F_addr_encode(f, &p, H5FD_get_eoa(f->shared->lf, H5FD_MEM_SUPER));
    H5F_addr_encode(f, &p, f->shared->driver_addr);
    if(H5G_obj_ent_encode(f, &p, H5G_oloc(f->shared->root_grp)) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, FAIL, "unable to encode root group information")

    /* Compute [possibly partial] superblock checksum */
    if(super_vers >= HDF5_SUPERBLOCK_VERSION_2)
        chksum = H5_checksum_metadata(buf, (H5F_SUPERBLOCK_SIZE(super_vers, f) - H5F_SIZEOF_CHKSUM), 0);

    /* Encode the driver information block. */
    H5_ASSIGN_OVERFLOW(driver_size, H5FD_sb_size(f->shared->lf), hsize_t, size_t);
    if(driver_size > 0) {
        char driver_name[9];    /* Name of driver, for driver info block */
        uint8_t *dbuf = p;      /* Pointer to beginning of driver info */

        /* Encode the driver information block */
	*p++ = HDF5_DRIVERINFO_VERSION_0; /* Version */
        *p++ = 0; /* reserved */
        *p++ = 0; /* reserved */
        *p++ = 0; /* reserved */

	/* Driver info size, excluding header */
	UINT32ENCODE(p, driver_size);

	/* Encode driver-specific data */
	if(H5FD_sb_encode(f->shared->lf, driver_name, dbuf + H5F_DRVINFOBLOCK_HDR_SIZE) < 0)
	    HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, FAIL, "unable to encode driver information")

	/* Store driver name (set in 'H5FD_sb_encode' call above) */
	HDmemcpy(dbuf + 8, driver_name, (size_t)8);

        /* Advance buffer pointer past name & variable-sized portion of driver info */
        /* (for later use in storing the checksum) */
        p += 8 + driver_size;

        /* Update superblock checksum with driver info block checksum */
        /* (on superblock versions > 1) */
        if(super_vers >= HDF5_SUPERBLOCK_VERSION_2)
            chksum = H5_checksum_metadata(dbuf, driver_size + H5F_DRVINFOBLOCK_HDR_SIZE, chksum);
    } /* end if */

    /* Encode the checksum on the superblock (for versions > 1) */
    if(super_vers >= HDF5_SUPERBLOCK_VERSION_2)
	UINT32ENCODE(p, chksum);

    /* Retrieve the total size of the superblock info */
    H5_ASSIGN_OVERFLOW(superblock_size, (p - buf), int, size_t);

    /* Double check we didn't overrun the block (unlikely) */
    HDassert(superblock_size <= sizeof(buf));

    /* Write superblock */
    if(H5FD_write(f->shared->lf, H5FD_MEM_SUPER, dxpl_id, f->shared->super_addr, superblock_size, buf) < 0)
        HGOTO_ERROR(H5E_IO, H5E_WRITEERROR, FAIL, "unable to write superblock")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5F_write_superblock() */

