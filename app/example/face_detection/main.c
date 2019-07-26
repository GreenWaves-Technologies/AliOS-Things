#include <stdio.h>
#include <aos/kernel.h>

/****************************************************************************/
/* PMSIS includes */
#include "pmsis.h"
#include "pmsis_types.h"

#include "rtos/pmsis_os.h"
#include "rtos/pmsis_driver_core_api/pmsis_driver_core_api.h"

#include "pmsis_api/include/drivers/hyperbus.h"
#include "pmsis_cluster/drivers/delegate/hyperbus/hyperbus_cl_internal.h"

#include "rtos/os_frontend_api/pmsis_task.h"
#include "pmsis_cluster/cluster_sync/fc_to_cl_delegate.h"
#include "pmsis_cluster/cluster_team/cl_team.h"

/* PMSIS BSP includes */
#include "bsp/display/ili9341.h"
#include "bsp/camera/mt9v034.h"
#include "stdio.h"

#include "ImageDraw.h"
#include "setup.h"
#include "FaceDetKernels.h"

#include "faceDet.h"

#define CAM_WIDTH    320
#define CAM_HEIGHT   240

#define LCD_WIDTH    320
#define LCD_HEIGHT   240

static unsigned char *imgBuff0;
static struct pi_device ili;
static pi_buffer_t buffer;
static struct pi_device device;
#ifdef USE_BRIDGE
static uint64_t fb;
#endif

RT_L2_DATA unsigned char *ImageOut;
RT_L2_DATA unsigned int *ImageIntegral;
RT_L2_DATA unsigned int *SquaredImageIntegral;
RT_L2_DATA char str_to_lcd[100];



struct pi_device cluster_dev;
struct pi_cluster_task *task;
struct cluster_driver_conf conf;
ArgCluster_T ClusterCall;

static void lcd_handler(void *arg);
static void cam_handler(void *arg);


static void cam_handler(void *arg)
{
  camera_control(&device, CAMERA_CMD_STOP, 0);

#ifdef USE_BRIDGE
  rt_bridge_fb_update(fb, (unsigned int)imgBuff0, 0, 0, CAM_WIDTH, CAM_HEIGHT, NULL);
  camera_capture_async(&device, imgBuff0, CAM_WIDTH*CAM_HEIGHT, pi_task_callback(&task, cam_handler, NULL));
  camera_control(&device, CAMERA_CMD_START, 0);

#else
  display_write_async(&ili, &buffer, 0, 0, LCD_WIDTH, LCD_HEIGHT, pi_task_callback(&task, lcd_handler, NULL));
#endif
}


static void lcd_handler(void *arg)
{
  camera_control(&device, CAMERA_CMD_START, 0);
  camera_capture_async(&device, imgBuff0, CAM_WIDTH*CAM_HEIGHT, pi_task_callback(&task, cam_handler, NULL));

}


#ifdef USE_BRIDGE
static int open_bridge()
{
  rt_bridge_connect(1, NULL);

  fb = rt_bridge_fb_open("Camera", CAM_WIDTH, CAM_HEIGHT, RT_FB_FORMAT_GRAY, NULL);
  if (fb == 0) return -1;

  return 0;
}
#endif


static int open_display(struct pi_device *device)
{
#ifndef USE_BRIDGE
  struct ili9341_conf ili_conf;

  ili9341_conf_init(&ili_conf);

  pi_open_from_conf(device, &ili_conf);

  if (display_open(device))
    return -1;

#else
  if (open_bridge())
  {
    printf("Failed to open bridge\n");
    return -1;
  }
#endif

  return 0;
}

static int open_camera_mt9v034(struct pi_device *device)
{
  struct mt9v034_conf cam_conf;

  mt9v034_conf_init(&cam_conf);
  cam_conf.format = CAMERA_QVGA;

  pi_open_from_conf(device, &cam_conf);
  if (camera_open(device))
    return -1;

  return 0;
}

static int open_camera(struct pi_device *device)
{
  return open_camera_mt9v034(device);
  return -1;
}

