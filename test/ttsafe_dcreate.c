/********************************************************************
 *
 * Testing thread safety in dataset creation in the HDF5 library
 * -------------------------------------------------------------
 *
 * Set of tests to run multiple threads so that each creates a different
 * dataset. This is likely to cause race-conditions if run in a non
 * threadsafe environment.
 *
 * Temporary files generated:
 *   ttsafe_dcreate.h5
 *
 * HDF5 APIs exercised in thread:
 * H5Screate_simple, H5Tcopy, H5Tset_order, H5Dcreate, H5Dwrite, H5Dclose,
 * H5Tclose, H5Sclose.
 *
 * Created: Apr 28 2000
 * Programmer: Chee Wai LEE
 *
 * Modification History
 * --------------------
 *
 *	19 May 2000, Bill Wendling
 *	Changed so that it creates its own HDF5 file and removes it at cleanup
 *	time. Added num_errs flag.
 *
 ********************************************************************/
#include "ttsafe.h"

#ifndef H5_HAVE_THREADSAFE
static int dummy;	/* just to create a non-empty object file */
#else

#define FILENAME		"ttsafe_dcreate.h5"
#define DATASETNAME_LENGTH	10
#define NUM_THREAD		16

void *tts_dcreate_creator(void *);

typedef struct thread_info {
	int id;
	hid_t file;
	const char *dsetname;
} thread_info; 

/*
 * Set individual dataset names (rather than generated the names
 * automatically)
 */
const char *dsetname[NUM_THREAD]={
    "zero",
    "one",
    "two",
    "three",
    "four",
    "five",
    "six",
    "seven",
    "eight",
    "nine",
    "ten",
    "eleven",
    "twelve",
    "thirteen",
    "fourteen",
    "fifteen"
};

thread_info thread_out[NUM_THREAD];

/*
 **********************************************************************
 * Thread safe test - multiple dataset creation
 **********************************************************************
 */
void tts_dcreate(void)
{
    /* Pthread definitions */
    pthread_t threads[NUM_THREAD];

    /* HDF5 data definitions */
    hid_t file, dataset;
    int datavalue, i;
    pthread_attr_t attribute;
    int ret;

    /* set pthread attribute to perform global scheduling */
    ret=pthread_attr_init(&attribute);
    assert(ret==0);
    ret=pthread_attr_setscope(&attribute, PTHREAD_SCOPE_SYSTEM);
/* Don't check return value on FreeBSD, since PTHREAD_SCOPE_SYSTEM is not
 * currently supported in v4.7
 */
#ifndef __FreeBSD__
    assert(ret==0);
#endif /* __FreeBSD__ */

    /*
     * Create a hdf5 file using H5F_ACC_TRUNC access, default file
     * creation plist and default file access plist
     */
    file = H5Fcreate(FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    assert(file>=0);

    /* simultaneously create a large number of datasets within the file */
    for (i = 0; i < NUM_THREAD; i++) {
        thread_out[i].id = i;
        thread_out[i].file = file;
        thread_out[i].dsetname = dsetname[i];
        ret=pthread_create(&threads[i], NULL, tts_dcreate_creator, &thread_out[i]);
        assert(ret==0);
    }

    for (i = 0;i < NUM_THREAD; i++) {
        ret=pthread_join(threads[i], NULL);
        assert(ret==0);
    } /* end for */

    /* compare data to see if it is written correctly */

    for (i = 0; i < NUM_THREAD; i++) {
        if ((dataset = H5Dopen(file,dsetname[i])) < 0) {
            fprintf(stderr, "Dataset name not found - test failed\n");
            H5Fclose(file);
            num_errs++;
            return;
        } else {
            ret=H5Dread(dataset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, &datavalue);
            assert(ret>=0);

            if (datavalue != i) {
                fprintf(stderr, "Wrong value read %d for dataset name %s - test failed\n",
                            datavalue, dsetname[i]);
                H5Dclose(dataset);
                H5Fclose(file);
                num_errs++;
                return;
            }

            ret=H5Dclose(dataset);
            assert(ret>=0);
        }
    }

    /* close remaining resources */
    ret=H5Fclose(file);
    assert(ret>=0);

    /* Destroy the thread attribute */
    ret=pthread_attr_destroy(&attribute);
    assert(ret==0);
}

void *tts_dcreate_creator(void *_thread_data)
{
	hid_t   dataspace, dataset;
        herr_t  ret;
	hsize_t dimsf[1]; /* dataset dimensions */
	struct thread_info thread_data;

	memcpy(&thread_data,_thread_data,sizeof(struct thread_info));

	/* define dataspace for dataset */
	dimsf[0] = 1;
	dataspace = H5Screate_simple(1,dimsf,NULL);
        assert(dataspace>=0);

	/* create a new dataset within the file */
	dataset = H5Dcreate(thread_data.file, thread_data.dsetname,
			    H5T_NATIVE_INT, dataspace, H5P_DEFAULT);
        assert(dataset>=0);

	/* initialize data for dataset and write value to dataset */
	ret=H5Dwrite(dataset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL,
		 H5P_DEFAULT, &thread_data.id);
        assert(ret>=0);

	/* close dataset and dataspace resources */
	ret=H5Dclose(dataset);
        assert(ret>=0);
	ret=H5Sclose(dataspace);
        assert(ret>=0);

	return NULL;
}

void cleanup_dcreate(void)
{
	HDunlink(FILENAME);
}
#endif /*H5_HAVE_THREADSAFE*/
