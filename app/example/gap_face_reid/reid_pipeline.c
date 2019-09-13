#include "reid_pipeline.h"
#include <stdlib.h>
#include <stdio.h>

#include "setup.h"
#include "cascade.h"
#include "ExtraKernels.h"
#include "network_process_manual.h"

static void copy_roi_with_padding(unsigned char* in, int Win, int Hin, int WinStride, short* out, int Wout, int Hout)
{
    // PRINTF("copy_roi_with_padding(Win=%d, Hin=%d, WinStride=%d, Wout=%d, Hout=%d)\n", Win, Hin, WinStride, Wout, Hout);
    int top_padding = (Hout - Hin) / 2;

    for (int i = 0; i < top_padding*Wout; i++)
    {
        out[i] = 0;
    }

    for(int i = 0; i < Hin; i++)
    {
        int left_padding = (Wout - Win) / 2;
        for(int j = 0; j < left_padding; j++)
        {
            out[(i+top_padding)*Wout + j] = 0;
        }
        for(int j = 0; j < Win; j++)
        {
            out[(i+top_padding)*Wout + j + left_padding] = in[i*WinStride + j];
        }
        for(int j = left_padding+Win; j < Wout; j++)
        {
            out[(i+top_padding)*Wout + j] = 0;
        }
    }

    for (int i = (top_padding+Hin)*Wout; i < Wout*Hout; i++)
    {
        out[i] = 0;
    }
}

static void copy_roi(unsigned char* in, int WinStride, unsigned char* out, int Wout, int Hout)
{
    for(int i = 0; i < Hout; i++)
    {
        for(int j = 0; j < Wout; j++)
        {
            out[i*Wout + j] = in[i*WinStride + j];
        }
    }
}

void reid_prepare_cluster(ArgClusterDnn_T* ArgC)
{
    PRINTF("reid_cluster_main call\n");
    char* name;
    int face_stride = CAMERA_WIDTH;

    unsigned char* face_ptr = ArgC->frame + ArgC->roi->y*CAMERA_WIDTH + ArgC->roi->x;
    if(ArgC->roi->layer_idx == 0)
    {
        PRINTF("Copy face with small padding\n");
        copy_roi_with_padding(face_ptr, ArgC->roi->w, ArgC->roi->h, face_stride, ArgC->scaled_face, 128, 128);
    }
    else if(ArgC->roi->layer_idx == 1)
    {
        PRINTF("Cut face ROI from scale 1\n");
        copy_roi(face_ptr, CAMERA_WIDTH, ArgC->face, 152, 152);
         PRINTF("CopyROI done\n");
        ResizeImageForDnn_Scale1(ArgC->face, ArgC->scaled_face);
         PRINTF("Resize done\n");
    }
    else if(ArgC->roi->layer_idx == 2)
    {
        PRINTF("Cut face ROI from scale 2\n");
        copy_roi(face_ptr, CAMERA_WIDTH, ArgC->face, 194, 194);
         PRINTF("CopyROI done\n");
        ResizeImageForDnn_Scale2(ArgC->face, ArgC->scaled_face);
         PRINTF("Resize done\n");
    }
    else
    {
        PRINTF("Respose structure corrupted, cascade scale %d does not exists\n", ArgC->roi->layer_idx);
        return;
    }
}

void reid_inference_cluster(ArgClusterDnn_T* ArgC)
{
    //PRINTF("Start DNN inference\n");
    unsigned int Ti;
    // gap8_resethwtimer();
    // Ti = gap8_readhwtimer();
    ArgC->output = network_process(&ArgC->activation_size);
    // Ti = gap8_readhwtimer() - Ti;
    // PRINTF("Network Cycles: %d\n",Ti);
    //PRINTF("DNN inference finished\n");
}