int main()
{
  //printf("Entering main controller...\n");

  unsigned int W = CAM_WIDTH, H = CAM_HEIGHT;
  unsigned int Wout = 64, Hout = 48;
  unsigned int ImgSize = W*H;

  imgBuff0 = (unsigned char *)pmsis_l2_malloc((CAM_WIDTH*CAM_HEIGHT)*sizeof(unsigned char));
  if (imgBuff0 == NULL) {
      printf("Failed to allocate Memory for Image \n");
      return 1;
  }

  //This can be moved in init
  ImageOut             = (unsigned char *) pmsis_l2_malloc((Wout*Hout)*sizeof(unsigned char));
  ImageIntegral        = (unsigned int *)  pmsis_l2_malloc((Wout*Hout)*sizeof(unsigned int));
  SquaredImageIntegral = (unsigned int *)  pmsis_l2_malloc((Wout*Hout)*sizeof(unsigned int));

  if (ImageOut==0) {
    printf("Failed to allocate Memory for Image (%d bytes)\n", ImgSize*sizeof(unsigned char));
    return 1;
  }
  if (ImageIntegral==0 || SquaredImageIntegral==0) {
    printf("Failed to allocate Memory for one or both Integral Images (%d bytes)\n", ImgSize*sizeof(unsigned int));
    return 1;
  }

  if (open_display(&ili))
  {
    printf("Failed to open display\n");
    return -1;
  }

  if (open_camera(&device))
  {
    printf("Failed to open camera\n");
    return -1;
  }

  buffer.data = imgBuff0;
  pi_buffer_init(&buffer, PI_BUFFER_TYPE_L2, imgBuff0);
  pi_buffer_set_format(&buffer, CAM_WIDTH, CAM_HEIGHT, 1, PI_BUFFER_FORMAT_GRAY);

  ClusterCall.ImageIn              = imgBuff0;
  ClusterCall.Win                  = W;
  ClusterCall.Hin                  = H;
  ClusterCall.Wout                 = Wout;
  ClusterCall.Hout                 = Hout;
  ClusterCall.ImageOut             = ImageOut;
  ClusterCall.ImageIntegral        = ImageIntegral;
  ClusterCall.SquaredImageIntegral = SquaredImageIntegral;

  conf.id = 0;
  conf.device_type = 0;


  //Why il faut faire le deux
  pi_open_from_conf(&cluster_dev, (void*)&conf);
  pi_cluster_open(&cluster_dev);


  task = pmsis_l2_malloc(sizeof(struct pi_cluster_task));
  memset(task, 0, sizeof(struct pi_cluster_task));
  task->entry = faceDet_cluster_init;
  task->arg = &ClusterCall;


  pi_cluster_send_task_to_cl(&cluster_dev, task);

  task->entry = faceDet_cluster_main;
  task->arg = &ClusterCall;


  //Setting Screen background to white
  writeFillRect(&ili, 0,0,320,240,0xFFFF);
  setCursor(0,0);
  writeText(&ili,"        Greenwaves \n       Technologies",2);
  while(1)
  {
    camera_control(&device, CAMERA_CMD_START, 0);
    camera_capture(&device, imgBuff0, CAM_WIDTH*CAM_HEIGHT);
    camera_control(&device, CAMERA_CMD_STOP, 0);

    pi_cluster_send_task_to_cl(&cluster_dev, task);


  sprintf(str_to_lcd,"1 Image/Sec: \n%d uWatt @ 1.2V   \n%d uWatt @ 1.0V   %c", (int)((float)(1/(50000000.f/ClusterCall.cycles)) * 28000.f),(int)((float)(1/(50000000.f/ClusterCall.cycles)) * 16800.f),'\0');
  //sprintf(out_perf_string,"%d  \n%d  %c", (int)((float)(1/(50000000.f/cycles)) * 28000.f),(int)((float)(1/(50000000.f/cycles)) * 16800.f),'\0');

  setCursor(0,190);
  writeText(&ili,str_to_lcd,2);
  buffer.data = ImageOut;
  display_write(&ili, &buffer, 80, 40, 160, 120);
  }

  return 0;
}
