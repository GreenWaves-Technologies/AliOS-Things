/*
 * Copyright (C) 2019 GreenWaves Technologies
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __AT__AT_API_PMSIS_H__
#define __AT__AT_API_PMSIS_H__

#include "pmsis.h"
#include "bsp/ram/hyperram.h"
#include "bsp/flash/hyperflash.h"
#include "bsp/fs.h"



/*
 * Utils
 */

#define AT_COREID()        __builtin_pulp_CoreId()
#define AT_CLUSTERID()     __builtin_pulp_ClusterId()
#define AT_NCORE()         __builtin_pulp_CoreCount()



/*
 * Allocators
 */

#define AT_HYPERRAM_ALLOC(dev,size) ({ uint32_t ptr; int err = ram_alloc((dev), &ptr, (size)); if (!err && ptr == 0) err = ram_alloc((dev), &ptr, (size)); if (err) ptr = 0; ptr; })

#define AT_HYPERRAM_FREE(dev,ptr,size) ram_free((dev), (ptr), (size))


#define AT_L2_ALLOC(dev,size) pmsis_l2_malloc(size)

#define AT_L2_FREE(dev,ptr,size) pmsis_l2_malloc_free((ptr), (size))


#define AT_L1_ALLOC(dev,size) pmsis_l1_malloc(size)

#define AT_L1_FREE(dev,ptr,size) pmsis_l1_malloc_free((ptr), (size))



/*
 * Hyperram
 */

#define AT_HYPERRAM_TYPE 0

typedef struct hyperram_conf AT_HYPERRAM_CONF_T;
typedef struct pi_device     AT_HYPERRAM_T;
typedef uint32_t             AT_HYPERRAM_EXT_ADDR_TYPE;
typedef void *               AT_HYPERRAM_LOC_ADDR_TYPE;
typedef cl_ram_req_t         AT_HYPERRAM_EVENT;
typedef char *AT_HYPERRAM_POINTER;
typedef char *AT_HYPERRAM_INT_ADDR_TYPE;

#define AT_HYPERRAM_EXT2LOC 0
#define AT_HYPERRAM_LOC2EXT 1

#define AT_HYPERRAM_CONF_INIT(dev,type,name) \
  hyperram_conf_init(dev)

#define AT_HYPERRAM_OPEN(dev,conf,err) \
  do { pi_open_from_conf((dev), (conf)); *(err) = ram_open(dev); } while(0)

#define AT_HYPERRAM_CLOSE(dev) \
  ram_close(dev)

#define AT_HYPERRAM_COPY(dev,ext,loc,size,dir,event) \
  cl_ram_copy(dev, (AT_HYPERRAM_EXT_ADDR_TYPE)(ext), (AT_HYPERRAM_LOC_ADDR_TYPE)(loc), (size), !(dir), (event))

#define AT_HYPERRAM_COPY2D(dev,ext,loc,size,stride,len,dir,event) \
  cl_ram_copy_2d(dev, (AT_HYPERRAM_EXT_ADDR_TYPE)(ext), (AT_HYPERRAM_LOC_ADDR_TYPE)(loc), (size), (stride), (len), !(dir), (event))

#define AT_HYPERRAM_WAIT(dev,event) \
  cl_ram_copy_wait(event)


/*
 * Hyperflash
 */

#define AT_HYPERFLASH_TYPE 1

typedef struct hyperflash_conf AT_HYPERFLASH_CONF_T;
typedef struct pi_device       AT_HYPERFLASH_T;
typedef uint32_t               AT_HYPERFLASH_EXT_ADDR_TYPE;
typedef void *                 AT_HYPERFLASH_LOC_ADDR_TYPE;
typedef cl_ram_req_t           AT_HYPERFLASH_EVENT;

#define AT_HYPERFLASH_EXT2LOC 0
#define AT_HYPERFLASH_LOC2EXT 1

#define AT_HYPERFLASH_CONF_INIT(dev,type,name) \
  hyperflash_conf_init(dev)

#define AT_HYPERFLASH_OPEN(dev,conf,err) \
  do { pi_open_from_conf((dev), (conf)); *(err) = flash_open(dev); } while(0)

#define AT_HYPERFLASH_CLOSE(dev) \
  flash_close(dev)

// TODO not yet supported
#define AT_HYPERFLASH_COPY(dev,ext,loc,size,dir,event)

// TODO not yet supported
#define AT_HYPERFLASH_COPY2D(dev,ext,loc,size,stride,len,dir,event)

// TODO not yet supported
#define AT_HYPERFLASH_WAIT(dev,event)



/*
 * Hyperflash FS
 */

#define AT_HYPERFLASH_FS_TYPE 1

typedef struct fs_conf AT_HYPERFLASH_FS_CONF_T;

typedef struct
{
  struct pi_device fs;
  struct pi_device hyperflash;
  fs_file_t *file;
} AT_HYPERFLASH_FS_T;

typedef unsigned int AT_HYPERFLASH_FS_EXT_ADDR_TYPE;
typedef void *AT_HYPERFLASH_FS_INT_ADDR_TYPE;
typedef pi_task_t AT_HYPERFLASH_FS_EVENT;
typedef cl_fs_req_t AT_HYPERFLASH_FS_CL_EVENT;

static inline void __at_hyperflash_fs_open(AT_HYPERFLASH_FS_T *file, struct fs_conf *conf, const char *filename, int *err)
{
  struct hyperflash_conf hyperflash_conf;
  hyperflash_conf_init(&hyperflash_conf);
  pi_open_from_conf(&file->hyperflash, &hyperflash_conf);
  if (flash_open(&file->hyperflash))
  {
    *err = -1;
    return;
  }
  conf->flash = &file->hyperflash;
  pi_open_from_conf(&file->fs, conf);
  if (fs_mount(&file->fs))
  {
    flash_close(&file->hyperflash);
    *err = -1;
    return;
  }
  file->file = fs_open(&file->fs, filename, 0);
  if (file->file == NULL)
  {
    fs_unmount(&file->fs);
    flash_close(&file->hyperflash);
    *err = -1;
    return;
  }
  *err = 0;
}

static inline void __at_hyperflash_fs_close(AT_HYPERFLASH_FS_T *file)
{
  fs_close(file->file);
  fs_unmount(&file->fs);
  flash_close(&file->hyperflash);
}

#define AT_HYPERFLASH_FS_EXT2LOC 0
#define AT_HYPERFLASH_FS_LOC2EXT 1

#define AT_HYPERFLASH_FS_CONF_INIT(dev,type,name) \
  fs_conf_init(dev)

#define AT_HYPERFLASH_FS_OPEN(file,conf,filename,err) \
  __at_hyperflash_fs_open(file, conf, filename, err)

#define AT_HYPERFLASH_FS_CLOSE(file) \
  __at_hyperflash_fs_close(file)

#define AT_HYPERFLASH_FS_COPY(fs,ext,loc,size,dir,event) \
  fs_copy_async((fs)->file, ext, loc, size, dir, event)

#define AT_HYPERFLASH_FS_COPY2D(file, dev,ext,loc,size,stride,len,dir,event) \
  fs_copy_2d_async(file->file, ext, loc, size, stride, len, dir, event)

#define AT_HYPERFLASH_FS_WAIT(file,event) \
  pi_task_wait_on(event)

#define AT_HYPERFLASH_FS_CL_COPY(fs,ext,loc,size,dir,event) \
  cl_fs_copy((fs)->file, ext, loc, size, dir, event)

#define AT_HYPERFLASH_FS_CL_COPY2D(file, dev,ext,loc,size,stride,len,dir,event) \
  cl_fs_copy_2d(file->file, ext, loc, size, stride, len, dir, event)

#define AT_HYPERFLASH_FS_CL_WAIT(file,event) \
  cl_fs_wait(event)




/*
 * DMA
 */

typedef cl_dma_cmd_t AT_L2_EVENT;
typedef uint32_t AT_L2_EXT_ADDR_TYPE;
typedef uint32_t AT_L2_LOC_ADDR_TYPE;

typedef char *AT_L2_POINTER;
typedef char *AT_L1_POINTER;
typedef char *AT_L2_INT_ADDR_TYPE;

#define AT_L2_EXT2LOC 0
#define AT_L2_LOC2EXT 1

#define AT_L2_COPY(dev,ext,loc,size,dir,event) cl_dma_cmd((AT_L2_EXT_ADDR_TYPE)(ext), (AT_L2_LOC_ADDR_TYPE)(loc), (size), !(dir), (event));

#define AT_L2_COPY2D(dev,ext,loc,size,stride,length,dir,event) cl_dma_cmd_2d((AT_L2_EXT_ADDR_TYPE)(ext), (AT_L2_LOC_ADDR_TYPE)(loc), (size), (stride), (length), !(dir), (event));

#define AT_L2_WAIT(dev,event) cl_dma_cmd_wait(event);



/*
 * Team
 */

typedef void (*AT_FORK_FUN_TYPE)(void *);
typedef void *AT_FORK_ARG_TYPE;

#define AT_FORK(nb_cores,entry,arg) cl_team_fork((nb_cores),(AT_FORK_FUN_TYPE)(entry),(AT_FORK_ARG_TYPE)(arg))
#define AT_FORK_WAIT()              cl_team_barrier()

#endif
