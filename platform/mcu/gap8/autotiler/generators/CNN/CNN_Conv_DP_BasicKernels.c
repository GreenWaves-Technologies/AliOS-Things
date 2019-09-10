#include "Gap.h"
#include "CNN_BasicKernels.h"

#ifdef __pulp__
#define Min(a, b)	__builtin_pulp_minsi((a), (b))
#define Max(a, b)	__builtin_pulp_maxsi((a), (b))
#else
#define Min(a, b)	(((a)<(b))?(a):(b))
#define Max(a, b)	(((a)>(b))?(a):(b))
#endif

#define VOL volatile

static int CoreCountDynamic = 1;
static int ActiveCore = gap_ncore();

static inline unsigned int __attribute__((always_inline)) ChunkSize(unsigned int X)

{
        unsigned int NCore;
        unsigned int Log2Core;
        unsigned int Chunk;

        if (CoreCountDynamic) NCore = ActiveCore; else NCore = gap_ncore();
        Log2Core = gap_fl1(NCore);
        Chunk = (X>>Log2Core) + ((X&(NCore-1))!=0);
        return Chunk;
}

static int FirstDefinedOutput(unsigned int F, unsigned int Pad, unsigned int Stride)

{
        // k*S - (F-1)/2 >=0 => k >= (((F-1)/2) + S-1)/S

        return ((Pad+Stride-1)/Stride);
}

static int LastDefinedOutput(unsigned int DimIn, unsigned int F, unsigned int PadL, unsigned int Stride)

{
        // k*S + ((F-1)/2 - PadL + F/2) < Dim  => k < (Dim-((F-1)/2 - PadL + (F/2)) + S-1)/S

        return ((DimIn - ((F-1)/2 - PadL + (F/2)) + Stride-1)/Stride);
}

static int __attribute__ ((always_inline)) MinCond(int a, int b)

{
#ifdef DIM_ALWAYS_GREATER_THAN_FILTER
	return a;
#else
	return Max(0, Min(a, b));
#endif
}

/* Padded Convolution Border processing

	Zero padding support. Implementation is based on partial convolutions derived from the original filter

	Input feature maps, Output feature maps and filter on bytes

		KerConv3x1BorderStrideNx1_DP_fps
			|------ KerConv2x1from3x1StrideNx1_V_DP_fps 3x1 convolution, stride Nx1, Left and right 0 padded stripes processing.

		KerConv1x3BorderStride1xN_DP_fps
			|------ KerConv1x2from1x3Stride1xN_H_DP_fps 1x3 convolution, stride 1xN, Left and right 0 padded stripes processing.

		KerConv3x3BorderStride1_DP_fps
			|------	KerConv2x3from3x3Stride1_V_DP_fps	3x3 convolution, stride 1, Left and right 0 padded stripes processing. 
			|------	KerConv3x2from3x3Stride1_H_DP_fps	3x3 convolution, stride 1, Top and bottom 0 padded stripes processing.

		KerConv3x3BorderStride2_DP_fps
			|------	KerConv2x3from3x3Stride2_V_DP_fps	3x3 convolution, stride 2, Left and right 0 padded stripes processing.
			|------	KerConv3x2from3x3Stride2_H_DP_fps	3x3 convolution, stride 2, Top and bottom 0 padded stripes processing.

		KerConv3x3BorderStrideS_DP_fps
			|------	KerConv2x3from3x3StrideS_V_DP_fps	3x3 convolution, stride S, Left and right 0 padded stripes processing.
			|------	KerConv3x2from3x3StrideS_H_DP_fps	3x3 convolution, stride S, Top and bottom 0 padded stripes processing.

		KerConv5x1BorderStrideNx1_DP_fps
			|------ KerConv4x1from5x1StrideNx1_V_DP_fps 5x1 convolution, stride Nx1, Left and right 0 padded stripes processing.

		KerConv1x5BorderStride1xN_DP_fps
			|------ KerConv1x4from1x5Stride1xN_H_DP_fps 1x5 convolution, stride 1xN, Left and right 0 padded stripes processing.

		KerConv5x5BorderStride1_DP_fps
			|------	KerConv4x5from5x5Stride1_V_DP_fps	5x5 convolution, stride 1, Left and right 0 padded stripes processing.
			|------	KerConv5x4from5x5Stride1_H_DP_fps	5x5 convolution, stride 1, Top and bottom 0 padded stripes processing.

		KerConv5x5BorderStride2_DP_fps
			|------	KerConv4x5from5x5Stride2_V_DP_fps	5x5 convolution, stride 2, Left and right 0 padded stripes processing.
			|------	KerConv5x4from5x5Stride2_H_DP_fps	5x5 convolution, stride 2, Top and bottom 0 padded stripes processing.

		KerConv5x5BorderStrideS_DP_fps
			|------	KerConv4x5from5x5StrideS_V_DP_fps	5x5 convolution, stride S, Left and right 0 padded stripes processing.
			|------	KerConv5x4from5x5StrideS_H_DP_fps	5x5 convolution, stride S, Top and bottom 0 padded stripes processing.

		KerConvNxNStrideS_Border_fp		NxN convolution, stride S, Left, Right, Top and Bottom borders

		KerConvNxMStrideSxSy_Border_fp		NxM convolution, stride Sx,Sy, Left, Right, Top and Bottom borders

		KerConvNxMDxDyStrideSxSy_Border_fp	NxM convolution, dilation Dx,Dy, stride Sx,Sy, Left, Right, Top and Bottom borders




	Input feature maps, Output feature maps and filter on half words

		KerConv3x1BorderStrideNx1_fp
			|------ KerConv2x1from3x1StrideNx1_V_fp 3x1 convolution, stride Nx1, Left and right 0 padded stripes processing.

		KerConv1x3BorderStride1xN_fp
			|------ KerConv1x2from1x3Stride1xN_H_fp 1x3 convolution, stride 1xN, Left and right 0 padded stripes processing.

		KerConv3x3BorderStride1_fp
			|------	KerConv2x3from3x3Stride1_V_fp	3x3 convolution, stride 1, Left and right 0 padded stripes processing. 
			|------	KerConv3x2from3x3Stride1_H_fp	3x3 convolution, stride 1, Top and bottom 0 padded stripes processing.

		KerConv3x3BorderStride2_fp
			|------	KerConv2x3from3x3Stride2_V_fp	3x3 convolution, stride 2, Left and right 0 padded stripes processing.
			|------	KerConv3x2from3x3Stride2_H_fp	3x3 convolution, stride 2, Top and bottom 0 padded stripes processing.

		KerConv3x3BorderStrideS_fp
			|------	KerConv2x3from3x3StrideS_V_fp	3x3 convolution, stride S, Left and right 0 padded stripes processing.
			|------	KerConv3x2from3x3StrideS_H_fp	3x3 convolution, stride S, Top and bottom 0 padded stripes processing.

		KerConv5x1BorderStrideNx1_fp
			|------ KerConv4x1from5x1StrideNx1_V_fp 5x1 convolution, stride Nx1, Left and right 0 padded stripes processing.

		KerConv1x5BorderStride1xN_fp
			|------ KerConv1x4from1x5Stride1xN_H_fp 1x5 convolution, stride 1xN, Left and right 0 padded stripes processing.

		KerConv5x5BorderStride1_fp
			|------	KerConv4x5from5x5Stride1_V_fp	5x5 convolution, stride 1, Left and right 0 padded stripes processing.
			|------	KerConv5x4from5x5Stride1_H_fp	5x5 convolution, stride 1, Top and bottom 0 padded stripes processing.

		KerConv5x5BorderStride2_fp
			|------	KerConv4x5from5x5Stride2_V_fp	5x5 convolution, stride 2, Left and right 0 padded stripes processing.
			|------	KerConv5x4from5x5Stride2_H_fp	5x5 convolution, stride 2, Top and bottom 0 padded stripes processing.

		KerConv5x5BorderStrideS_fp
			|------	KerConv4x5from5x5StrideS_V_fp	5x5 convolution, stride S, Left and right 0 padded stripes processing.
			|------	KerConv5x4from5x5StrideS_H_fp	5x5 convolution, stride S, Top and bottom 0 padded stripes processing.

		KerConvNxNStrideS_Border_DP_fps		NxN convolution, stride S, Left, Right, Top and Bottom borders

		KerConvNxMStrideSxSy_Border_DP_fps	NxM convolution, stride Sx,Sy, Left, Right, Top and Bottom borders

		KerConvNxMDxDyStrideSxSy_Border_DP_fps	NxM convolution, dilation Dx,Dy, stride Sx,Sy, Left, Right, Top and Bottom borders
*/

static void __attribute__ ((noinline)) KerConv2x1from3x1StrideNx1_V_DP_fps(
        signed char * __restrict__ In,
        int W, int PadTOrg,
        int Wo, int Ho, int Ho_F, int Ho_L,
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
	int FilterConf
	)
{
        int V0,V1;
        int C0,C1;
        signed char *PtIn;
	short int *PtOut;

        if (FilterConf) { /* Right Side */
                C0 = Filter[0]; C1 = Filter[1];
        } else { /* Left Side */
                C0 = Filter[1]; C1 = Filter[2];
        }
        PtIn = In + (Ho_F*1-PadTOrg)*W; PtOut = Out+Ho_F*Wo;
        for (unsigned int i=Ho_F; i<Ho_L; i++) {
                int Acc = *PtOut;
                V0 = *PtIn; V1 = *(PtIn+1); PtIn += W;
                Acc += V0*C0; Acc += V1*C1;
                *PtOut =  Acc; PtOut+=Wo;
        }
}

static void __attribute__ ((noinline)) KerConv1x2from1x3Stride1xN_H_DP_fps(
        signed char * __restrict__ In,
        int W, int PadL,
        int Wo, int Wo_F, int Wo_L,
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
	int FilterConf
	)
{
        int V0,V1;
        int C0,C1;
        signed char *PtIn = In+Wo_F*1-PadL;
        short int *PtOut = Out;

        if (FilterConf) { /* Bottom Side */
                C0 = Filter[0]; C1 = Filter[1];
        } else { /* Top Side */
                C0 = Filter[1]; C1 = Filter[2];
        }
        for (unsigned int i=Wo_F; i<Wo_L; i++) {
                int Acc = *PtOut;
                V0 = *(PtIn+0*W+0); V1 = *(PtIn+1*W+0); PtIn++;
                Acc += V0*C0; Acc += V1*C1;
                *PtOut = Acc; PtOut++;
        }
}

static void __attribute__ ((noinline)) KerConv2x3from3x3Stride1_V_DP_fps(
        signed char * __restrict__ In,
        int W, int PadTOrg,
        int Wo, int Ho, int Ho_F, int Ho_L,
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
	int FilterConf
	)
{
	v4s V0, V1, V2;
	v4s C0, C1, C2;
	signed char *PtIn;
	short int *PtOut;
	int Bottom = Ho-Ho_L;

	if (FilterConf) {
		C0 = (v4s) (int) *((unsigned short *) (Filter + 0*3+0)); C1 = (v4s) (int) *((unsigned short *) (Filter + 1*3+0)); C2 = (v4s) (int) *((unsigned short *) (Filter + 2*3+0));
	} else {
		C0 = (v4s) (int) *((unsigned short *) (Filter + 0*3+1)); C1 = (v4s) (int) *((unsigned short *) (Filter + 1*3+1)); C2 = (v4s) (int) *((unsigned short *) (Filter + 2*3+1));
	}
	if (Ho_F==1) {
		PtIn = In; PtOut = Out; Ho_F = 0;
		V0 = (v4s){0,0,0,0};
		V1 = *((v4s *) PtIn); PtIn += W;
	} else  { // == 0
		PtIn = In + (Ho_F*1-PadTOrg)*W; PtOut = Out+Ho_F*Wo;
		V0 = *((v4s *) PtIn); PtIn += W;
		V1 = *((v4s *) PtIn); PtIn += W;
	}
	for (unsigned int i=Ho_F; i<Ho_L; i++) {
		int Acc = *PtOut;
		V2 = *((v4s *) PtIn); PtIn += W;
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc);
		*PtOut =  Acc; PtOut+=Wo;
		V0 = V1; V1 = V2;
	}
	if (Bottom) {
		int Acc = *PtOut;
		PtIn -= 2*W;
		V0 = *((v4s *) PtIn); PtIn += W;
		V1 = *((v4s *) PtIn);;
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc);
		*PtOut =  Acc;
	}
}

static void __attribute__ ((noinline)) KerConv2x3from3x3Stride2_V_DP_fps(
        signed char * __restrict__ In,
        int W, int PadTOrg,
        int Wo, int Ho, int Ho_F, int Ho_L,
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
	int FilterConf
	)
{
	v4s V0, V1, V2;
	v4s C0, C1, C2;
	signed char *PtIn;
	short int *PtOut = Out;
	int Bottom = Ho-Ho_L;

	if (FilterConf) {
		C0 = (v4s) (int) *((unsigned short *) (Filter + 0*3+0)); C1 = (v4s) (int) *((unsigned short *) (Filter + 1*3+0)); C2 = (v4s) (int) *((unsigned short *) (Filter + 2*3+0));
	} else {
		C0 = (v4s) (int) *((unsigned short *) (Filter + 0*3+1)); C1 = (v4s) (int) *((unsigned short *) (Filter + 1*3+1)); C2 = (v4s) (int) *((unsigned short *) (Filter + 2*3+1));
	}
	if (Ho_F==1) {
		PtIn = In; PtOut = Out; Ho_F = 0;
		V0 = (v4s){0,0,0,0};
	} else  { // == 0
		PtIn = In + (Ho_F*2-PadTOrg)*W; PtOut = Out+Ho_F*Wo;
		V0 = *((v4s *) PtIn); PtIn += W;
	}
	for (unsigned int i=Ho_F; i<Ho_L; i++) {
		int Acc = *PtOut;
		V1 = *((v4s *) PtIn); PtIn += W; V2 = *((v4s *) PtIn); PtIn += W;
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc);
		*PtOut =  Acc; PtOut+=Wo;
		V0 = V2;
	}
	if (Bottom) {
		int Acc = *PtOut;
		PtIn -= W;
		V0 = *((v4s *) PtIn); PtIn += W;
		V1 = *((v4s *) PtIn);;
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc);
		*PtOut =  Acc;
	}
}

static void __attribute__ ((noinline)) KerConv2x3from3x3StrideS_V_DP_fps(
        signed char * __restrict__ In,
        int W, int PadTOrg,
        int Wo, int Ho, int Ho_F, int Ho_L,
	int Stride,
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
	int FilterConf
	)
{
	v4s V0, V1, V2;
	v4s C0, C1, C2;
	signed char *PtIn;
	short int *PtOut;
	int Bottom = Ho-Ho_L;

	if (FilterConf) {
		C0 = (v4s) (int) *((unsigned short *) (Filter + 0*3+0)); C1 = (v4s) (int) *((unsigned short *) (Filter + 1*3+0)); C2 = (v4s) (int) *((unsigned short *) (Filter + 2*3+0));
	} else {
		C0 = (v4s) (int) *((unsigned short *) (Filter + 0*3+1)); C1 = (v4s) (int) *((unsigned short *) (Filter + 1*3+1)); C2 = (v4s) (int) *((unsigned short *) (Filter + 2*3+1));
	}
	if (Ho_F==1) {
                PtIn = In; PtOut = Out; Ho_F = 0;
		V0 = (v4s){0,0,0,0};
	} else  { // == 0
                PtIn = In + (Ho_F*Stride-PadTOrg)*W; PtOut = Out+Ho_F*Wo;
		V0 = *((v4s *) PtIn); PtIn += W;
	}
	for (unsigned int i=Ho_F; i<Ho_L; i++) {
		int Acc = *PtOut;
		V1 = *((v4s *) PtIn); PtIn += W; V2 = *((v4s *) PtIn); PtIn += (Stride-2)*W;
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc);
		*PtOut =  Acc; PtOut+=Wo;
		V0 = *((v4s *) PtIn); PtIn += W;
	}
	if (Bottom) {
		int Acc = *PtOut;
		PtIn -= W;
		V0 = *((v4s *) PtIn); PtIn += W;
		V1 = *((v4s *) PtIn);;
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc);
		*PtOut =  Acc;
	}
}


static void __attribute__ ((noinline)) KerConv3x2from3x3Stride1_H_DP_fps(
        signed char * __restrict__ In,
        int W, int PadL,
        int Wo, int Wo_F, int Wo_L,
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
	int FilterConf
	)

{
	v4s V0, V1;
	v4s C0, C1;
	signed char *PtIn = In+Wo_F*1-PadL;
	short int *PtOut = Out;

	if (FilterConf) {
		C0 = *((v4s *) &Filter[0*3+0]); C1 = *((v4s *) &Filter[1*3+0]); C0[3] = 0; C1[3] = 0;
	} else {
		C0 = *((v4s *) &Filter[1*3+0]); C1 = *((v4s *) &Filter[2*3+0]); C0[3] = 0; C1[3] = 0;
	}
	for (unsigned int i=Wo_F; i<Wo_L; i++) {
		int Acc = *PtOut;
		V0 = *((v4s *) (PtIn+0*W+0)); V1 = *((v4s *) (PtIn+1*W+0)); PtIn++;
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc);
		*PtOut = Acc; PtOut++;
	}
}

static void __attribute__ ((noinline)) KerConv3x2from3x3Stride2_H_DP_fps(
        signed char * __restrict__ In,
        int W, int PadL,
        int Wo, int Wo_F, int Wo_L,
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
	int FilterConf
	)

{
	v4s V0, V1;
	v4s C0, C1;
	signed char *PtIn = In+Wo_F*2-PadL;
	short int *PtOut = Out;

	if (FilterConf) {
		C0 = *((v4s *) &Filter[0*3+0]); C1 = *((v4s *) &Filter[1*3+0]); C0[3] = 0; C1[3] = 0;
	} else {
		C0 = *((v4s *) &Filter[1*3+0]); C1 = *((v4s *) &Filter[2*3+0]); C0[3] = 0; C1[3] = 0;
	}
	for (unsigned int i=Wo_F; i<Wo_L; i++) {
		int Acc = *PtOut;
		V0 = *((v4s *) (PtIn+0*W+0)); V1 = *((v4s *) (PtIn+1*W+0)); PtIn+=2;
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc);
		*PtOut = Acc; PtOut++;
	}
}

static void __attribute__ ((noinline)) KerConv3x2from3x3StrideS_H_DP_fps(
        signed char * __restrict__ In,
        int W, int PadL,
        int Wo, int Wo_F, int Wo_L,
	int Stride,
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
	int FilterConf
	)

{
	v4s V0, V1;
	v4s C0, C1;
	signed char *PtIn = In+Wo_F*Stride-PadL;
	short int *PtOut = Out;

	if (FilterConf) {
		C0 = *((v4s *) &Filter[0*3+0]); C1 = *((v4s *) &Filter[1*3+0]); C0[3] = 0; C1[3] = 0;
	} else {
		C0 = *((v4s *) &Filter[1*3+0]); C1 = *((v4s *) &Filter[2*3+0]); C0[3] = 0; C1[3] = 0;
	}
	for (unsigned int i=Wo_F; i<Wo_L; i++) {
		int Acc = *PtOut;
		V0 = *((v4s *) (PtIn+0*W+0)); V1 = *((v4s *) (PtIn+1*W+0)); PtIn+=Stride;
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc);
		*PtOut = Acc; PtOut++;
	}
}

static void __attribute__ ((noinline)) KerConv4x1from5x1StrideNx1_V_DP_fps(
        signed char * __restrict__ In,
        int W, v4s PadOrg, v4s Pad,
        int Wo, int Ho, int Ho_F, int Ho_L,
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
	int FilterConf
	)

{
        v4s V0;
        v4s C0;
        signed char *PtIn;
	short int *PtOut;

        switch (FilterConf) {
                case 2: // [0..4 x 0] => [2..4 x 0] => PadL==2
                        C0 = *((v4s*) (Filter + 0*5+2)); C0[3] = 0;
                        break;
                case 1: // [0..4 x 0] => [1..4 x 0] => PadL==1
                        C0 = *((v4s*) (Filter + 0*5+1));
                        break;
                case 3: // [0..4 x 0] => [0..3 x 0] => PadR==1
                        C0 = *((v4s*) (Filter + 0*5+0));
                        break;
                case 4: // [0..4 x 0] => [0..2 x 0] => PadR==2
                        C0 = *((v4s*) (Filter + 0*5+0)); C0 = (v4s)(((int)C0)<<8);
                        break;
        }
        PtIn = In + (Ho_F*1-PadOrg[2])*W; PtOut = Out+Ho_F*Wo;
        V0 = * (v4s *) PtIn; PtIn += W;
        for (unsigned int i=Ho_F; i<Ho_L; i++) {
                int Acc = *PtOut;
		Acc = gap_sumdotp4(V0, C0, Acc);
        	V0 = * (v4s *) PtIn; PtIn += W;
                *PtOut =  Acc; PtOut+=Wo;
        }
}

static void __attribute__ ((noinline)) KerConv1x4from1x5Stride1xN_H_DP_fps(
        signed char * __restrict__ In,
        int W, int PadL,
        int Wo, int Wo_F, int Wo_L,
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
	int FilterConf
	)

{
        v4s V0;
        v4s C0;
	int x0,x1,x2,x3;
        signed char *PtIn = In+Wo_F*1-PadL;
        short int *PtOut = Out;

        switch (FilterConf) {
                case 2: // PadT==2
                        C0 = *((v4s *) &Filter[2]);  C0[3] = 0;
                        break;
                case 1: // PadT==1
                        C0 = *((v4s *) &Filter[1]);
                        break;
                case 3: // PadB == 1
                        C0 = *((v4s *) &Filter[0]);
                        break;
                case 4: // PadB == 2
                        C0 = *((v4s *) &Filter[0]); C0 = (v4s)((int)C0<<8);
                        break;
        }
        x0 = *(PtIn+0*W+0); x1 = *(PtIn+1*W+0); x2 = *(PtIn+2*W+0); x3 = *(PtIn+3*W+0); V0 = gap_pack4(x0,x1,x2,x3); PtIn++;
        for (unsigned int i=Wo_F; i<Wo_L; i++) {
                int Acc = *PtOut;
		Acc = gap_sumdotp4(V0, C0, Acc);
        	x0 = *(PtIn+0*W+0); x1 = *(PtIn+1*W+0); x2 = *(PtIn+2*W+0); x3 = *(PtIn+3*W+0); V0 = gap_pack4(x0,x1,x2,x3); PtIn++;
                *PtOut = Acc; PtOut++;
        }
}

static void __attribute__ ((noinline)) KerConv4x5from5x5Stride1_V_DP_fps(
        signed char * __restrict__ In,
        int W, v4s PadOrg, v4s Pad,
        int Wo, int Ho, int Ho_F, int Ho_L,
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
	int FilterConf
	)
{
	v4s V0, V1, V2, V3, V4;
	v4s C0, C1, C2, C3, C4;
        signed char *PtIn;
	short int *PtOut;
        int Bottom, PadT = Pad[2], PadTOrg = PadOrg[2], PadB = Pad[3];

        switch (FilterConf) {
                case 2: // [0..4 x 0..4] => [2..4 x 0..4] PadL == 2
                        C0 = *((v4s*) (Filter + 0*5+2)); C0[3] = 0;
                        C1 = *((v4s*) (Filter + 1*5+2)); C1[3] = 0;
                        C2 = *((v4s*) (Filter + 2*5+2)); C2[3] = 0;
                        C3 = *((v4s*) (Filter + 3*5+2)); C3[3] = 0;
                        C4 = *((v4s*) (Filter + 4*5+2)); C4[3] = 0;
                        break;
                case 1: // [0..4 x 0..4] => [1..4 x 0..4] PadL == 1
                        C0 = *((v4s*) (Filter + 0*5+1));
                        C1 = *((v4s*) (Filter + 1*5+1));
                        C2 = *((v4s*) (Filter + 2*5+1));
                        C3 = *((v4s*) (Filter + 3*5+1));
                        C4 = *((v4s*) (Filter + 4*5+1));
                        break;
                case 3: // [0..4 x 0..4] => [0..3 x 0..4] PadR == 1
                        C0 = *((v4s*) (Filter + 0*5+0));
                        C1 = *((v4s*) (Filter + 1*5+0));
                        C2 = *((v4s*) (Filter + 2*5+0));
                        C3 = *((v4s*) (Filter + 3*5+0));
                        C4 = *((v4s*) (Filter + 4*5+0));
                        break;
                case 4: // [0..4 x 0..4] => [0..2 x 0..4] PadR == 2
                        C0 = *((v4s*) (Filter + 0*5+0)); C0 = (v4s)(((int)C0)<<8);
                        C1 = *((v4s*) (Filter + 1*5+0)); C1 = (v4s)(((int)C1)<<8);
                        C2 = *((v4s*) (Filter + 2*5+0)); C2 = (v4s)(((int)C2)<<8);
                        C3 = *((v4s*) (Filter + 3*5+0)); C3 = (v4s)(((int)C3)<<8);
                        C4 = *((v4s*) (Filter + 4*5+0)); C4 = (v4s)(((int)C4)<<8);
                        break;
        }
        if (PadT==2) {
                PtIn = In; Ho_F = 0;
                V0 = (v4s){0,0,0,0}; V1 = (v4s){0,0,0,0};
        } else if (PadT) { // == 1
                PtIn = In; Ho_F = 0;
                V0 = (v4s){0,0,0,0};
                V1 = *((v4s *) PtIn); PtIn += W;
        } else { // Ho_F==0
                PtIn = In + (Ho_F*1-PadTOrg)*W;
                V0 = *((v4s *) PtIn); PtIn += W;
                V1 = *((v4s *) PtIn); PtIn += W;
        }
        V2 = *((v4s *) PtIn); PtIn += W;
        V3 = *((v4s *) PtIn); PtIn += W;
        PtOut = Out+Ho_F*Wo;
	for (unsigned int i=Ho_F; i<Ho_L; i++) {
		int Acc = *PtOut;
		V4 = *((v4s *) PtIn); PtIn += W;
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc); Acc = gap_sumdotp4(V3, C3, Acc); Acc = gap_sumdotp4(V4, C4, Acc);
		*PtOut = Acc; PtOut+=Wo;
		V0 = V1; V1 = V2; V2 = V3; V3 = V4;
	}
	if (PadB) {
		int Acc = *PtOut;
		PtIn -= 4*W;
		V0 = *((v4s *) PtIn); PtIn += W; V1 = *((v4s *) PtIn); PtIn += W; V2 = *((v4s *) PtIn); PtIn += W; V3 = *((v4s *) PtIn); PtIn += W;
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc); Acc = gap_sumdotp4(V3, C3, Acc);
		*PtOut = Acc; PtOut+=Wo;
		if (PadB==2) {
			Acc = *PtOut;
			V0 = V1; V1 = V2; V2 = V3;
			Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc);
			*PtOut =  Acc;
		}
	}
}

static void __attribute__ ((noinline)) KerConv4x5from5x5Stride2_V_DP_fps(
        signed char * __restrict__ In,
        int W, int H, v4s PadOrg, v4s Pad,
        int Wo, int Ho, int Ho_F, int Ho_L,
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
	int FilterConf
	)
{
	v4s V0, V1, V2, V3, V4;
	v4s C0, C1, C2, C3, C4;
        signed char *PtIn;
	short int *PtOut;
        int PadL = PadOrg[0], PadT = Pad[2], PadTOrg = PadOrg[2], PadB = Pad[3];
        switch (FilterConf) {
                case 2: // [0..4 x 0..4] => [2..4 x 0..4] PadL == 2
                        C0 = *((v4s*) (Filter + 0*5+2)); C0[3] = 0;
                        C1 = *((v4s*) (Filter + 1*5+2)); C1[3] = 0;
                        C2 = *((v4s*) (Filter + 2*5+2)); C2[3] = 0;
                        C3 = *((v4s*) (Filter + 3*5+2)); C3[3] = 0;
                        C4 = *((v4s*) (Filter + 4*5+2)); C4[3] = 0;
                        break;
                case 1: // [0..4 x 0..4] => [1..4 x 0..4] PadL==1
                        C0 = *((v4s*) (Filter + 0*5+1));
                        C1 = *((v4s*) (Filter + 1*5+1));
                        C2 = *((v4s*) (Filter + 2*5+1));
                        C3 = *((v4s*) (Filter + 3*5+1));
                        C4 = *((v4s*) (Filter + 4*5+1));
                        break;
                case 3: // [0..4 x 0..4] => [0..3 x 0..4] PadR==1
                        C0 = *((v4s*) (Filter + 0*5+0));
                        C1 = *((v4s*) (Filter + 1*5+0));
                        C2 = *((v4s*) (Filter + 2*5+0));
                        C3 = *((v4s*) (Filter + 3*5+0));
                        C4 = *((v4s*) (Filter + 4*5+0));
                        break;
                case 4: // [0..4 x 0..4] => [0..2 x 0..4] PadR==2
                        C0 = *((v4s*) (Filter + 0*5+0)); C0[3] = 0;
                        C1 = *((v4s*) (Filter + 1*5+0)); C1[3] = 0;
                        C2 = *((v4s*) (Filter + 2*5+0)); C2[3] = 0;
                        C3 = *((v4s*) (Filter + 3*5+0)); C3[3] = 0;
                        C4 = *((v4s*) (Filter + 4*5+0)); C4[3] = 0;
                        break;
        }
        if (PadT==2) {
                PtIn = In; Ho_F = 0;
                V0 = (v4s){0,0,0,0}; V1 = (v4s){0,0,0,0};
        } else if (PadT) { // == 1
                PtIn = In; Ho_F = 0;
                V0 = (v4s){0,0,0,0}; V1 = *((v4s *) PtIn); PtIn += W;
        } else {
                PtIn = In + (Ho_F*2-PadTOrg)*W;
                V0 = *((v4s *) PtIn); PtIn += W;
                V1 = *((v4s *) PtIn); PtIn += W;
        }
        PtOut = Out+Ho_F*Wo;
	V2 = *((v4s *) PtIn); PtIn += W;
	for (unsigned int i=Ho_F; i<Ho_L; i++) {
		int Acc = *PtOut;
		V3 = *((v4s *) PtIn); PtIn += W; V4 = *((v4s *) PtIn); PtIn += W;
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc); Acc = gap_sumdotp4(V3, C3, Acc); Acc = gap_sumdotp4(V4, C4, Acc);
		*PtOut = Acc; PtOut+=Wo;
		V0 = V2; V1 = V3; V2 = V4;
	}
	if (PadB) {
		int Acc = *PtOut;
		PtIn -= 3*W;
		V0 = *((v4s *) PtIn); PtIn += W; V1 = *((v4s *) PtIn); PtIn += W; V2 = *((v4s *) PtIn); PtIn += W;
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc);
		if (PadB==1) {
			V3 = *((v4s *) PtIn); Acc = gap_sumdotp4(V3, C3, Acc);
		}
		*PtOut = Acc;
	}
}

static void __attribute__ ((noinline)) KerConv4x5from5x5StrideS_V_DP_fps(
        signed char * __restrict__ In,
        int W, int H, v4s PadOrg, v4s Pad,
        int Wo, int Ho, int Ho_F, int Ho_L,
	int Stride, 
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
	int FilterConf
	)
{
	v4s V0, V1, V2, V3, V4;
	v4s C0, C1, C2, C3, C4;
        signed char *PtIn;
	short int *PtOut;
        int PadL = PadOrg[0], PadT = Pad[2], PadTOrg = PadOrg[2], PadB = Pad[3];

        switch (FilterConf) {
                case 2: // [0..4 x 0..4] => [2..4 x 0..4] PadL==2
                        C0 = *((v4s*) (Filter + 0*5+2)); C0[3] = 0;
                        C1 = *((v4s*) (Filter + 1*5+2)); C1[3] = 0;
                        C2 = *((v4s*) (Filter + 2*5+2)); C2[3] = 0;
                        C3 = *((v4s*) (Filter + 3*5+2)); C3[3] = 0;
                        C4 = *((v4s*) (Filter + 4*5+2)); C4[3] = 0;
                        break;
                case 1: // [0..4 x 0..4] => [1..4 x 0..4] PadL==1
                        C0 = *((v4s*) (Filter + 0*5+1));
                        C1 = *((v4s*) (Filter + 1*5+1));
                        C2 = *((v4s*) (Filter + 2*5+1));
                        C3 = *((v4s*) (Filter + 3*5+1));
                        C4 = *((v4s*) (Filter + 4*5+1));
                        break;
                case 3: // [0..4 x 0..4] => [0..3 x 0..4] PadR==1
                        C0 = *((v4s*) (Filter + 0*5+0));
                        C1 = *((v4s*) (Filter + 1*5+0));
                        C2 = *((v4s*) (Filter + 2*5+0));
                        C3 = *((v4s*) (Filter + 3*5+0));
                        C4 = *((v4s*) (Filter + 4*5+0));
                        break;
                case 4: // [0..4 x 0..4] => [0..2 x 0..4] PadR==2
                        C0 = *((v4s*) (Filter + 0*5+0)); C0[3] = 0;
                        C1 = *((v4s*) (Filter + 1*5+0)); C1[3] = 0;
                        C2 = *((v4s*) (Filter + 2*5+0)); C2[3] = 0;
                        C3 = *((v4s*) (Filter + 3*5+0)); C3[3] = 0;
                        C4 = *((v4s*) (Filter + 4*5+0)); C4[3] = 0;
                        break;
        }
        if (PadT==2) {
                PtIn = In; Ho_F = 0;
                V0 = (v4s){0,0,0,0}; V1 = (v4s){0,0,0,0};
        } else if (PadT) { // == 1
                PtIn = In; Ho_F = 0;
                V0 = (v4s){0,0,0,0}; V1 = *((v4s *) PtIn); PtIn += W;
        } else {
                PtIn = In + (Ho_F*Stride-PadTOrg)*W; PtOut = Out+Ho_F*Wo;
                V0 = *((v4s *) PtIn); PtIn += W;
                V1 = *((v4s *) PtIn); PtIn += W;
        }
        PtOut = Out+Ho_F*Wo;

	for (unsigned int i=Ho_F; i<Ho_L; i++) {
		int Acc = *PtOut;
		V2 = *((v4s *) PtIn); PtIn += W; V3 = *((v4s *) PtIn); PtIn += W; V4 = *((v4s *) PtIn); PtIn += (Stride-4)*W;
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc); Acc = gap_sumdotp4(V3, C3, Acc); Acc = gap_sumdotp4(V4, C4, Acc);
		*PtOut = Acc; PtOut+=Wo;
		V0 = *((v4s *) PtIn); PtIn += W; V1 = *((v4s *) PtIn); PtIn += W;
	}
	if (PadB) {
		int Acc = *PtOut;
		PtIn -= 2*W;
		V0 = *((v4s *) PtIn); PtIn += W; V1 = *((v4s *) PtIn); PtIn += W; V2 = *((v4s *) PtIn); PtIn += W;
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc);
		if (PadB==1) {
			V3 = *((v4s *) PtIn); Acc = gap_sumdotp4(V3, C3, Acc);
		}
		*PtOut = Acc;
	}
}

static void __attribute__ ((noinline)) KerConv5x4from5x5Stride1_H_DP_fps(
        signed char * __restrict__ In,
        int W, int PadL,
        int Wo, int Wo_F, int Wo_L,
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
	int FilterConf
	)

{
	v4s V0, V1, V2, V3, V4;
	v4s C0, C1, C2, C3, C4;

	signed char *PtIn = In+Wo_F*1-PadL;
	short int *PtOut = Out;

	switch (FilterConf) {
		case 2:
			C0 = *((v4s *) &Filter[2*5+0]); 
			C1 = *((v4s *) &Filter[3*5+0]);
			C2 = *((v4s *) &Filter[4*5+0]);
			C3 = (v4s){0,0,0,0}; C4 = (v4s){Filter[2*5+4], Filter[3*5+4], Filter[4*5+4], 0};
			break;
		case 1:
			C0 = *((v4s *) &Filter[1*5+0]);
			C1 = *((v4s *) &Filter[2*5+0]);
			C2 = *((v4s *) &Filter[3*5+0]);
			C3 = *((v4s *) &Filter[4*5+0]); C4 = (v4s){Filter[1*5+4], Filter[2*5+4], Filter[3*5+4], Filter[4*5+4]};
			break;
		case 3:
			C0 = *((v4s *) &Filter[0*5+0]);
			C1 = *((v4s *) &Filter[1*5+0]);
			C2 = *((v4s *) &Filter[2*5+0]);
			C3 = *((v4s *) &Filter[3*5+0]); C4 = (v4s){Filter[0*5+4], Filter[1*5+4], Filter[2*5+4], Filter[3*5+4]};
			break;
		case 4:
			C0 = (v4s){0,0,0,0};
			C1 = *((v4s *) &Filter[0*5+0]);
			C2 = *((v4s *) &Filter[1*5+0]);
			C3 = *((v4s *) &Filter[2*5+0]); C4 = (v4s){0, Filter[0*5+4], Filter[1*5+4], Filter[2*5+4]};
			break;
	}
	V0 = *((v4s *) (PtIn+0*W+0)); V1 = *((v4s *) (PtIn+1*W+0)); V2 = *((v4s *) (PtIn+2*W+0)); V3 = *((v4s *) (PtIn+3*W+0)); PtIn += 4;
	for (unsigned int i=Wo_F; i<Wo_L; i++) {
		int x0, x1, x2, x3;
		int Acc = *PtOut;
		x0 = PtIn[0*W]; x1 = PtIn[1*W]; x2 = PtIn[2*W]; x3 = PtIn[3*W]; PtIn++;
		V4 = gap_pack4(x0,x1,x2,x3);
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc); Acc = gap_sumdotp4(V3, C3, Acc); Acc = gap_sumdotp4(V4, C4, Acc);
		*PtOut = Acc; PtOut++;
		V0 = __builtin_shuffle(V0, (v4s)(int)V4, (v4s){1,2,3,4});
		V1 = __builtin_shuffle(V1, (v4s)(int)V4, (v4s){1,2,3,5});
		V2 = __builtin_shuffle(V2, (v4s)(int)V4, (v4s){1,2,3,6});
		V3 = __builtin_shuffle(V3, (v4s)(int)V4, (v4s){1,2,3,7});
	}
}

static void __attribute__ ((noinline)) KerConv5x4from5x5Stride2_H_DP_fps(
        signed char * __restrict__ In,
        int W, int H, int PadL, int PadT,
        int Wo, int Wo_F, int Wo_L,
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
        int FilterConf
	)

{
	v4s V0, V1, V2, V3, V4;
	v4s C0, C1, C2, C3, C4;

	signed char *PtIn = In+Wo_F*2-PadL;
	short int *PtOut = Out;

        switch (FilterConf) {
                case 2: // PadT==2
                        C0 = *((v4s *) &Filter[2*5+0]);
                        C1 = *((v4s *) &Filter[3*5+0]);
                        C2 = *((v4s *) &Filter[4*5+0]);
                        C3 = (v4s){0,0,0,0}; C4 = (v4s){Filter[2*5+4], Filter[3*5+4], Filter[4*5+4], 0};
                        break;
                case 1: // PadT==1
                        C0 = *((v4s *) &Filter[1*5+0]);
                        C1 = *((v4s *) &Filter[2*5+0]);
                        C2 = *((v4s *) &Filter[3*5+0]);
                        C3 = *((v4s *) &Filter[4*5+0]); C4 = (v4s){Filter[1*5+4], Filter[2*5+4], Filter[3*5+4], Filter[4*5+4]};
                        break;
                case 3: // PadB==1
                        C0 = *((v4s *) &Filter[0*5+0]);
                        C1 = *((v4s *) &Filter[1*5+0]);
                        C2 = *((v4s *) &Filter[2*5+0]);
                        C3 = *((v4s *) &Filter[3*5+0]); C4 = (v4s){Filter[0*5+4], Filter[1*5+4], Filter[2*5+4], Filter[3*5+4]};
                        break;
                case 4: // PadB==2
                        C0 = *((v4s *) &Filter[0*5+0]);
                        C1 = *((v4s *) &Filter[1*5+0]);
                        C2 = *((v4s *) &Filter[2*5+0]);
                        C3 = (v4s){0,0,0,0}; C4 = (v4s){Filter[0*5+4], Filter[1*5+4], Filter[2*5+4], 0};
                        break;
        }
	for (unsigned int i=Wo_F; i<Wo_L; i++) {
		int x0, x1, x2, x3;
		int Acc = *PtOut;
		V0 = *((v4s *) (PtIn+0*W+0)); V1 = *((v4s *) (PtIn+1*W+0)); V2 = *((v4s *) (PtIn+2*W+0)); V3 = *((v4s *) (PtIn+3*W+0)); PtIn += 4;
		x0 = PtIn[0*W]; x1 = PtIn[1*W]; x2 = PtIn[2*W]; x3 = PtIn[3*W]; PtIn+=(2-4);
		V4 = gap_pack4(x0,x1,x2,x3);
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc); Acc = gap_sumdotp4(V3, C3, Acc); Acc = gap_sumdotp4(V4, C4, Acc);
		*PtOut = Acc; PtOut++;
	}
}

static void __attribute__ ((noinline)) KerConv5x4from5x5StrideS_H_DP_fps(
        signed char * __restrict__ In,
        int W, int H, int PadL, int PadT,
        int Wo, int Wo_F, int Wo_L,
	unsigned int Stride,
        short int * __restrict__ Out,
        signed char * __restrict__ Filter,
        int FilterConf
	)

{
	v4s V0, V1, V2, V3, V4;
	v4s C0, C1, C2, C3, C4;

	signed char *PtIn = In+Wo_F*Stride-PadL;
	short int *PtOut = Out;

        switch (FilterConf) {
                case 2: // PadT==2
                        C0 = *((v4s *) &Filter[2*5+0]);
                        C1 = *((v4s *) &Filter[3*5+0]);
                        C2 = *((v4s *) &Filter[4*5+0]);
                        C3 = (v4s){0,0,0,0}; C4 = (v4s){Filter[2*5+4], Filter[3*5+4], Filter[4*5+4], 0};
                        break;
                case 1: // PadT==1
                        C0 = *((v4s *) &Filter[1*5+0]);
                        C1 = *((v4s *) &Filter[2*5+0]);
                        C2 = *((v4s *) &Filter[3*5+0]);
                        C3 = *((v4s *) &Filter[4*5+0]); C4 = (v4s){Filter[1*5+4], Filter[2*5+4], Filter[3*5+4], Filter[4*5+4]};
                        break;
                case 3: // PadB==1
                        C0 = *((v4s *) &Filter[0*5+0]);
                        C1 = *((v4s *) &Filter[1*5+0]);
                        C2 = *((v4s *) &Filter[2*5+0]);
                        C3 = *((v4s *) &Filter[3*5+0]); C4 = (v4s){Filter[0*5+4], Filter[1*5+4], Filter[2*5+4], Filter[3*5+4]};
                        break;
                case 4: // PadB==2
                        C0 = *((v4s *) &Filter[0*5+0]);
                        C1 = *((v4s *) &Filter[1*5+0]);
                        C2 = *((v4s *) &Filter[2*5+0]);
                        C3 = (v4s){0,0,0,0};
                        C4 = (v4s){Filter[0*5+4], Filter[1*5+4], Filter[2*5+4], 0};
                        break;
        }
	for (unsigned int i=Wo_F; i<Wo_L; i++) {
		int x0, x1, x2, x3;
		int Acc = *PtOut;
		V0 = *((v4s *) (PtIn+0*W+0)); V1 = *((v4s *) (PtIn+1*W+0)); V2 = *((v4s *) (PtIn+2*W+0)); V3 = *((v4s *) (PtIn+3*W+0)); PtIn += 4;
		x0 = PtIn[0*W]; x1 = PtIn[1*W]; x2 = PtIn[2*W]; x3 = PtIn[3*W]; PtIn+=((int)Stride-4);
		V4 = gap_pack4(x0,x1,x2,x3);
		Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc); Acc = gap_sumdotp4(V3, C3, Acc); Acc = gap_sumdotp4(V4, C4, Acc);
		*PtOut = Acc; PtOut++;
	}
}

void __attribute__ ((noinline)) KerConvNxNStrideS_Border_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int Fw,
	int Fh,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	int Stride,
	v4s Pad,
	v4s PadOrg
	)

{
	int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
	int PadROrg = PadOrg[1], PadBOrg = PadOrg[3];
	int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];
	int Hi_F = (Fh-1)/2 - PadTOrg;
	int Hi_L = Hi_F + (Ho_L-1)*Stride;	// iff Hi_L>Hi_F
	int Wi_F = (Fw-1)/2 - PadLOrg;
	int Wi_L = Wi_F + (Wo_L-1)*Stride;	// iff Wi_L>Wi_F

	if (PadT) { /* Top */
		int ht = PadTOrg, hb = H - Hi_F + Fh/2;
               	for (unsigned int h=0; h<Ho_F; h++) {
			int Fh_min = ht, Fh_max = MinCond(Fh, hb); // ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
        		for (unsigned int w=Wo_F; w<Wo_L; w++) {
                        	int Acc = Out[Wo*h+w];
                        	for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                	for (unsigned int j=0; j<Fw; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			ht -= Stride; hb -= Stride;
		}
	}
	if (PadB) { /* Bottom */
		int ht = 0, hb = H - (Hi_L+Stride) + Fh/2;
               	for (unsigned int h=Ho_L; h<Ho; h++) {
			int Fh_min = ht, Fh_max = MinCond(hb, Fh); // ht Can't be > F by definition of Ho_L so we can remove and use ht only
        		for (unsigned int w=Wo_F; w<Wo_L; w++) {
                        	int Acc = Out[Wo*h+w];
                        	for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                	for (unsigned int j=0; j<Fw; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			hb -= Stride;
		}
	}
	if (PadL) { /* Left */
		int wl = PadLOrg, wr = W - Wi_F + Fw/2;
               	for (unsigned int w=0; w<Wo_F; w++) {
			int Wh_min = wl, Wh_max = MinCond(Fw, wr); // wh Can't be < 0 by definition of Wo_F so we can remove and use wl only
        		for (unsigned int h=Ho_F; h<Ho_L; h++) {
                        	int Acc = Out[Wo*h+w];
                        	for (unsigned int i=0; i<Fh; i++) 
                                	for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			wl -= Stride; wr -= Stride;
		}
	}
	if (PadR) { /* Right */
		int wl = 0, wr = W - (Wi_L+Stride) + Fw/2;
               	for (unsigned int w=Wo_L; w<Wo; w++) {
			int Wh_min = wl, Wh_max = MinCond(wr, Fw); // ht Can't be > F by definition of Ho_L so we can remove and use ht only
        		for (unsigned int h=Ho_F; h<Ho_L; h++) {
                       		int Acc = Out[Wo*h+w];
                        	for (unsigned int i=0; i<Fh; i++) 
                                	for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			wr -= Stride;
		}
	}
	if (PadT) {
		if (PadL) { /* Upper left corner */
			int ht = PadTOrg, hb = H - Hi_F + Fh/2;
        		for (unsigned int h=0; h<Ho_F; h++) {
				int wl = PadLOrg, wr = W - Wi_F + Fw/2;
                		for (unsigned int w=0; w<Wo_F; w++) {
                        		int Acc = Out[Wo*h+w];
					// wh Can't be < 0 by definition of Wo_F so we can remove and use wl only. ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(Fw, wr), Fh_min = ht, Fh_max = MinCond(Fh, hb);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wl -= Stride; wr -= Stride;
				}
				ht -= Stride; hb -= Stride;
			}
		}
		if (PadR) { /* Upper right corner */
			int ht = PadTOrg, hb = H - Hi_F + Fh/2;
        		for (unsigned int h=0; h<Ho_F; h++) {
				int wl = 0, wr = W - (Wi_L+Stride) + Fw/2;
                		for (unsigned int w=Wo_L; w<Wo; w++) {
                        		int Acc = Out[Wo*h+w];
					// ht Can't be > F by definition of Ho_L so we can remove and use ht only. ht Can't be > F by definition of Ho_L so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(wr, Fw), Fh_min = ht, Fh_max = MinCond(Fh, hb);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wr -= Stride;
				}
				ht -= Stride; hb -= Stride;
			}
		}
	}
	if (PadB) {
		if (PadL) { /* Bottom Left corner */
			int ht = 0, hb = H - (Hi_L+Stride) + Fh/2;
        		for (unsigned int h=Ho_L; h<Ho; h++) {
				int wl = PadLOrg, wr = W - Wi_F + Fw/2;
                		for (unsigned int w=0; w<Wo_F; w++) {
                        		int Acc = Out[Wo*h+w];
 					// wh Can't be < 0 by definition of Wo_F so we can remove and use wl only.  ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(Fw, wr), Fh_min = ht, Fh_max = MinCond(hb, Fh);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wl -= Stride; wr -= Stride;
				}
				hb -= Stride;
			}
		}
		if (PadR) { /* Bottom Right corner */
			int ht = 0, hb = H - (Hi_L+Stride) + Fh/2;
        		for (unsigned int h=Ho_L; h<Ho; h++) {
				int wl = 0, wr = W - (Wi_L+Stride) + Fw/2;
                		for (unsigned int w=Wo_L; w<Wo; w++) {
                        		int Acc = Out[Wo*h+w];
 					// wh Can't be < 0 by definition of Wo_F so we can remove and use wl only.  ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(wr, Fw), Fh_min = ht, Fh_max = MinCond(hb, Fh);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wr -= Stride;
				}
				hb -= Stride;
			}
		}
	}
}

void __attribute__ ((noinline)) KerConvNxMStrideSxSy_Border_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int Fw,
	int Fh,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	int StrideX,
	int StrideY,
	v4s Pad,
	v4s PadOrg
	)

{
	int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
	int PadROrg = PadOrg[1], PadBOrg = PadOrg[3];
	int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];
	int Hi_F = (Fh-1)/2 - PadTOrg;
	int Hi_L = Hi_F + (Ho_L-1)*StrideY;	// iff Hi_L>Hi_F
	int Wi_F = (Fw-1)/2 - PadLOrg;
	int Wi_L = Wi_F + (Wo_L-1)*StrideX;	// iff Wi_L>Wi_F

	if (PadT) { /* Top */
		int ht = PadTOrg, hb = H - Hi_F + Fh/2;
               	for (unsigned int h=0; h<Ho_F; h++) {
			int Fh_min = ht, Fh_max = MinCond(Fh, hb); // ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
        		for (unsigned int w=Wo_F; w<Wo_L; w++) {
                        	int Acc = Out[Wo*h+w];
                        	for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                	for (unsigned int j=0; j<Fw; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			ht -= StrideY; hb -= StrideY;
		}
	}
	if (PadB) { /* Bottom */
		int ht = 0, hb = H - (Hi_L+StrideY) + Fh/2;
               	for (unsigned int h=Ho_L; h<Ho; h++) {
			int Fh_min = ht, Fh_max = MinCond(hb, Fh); // ht Can't be > F by definition of Ho_L so we can remove and use ht only
        		for (unsigned int w=Wo_F; w<Wo_L; w++) {
                        	int Acc = Out[Wo*h+w];
                        	for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                	for (unsigned int j=0; j<Fw; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			hb -= StrideY;
		}
	}
	if (PadL) { /* Left */
		int wl = PadLOrg, wr = W - Wi_F + Fw/2;
               	for (unsigned int w=0; w<Wo_F; w++) {
			int Wh_min = wl, Wh_max = MinCond(Fw, wr); // wh Can't be < 0 by definition of Wo_F so we can remove and use wl only
        		for (unsigned int h=Ho_F; h<Ho_L; h++) {
                        	int Acc = Out[Wo*h+w];
                        	for (unsigned int i=0; i<Fh; i++) 
                                	for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			wl -= StrideX; wr -= StrideX;
		}
	}
	if (PadR) { /* Right */
		int wl = 0, wr = W - (Wi_L+StrideX) + Fw/2;
               	for (unsigned int w=Wo_L; w<Wo; w++) {
			int Wh_min = wl, Wh_max = MinCond(wr, Fw); // ht Can't be > F by definition of Ho_L so we can remove and use ht only
        		for (unsigned int h=Ho_F; h<Ho_L; h++) {
                       		int Acc = Out[Wo*h+w];
                        	for (unsigned int i=0; i<Fh; i++) 
                                	for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			wr -= StrideX;
		}
	}
	if (PadT) {
		if (PadL) { /* Upper left corner */
			int ht = PadTOrg, hb = H - Hi_F + Fh/2;
        		for (unsigned int h=0; h<Ho_F; h++) {
				int wl = PadLOrg, wr = W - Wi_F + Fw/2;
                		for (unsigned int w=0; w<Wo_F; w++) {
                        		int Acc = Out[Wo*h+w];
					// wh Can't be < 0 by definition of Wo_F so we can remove and use wl only. ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(Fw, wr), Fh_min = ht, Fh_max = MinCond(Fh, hb);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wl -= StrideX; wr -= StrideX;
				}
				ht -= StrideY; hb -= StrideY;
			}
		}
		if (PadR) { /* Upper right corner */
			int ht = PadTOrg, hb = H - Hi_F + Fh/2;
        		for (unsigned int h=0; h<Ho_F; h++) {
				int wl = 0, wr = W - (Wi_L+StrideX) + Fw/2;
                		for (unsigned int w=Wo_L; w<Wo; w++) {
                        		int Acc = Out[Wo*h+w];
					// ht Can't be > F by definition of Ho_L so we can remove and use ht only. ht Can't be > F by definition of Ho_L so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(wr, Fw), Fh_min = ht, Fh_max = MinCond(Fh, hb);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wr -= StrideX;
				}
				ht -= StrideY; hb -= StrideY;
			}
		}
	}
	if (PadB) {
		if (PadL) { /* Bottom Left corner */
			int ht = 0, hb = H - (Hi_L+StrideY) + Fh/2;
        		for (unsigned int h=Ho_L; h<Ho; h++) {
				int wl = PadLOrg, wr = W - Wi_F + Fw/2;
                		for (unsigned int w=0; w<Wo_F; w++) {
                        		int Acc = Out[Wo*h+w];
 					// wh Can't be < 0 by definition of Wo_F so we can remove and use wl only.  ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(Fw, wr), Fh_min = ht, Fh_max = MinCond(hb, Fh);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wl -= StrideX; wr -= StrideX;
				}
				hb -= StrideY;
			}
		}
		if (PadR) { /* Bottom Right corner */
			int ht = 0, hb = H - (Hi_L+StrideY) + Fh/2;
        		for (unsigned int h=Ho_L; h<Ho; h++) {
				int wl = 0, wr = W - (Wi_L+StrideX) + Fw/2;
                		for (unsigned int w=Wo_L; w<Wo; w++) {
                        		int Acc = Out[Wo*h+w];
 					// wh Can't be < 0 by definition of Wo_F so we can remove and use wl only.  ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(wr, Fw), Fh_min = ht, Fh_max = MinCond(hb, Fh);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wr -= StrideX;
				}
				hb -= StrideY;
			}
		}
	}
}

void __attribute__ ((noinline)) KerConvNxMDxDyStrideSxSy_Border_DP_fps(
        signed char *__restrict__ In,
	short int *__restrict__ Out,
        signed char *__restrict__ Filter,
        int Fw,
        int Fh,
        int Dw,
        int Dh,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        int StrideX,
        int StrideY,
        v4s Pad,
        v4s PadOrg,
        int Norm
        )

{
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadROrg = PadOrg[1], PadBOrg = PadOrg[3];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];
        int TFw = Dw*(Fw-1)+1, TFh = Dh*(Fh-1)+1;
        int Hi_F = (TFh-1)/2 - PadTOrg;
        int Hi_L = Hi_F + (Ho_L-1)*StrideY;     // iff Hi_L>Hi_F
        int Wi_F = (TFw-1)/2 - PadLOrg;
        int Wi_L = Wi_F + (Wo_L-1)*StrideX;     // iff Wi_L>Wi_F
        int Prec=10;
        int InvDh = ((1<<Prec)+Dh-1)/Dh;
        int InvDw = ((1<<Prec)+Dw-1)/Dw;

        /*
        Here we assume that for a given filter output we don't have padding on both side of the input.
        Thanks to this assumption we can simplify a bit where filter starts and stops in the input.
        Either starts at 0 if (right or bottom) and stops at a place function of the padding or
        stops at Fw/Fh if (left or bottom) and starts  a place function of the padding
        */
        if (PadT) { /* Top */
                int ht = PadTOrg;
                for (unsigned int h=0; h<Ho_F; h++) {
                        int hta = gap_mulsN(ht-1, InvDh, Prec) + 1; // hta = (ht-1)/Dh+1
                        int Fh_min = hta;
                        for (unsigned int w=Wo_F; w<Wo_L; w++) {
                                int Acc = Out[Wo*h+w];
                                for (unsigned int i=Fh_min; i<Fh; i++)
                                        for (unsigned int j=0; j<Fw; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                Out[Wo*h+w] = Acc;
                        }
                        ht -= StrideY;
                }
        }
        if (PadB) { /* Bottom */
                int hb = H - (Hi_L+StrideY) + TFh/2;
                for (unsigned int h=Ho_L; h<Ho; h++) {
                        int hba = gap_mulsN(hb-1, InvDh, Prec) + 1; // hba = (hb-1)/Dh+1
                        int Fh_max = MinCond(hba, Fh);
                        for (unsigned int w=Wo_F; w<Wo_L; w++) {
                                int Acc = Out[Wo*h+w];
                                for (unsigned int i=0; i<Fh_max; i++)
                                        for (unsigned int j=0; j<Fw; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                Out[Wo*h+w] = Acc;
                        }
                        hb -= StrideY;
                }
        }
        if (PadL) { /* Left */
                int wl = PadLOrg;
                for (unsigned int w=0; w<Wo_F; w++) {
                        int wla = gap_mulsN(wl-1, InvDw, Prec) + 1; // wla = (wl-1)/Dw+1
                        int Wl_min = wla;
                        for (unsigned int h=Ho_F; h<Ho_L; h++) {
                                int Acc = Out[Wo*h+w];
                                for (unsigned int i=0; i<Fh; i++)
                                        for (unsigned int j=Wl_min; j<Fw; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                Out[Wo*h+w] = Acc;
                        }
                        wl -= StrideX;
                }
        }
        if (PadR) { /* Right */
                int wr = W - (Wi_L+StrideX) + TFw/2;
                for (unsigned int w=Wo_L; w<Wo; w++) {
                        int wra = gap_mulsN(wr-1, InvDw, Prec) + 1; // wra = (wr-1)/Dw+1
                        int Wr_max = MinCond(wra, Fw); // ht Can't be > F by definition of Ho_L so we can remove and use ht only
                        for (unsigned int h=Ho_F; h<Ho_L; h++) {
                                int Acc = Out[Wo*h+w];
                                for (unsigned int i=0; i<Fh; i++)
                                        for (unsigned int j=0; j<Wr_max; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                Out[Wo*h+w] = Acc;
                        }
                        wr -= StrideX;
                }
        }
        if (PadT) {
                if (PadL) { /* Upper left corner */
                        int ht = PadTOrg;
                        for (unsigned int h=0; h<Ho_F; h++) {
                                int wl = PadLOrg;
                                int hta = gap_mulsN(ht-1, InvDh, Prec) + 1; // hta = (ht-1)/Dh+1
                                for (unsigned int w=0; w<Wo_F; w++) {
                                        int Acc = Out[Wo*h+w];
                                        int wla = gap_mulsN(wl-1, InvDw, Prec) + 1; // wla = (wl-1)/Dw+1
                                        int Wl_min = wla, Fh_min = hta;
                                        for (unsigned int i=Fh_min; i<Fh; i++)
                                                for (unsigned int j=Wl_min; j<Fw; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                        Out[Wo*h+w] = Acc;
                                        wl -= StrideX;
                                }
                                ht -= StrideY;
                        }
                }
                if (PadR) { /* Upper right corner */
                        int ht = PadTOrg;
                        for (unsigned int h=0; h<Ho_F; h++) {
                                int wr = W - (Wi_L+StrideX) + TFw/2;
                                int hta = gap_mulsN(ht-1, InvDh, Prec) + 1; // hta = (ht-1)/Dh+1
                                for (unsigned int w=Wo_L; w<Wo; w++) {
                                        int Acc = Out[Wo*h+w];
                                        int wra = gap_mulsN(wr-1, InvDw, Prec) + 1; // wra = (wr-1)/Dw+1
                                        int Wr_max = MinCond(wra, Fw), Fh_min = hta;
                                        for (unsigned int i=Fh_min; i<Fh; i++)
                                                for (unsigned int j=0; j<Wr_max; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                        Out[Wo*h+w] = Acc;
                                        wr -= StrideX;
                                }
                                ht -= StrideY;
                        }
                }
        }
        if (PadB) {
                if (PadL) { /* Bottom Left corner */
                        int hb = H - (Hi_L+StrideY) + TFh/2;
                        for (unsigned int h=Ho_L; h<Ho; h++) {
                                int wl = PadLOrg;
                                int hba = gap_mulsN(hb-1, InvDh, Prec) + 1; // hba = (hb-1)/Dh+1
                                for (unsigned int w=0; w<Wo_F; w++) {
                                        int Acc = Out[Wo*h+w];
                                        int wla = gap_mulsN(wl-1, InvDw, Prec) + 1; // wla = (wl-1)/Dw+1
                                        int Wl_min = wla, Fh_max = MinCond(hba, Fh);
                                        for (unsigned int i=0; i<Fh_max; i++)
                                                for (unsigned int j=Wl_min; j<Fw; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                        Out[Wo*h+w] = Acc;
                                        wl -= StrideX;
                                }
                                hb -= StrideY;
                        }
                }
                if (PadR) { /* Bottom Right corner */
                        int hb = H - (Hi_L+StrideY) + TFh/2;
                        for (unsigned int h=Ho_L; h<Ho; h++) {
                                int wr = W - (Wi_L+StrideX) + TFw/2;
                                int hba = gap_mulsN(hb-1, InvDh, Prec) + 1; // hba = (hb-1)/Dh+1
                                for (unsigned int w=Wo_L; w<Wo; w++) {
                                        int Acc = Out[Wo*h+w];
                                        int wra = gap_mulsN(wr-1, InvDw, Prec) + 1; // wra = (wr-1)/Dw+1
                                        int Wr_max = MinCond(wra, Fw), Fh_max = MinCond(hba, Fh);
                                        for (unsigned int i=0; i<Fh_max; i++)
                                                for (unsigned int j=0; j<Wr_max; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                        Out[Wo*h+w] = Acc;
                                        wr -= StrideX;
                                }
                                hb -= StrideY;
                        }
                }
        }
}

static void __attribute__ ((noinline)) KerConv2x1from3x1StrideNx1_V_DP_fp(
        short int * __restrict__ In,
        int W, int PadTOrg,
        int Wo, int Ho, int Ho_F, int Ho_L,
        int * __restrict__ Out,
        short int * __restrict__ Filter,
	int FilterConf
	)
{
        int V0,V1;
        int C0,C1;
        short int *PtIn;
	int *PtOut;

        if (FilterConf) { /* Right Side */
                C0 = Filter[0]; C1 = Filter[1];
        } else { /* Left Side */
                C0 = Filter[1]; C1 = Filter[2];
        }
        PtIn = In + (Ho_F*1-PadTOrg)*W; PtOut = Out+Ho_F*Wo;
        for (unsigned int i=Ho_F; i<Ho_L; i++) {
                int Acc = *PtOut;
                V0 = *PtIn; V1 = *(PtIn+1); PtIn += W;
                Acc += V0*C0; Acc += V1*C1;
                *PtOut =  Acc; PtOut+=Wo;
        }
}

static void __attribute__ ((noinline)) KerConv1x2from1x3Stride1xN_H_DP_fp(
        short int * __restrict__ In,
        int W, int PadL,
        int Wo, int Wo_F, int Wo_L,
        int * __restrict__ Out,
        short * __restrict__ Filter,
	int FilterConf
	)

{
        int V0,V1;
        int C0,C1;
        short int *PtIn = In+Wo_F*1-PadL;
        int *PtOut = Out;

        if (FilterConf) { /* Bottom Side */
                C0 = Filter[0]; C1 = Filter[1];
        } else { /* Top Side */
                C0 = Filter[1]; C1 = Filter[2];
        }
        for (unsigned int i=Wo_F; i<Wo_L; i++) {
                int Acc = *PtOut;
                V0 = *(PtIn+0*W+0); V1 = *(PtIn+1*W+0); PtIn++;
                Acc += V0*C0; Acc += V1*C1;
                *PtOut = Acc; PtOut++;
        }
}

static void __attribute__ ((noinline)) KerConv2x3from3x3Stride1_V_DP_fp(
        short int * __restrict__ In,
        int W, int PadTOrg,
        int Wo, int Ho, int Ho_F, int Ho_L,
        int * __restrict__ Out,
        short int * __restrict__ Filter,
	int FilterConf
	)
{
	v2s V0, V1, V2;
	v2s C0, C1, C2;
	short int *PtIn;
	int *PtOut;
	int Bottom = Ho-Ho_L;

	if (FilterConf) {
		C0 = *((v2s*) (Filter + 0*3+0)); C1 = *((v2s*) (Filter + 1*3+0)); C2 = *((v2s*) (Filter + 2*3+0));
	} else {
		C0 = *((v2s*) (Filter + 0*3+1)); C1 = *((v2s*) (Filter + 1*3+1)); C2 = *((v2s*) (Filter + 2*3+1));
	}
	if (Ho_F==1) {
		PtIn = In; PtOut = Out; Ho_F = 0;
		V0 = (v2s){0,0};
		V1 = *((v2s *) PtIn); PtIn += W;
	} else  { // == 0
		PtIn = In + (Ho_F*1-PadTOrg)*W; PtOut = Out+Ho_F*Wo;
		V0 = *((v2s *) PtIn); PtIn += W;
		V1 = *((v2s *) PtIn); PtIn += W;
	}
	for (unsigned int i=Ho_F; i<Ho_L; i++) {
		int Acc = *PtOut;
		V2 = *((v2s *) PtIn); PtIn += W;
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc); Acc = gap_sumdotp2(V2, C2, Acc);
		*PtOut =  Acc; PtOut+=Wo;
		V0 = V1; V1 = V2;
	}
	if (Bottom) {
		int Acc = *PtOut;
		PtIn -= 2*W;
		V0 = *((v2s *) PtIn); PtIn += W;
		V1 = *((v2s *) PtIn);
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc);
		*PtOut =  Acc;
	}
}

static void __attribute__ ((noinline)) KerConv2x3from3x3Stride2_V_DP_fp(
        short int * __restrict__ In,
        int W, int PadTOrg,
        int Wo, int Ho, int Ho_F, int Ho_L,
        int * __restrict__ Out,
        short * __restrict__ Filter,
	int FilterConf
	)
{
	v2s V0, V1, V2;
	v2s C0, C1, C2;
	short int *PtIn;
	int *PtOut;
	int Bottom = Ho-Ho_L;

	if (FilterConf) {
		C0 = *((v2s*) (Filter + 0*3+0)); C1 = *((v2s*) (Filter + 1*3+0)); C2 = *((v2s*) (Filter + 2*3+0));
	} else {
		C0 = *((v2s*) (Filter + 0*3+1)); C1 = *((v2s*) (Filter + 1*3+1)); C2 = *((v2s*) (Filter + 2*3+1));
	}
	if (Ho_F==1) {
		PtIn = In; PtOut = Out; Ho_F = 0;
		V0 = (v2s){0,0};
	} else  { // == 0
		PtIn = In + (Ho_F*2-PadTOrg)*W; PtOut = Out+Ho_F*Wo;
		V0 = *((v2s *) PtIn); PtIn += W;
	}
	for (unsigned int i=Ho_F; i<Ho_L; i++) {
		int Acc = *PtOut;
		V1 = *((v2s *) PtIn); PtIn += W; V2 = *((v2s *) PtIn); PtIn += W;
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc); Acc = gap_sumdotp2(V2, C2, Acc);
		*PtOut =  Acc; PtOut+=Wo;
		V0 = V2;
	}
	if (Bottom) {
		int Acc = *PtOut;
		PtIn -= W;
		V0 = *((v2s *) PtIn); PtIn += W;
		V1 = *((v2s *) PtIn);;
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc);
		*PtOut =  Acc;
	}
}

static void __attribute__ ((noinline)) KerConv2x3from3x3StrideS_V_DP_fp(
        short int * __restrict__ In,
        int W, int PadTOrg,
        int Wo, int Ho, int Ho_F, int Ho_L,
	int Stride,
        int * __restrict__ Out,
        short * __restrict__ Filter,
	int FilterConf
	)
{
	v2s V0, V1, V2;
	v2s C0, C1, C2;
	short int *PtIn;
	int *PtOut;
	int Bottom = Ho-Ho_L;

	if (FilterConf) {
		C0 = *((v2s*) (Filter + 0*3+0)); C1 = *((v2s*) (Filter + 1*3+0)); C2 = *((v2s*) (Filter + 2*3+0));
	} else {
		C0 = *((v2s*) (Filter + 0*3+1)); C1 = *((v2s*) (Filter + 1*3+1)); C2 = *((v2s*) (Filter + 2*3+1));
	}
	if (Ho_F==1) {
		PtIn = In; PtOut = Out; Ho_F = 0;
		V0 = (v2s){0,0};
	} else  { // == 0
		PtIn = In + (Ho_F*Stride-PadTOrg)*W; PtOut = Out+Ho_F*Wo;
		V0 = *((v2s *) PtIn); PtIn += W;
	}
	for (unsigned int i=Ho_F; i<Ho_L; i++) {
		int Acc = *PtOut;
		V1 = *((v2s *) PtIn); PtIn += W; V2 = *((v2s *) PtIn); PtIn += (Stride-2)*W;
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc); Acc = gap_sumdotp2(V2, C2, Acc);
		*PtOut =  Acc; PtOut+=Wo;
		V0 = *((v2s *) PtIn); PtIn += W;
	}
	if (Bottom) {
		int Acc = *PtOut;
		PtIn -= W;
		V0 = *((v2s *) PtIn); PtIn += W;
		V1 = *((v2s *) PtIn);
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc);
		*PtOut =  Acc;
	}
}

static void __attribute__ ((noinline)) KerConv3x2from3x3Stride1_H_DP_fp(
        short int * __restrict__ In,
        int W, int PadL,
        int Wo, int Wo_F, int Wo_L,
        int * __restrict__ Out,
        short * __restrict__ Filter,
	int FilterConf
	)

{
	v2s X, Y, V0, V1, V2;
	v2s C0, C1, C2;
	short int *PtIn = In+Wo_F*1-PadL;
	int *PtOut = Out;

	if (FilterConf) {
		X = *((v2s *) &Filter[0*3+0]); Y = *((v2s *) &Filter[1*3+0]); C0 = __builtin_shuffle(X,Y,(v2s){0,2}); C1 = __builtin_shuffle(X,Y,(v2s){1,3}); C2 = gap_pack2(Filter[2], Filter[5]);
	} else {
		X = *((v2s *) &Filter[1*3+0]); Y = *((v2s *) &Filter[2*3+0]); C0 = __builtin_shuffle(X,Y,(v2s){0,2}); C1 = __builtin_shuffle(X,Y,(v2s){1,3}); C2 = gap_pack2(Filter[5], Filter[8]);
	}
	X = *((v2s *) (PtIn+0*W+0)); Y = *((v2s *) (PtIn+1*W+0)); V0 = __builtin_shuffle(X,Y,(v2s){0,2}); V1 = __builtin_shuffle(X,Y,(v2s){1,3});
	PtIn += 2;
	for (unsigned int i=Wo_F; i<Wo_L; i++) {
		int x0, x1, Acc = *PtOut;
		x0 = PtIn[0*W]; x1 = PtIn[1*W]; PtIn++;
		V2 = gap_pack2(x0,x1);
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc); Acc = gap_sumdotp2(V2, C2, Acc);
		*PtOut = Acc; PtOut++;
		V0=V1; V1=V2;
	}
}

static void __attribute__ ((noinline)) KerConv3x2from3x3Stride2_H_DP_fp(
        short int * __restrict__ In,
        int W, int PadL,
        int Wo, int Wo_F, int Wo_L,
        int * __restrict__ Out,
        short * __restrict__ Filter,
	int FilterConf
	)

{
	v2s X, Y, V0, V1, V2;
	v2s C0, C1, C2;
	short int *PtIn = In+Wo_F*2-PadL;
	int *PtOut = Out;

	if (FilterConf) {
		X = *((v2s *) &Filter[0*3+0]); Y = *((v2s *) &Filter[1*3+0]); C0 = __builtin_shuffle(X,Y,(v2s){0,2}); C1 = __builtin_shuffle(X,Y,(v2s){1,3}); C2 = gap_pack2(Filter[2], Filter[5]);
	} else {
		X = *((v2s *) &Filter[1*3+0]); Y = *((v2s *) &Filter[2*3+0]); C0 = __builtin_shuffle(X,Y,(v2s){0,2}); C1 = __builtin_shuffle(X,Y,(v2s){1,3}); C2 = gap_pack2(Filter[5], Filter[8]);
	}
	X = *((v2s *) (PtIn+0*W+0)); Y = *((v2s *) (PtIn+1*W+0)); V0 = __builtin_shuffle(X,Y,(v2s){0,2}); V1 = __builtin_shuffle(X,Y,(v2s){1,3});
	V0 = gap_pack2(PtIn[0], PtIn[W]); PtIn++;
	for (unsigned int i=Wo_F; i<Wo_L; i++) {
		int Acc = *PtOut;
		X = *((v2s *) (PtIn+0*W)); Y = *((v2s *) (PtIn+1*W)); V1 = __builtin_shuffle(X,Y,(v2s){0,2}); V2 = __builtin_shuffle(X,Y,(v2s){1,3}); PtIn+=2;
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc); Acc = gap_sumdotp2(V2, C2, Acc);
		*PtOut = Acc; PtOut++;
		V0=V2;
	}
}

static void __attribute__ ((noinline)) KerConv3x2from3x3StrideS_H_DP_fp(
        short int * __restrict__ In,
        int W, int PadL,
        int Wo, int Wo_F, int Wo_L,
	int Stride,
        int * __restrict__ Out,
        short * __restrict__ Filter,
	int FilterConf
	)

{
	int Fw = 3;
	v2s Cv0, Cv1;
	int C0, C1;
	int *PtOut = Out;
	In += Wo_F*Stride-PadL;

	if (FilterConf) {
		Cv0 =  *((v2s *) &Filter[0*3+0]); C0 = Filter[0*3+2];
		Cv1 =  *((v2s *) &Filter[1*3+0]); C1 = Filter[1*3+2];
	} else {
		Cv0 =  *((v2s *) &Filter[1*3+0]); C0 = Filter[1*3+2];
		Cv1 =  *((v2s *) &Filter[2*3+0]); C1 = Filter[2*3+2];
	}
	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		short int *PtI = In;
		v2s Iv0;
		int I;
		int Acc = *PtOut;
		Iv0 = *((v2s *) PtI); PtI+=2; I = *PtI; PtI+=W-(Fw-1);
		Acc = gap_sumdotp2(Iv0, Cv0, Acc); Acc += I*C0;
		Iv0 = *((v2s *) PtI); PtI+=2; I = *PtI; PtI+=W-(Fw-1);
		Acc = gap_sumdotp2(Iv0, Cv1, Acc); Acc += I*C1;
		*PtOut = Acc; PtOut++; In += Stride;
	}
}


static void __attribute__ ((noinline)) KerConv4x1from5x1StrideNx1_V_DP_fp(
        short int * __restrict__ In,
        int W, v4s PadOrg, v4s Pad,
        int Wo, int Ho, int Ho_F, int Ho_L,
        int * __restrict__ Out,
        short * __restrict__ Filter,
	int FilterConf
	)
{
        v2s V0, V1;
        v2s C0, C1;
        short int *PtIn;
	int *PtOut;

        switch (FilterConf) {
                case 2: // [0..4 x 0] => [2..4 x 0] PadL==2
                        C0 = *((v2s*) (Filter + 0*5+2)); C1 = gap_pack2(Filter[0*5+4], 0);
                        break;
                case 1: // [0..4 x 0] => [1..4 x 0] PadL==1
                        C0 = *((v2s*) (Filter + 0*5+1)); C1 = *((v2s*) (Filter + 0*5+3));
                        break;
                case 3: // [0..4 x 0] => [0..3 x 0] PadR==1
                        C0 = *((v2s*) (Filter + 0*5+0)); C1 = *((v2s*) (Filter + 0*5+2));
                        break;
                case 4: // [0..4 x 0] => [0..2 x 0] PadR==2
                        C0 = gap_pack2(0, Filter[0*5+0]); C1 = *((v2s*) (Filter + 0*5+1));
                        break;
        }
        PtIn = In + (Ho_F*1-PadOrg[2])*W; PtOut = Out+Ho_F*Wo;
        V0 = * (v2s *) PtIn; V1 = *((v2s *) PtIn + 1); PtIn += W;
        for (unsigned int i=Ho_F; i<Ho_L; i++) {
                int Acc = *PtOut;
                Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc);
                V0 = * (v2s *) PtIn; V1 = *((v2s *) PtIn + 1); PtIn += W;
                *PtOut =  Acc; PtOut+=Wo;
        }

}

static void __attribute__ ((noinline)) KerConv1x4from1x5Stride1xN_H_DP_fp(
        short int * __restrict__ In,
        int W, int PadL,
        int Wo, int Wo_F, int Wo_L,
        int * __restrict__ Out,
        short * __restrict__ Filter,
	int FilterConf
	)

{
        v2s V0, V1;
        v2s C0, C1;
        int x0,x1,x2,x3;
        short int *PtIn = In+Wo_F*1-PadL;
        int *PtOut = Out;

        switch (FilterConf) {
                case 2: // [0 x 0..4] => [0 x 2..4] PadT == 2
                        C0 = *((v2s*) (Filter + 0*5+2)); C1 = gap_pack2(Filter[0*5+4], 0);
                        break;
                case 1: // [0 x 0..4] => [0 x 1..4] PadT == 1
                        C0 = *((v2s*) (Filter + 0*5+1)); C1 = *((v2s*) (Filter + 0*5+3));
                        break;
                case 3: // [0 x 0..4] => [0 x 1..4] PadB == 1
                        C0 = *((v2s*) (Filter + 0*5+0)); C1 = *((v2s*) (Filter + 0*5+2));
                        break;
                case 4: // [0 x 0..4] => [0 x 2..4] PadB == 2
                        C0 = gap_pack2(0, Filter[0*5+0]); C1 = *((v2s*) (Filter + 0*5+1));
                        break;
        }
        x0 = *(PtIn+0*W+0); x1 = *(PtIn+1*W+0); x2 = *(PtIn+2*W+0); x3 = *(PtIn+3*W+0); V0 = gap_pack2(x0,x1); V1 = gap_pack2(x2,x3); PtIn++;
        for (unsigned int i=Wo_F; i<Wo_L; i++) {
                int Acc = *PtOut;
                Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc);
                x0 = *(PtIn+0*W+0); x1 = *(PtIn+1*W+0); x2 = *(PtIn+2*W+0); x3 = *(PtIn+3*W+0); V0 = gap_pack2(x0,x1); V1 = gap_pack2(x2,x3); PtIn++;
                *PtOut = Acc; PtOut++;
        }
}

static void __attribute__ ((noinline)) KerConv4x5from5x5Stride1_V_DP_fp(
        short int * __restrict__ In,
        int W, v4s PadOrg, v4s Pad,
        int Wo, int Ho, int Ho_F, int Ho_L,
        int * __restrict__ Out,
        short * __restrict__ Filter,
	int FilterConf
	)
{
	v2s V0, V1, V2, V3, V4, V5, V6, V7, V8, V9;
	v2s C0, C1, C2, C3, C4, C5, C6, C7, C8, C9;
	short int *PtIn;
	int *PtOut;
	int Bottom, PadT = Pad[2], PadTOrg = PadOrg[2], PadB = Pad[3];

        switch (FilterConf) {
                case 2: // [0..4 x 0..4] => [2..4 x 0..4] PadL == 2
                        C0 = *((v2s*) (Filter + 0*5+2)); C1 = gap_pack2(Filter[0*5+4], 0);
                        C2 = *((v2s*) (Filter + 1*5+2)); C3 = gap_pack2(Filter[1*5+4], 0);
                        C4 = *((v2s*) (Filter + 2*5+2)); C5 = gap_pack2(Filter[2*5+4], 0);
                        C6 = *((v2s*) (Filter + 3*5+2)); C7 = gap_pack2(Filter[3*5+4], 0);
                        C8 = *((v2s*) (Filter + 4*5+2)); C9 = gap_pack2(Filter[4*5+4], 0);
                        break;
                case 1: // [0..4 x 0..4] => [1..4 x 0..4] PadL == 1
                        C0 = *((v2s*) (Filter + 0*5+1)); C1 = *((v2s*) (Filter + 0*5+3));
                        C2 = *((v2s*) (Filter + 1*5+1)); C3 = *((v2s*) (Filter + 1*5+3));
                        C4 = *((v2s*) (Filter + 2*5+1)); C5 = *((v2s*) (Filter + 2*5+3));
                        C6 = *((v2s*) (Filter + 3*5+1)); C7 = *((v2s*) (Filter + 3*5+3));
                        C8 = *((v2s*) (Filter + 4*5+1)); C9 = *((v2s*) (Filter + 4*5+3));
                        break;
                case 3: // [0..4 x 0..4] => [0..3 x 0..4] PadR == 1
                        C0 = *((v2s*) (Filter + 0*5+0)); C1 = *((v2s*) (Filter + 0*5+2));
                        C2 = *((v2s*) (Filter + 1*5+0)); C3 = *((v2s*) (Filter + 1*5+2));
                        C4 = *((v2s*) (Filter + 2*5+0)); C5 = *((v2s*) (Filter + 2*5+2));
                        C6 = *((v2s*) (Filter + 3*5+0)); C7 = *((v2s*) (Filter + 3*5+2));
                        C8 = *((v2s*) (Filter + 4*5+0)); C9 = *((v2s*) (Filter + 4*5+2));
                        break;
                case 4: // [0..4 x 0..4] => [0..2 x 0..4] PadR == 2
                        C0 = gap_pack2(0, Filter[0*5+0]); C1 = *((v2s*) (Filter + 0*5+1));
                        C2 = gap_pack2(0, Filter[1*5+0]); C3 = *((v2s*) (Filter + 1*5+1));
                        C4 = gap_pack2(0, Filter[2*5+0]); C5 = *((v2s*) (Filter + 2*5+1));
                        C6 = gap_pack2(0, Filter[3*5+0]); C7 = *((v2s*) (Filter + 3*5+1));
                        C8 = gap_pack2(0, Filter[4*5+0]); C9 = *((v2s*) (Filter + 4*5+1));
                        break;
        }
	if (PadT==2) {
		PtIn = In; Ho_F = 0;
		V0 = (v2s){0,0}; V1 = (v2s){0,0};
		V2 = (v2s){0,0}; V3 = (v2s){0,0};
	} else if (PadT) { // == 1
		PtIn = In; Ho_F = 0;
		V0 = (v2s){0,0}; V1 = (v2s){0,0};
		V0 = *((v2s *) PtIn); PtIn += 2; V1 = *((v2s *) PtIn); PtIn += (W-2);
	} else {
		PtIn = In + (Ho_F*1-PadTOrg)*W;
		V0 = *((v2s *) PtIn); PtIn += 2; V1 = *((v2s *) PtIn); PtIn += (W-2);
		V2 = *((v2s *) PtIn); PtIn += 2; V3 = *((v2s *) PtIn); PtIn += (W-2);
	}
	PtOut = Out+Ho_F*Wo;
	V4 = *((v2s *) PtIn); PtIn += 2; V5 = *((v2s *) PtIn); PtIn += (W-2);
	V6 = *((v2s *) PtIn); PtIn += 2; V7 = *((v2s *) PtIn); PtIn += (W-2);
	for (unsigned int i=Ho_F; i<Ho_L; i++) {
		int Acc = *PtOut;
		V8 = *((v2s *) PtIn); PtIn += 2; V9 = *((v2s *) PtIn); PtIn += (W-2);
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc);
		Acc = gap_sumdotp2(V2, C2, Acc); Acc = gap_sumdotp2(V3, C3, Acc);
		Acc = gap_sumdotp2(V4, C4, Acc); Acc = gap_sumdotp2(V5, C5, Acc);
		Acc = gap_sumdotp2(V6, C6, Acc); Acc = gap_sumdotp2(V7, C7, Acc);
		Acc = gap_sumdotp2(V8, C8, Acc); Acc = gap_sumdotp2(V9, C9, Acc);
		*PtOut = Acc; PtOut+=Wo;
		V0 = V2; V1 = V3; V2 = V4; V3 = V5; V4 = V6; V5 = V7; V6 = V8; V7 = V9;
	}
	if (PadB) {
		int Acc = *PtOut;
		PtIn -= 4*W;
		V0 = *((v2s *) PtIn); PtIn += 2; V1 = *((v2s *) PtIn); PtIn += (W-2);
		V2 = *((v2s *) PtIn); PtIn += 2; V3 = *((v2s *) PtIn); PtIn += (W-2);
		V4 = *((v2s *) PtIn); PtIn += 2; V5 = *((v2s *) PtIn); PtIn += (W-2);
		V6 = *((v2s *) PtIn); PtIn += 2; V7 = *((v2s *) PtIn); PtIn += (W-2);
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc);
		Acc = gap_sumdotp2(V2, C2, Acc); Acc = gap_sumdotp2(V3, C3, Acc);
		Acc = gap_sumdotp2(V4, C4, Acc); Acc = gap_sumdotp2(V5, C5, Acc);
		Acc = gap_sumdotp2(V6, C6, Acc); Acc = gap_sumdotp2(V7, C7, Acc);
		*PtOut = Acc; PtOut+=Wo;
		if (PadB==2) {
			Acc = *PtOut;
			V0 = V2; V1 = V3; V2 = V4; V3 = V5; V4 = V6; V5 = V7;
			Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc);
			Acc = gap_sumdotp2(V2, C2, Acc); Acc = gap_sumdotp2(V3, C3, Acc);
			Acc = gap_sumdotp2(V4, C4, Acc); Acc = gap_sumdotp2(V5, C5, Acc);
			*PtOut =  Acc;
		}
	}
}

static void __attribute__ ((noinline)) KerConv4x5from5x5Stride2_V_DP_fp(
        short int * __restrict__ In,
        int W, int H, v4s PadOrg, v4s Pad,
        int Wo, int Ho, int Ho_F, int Ho_L,
        int * __restrict__ Out,
        short * __restrict__ Filter,
	int FilterConf
	)
{
	v2s V0, V1, V2, V3, V4, V5, V6, V7, V8, V9;
	v2s C0, C1, C2, C3, C4, C5, C6, C7, C8, C9;
	short int *PtIn;
	int *PtOut;
	int PadL = PadOrg[0], PadT = Pad[2], PadTOrg = PadOrg[2], PadB = Pad[3];

        switch (FilterConf) {
                case 2: // [0..4 x 0..4] => [2..4 x 0..4] PadL==2
                        C0 = *((v2s*) (Filter + 0*5+2)); C1 = gap_pack2(Filter[0*5+4], 0);
                        C2 = *((v2s*) (Filter + 1*5+2)); C3 = gap_pack2(Filter[1*5+4], 0);
                        C4 = *((v2s*) (Filter + 2*5+2)); C5 = gap_pack2(Filter[2*5+4], 0);
                        C6 = *((v2s*) (Filter + 3*5+2)); C7 = gap_pack2(Filter[3*5+4], 0);
                        C8 = *((v2s*) (Filter + 4*5+2)); C9 = gap_pack2(Filter[4*5+4], 0);
                        break;
                case 1: // [0..4 x 0..4] => [1..4 x 0..4] PadL==1
                        C0 = *((v2s*) (Filter + 0*5+1)); C1 = *((v2s*) (Filter + 0*5+3));
                        C2 = *((v2s*) (Filter + 1*5+1)); C3 = *((v2s*) (Filter + 1*5+3));
                        C4 = *((v2s*) (Filter + 2*5+1)); C5 = *((v2s*) (Filter + 2*5+3));
                        C6 = *((v2s*) (Filter + 3*5+1)); C7 = *((v2s*) (Filter + 3*5+3));
                        C8 = *((v2s*) (Filter + 4*5+1)); C9 = *((v2s*) (Filter + 4*5+3));
                        break;
                case 3: // [0..4 x 0..4] => [0..3 x 0..4] PadR==1
                        C0 = *((v2s*) (Filter + 0*5+0)); C1 = *((v2s*) (Filter + 0*5+2));
                        C2 = *((v2s*) (Filter + 1*5+0)); C3 = *((v2s*) (Filter + 1*5+2));
                        C4 = *((v2s*) (Filter + 2*5+0)); C5 = *((v2s*) (Filter + 2*5+2));
                        C6 = *((v2s*) (Filter + 3*5+0)); C7 = *((v2s*) (Filter + 3*5+2));
                        C8 = *((v2s*) (Filter + 4*5+0)); C9 = *((v2s*) (Filter + 4*5+2));
                        break;
                case 4: // [0..4 x 0..4] => [0..2 x 0..4] PadR==2
                        C0 = *((v2s*) (Filter + 0*5+0)); C1 = gap_pack2(Filter[0*5+2], 0);
                        C2 = *((v2s*) (Filter + 1*5+0)); C3 = gap_pack2(Filter[1*5+2], 0);
                        C4 = *((v2s*) (Filter + 2*5+0)); C5 = gap_pack2(Filter[2*5+2], 0);
                        C6 = *((v2s*) (Filter + 3*5+0)); C7 = gap_pack2(Filter[3*5+2], 0);
                        C8 = *((v2s*) (Filter + 4*5+0)); C9 = gap_pack2(Filter[4*5+2], 0);
                        break;
        }
        if (PadT==2) {
                PtIn = In; Ho_F = 0;
                V0 = (v2s){0,0}; V1 = (v2s){0,0};
                V2 = (v2s){0,0}; V3 = (v2s){0,0};
        } else if (PadT) { // == 1
                PtIn = In; Ho_F = 0;
                V0 = (v2s){0,0}; V1 = (v2s){0,0};
                V2 = *((v2s *) PtIn); PtIn += 2; V3 = *((v2s *) PtIn); PtIn += (W-2);
        } else {
                PtIn = In + (Ho_F*2-PadTOrg)*W;
                V0 = *((v2s *) PtIn); PtIn += 2; V1 = *((v2s *) PtIn); PtIn += (W-2);
                V2 = *((v2s *) PtIn); PtIn += 2; V3 = *((v2s *) PtIn); PtIn += (W-2);
        }
        PtOut = Out+Ho_F*Wo;
	V4 = *((v2s *) PtIn); PtIn += 2; V5 = *((v2s *) PtIn); PtIn += (W-2);
	for (unsigned int i=Ho_F; i<Ho_L; i++) {
		int Acc = *PtOut;
		V6 = *((v2s *) PtIn); PtIn += 2; V7 = *((v2s *) PtIn); PtIn += (W-2);
		V8 = *((v2s *) PtIn); PtIn += 2; V9 = *((v2s *) PtIn); PtIn += (W-2);
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc);
		Acc = gap_sumdotp2(V2, C2, Acc); Acc = gap_sumdotp2(V3, C3, Acc);
		Acc = gap_sumdotp2(V4, C4, Acc); Acc = gap_sumdotp2(V5, C5, Acc);
		Acc = gap_sumdotp2(V6, C6, Acc); Acc = gap_sumdotp2(V7, C7, Acc);
		Acc = gap_sumdotp2(V8, C8, Acc); Acc = gap_sumdotp2(V9, C9, Acc);
		*PtOut =  Acc; PtOut+=Wo;
                V0 = V4; V1 = V5; V2 = V6; V3 = V7; V4 = V8; V5 = V9;
	}
	if (PadB) {
		int PadTOrg = PadOrg[2];
		int Acc = *PtOut;
		PtIn -= 3*W;
		V0 = *((v2s *) PtIn); PtIn += 2; V1 = *((v2s *) PtIn); PtIn += (W-2);
		V2 = *((v2s *) PtIn); PtIn += 2; V3 = *((v2s *) PtIn); PtIn += (W-2);
		V4 = *((v2s *) PtIn); PtIn += 2; V5 = *((v2s *) PtIn); PtIn += (W-2);
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc);
		Acc = gap_sumdotp2(V2, C2, Acc); Acc = gap_sumdotp2(V3, C3, Acc);
		Acc = gap_sumdotp2(V4, C4, Acc); Acc = gap_sumdotp2(V5, C5, Acc);
		if (PadB==1) {
			V6 = *((v2s *) PtIn); PtIn += 2; V7 = *((v2s *) PtIn);
			Acc = gap_sumdotp2(V6, C6, Acc); Acc = gap_sumdotp2(V7, C7, Acc);
		}
		*PtOut =  Acc;
	}
}

static void __attribute__ ((noinline)) KerConv4x5from5x5StrideS_V_DP_fp(
        short int * __restrict__ In,
        int W, int H, v4s PadOrg, v4s Pad,
        int Wo, int Ho, int Ho_F, int Ho_L,
	int Stride,
        int * __restrict__ Out,
        short * __restrict__ Filter,
	int FilterConf
	)
{
	v2s V0, V1, V2, V3, V4, V5, V6, V7, V8, V9;
	v2s C0, C1, C2, C3, C4, C5, C6, C7, C8, C9;
	short int *PtIn;
	int *PtOut;
	int PadL = PadOrg[0], PadT = Pad[2], PadTOrg = PadOrg[2], PadB = Pad[3];

        switch (FilterConf) {
                case 2: // [0..4 x 0..4] => [2..4 x 0..4] PadL==2
                        C0 = *((v2s*) (Filter + 0*5+2)); C1 = gap_pack2(Filter[0*5+4], 0);
                        C2 = *((v2s*) (Filter + 1*5+2)); C3 = gap_pack2(Filter[1*5+4], 0);
                        C4 = *((v2s*) (Filter + 2*5+2)); C5 = gap_pack2(Filter[2*5+4], 0);
                        C6 = *((v2s*) (Filter + 3*5+2)); C7 = gap_pack2(Filter[3*5+4], 0);
                        C8 = *((v2s*) (Filter + 4*5+2)); C9 = gap_pack2(Filter[4*5+4], 0);
                        break;
                case 1: // [0..4 x 0..4] => [1..4 x 0..4] PadL==1
                        C0 = *((v2s*) (Filter + 0*5+1)); C1 = *((v2s*) (Filter + 0*5+3));
                        C2 = *((v2s*) (Filter + 1*5+1)); C3 = *((v2s*) (Filter + 1*5+3));
                        C4 = *((v2s*) (Filter + 2*5+1)); C5 = *((v2s*) (Filter + 2*5+3));
                        C6 = *((v2s*) (Filter + 3*5+1)); C7 = *((v2s*) (Filter + 3*5+3));
                        C8 = *((v2s*) (Filter + 4*5+1)); C9 = *((v2s*) (Filter + 4*5+3));
                        break;
                case 3: // [0..4 x 0..4] => [0..3 x 0..4] PadR==1
                        C0 = *((v2s*) (Filter + 0*5+0)); C1 = *((v2s*) (Filter + 0*5+2));
                        C2 = *((v2s*) (Filter + 1*5+0)); C3 = *((v2s*) (Filter + 1*5+2));
                        C4 = *((v2s*) (Filter + 2*5+0)); C5 = *((v2s*) (Filter + 2*5+2));
                        C6 = *((v2s*) (Filter + 3*5+0)); C7 = *((v2s*) (Filter + 3*5+2));
                        C8 = *((v2s*) (Filter + 4*5+0)); C9 = *((v2s*) (Filter + 4*5+2));
                        break;
                case 4: // [0..4 x 0..4] => [0..2 x 0..4] PadR==2
                        C0 = *((v2s*) (Filter + 0*5+0)); C1 = gap_pack2(Filter[0*5+2], 0);
                        C2 = *((v2s*) (Filter + 1*5+0)); C3 = gap_pack2(Filter[1*5+2], 0);
                        C4 = *((v2s*) (Filter + 2*5+0)); C5 = gap_pack2(Filter[2*5+2], 0);
                        C6 = *((v2s*) (Filter + 3*5+0)); C7 = gap_pack2(Filter[3*5+2], 0);
                        C8 = *((v2s*) (Filter + 4*5+0)); C9 = gap_pack2(Filter[4*5+2], 0);
                        break;
        }

        if (PadT==2) {
                PtIn = In; Ho_F = 0;
                V0 = (v2s){0,0}; V1 = (v2s){0,0};
                V2 = (v2s){0,0}; V3 = (v2s){0,0};
        } else if (PadT) { // == 1
                PtIn = In; Ho_F = 0;
                V0 = (v2s){0,0}; V1 = (v2s){0,0};
                V2 = *((v2s *) PtIn); PtIn += 2; V3 = *((v2s *) PtIn); PtIn += (W-2);
        } else {
                PtIn = In + (Ho_F*2-PadTOrg)*W;
                V0 = *((v2s *) PtIn); PtIn += 2; V1 = *((v2s *) PtIn); PtIn += (W-2);
                V2 = *((v2s *) PtIn); PtIn += 2; V3 = *((v2s *) PtIn); PtIn += (W-2);
        }
        PtOut = Out+Ho_F*Wo;
	for (unsigned int i=Ho_F; i<Ho_L; i++) {
		int Acc = *PtOut;
		V4 = *((v2s *) PtIn); PtIn += 2; V5 = *((v2s *) PtIn); PtIn += (W-2);
		V6 = *((v2s *) PtIn); PtIn += 2; V7 = *((v2s *) PtIn); PtIn += (W-2);
		V8 = *((v2s *) PtIn); PtIn += 2; V9 = *((v2s *) PtIn); PtIn += ((Stride-4)*W)-2;
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc);
		Acc = gap_sumdotp2(V2, C2, Acc); Acc = gap_sumdotp2(V3, C3, Acc);
		Acc = gap_sumdotp2(V4, C4, Acc); Acc = gap_sumdotp2(V5, C5, Acc);
		Acc = gap_sumdotp2(V6, C6, Acc); Acc = gap_sumdotp2(V7, C7, Acc);
		Acc = gap_sumdotp2(V8, C8, Acc); Acc = gap_sumdotp2(V9, C9, Acc);
		*PtOut =  Acc; PtOut+=Wo;
		V0 = *((v2s *) PtIn); PtIn += 2; V1 = *((v2s *) PtIn); PtIn += (W-2);
		V2 = *((v2s *) PtIn); PtIn += 2; V3 = *((v2s *) PtIn); PtIn += (W-2);
	}
	if (PadB) {
		int PadTOrg = PadOrg[2];
		int Acc = *PtOut;
		PtIn -= 2*W;
		V0 = *((v2s *) PtIn); PtIn += 2; V1 = *((v2s *) PtIn); PtIn += (W-2);
		V2 = *((v2s *) PtIn); PtIn += 2; V3 = *((v2s *) PtIn); PtIn += (W-2);
		V4 = *((v2s *) PtIn); PtIn += 2; V5 = *((v2s *) PtIn); PtIn += (W-2);
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc);
		Acc = gap_sumdotp2(V2, C2, Acc); Acc = gap_sumdotp2(V3, C3, Acc);
		Acc = gap_sumdotp2(V4, C4, Acc); Acc = gap_sumdotp2(V5, C5, Acc);
		if (PadB==1) {
			V6 = *((v2s *) PtIn); PtIn += 2; V7 = *((v2s *) PtIn);
			Acc = gap_sumdotp2(V6, C6, Acc); Acc = gap_sumdotp2(V7, C7, Acc);
		}
		*PtOut =  Acc;
	}
}

static void __attribute__ ((noinline)) KerConv5x4from5x5Stride1_H_DP_fp(
        short int * __restrict__ In,
        int W, int PadL,
        int Wo, int Wo_F, int Wo_L,
        int * __restrict__ Out,
        short * __restrict__ Filter,
	int FilterConf
	)

{
	v2s X, Y, V0, V1, V2, V3, V4, V5, V6, V7, V8, V9;
	v2s C0, C1, C2, C3, C4, C5, C6, C7, C8, C9;
	short int *PtIn = In+Wo_F*1-PadL;
	int *PtOut = Out;

	switch (FilterConf) {
		case 2:
			X = *((v2s *) &Filter[2*5+0]); Y = *((v2s *) &Filter[3*5+0]); C0 = __builtin_shuffle(X,Y,(v2s){0,2}); C1 = __builtin_shuffle(X,Y,(v2s){1,3});
			X = *((v2s *) &Filter[2*5+2]); Y = *((v2s *) &Filter[3*5+2]); C2 = __builtin_shuffle(X,Y,(v2s){0,2}); C3 = __builtin_shuffle(X,Y,(v2s){1,3});
			C4 = gap_pack2(Filter[14], Filter[19]);
			C5 = gap_pack2(Filter[20],0); C6 = gap_pack2(Filter[21],0); C7 = gap_pack2(Filter[22],0); C8 = gap_pack2(Filter[23],0); C9 = gap_pack2(Filter[24],0);
			break;
		case 1:
			X = *((v2s *) &Filter[1*5+0]); Y = *((v2s *) &Filter[2*5+0]); C0 = __builtin_shuffle(X,Y,(v2s){0,2}); C1 = __builtin_shuffle(X,Y,(v2s){1,3});
			X = *((v2s *) &Filter[1*5+2]); Y = *((v2s *) &Filter[2*5+2]); C2 = __builtin_shuffle(X,Y,(v2s){0,2}); C3 = __builtin_shuffle(X,Y,(v2s){1,3});
			C4 = gap_pack2(Filter[9], Filter[14]);
			X = *((v2s *) &Filter[3*5+0]); Y = *((v2s *) &Filter[4*5+0]); C5 = __builtin_shuffle(X,Y,(v2s){0,2}); C6 = __builtin_shuffle(X,Y,(v2s){1,3});
			X = *((v2s *) &Filter[3*5+2]); Y = *((v2s *) &Filter[4*5+2]); C7 = __builtin_shuffle(X,Y,(v2s){0,2}); C8 = __builtin_shuffle(X,Y,(v2s){1,3});
			C9 = gap_pack2(Filter[19], Filter[24]);
			break;
		case 3:
			X = *((v2s *) &Filter[0*5+0]); Y = *((v2s *) &Filter[1*5+0]); C0 = __builtin_shuffle(X,Y,(v2s){0,2}); C1 = __builtin_shuffle(X,Y,(v2s){1,3});
			X = *((v2s *) &Filter[0*5+2]); Y = *((v2s *) &Filter[1*5+2]); C2 = __builtin_shuffle(X,Y,(v2s){0,2}); C3 = __builtin_shuffle(X,Y,(v2s){1,3});
			C4 = gap_pack2(Filter[4], Filter[9]);
			X = *((v2s *) &Filter[2*5+0]); Y = *((v2s *) &Filter[3*5+0]); C5 = __builtin_shuffle(X,Y,(v2s){0,2}); C6 = __builtin_shuffle(X,Y,(v2s){1,3});
			X = *((v2s *) &Filter[2*5+2]); Y = *((v2s *) &Filter[3*5+2]); C7 = __builtin_shuffle(X,Y,(v2s){0,2}); C8 = __builtin_shuffle(X,Y,(v2s){1,3});
			C9 = gap_pack2(Filter[14], Filter[19]);
			break;
		case 4:
			C0 = gap_pack2(0, Filter[0]); C1 = gap_pack2(0, Filter[1]); C2 = gap_pack2(0, Filter[2]); C3 = gap_pack2(0, Filter[3]); C4 = gap_pack2(0, Filter[4]);
			// X = *((v2s *) &Filter[2*5+0]); Y = *((v2s *) &Filter[3*5+0]); C5 = __builtin_shuffle(X,Y,(v2s){0,2}); C6 = __builtin_shuffle(X,Y,(v2s){1,3});
			// X = *((v2s *) &Filter[2*5+2]); Y = *((v2s *) &Filter[3*5+2]); C7 = __builtin_shuffle(X,Y,(v2s){0,2}); C8 = __builtin_shuffle(X,Y,(v2s){1,3});
			X = *((v2s *) &Filter[1*5+0]); Y = *((v2s *) &Filter[2*5+0]); C5 = __builtin_shuffle(X,Y,(v2s){0,2}); C6 = __builtin_shuffle(X,Y,(v2s){1,3});
			X = *((v2s *) &Filter[1*5+2]); Y = *((v2s *) &Filter[2*5+2]); C7 = __builtin_shuffle(X,Y,(v2s){0,2}); C8 = __builtin_shuffle(X,Y,(v2s){1,3});
			C9 = gap_pack2(Filter[9], Filter[14]);
			break;
	}
	X = *((v2s *) (PtIn+0*W+0)); Y = *((v2s *) (PtIn+1*W+0)); V0 = __builtin_shuffle(X,Y,(v2s){0,2}); V1 = __builtin_shuffle(X,Y,(v2s){1,3});
	X = *((v2s *) (PtIn+0*W+2)); Y = *((v2s *) (PtIn+1*W+2)); V2 = __builtin_shuffle(X,Y,(v2s){0,2}); V3 = __builtin_shuffle(X,Y,(v2s){1,3});
	X = *((v2s *) (PtIn+2*W+0)); Y = *((v2s *) (PtIn+3*W+0)); V5 = __builtin_shuffle(X,Y,(v2s){0,2}); V6 = __builtin_shuffle(X,Y,(v2s){1,3});
	X = *((v2s *) (PtIn+2*W+2)); Y = *((v2s *) (PtIn+3*W+2)); V7 = __builtin_shuffle(X,Y,(v2s){0,2}); V8 = __builtin_shuffle(X,Y,(v2s){1,3});
	PtIn += 4;
	for (unsigned int i=Wo_F; i<Wo_L; i++) {
		int x0, x1, x2, x3, Acc = *PtOut;
		x0 = PtIn[0*W]; x1 = PtIn[1*W]; x2 = PtIn[2*W]; x3 = PtIn[3*W]; PtIn++;
		V4 = gap_pack2(x0,x1); V9 = gap_pack2(x2,x3);
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc); Acc = gap_sumdotp2(V2, C2, Acc); Acc = gap_sumdotp2(V3, C3, Acc); Acc = gap_sumdotp2(V4, C4, Acc);
		Acc = gap_sumdotp2(V5, C5, Acc); Acc = gap_sumdotp2(V6, C6, Acc); Acc = gap_sumdotp2(V7, C7, Acc); Acc = gap_sumdotp2(V8, C8, Acc); Acc = gap_sumdotp2(V9, C9, Acc);
		*PtOut = Acc; PtOut++;
		V0=V1; V1=V2; V2=V3; V3=V4; V5=V6; V6=V7; V7=V8; V8=V9;
	}
}

static void __attribute__ ((noinline)) KerConv5x4from5x5Stride2_H_DP_fp(
        short int * __restrict__ In,
        int W, int H, int PadL, int PadT,
        int Wo, int Wo_F, int Wo_L,
        int * __restrict__ Out,
        short * __restrict__ Filter,
	int FilterConf
	)

{
	v2s X, Y, V0, V1, V2, V3, V4, V5, V6, V7, V8, V9;
	v2s C0, C1, C2, C3, C4, C5, C6, C7, C8, C9;
	short int *PtIn = In+Wo_F*2-PadL;
	int *PtOut = Out;

        switch (FilterConf) {
                case 2: // PadT == 2
                        X = *((v2s *) &Filter[2*5+0]); Y = *((v2s *) &Filter[3*5+0]); C0 = __builtin_shuffle(X,Y,(v2s){0,2}); C1 = __builtin_shuffle(X,Y,(v2s){1,3});
                        X = *((v2s *) &Filter[2*5+2]); Y = *((v2s *) &Filter[3*5+2]); C2 = __builtin_shuffle(X,Y,(v2s){0,2}); C3 = __builtin_shuffle(X,Y,(v2s){1,3});
                        C4 = gap_pack2(Filter[14], Filter[19]);
                        C5 = gap_pack2(Filter[20],0); C6 = gap_pack2(Filter[21],0); C7 = gap_pack2(Filter[22],0); C8 = gap_pack2(Filter[23],0); C9 = gap_pack2(Filter[24],0);
                        break;
                case 1: // PadT == 1
                        X = *((v2s *) &Filter[1*5+0]); Y = *((v2s *) &Filter[2*5+0]); C0 = __builtin_shuffle(X,Y,(v2s){0,2}); C1 = __builtin_shuffle(X,Y,(v2s){1,3});
                        X = *((v2s *) &Filter[1*5+2]); Y = *((v2s *) &Filter[2*5+2]); C2 = __builtin_shuffle(X,Y,(v2s){0,2}); C3 = __builtin_shuffle(X,Y,(v2s){1,3});
                        C4 = gap_pack2(Filter[9], Filter[14]);
                        X = *((v2s *) &Filter[3*5+0]); Y = *((v2s *) &Filter[4*5+0]); C5 = __builtin_shuffle(X,Y,(v2s){0,2}); C6 = __builtin_shuffle(X,Y,(v2s){1,3});
                        X = *((v2s *) &Filter[3*5+2]); Y = *((v2s *) &Filter[4*5+2]); C7 = __builtin_shuffle(X,Y,(v2s){0,2}); C8 = __builtin_shuffle(X,Y,(v2s){1,3});
                        C9 = gap_pack2(Filter[19], Filter[24]);
                        break;
                case 3: // PadB == 1
                        X = *((v2s *) &Filter[0*5+0]); Y = *((v2s *) &Filter[1*5+0]); C0 = __builtin_shuffle(X,Y,(v2s){0,2}); C1 = __builtin_shuffle(X,Y,(v2s){1,3});
                        X = *((v2s *) &Filter[0*5+2]); Y = *((v2s *) &Filter[1*5+2]); C2 = __builtin_shuffle(X,Y,(v2s){0,2}); C3 = __builtin_shuffle(X,Y,(v2s){1,3});
                        C4 = gap_pack2(Filter[4], Filter[9]);
                        X = *((v2s *) &Filter[2*5+0]); Y = *((v2s *) &Filter[3*5+0]); C5 = __builtin_shuffle(X,Y,(v2s){0,2}); C6 = __builtin_shuffle(X,Y,(v2s){1,3});
                        X = *((v2s *) &Filter[2*5+2]); Y = *((v2s *) &Filter[3*5+2]); C7 = __builtin_shuffle(X,Y,(v2s){0,2}); C8 = __builtin_shuffle(X,Y,(v2s){1,3});
                        C9 = gap_pack2(Filter[14], Filter[19]);
                        break;
                case 4: // PadB == 2
                        X = *((v2s *) &Filter[0*5+0]); Y = *((v2s *) &Filter[1*5+0]); C0 = __builtin_shuffle(X,Y,(v2s){0,2}); C1 = __builtin_shuffle(X,Y,(v2s){1,3});
                        X = *((v2s *) &Filter[0*5+2]); Y = *((v2s *) &Filter[1*5+2]); C2 = __builtin_shuffle(X,Y,(v2s){0,2}); C3 = __builtin_shuffle(X,Y,(v2s){1,3});
                        C4 = gap_pack2(Filter[4], Filter[9]);
                        C5 = gap_pack2(Filter[2*5+0], 0); C6 = gap_pack2(Filter[2*5+1], 0); C7 = gap_pack2(Filter[2*5+2], 0); C8 = gap_pack2(Filter[2*5+3], 0); C9 = gap_pack2(Filter[2*5+4], 0);
                        break;
        }

	X = *((v2s *) (PtIn+0*W+0)); Y = *((v2s *) (PtIn+1*W+0)); V0 = __builtin_shuffle(X,Y,(v2s){0,2}); V1 = __builtin_shuffle(X,Y,(v2s){1,3});
	X = *((v2s *) (PtIn+0*W+2)); Y = *((v2s *) (PtIn+1*W+2)); V2 = __builtin_shuffle(X,Y,(v2s){0,2});
	X = *((v2s *) (PtIn+2*W+0)); Y = *((v2s *) (PtIn+3*W+0)); V5 = __builtin_shuffle(X,Y,(v2s){0,2}); V6 = __builtin_shuffle(X,Y,(v2s){1,3});
	X = *((v2s *) (PtIn+2*W+2)); Y = *((v2s *) (PtIn+3*W+2)); V7 = __builtin_shuffle(X,Y,(v2s){0,2});
	PtIn += 3;
	for (unsigned int i=Wo_F; i<Wo_L; i++) {
		int Acc = *PtOut;
		X = *((v2s *) (PtIn+0*W+0)); Y = *((v2s *) (PtIn+1*W+0)); V3 = __builtin_shuffle(X,Y,(v2s){0,2}); V4 = __builtin_shuffle(X,Y,(v2s){1,3});
		X = *((v2s *) (PtIn+2*W+0)); Y = *((v2s *) (PtIn+3*W+0)); V8 = __builtin_shuffle(X,Y,(v2s){0,2}); V9 = __builtin_shuffle(X,Y,(v2s){1,3}); PtIn+=2;
		Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc); Acc = gap_sumdotp2(V2, C2, Acc); Acc = gap_sumdotp2(V3, C3, Acc); Acc = gap_sumdotp2(V4, C4, Acc);
		Acc = gap_sumdotp2(V5, C5, Acc); Acc = gap_sumdotp2(V6, C6, Acc); Acc = gap_sumdotp2(V7, C7, Acc); Acc = gap_sumdotp2(V8, C8, Acc); Acc = gap_sumdotp2(V9, C9, Acc);
		*PtOut = Acc; PtOut++;
		V0=V2; V1=V3; V2=V4; V5=V7; V6=V8; V7=V9;
	}
}

static void __attribute__ ((noinline)) KerConv5x4from5x5StrideS_H_DP_fp(
        short int * __restrict__ In,
        int W, int H, int PadL, int PadT,
        int Wo, int Wo_F, int Wo_L,
	int Stride,
        int * __restrict__ Out,
        short * __restrict__ Filter,
	int FilterConf
	)

{
	int Fw=5;
        v2s C0, C1, C3, C4, C6, C7, C9, C10;
        int C2, C5, C8, C11;
	int *PtOut = Out;

	In += Wo_F*Stride-PadL;
        switch (FilterConf) {
                case 2: // PadT == 2
                        C0 = *((v2s *) &Filter[2*5+0]); C1 = *((v2s *) &Filter[2*5+2]); C2 = Filter[2*5+4];
                        C3 = *((v2s *) &Filter[3*5+0]); C4 = *((v2s *) &Filter[3*5+2]); C5 = Filter[3*5+4];
                        C6 = *((v2s *) &Filter[4*5+0]); C7 = *((v2s *) &Filter[4*5+2]); C8 = Filter[4*5+4];
                        C9 = (v2s) {0,0}; C10 = (v2s) {0,0}; C11 = 0;
                        break;
                case 1: // PadT == 1
                        C0 = *((v2s *) &Filter[1*5+0]);  C1 = *((v2s *) &Filter[1*5+2]);  C2 = Filter[1*5+4];
                        C3 = *((v2s *) &Filter[2*5+0]);  C4 = *((v2s *) &Filter[2*5+2]);  C5 = Filter[2*5+4];
                        C6 = *((v2s *) &Filter[3*5+0]);  C7 = *((v2s *) &Filter[3*5+2]);  C8 = Filter[3*5+4];
                        C9 = *((v2s *) &Filter[4*5+0]); C10 = *((v2s *) &Filter[4*5+2]); C11 = Filter[4*5+4];
                        break;
                case 3: // PadB == 1
                        C0 = *((v2s *) &Filter[0*5+0]);  C1 = *((v2s *) &Filter[0*5+2]);  C2 = Filter[0*5+4];
                        C3 = *((v2s *) &Filter[1*5+0]);  C4 = *((v2s *) &Filter[1*5+2]);  C5 = Filter[1*5+4];
                        C6 = *((v2s *) &Filter[2*5+0]);  C7 = *((v2s *) &Filter[2*5+2]);  C8 = Filter[2*5+4];
                        C9 = *((v2s *) &Filter[3*5+0]); C10 = *((v2s *) &Filter[3*5+2]); C11 = Filter[3*5+4];
                        break;
                case 4: // PadB == 2
                        C0 = *((v2s *) &Filter[0*5+0]);  C1 = *((v2s *) &Filter[0*5+2]);  C2 = Filter[0*5+4];
                        C3 = *((v2s *) &Filter[1*5+0]);  C4 = *((v2s *) &Filter[1*5+2]);  C5 = Filter[1*5+4];
                        C6 = *((v2s *) &Filter[2*5+0]);  C7 = *((v2s *) &Filter[2*5+2]);  C8 = Filter[2*5+4];
                        C9 = (v2s) {0,0}; C10 = (v2s) {0,0}; C11 = 0;
                        break;
        }
        for (unsigned int w=Wo_F; w<Wo_L; w++) {
                short int *PtI = In;
                v2s Iv0, Iv1;
                int I;
                int Acc = *PtOut;
                Iv0 = *((v2s *) PtI); PtI+=2; Iv1 = *((v2s *) PtI); PtI+=2; I = *PtI; PtI+=W-(Fw-1);
                Acc = gap_sumdotp2(Iv0, C0, Acc); Acc = gap_sumdotp2(Iv1, C1, Acc); Acc += I*C2;

                Iv0 = *((v2s *) PtI); PtI+=2; Iv1 = *((v2s *) PtI); PtI+=2; I = *PtI; PtI+=W-(Fw-1);
                Acc = gap_sumdotp2(Iv0, C3, Acc); Acc = gap_sumdotp2(Iv1, C4, Acc); Acc += I*C5;

                Iv0 = *((v2s *) PtI); PtI+=2; Iv1 = *((v2s *) PtI); PtI+=2; I = *PtI; PtI+=W-(Fw-1);
                Acc = gap_sumdotp2(Iv0, C6, Acc); Acc = gap_sumdotp2(Iv1, C7, Acc); Acc += I*C8;

                Iv0 = *((v2s *) PtI); PtI+=2; Iv1 = *((v2s *) PtI); PtI+=2; I = *PtI; PtI+=W-(Fw-1);
                Acc = gap_sumdotp2(Iv0, C9, Acc); Acc = gap_sumdotp2(Iv1, C10, Acc); Acc += I*C11;

                *PtOut = Acc; PtOut++; In += Stride;

        }
}

void __attribute__ ((noinline)) KerConvNxNStrideS_Border_DP_fp(
	short int *__restrict__ In,
	int *__restrict__ Out,
	short int *__restrict__ Filter,
	int Fw,
	int Fh,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	int Stride,
	v4s Pad,
	v4s PadOrg
	)

{
	int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
	int PadROrg = PadOrg[1], PadBOrg = PadOrg[3];
	int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];
	int Hi_F = (Fh-1)/2 - PadTOrg;
	int Hi_L = Hi_F + (Ho_L-1)*Stride;	// iff Hi_L>Hi_F
	int Wi_F = (Fw-1)/2 - PadLOrg;
	int Wi_L = Wi_F + (Wo_L-1)*Stride;	// iff Wi_L>Wi_F

	if (PadT) { /* Top */
		int ht = PadTOrg, hb = H - Hi_F + Fh/2;
               	for (unsigned int h=0; h<Ho_F; h++) {
			int Fh_min = ht, Fh_max = MinCond(Fh, hb); // ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
        		for (unsigned int w=Wo_F; w<Wo_L; w++) {
                        	int Acc = Out[Wo*h+w];
                        	for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                	for (unsigned int j=0; j<Fw; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			ht -= Stride; hb -= Stride;
		}
	}
	if (PadB) { /* Bottom */
		int ht = 0, hb = H - (Hi_L+Stride) + Fh/2;
               	for (unsigned int h=Ho_L; h<Ho; h++) {
			int Fh_min = ht, Fh_max = MinCond(hb, Fh); // ht Can't be > F by definition of Ho_L so we can remove and use ht only
        		for (unsigned int w=Wo_F; w<Wo_L; w++) {
                        	int Acc = Out[Wo*h+w];
                        	for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                	for (unsigned int j=0; j<Fw; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			hb -= Stride;
		}
	}
	if (PadL) { /* Left */
		int wl = PadLOrg, wr = W - Wi_F + Fw/2;
               	for (unsigned int w=0; w<Wo_F; w++) {
			int Wh_min = wl, Wh_max = MinCond(Fw, wr); // wh Can't be < 0 by definition of Wo_F so we can remove and use wl only
        		for (unsigned int h=Ho_F; h<Ho_L; h++) {
                        	int Acc = Out[Wo*h+w];
                        	for (unsigned int i=0; i<Fh; i++) 
                                	for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			wl -= Stride; wr -= Stride;
		}
	}
	if (PadR) { /* Right */
		int wl = 0, wr = W - (Wi_L+Stride) + Fw/2;
               	for (unsigned int w=Wo_L; w<Wo; w++) {
			int Wh_min = wl, Wh_max = MinCond(wr, Fw); // ht Can't be > F by definition of Ho_L so we can remove and use ht only
        		for (unsigned int h=Ho_F; h<Ho_L; h++) {
                       		int Acc = Out[Wo*h+w];
                        	for (unsigned int i=0; i<Fh; i++) 
                                	for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			wr -= Stride;
		}
	}
	if (PadT) {
		if (PadL) { /* Upper left corner */
			int ht = PadTOrg, hb = H - Hi_F + Fh/2;
        		for (unsigned int h=0; h<Ho_F; h++) {
				int wl = PadLOrg, wr = W - Wi_F + Fw/2;
                		for (unsigned int w=0; w<Wo_F; w++) {
                        		int Acc = Out[Wo*h+w];
					// wh Can't be < 0 by definition of Wo_F so we can remove and use wl only. ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(Fw, wr), Fh_min = ht, Fh_max = MinCond(Fh, hb);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wl -= Stride; wr -= Stride;
				}
				ht -= Stride; hb -= Stride;
			}
		}
		if (PadR) { /* Upper right corner */
			int ht = PadTOrg, hb = H - Hi_F + Fh/2;
        		for (unsigned int h=0; h<Ho_F; h++) {
				int wl = 0, wr = W - (Wi_L+Stride) + Fw/2;
                		for (unsigned int w=Wo_L; w<Wo; w++) {
                        		int Acc = Out[Wo*h+w];
					// ht Can't be > F by definition of Ho_L so we can remove and use ht only. ht Can't be > F by definition of Ho_L so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(wr, Fw), Fh_min = ht, Fh_max = MinCond(Fh, hb);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wr -= Stride;
				}
				ht -= Stride; hb -= Stride;
			}
		}
	}
	if (PadB) {
		if (PadL) { /* Bottom Left corner */
			int ht = 0, hb = H - (Hi_L+Stride) + Fh/2;
        		for (unsigned int h=Ho_L; h<Ho; h++) {
				int wl = PadLOrg, wr = W - Wi_F + Fw/2;
                		for (unsigned int w=0; w<Wo_F; w++) {
                        		int Acc = Out[Wo*h+w];
 					// wh Can't be < 0 by definition of Wo_F so we can remove and use wl only.  ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(Fw, wr), Fh_min = ht, Fh_max = MinCond(hb, Fh);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wl -= Stride; wr -= Stride;
				}
				hb -= Stride;
			}
		}
		if (PadR) { /* Bottom Right corner */
			int ht = 0, hb = H - (Hi_L+Stride) + Fh/2;
        		for (unsigned int h=Ho_L; h<Ho; h++) {
				int wl = 0, wr = W - (Wi_L+Stride) + Fw/2;
                		for (unsigned int w=Wo_L; w<Wo; w++) {
                        		int Acc = Out[Wo*h+w];
 					// wh Can't be < 0 by definition of Wo_F so we can remove and use wl only.  ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(wr, Fw), Fh_min = ht, Fh_max = MinCond(hb, Fh);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*Stride-PadTOrg+i)*W + (w*Stride-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wr -= Stride;
				}
				hb -= Stride;
			}
		}
	}
}

void __attribute__ ((noinline)) KerConvNxMStrideSxSy_Border_DP_fp(
	short int *__restrict__ In,
	int *__restrict__ Out,
	short int *__restrict__ Filter,
	int Fw,
	int Fh,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	int StrideX,
	int StrideY,
	v4s Pad,
	v4s PadOrg
	)

{
	int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
	int PadROrg = PadOrg[1], PadBOrg = PadOrg[3];
	int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];
	int Hi_F = (Fh-1)/2 - PadTOrg;
	int Hi_L = Hi_F + (Ho_L-1)*StrideY;	// iff Hi_L>Hi_F
	int Wi_F = (Fw-1)/2 - PadLOrg;
	int Wi_L = Wi_F + (Wo_L-1)*StrideX;	// iff Wi_L>Wi_F

	if (PadT) { /* Top */
		int ht = PadTOrg, hb = H - Hi_F + Fh/2;
               	for (unsigned int h=0; h<Ho_F; h++) {
			int Fh_min = ht, Fh_max = MinCond(Fh, hb); // ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
        		for (unsigned int w=Wo_F; w<Wo_L; w++) {
                        	int Acc = Out[Wo*h+w];
                        	for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                	for (unsigned int j=0; j<Fw; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			ht -= StrideY; hb -= StrideY;
		}
	}
	if (PadB) { /* Bottom */
		int ht = 0, hb = H - (Hi_L+StrideY) + Fh/2;
               	for (unsigned int h=Ho_L; h<Ho; h++) {
			int Fh_min = ht, Fh_max = MinCond(hb, Fh); // ht Can't be > F by definition of Ho_L so we can remove and use ht only
        		for (unsigned int w=Wo_F; w<Wo_L; w++) {
                        	int Acc = Out[Wo*h+w];
                        	for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                	for (unsigned int j=0; j<Fw; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			hb -= StrideY;
		}
	}
	if (PadL) { /* Left */
		int wl = PadLOrg, wr = W - Wi_F + Fw/2;
               	for (unsigned int w=0; w<Wo_F; w++) {
			int Wh_min = wl, Wh_max = MinCond(Fw, wr); // wh Can't be < 0 by definition of Wo_F so we can remove and use wl only
        		for (unsigned int h=Ho_F; h<Ho_L; h++) {
                        	int Acc = Out[Wo*h+w];
                        	for (unsigned int i=0; i<Fh; i++) 
                                	for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			wl -= StrideX; wr -= StrideX;
		}
	}
	if (PadR) { /* Right */
		int wl = 0, wr = W - (Wi_L+StrideX) + Fw/2;
               	for (unsigned int w=Wo_L; w<Wo; w++) {
			int Wh_min = wl, Wh_max = MinCond(wr, Fw); // ht Can't be > F by definition of Ho_L so we can remove and use ht only
        		for (unsigned int h=Ho_F; h<Ho_L; h++) {
                       		int Acc = Out[Wo*h+w];
                        	for (unsigned int i=0; i<Fh; i++) 
                                	for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        	Out[Wo*h+w] = Acc;
			}
			wr -= StrideX;
		}
	}
	if (PadT) {
		if (PadL) { /* Upper left corner */
			int ht = PadTOrg, hb = H - Hi_F + Fh/2;
        		for (unsigned int h=0; h<Ho_F; h++) {
				int wl = PadLOrg, wr = W - Wi_F + Fw/2;
                		for (unsigned int w=0; w<Wo_F; w++) {
                        		int Acc = Out[Wo*h+w];
					// wh Can't be < 0 by definition of Wo_F so we can remove and use wl only. ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(Fw, wr), Fh_min = ht, Fh_max = MinCond(Fh, hb);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wl -= StrideX; wr -= StrideX;
				}
				ht -= StrideY; hb -= StrideY;
			}
		}
		if (PadR) { /* Upper right corner */
			int ht = PadTOrg, hb = H - Hi_F + Fh/2;
        		for (unsigned int h=0; h<Ho_F; h++) {
				int wl = 0, wr = W - (Wi_L+StrideX) + Fw/2;
                		for (unsigned int w=Wo_L; w<Wo; w++) {
                        		int Acc = Out[Wo*h+w];
					// ht Can't be > F by definition of Ho_L so we can remove and use ht only. ht Can't be > F by definition of Ho_L so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(wr, Fw), Fh_min = ht, Fh_max = MinCond(Fh, hb);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wr -= StrideX;
				}
				ht -= StrideY; hb -= StrideY;
			}
		}
	}
	if (PadB) {
		if (PadL) { /* Bottom Left corner */
			int ht = 0, hb = H - (Hi_L+StrideY) + Fh/2;
        		for (unsigned int h=Ho_L; h<Ho; h++) {
				int wl = PadLOrg, wr = W - Wi_F + Fw/2;
                		for (unsigned int w=0; w<Wo_F; w++) {
                        		int Acc = Out[Wo*h+w];
 					// wh Can't be < 0 by definition of Wo_F so we can remove and use wl only.  ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(Fw, wr), Fh_min = ht, Fh_max = MinCond(hb, Fh);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wl -= StrideX; wr -= StrideX;
				}
				hb -= StrideY;
			}
		}
		if (PadR) { /* Bottom Right corner */
			int ht = 0, hb = H - (Hi_L+StrideY) + Fh/2;
        		for (unsigned int h=Ho_L; h<Ho; h++) {
				int wl = 0, wr = W - (Wi_L+StrideX) + Fw/2;
                		for (unsigned int w=Wo_L; w<Wo; w++) {
                        		int Acc = Out[Wo*h+w];
 					// wh Can't be < 0 by definition of Wo_F so we can remove and use wl only.  ht Can't be < 0 by definition of Ho_F so we can remove and use ht only
					int Wh_min = wl, Wh_max = MinCond(wr, Fw), Fh_min = ht, Fh_max = MinCond(hb, Fh);
                        		for (unsigned int i=Fh_min; i<Fh_max; i++) 
                                		for (unsigned int j=Wh_min; j<Wh_max; j++) Acc += In[(h*StrideY-PadTOrg+i)*W + (w*StrideX-PadLOrg+j)]*Filter[Fw*i+j];
                        		Out[Wo*h+w] = Acc;
					wr -= StrideX;
				}
				hb -= StrideY;
			}
		}
	}
}

void __attribute__ ((noinline)) KerConvNxMDxDyStrideSxSy_Border_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int Fw,
        int Fh,
        int Dw,
        int Dh,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        int StrideX,
        int StrideY,
        v4s Pad,
        v4s PadOrg,
        int Norm
        )

{
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadROrg = PadOrg[1], PadBOrg = PadOrg[3];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];
        int TFw = Dw*(Fw-1)+1, TFh = Dh*(Fh-1)+1;
        int Hi_F = (TFh-1)/2 - PadTOrg;
        int Hi_L = Hi_F + (Ho_L-1)*StrideY;     // iff Hi_L>Hi_F
        int Wi_F = (TFw-1)/2 - PadLOrg;
        int Wi_L = Wi_F + (Wo_L-1)*StrideX;     // iff Wi_L>Wi_F
        int Prec=10;
        int InvDh = ((1<<Prec)+Dh-1)/Dh;
        int InvDw = ((1<<Prec)+Dw-1)/Dw;

        /*
        Here we assume that for a given filter output we don't have padding on both side of the input.
        Thanks to this assumption we can simplify a bit where filter starts and stops in the input.
        Either starts at 0 if (right or bottom) and stops at a place function of the padding or
        stops at Fw/Fh if (left or bottom) and starts  a place function of the padding
        */
        if (PadT) { /* Top */
                int ht = PadTOrg;
                for (unsigned int h=0; h<Ho_F; h++) {
                        int hta = gap_mulsN(ht-1, InvDh, Prec) + 1; // hta = (ht-1)/Dh+1
                        int Fh_min = hta;
                        for (unsigned int w=Wo_F; w<Wo_L; w++) {
                                int Acc = Out[Wo*h+w];
                                for (unsigned int i=Fh_min; i<Fh; i++)
                                        for (unsigned int j=0; j<Fw; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                Out[Wo*h+w] = Acc;
                        }
                        ht -= StrideY;
                }
        }
        if (PadB) { /* Bottom */
                int hb = H - (Hi_L+StrideY) + TFh/2;
                for (unsigned int h=Ho_L; h<Ho; h++) {
                        int hba = gap_mulsN(hb-1, InvDh, Prec) + 1; // hba = (hb-1)/Dh+1
                        int Fh_max = MinCond(hba, Fh);
                        for (unsigned int w=Wo_F; w<Wo_L; w++) {
                                int Acc = Out[Wo*h+w];
                                for (unsigned int i=0; i<Fh_max; i++)
                                        for (unsigned int j=0; j<Fw; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                Out[Wo*h+w] = Acc;
                        }
                        hb -= StrideY;
                }
        }
        if (PadL) { /* Left */
                int wl = PadLOrg;
                for (unsigned int w=0; w<Wo_F; w++) {
                        int wla = gap_mulsN(wl-1, InvDw, Prec) + 1; // wla = (wl-1)/Dw+1
                        int Wl_min = wla;
                        for (unsigned int h=Ho_F; h<Ho_L; h++) {
                                int Acc = Out[Wo*h+w];
                                for (unsigned int i=0; i<Fh; i++)
                                        for (unsigned int j=Wl_min; j<Fw; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                Out[Wo*h+w] = Acc;
                        }
                        wl -= StrideX;
                }
        }
        if (PadR) { /* Right */
                int wr = W - (Wi_L+StrideX) + TFw/2;
                for (unsigned int w=Wo_L; w<Wo; w++) {
                        int wra = gap_mulsN(wr-1, InvDw, Prec) + 1; // wra = (wr-1)/Dw+1
                        int Wr_max = MinCond(wra, Fw); // ht Can't be > F by definition of Ho_L so we can remove and use ht only
                        for (unsigned int h=Ho_F; h<Ho_L; h++) {
                                int Acc = Out[Wo*h+w];
                                for (unsigned int i=0; i<Fh; i++)
                                        for (unsigned int j=0; j<Wr_max; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                Out[Wo*h+w] = Acc;
                        }
                        wr -= StrideX;
                }
        }
        if (PadT) {
                if (PadL) { /* Upper left corner */
                        int ht = PadTOrg;
                        for (unsigned int h=0; h<Ho_F; h++) {
                                int wl = PadLOrg;
                                int hta = gap_mulsN(ht-1, InvDh, Prec) + 1; // hta = (ht-1)/Dh+1
                                for (unsigned int w=0; w<Wo_F; w++) {
                                        int Acc = Out[Wo*h+w];
                                        int wla = gap_mulsN(wl-1, InvDw, Prec) + 1; // wla = (wl-1)/Dw+1
                                        int Wl_min = wla, Fh_min = hta;
                                        for (unsigned int i=Fh_min; i<Fh; i++)
                                                for (unsigned int j=Wl_min; j<Fw; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                        Out[Wo*h+w] = Acc;
                                        wl -= StrideX;
                                }
                                ht -= StrideY;
                        }
                }
                if (PadR) { /* Upper right corner */
                        int ht = PadTOrg;
                        for (unsigned int h=0; h<Ho_F; h++) {
                                int wr = W - (Wi_L+StrideX) + TFw/2;
                                int hta = gap_mulsN(ht-1, InvDh, Prec) + 1; // hta = (ht-1)/Dh+1
                                for (unsigned int w=Wo_L; w<Wo; w++) {
                                        int Acc = Out[Wo*h+w];
                                        int wra = gap_mulsN(wr-1, InvDw, Prec) + 1; // wra = (wr-1)/Dw+1
                                        int Wr_max = MinCond(wra, Fw), Fh_min = hta;
                                        for (unsigned int i=Fh_min; i<Fh; i++)
                                                for (unsigned int j=0; j<Wr_max; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                        Out[Wo*h+w] = Acc;
                                        wr -= StrideX;
                                }
                                ht -= StrideY;
                        }
                }
        }
        if (PadB) {
                if (PadL) { /* Bottom Left corner */
                        int hb = H - (Hi_L+StrideY) + TFh/2;
                        for (unsigned int h=Ho_L; h<Ho; h++) {
                                int wl = PadLOrg;
                                int hba = gap_mulsN(hb-1, InvDh, Prec) + 1; // hba = (hb-1)/Dh+1
                                for (unsigned int w=0; w<Wo_F; w++) {
                                        int Acc = Out[Wo*h+w];
                                        int wla = gap_mulsN(wl-1, InvDw, Prec) + 1; // wla = (wl-1)/Dw+1
                                        int Wl_min = wla, Fh_max = MinCond(hba, Fh);
                                        for (unsigned int i=0; i<Fh_max; i++)
                                                for (unsigned int j=Wl_min; j<Fw; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                        Out[Wo*h+w] = Acc;
                                        wl -= StrideX;
                                }
                                hb -= StrideY;
                        }
                }
                if (PadR) { /* Bottom Right corner */
                        int hb = H - (Hi_L+StrideY) + TFh/2;
                        for (unsigned int h=Ho_L; h<Ho; h++) {
                                int wr = W - (Wi_L+StrideX) + TFw/2;
                                int hba = gap_mulsN(hb-1, InvDh, Prec) + 1; // hba = (hb-1)/Dh+1
                                for (unsigned int w=Wo_L; w<Wo; w++) {
                                        int Acc = Out[Wo*h+w];
                                        int wra = gap_mulsN(wr-1, InvDw, Prec) + 1; // wra = (wr-1)/Dw+1
                                        int Wr_max = MinCond(wra, Fw), Fh_max = MinCond(hba, Fh);
                                        for (unsigned int i=0; i<Fh_max; i++)
                                                for (unsigned int j=0; j<Wr_max; j++) Acc += In[(h*StrideY-PadTOrg+i*Dh)*W + (w*StrideX-PadLOrg+j*Dw)]*Filter[Fw*i+j];
                                        Out[Wo*h+w] = Acc;
                                        wr -= StrideX;
                                }
                                hb -= StrideY;
                        }
                }
        }
}


/* Wrapper functions */

static void __attribute__ ((noinline)) KerConv3x1BorderStrideNx1_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
	int Stride,
        v4s Pad,
        v4s PadOrg
	)

{
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadL) KerConv2x1from3x1StrideNx1_V_DP_fp(In, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Out, Filter, 0);
	if (PadR) KerConv2x1from3x1StrideNx1_V_DP_fp(In+Wo_L*Stride-PadLOrg, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, 1);
}

static void __attribute__ ((noinline)) KerConv1x3BorderStride1xN_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
	int Stride,
        v4s Pad,
        v4s PadOrg
        )

{
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadT) KerConv1x2from1x3Stride1xN_H_DP_fp(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, 0);
	if (PadB) KerConv1x2from1x3Stride1xN_H_DP_fp(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, 1);
}

static void __attribute__ ((noinline)) KerConv3x3BorderStride1_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad,
        v4s PadOrg
        )

{
	int Fh=3, Fw=3, Stride=1;
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadL) KerConv2x3from3x3Stride1_V_DP_fp(In, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Out, Filter, 0);
	if (PadR) KerConv2x3from3x3Stride1_V_DP_fp(In+Wo_L*Stride-PadLOrg, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, 1);
	if (PadT) KerConv3x2from3x3Stride1_H_DP_fp(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, 0);
	if (PadB) KerConv3x2from3x3Stride1_H_DP_fp(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, 1);
}

static void __attribute__ ((noinline)) KerConv3x3BorderStride2_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad,
        v4s PadOrg
        )

{
	int Fh=3, Fw=3, Stride=2;
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadL) KerConv2x3from3x3Stride2_V_DP_fp(In, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Out, Filter, 0);
	if (PadR) KerConv2x3from3x3Stride2_V_DP_fp(In+Wo_L*Stride-PadLOrg, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, 1);
	if (PadT) KerConv3x2from3x3Stride2_H_DP_fp(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, 0);
	if (PadB) KerConv3x2from3x3Stride2_H_DP_fp(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, 1);
}

static void __attribute__ ((noinline)) KerConv3x3BorderStrideS_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
	int Stride,
        v4s Pad,
        v4s PadOrg
        )

{
	int Fh=3, Fw=3;
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadL) KerConv2x3from3x3StrideS_V_DP_fp(In, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Stride, Out, Filter, 0);
	if (PadR) KerConv2x3from3x3StrideS_V_DP_fp(In+Wo_L*Stride-PadLOrg, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Stride, Out+Wo-1, Filter, 1);
	if (PadT) KerConv3x2from3x3StrideS_H_DP_fp(In, W, PadLOrg, Wo, Wo_F, Wo_L, Stride, Out+Wo_F, Filter, 0);
	if (PadB) KerConv3x2from3x3StrideS_H_DP_fp(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Stride, Out+Ho_L*Wo+Wo_F, Filter, 1);
}

static void __attribute__ ((noinline)) KerConv5x1BorderStrideNx1_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
	int Stride,
        v4s Pad,
        v4s PadOrg
        )

{
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

        if (PadL) {
                if (Wo_F==2) {
                        KerConv4x1from5x1StrideNx1_V_DP_fp(In, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out, Filter, 2);
                        KerConv4x1from5x1StrideNx1_V_DP_fp(In, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+1, Filter, 1);
                } else KerConv4x1from5x1StrideNx1_V_DP_fp(In, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out, Filter, PadL);
        }
        if (PadR) {
                if  ((Wo-Wo_L)==2) {
                        KerConv4x1from5x1StrideNx1_V_DP_fp(In+Wo_L*Stride-PadLOrg, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+Wo-2, Filter, 3);
                        KerConv4x1from5x1StrideNx1_V_DP_fp(In+Wo_L*Stride-PadLOrg, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, 4);
                } else KerConv4x1from5x1StrideNx1_V_DP_fp(In+Wo_L*Stride-PadLOrg, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, PadR+2);
        }
}

static void __attribute__ ((noinline)) KerConv1x5BorderStride1xN_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
	int Stride,
        v4s Pad,
        v4s PadOrg
        )

{
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

        if (PadT) {
                if(Ho_F==2) {
                        KerConv1x4from1x5Stride1xN_H_DP_fp(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, 2);
                        KerConv1x4from1x5Stride1xN_H_DP_fp(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F+Wo, Filter, 1);
                } else KerConv1x4from1x5Stride1xN_H_DP_fp(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, PadT);
        }
        if (PadB) {
                if((Ho-Ho_L)==2) {
                        KerConv1x4from1x5Stride1xN_H_DP_fp(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, 3);
                        KerConv1x4from1x5Stride1xN_H_DP_fp(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+(Ho_L+1)*Wo+Wo_F, Filter, 4);
                } else KerConv1x4from1x5Stride1xN_H_DP_fp(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, PadB+2);
        }
}

static void __attribute__ ((noinline)) KerConv5x5BorderStride1_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad,
        v4s PadOrg
        )

{
	int Fh=5, Fw=5, Stride=1;
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadL==2) {
		KerConv4x5from5x5Stride1_V_DP_fp(In, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out, Filter, 2);
		KerConv4x5from5x5Stride1_V_DP_fp(In, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+1, Filter, 1);
	} else if (PadL) KerConv4x5from5x5Stride1_V_DP_fp(In, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out, Filter, 1);
	if (PadR==2) {
		KerConv4x5from5x5Stride1_V_DP_fp(In+Wo_L*Stride-PadLOrg, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+Wo-2, Filter, 3);
		KerConv4x5from5x5Stride1_V_DP_fp(In+Wo_L*Stride-PadLOrg, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, 4);
	} else if (PadR) KerConv4x5from5x5Stride1_V_DP_fp(In+Wo_L*Stride-PadLOrg, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, 3);
	if (PadT==2) {
		KerConv5x4from5x5Stride1_H_DP_fp(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, 2);
		KerConv5x4from5x5Stride1_H_DP_fp(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F+Wo, Filter, 1);
	} else if (PadT) KerConv5x4from5x5Stride1_H_DP_fp(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, 1);
	if (PadB==2) {
		KerConv5x4from5x5Stride1_H_DP_fp(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, 3);
		KerConv5x4from5x5Stride1_H_DP_fp(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+(Ho_L+1)*Wo+Wo_F, Filter, 4);
	} else if (PadB) KerConv5x4from5x5Stride1_H_DP_fp(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, 3);
}

static void __attribute__ ((noinline)) KerConv5x5BorderStride2_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad,
        v4s PadOrg
        )

{
	int Fh=5, Fw=5, Stride=2;
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadL) KerConv4x5from5x5Stride2_V_DP_fp(In, W, H, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out, Filter, PadL);
	if (PadR) KerConv4x5from5x5Stride2_V_DP_fp(In+Wo_L*Stride-PadLOrg, W, H, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, PadR+2);

	if (PadT) KerConv5x4from5x5Stride2_H_DP_fp(In, W, H, PadLOrg, PadTOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, PadT);
	if (PadB) KerConv5x4from5x5Stride2_H_DP_fp(In+(Ho_L*Stride-PadTOrg)*W, W, H, PadLOrg, PadTOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, PadB+2);
}

static void __attribute__ ((noinline)) KerConv5x5BorderStrideS_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
	int Stride,
        v4s Pad,
        v4s PadOrg
        )

{
	int Fh=5, Fw=5;
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadL) KerConv4x5from5x5StrideS_V_DP_fp(In, W, H, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Stride, Out, Filter, PadL);
	if (PadR) KerConv4x5from5x5StrideS_V_DP_fp(In+Wo_L*Stride-PadLOrg, W, H, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Stride, Out+Wo-1, Filter, PadR+2);

	if (PadT) KerConv5x4from5x5StrideS_H_DP_fp(In, W, H, PadLOrg, PadTOrg, Wo, Wo_F, Wo_L, Stride, Out+Wo_F, Filter, PadT);
	if (PadB) KerConv5x4from5x5StrideS_H_DP_fp(In+(Ho_L*Stride-PadTOrg)*W, W, H, PadLOrg, PadTOrg, Wo, Wo_F, Wo_L, Stride, Out+Ho_L*Wo+Wo_F, Filter, PadB+2);
}

static void __attribute__ ((noinline)) KerConv3x1BorderStrideNx1_DP_fps(
        signed char *__restrict__ In,
        short int *__restrict__ Out,
        signed char *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
	int Stride,
        v4s Pad,
        v4s PadOrg
        )

{
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadL) KerConv2x1from3x1StrideNx1_V_DP_fps(In, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Out, Filter, 0);
	if (PadR) KerConv2x1from3x1StrideNx1_V_DP_fps(In+Wo_L*Stride-PadLOrg, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, 1);
}

static void __attribute__ ((noinline)) KerConv1x3BorderStride1xN_DP_fps(
        signed char *__restrict__ In,
        short int *__restrict__ Out,
        signed char *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
	int Stride,
        v4s Pad,
        v4s PadOrg
        )

{
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadT) KerConv1x2from1x3Stride1xN_H_DP_fps(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, 0);
	if (PadB) KerConv1x2from1x3Stride1xN_H_DP_fps(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, 1);
}

static void __attribute__ ((noinline)) KerConv3x3BorderStride1_DP_fps(
        signed char *__restrict__ In,
        short int *__restrict__ Out,
        signed char *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad,
        v4s PadOrg
        )

{
	int Fh=3, Fw=3, Stride=1;
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadL) KerConv2x3from3x3Stride1_V_DP_fps(In, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Out, Filter, 0);
	if (PadR) KerConv2x3from3x3Stride1_V_DP_fps(In+Wo_L*Stride-PadLOrg, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, 1);
	if (PadT) KerConv3x2from3x3Stride1_H_DP_fps(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, 0);
	if (PadB) KerConv3x2from3x3Stride1_H_DP_fps(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, 1);
}

static void __attribute__ ((noinline)) KerConv3x3BorderStride2_DP_fps(
        signed char *__restrict__ In,
        short int *__restrict__ Out,
        signed char *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad,
        v4s PadOrg
        )

{
	int Fh=3, Fw=3, Stride=2;
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadL) KerConv2x3from3x3Stride2_V_DP_fps(In, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Out, Filter, 0);
	if (PadR) KerConv2x3from3x3Stride2_V_DP_fps(In+Wo_L*Stride-PadLOrg, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, 1);
	if (PadT) KerConv3x2from3x3Stride2_H_DP_fps(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, 0);
	if (PadB) KerConv3x2from3x3Stride2_H_DP_fps(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, 1);
}

static void __attribute__ ((noinline)) KerConv3x3BorderStrideS_DP_fps(
        signed char *__restrict__ In,
        short int *__restrict__ Out,
        signed char *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
	int Stride,
        v4s Pad,
        v4s PadOrg
        )

{
	int Fh=3, Fw=3;
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadL) KerConv2x3from3x3StrideS_V_DP_fps(In, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Stride, Out, Filter, 0);
	if (PadR) KerConv2x3from3x3StrideS_V_DP_fps(In+Wo_L*Stride-PadLOrg, W, PadTOrg, Wo, Ho, Ho_F, Ho_L, Stride, Out+Wo-1, Filter, 1);
	if (PadT) KerConv3x2from3x3StrideS_H_DP_fps(In, W, PadLOrg, Wo, Wo_F, Wo_L, Stride, Out+Wo_F, Filter, 0);
	if (PadB) KerConv3x2from3x3StrideS_H_DP_fps(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Stride, Out+Ho_L*Wo+Wo_F, Filter, 1);
}

static void __attribute__ ((noinline)) KerConv5x1BorderStrideNx1_DP_fps(
        signed char *__restrict__ In,
        short int *__restrict__ Out,
        signed char *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
	int Stride,
        v4s Pad,
        v4s PadOrg
        )

{
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];
        if (PadL) {
                if (Wo_F==2) {
                        KerConv4x1from5x1StrideNx1_V_DP_fps(In, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out, Filter, 2);
                        KerConv4x1from5x1StrideNx1_V_DP_fps(In, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+1, Filter, 1);
                } else KerConv4x1from5x1StrideNx1_V_DP_fps(In, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out, Filter, PadL);
        }
        if (PadR) {
                if ((Wo-Wo_L)==2) {
                        KerConv4x1from5x1StrideNx1_V_DP_fps(In+Wo_L*Stride-PadLOrg, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+Wo-2, Filter, 3);
                        KerConv4x1from5x1StrideNx1_V_DP_fps(In+Wo_L*Stride-PadLOrg, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, 4);
                } else KerConv4x1from5x1StrideNx1_V_DP_fps(In+Wo_L*Stride-PadLOrg, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, PadR+2);
        }
}

static void __attribute__ ((noinline)) KerConv1x5BorderStride1xN_DP_fps(
        signed char *__restrict__ In,
        short int *__restrict__ Out,
        signed char *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
	int Stride,
        v4s Pad,
        v4s PadOrg
        )

{
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

        if (PadT) {
                if (Ho_F==2) { // Happens only if stride = 1
                        KerConv1x4from1x5Stride1xN_H_DP_fps(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, 2);
                        KerConv1x4from1x5Stride1xN_H_DP_fps(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F+Wo, Filter, 1);
                } else KerConv1x4from1x5Stride1xN_H_DP_fps(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, PadT);
        }
        if (PadB) {
                if ((Ho-Ho_L)==2) { // Happens only if stride == 1
                        KerConv1x4from1x5Stride1xN_H_DP_fps(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, 3);
                        KerConv1x4from1x5Stride1xN_H_DP_fps(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+(Ho_L+1)*Wo+Wo_F, Filter, 4);
                } else KerConv1x4from1x5Stride1xN_H_DP_fps(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, PadB+2);
        }
}

static void __attribute__ ((noinline)) KerConv5x5BorderStride1_DP_fps(
        signed char *__restrict__ In,
        short int *__restrict__ Out,
        signed char *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad,
        v4s PadOrg
        )

{
	int Fh=5, Fw=5, Stride=1;
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadL==2) {
		KerConv4x5from5x5Stride1_V_DP_fps(In, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out, Filter, 2);
		KerConv4x5from5x5Stride1_V_DP_fps(In, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+1, Filter, 1);
	} else if (PadL==1) KerConv4x5from5x5Stride1_V_DP_fps(In, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out, Filter, 1);
	if (PadR==2) {
		KerConv4x5from5x5Stride1_V_DP_fps(In+Wo_L*Stride-PadLOrg, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+Wo-2, Filter, 3);
		KerConv4x5from5x5Stride1_V_DP_fps(In+Wo_L*Stride-PadLOrg, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, 4);
	} else if (PadR==1) KerConv4x5from5x5Stride1_V_DP_fps(In+Wo_L*Stride-PadLOrg, W, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, 3);
	if (PadT==2) {
		KerConv5x4from5x5Stride1_H_DP_fps(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, 2);
		KerConv5x4from5x5Stride1_H_DP_fps(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F+Wo, Filter, 1);
	} else if (PadT==1) KerConv5x4from5x5Stride1_H_DP_fps(In, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, 1);
	if (PadB==2) {
		KerConv5x4from5x5Stride1_H_DP_fps(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, 3);
		KerConv5x4from5x5Stride1_H_DP_fps(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+(Ho_L+1)*Wo+Wo_F, Filter, 4);
	} else if (PadB==1) KerConv5x4from5x5Stride1_H_DP_fps(In+(Ho_L*Stride-PadTOrg)*W, W, PadLOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, 3);
}

static void __attribute__ ((noinline)) KerConv5x5BorderStride2_DP_fps(
        signed char *__restrict__ In,
        short int *__restrict__ Out,
        signed char *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad,
        v4s PadOrg
        )

{
	int Fh=5, Fw=5, Stride=2;
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadL) KerConv4x5from5x5Stride2_V_DP_fps(In, W, H, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out, Filter, PadL);
	if (PadR) KerConv4x5from5x5Stride2_V_DP_fps(In+Wo_L*Stride-PadLOrg, W, H, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Out+Wo-1, Filter, PadR+2);

	if (PadT) KerConv5x4from5x5Stride2_H_DP_fps(In, W, H, PadLOrg, PadTOrg, Wo, Wo_F, Wo_L, Out+Wo_F, Filter, PadT);
	if (PadB) KerConv5x4from5x5Stride2_H_DP_fps(In+(Ho_L*Stride-PadTOrg)*W, W, H, PadLOrg, PadTOrg, Wo, Wo_F, Wo_L, Out+Ho_L*Wo+Wo_F, Filter, PadB+2);
}

static void __attribute__ ((noinline)) KerConv5x5BorderStrideS_DP_fps(
        signed char *__restrict__ In,
        short int *__restrict__ Out,
        signed char *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
	int Stride,
        v4s Pad,
        v4s PadOrg
        )

{
	int Fh=5, Fw=5;
        int PadLOrg = PadOrg[0], PadTOrg = PadOrg[2];
        int PadL = Pad[0], PadR = Pad[1], PadT = Pad[2], PadB = Pad[3];

	if (PadL) KerConv4x5from5x5StrideS_V_DP_fps(In, W, H, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Stride, Out, Filter, PadL);
	if (PadR) KerConv4x5from5x5StrideS_V_DP_fps(In+Wo_L*Stride-PadLOrg, W, H, PadOrg, Pad, Wo, Ho, Ho_F, Ho_L, Stride, Out+Wo-1, Filter, PadR+2);

	if (PadT) KerConv5x4from5x5StrideS_H_DP_fps(In, W, H, PadLOrg, PadTOrg, Wo, Wo_F, Wo_L, Stride, Out+Wo_F, Filter, PadT);
	if (PadB) KerConv5x4from5x5StrideS_H_DP_fps(In+(Ho_L*Stride-PadTOrg)*W, W, H, PadLOrg, PadTOrg, Wo, Wo_F, Wo_L, Stride, Out+Ho_L*Wo+Wo_F, Filter, PadB+2);
}

/* Convolution, body processing (covers both padded and non padded variants)

	Input feature maps, Output feature maps and filter on bytes

		KerConv1x1Stride1_Body_DP_fps		1x1 convolution, stride 1
		KerConv1x1Stride2_Body_DP_fps		1x1 convolution, stride 2
		KerConv1x1StrideS_Body_DP_fps		1x1 convolution, stride S

		KerConv3x1Stride1x1_Body_DP_fps		3x1 convolution, stride 1x1
		KerConv3x1Stride2x1_Body_DP_fps		3x1 convolution, stride 2x1
		KerConv1x3Stride1x1_Body_DP_fps		1x3 convolution, stride 1x1
		KerConv1x3Stride1x2_Body_DP_fps		1x3 convolution, stride 1x2
		KerConv3x3Stride1_Body_DP_fps		3x3 convolution, stride 1
		KerConv3x3Stride2_Body_DP_fps		3x3 convolution, stride 2
		KerConv3x3StrideS_Body_DP_fps		3x3 convolution, stride S

		KerConv5x1Stride1x1_Body_DP_fps		5x1 convolution, stride 1x1
		KerConv5x1Stride2x1_Body_DP_fps		5x1 convolution, stride 2x1
		KerConv1x5Stride1x1_Body_DP_fps		1x5 convolution, stride 1x1
		KerConv1x5Stride1x2_Body_DP_fps		1x5 convolution, stride 1x2
		KerConv5x5Stride1_Body_DP_fps		5x5 convolution, stride 1
		KerConv5x5Stride2_Body_DP_fps		5x5 convolution, stride 2
		KerConv5x5StrideS_Body_DP_fps		5x5 convolution, stride S

		KerConvNxNStrideS_Body_DP_fps		NxN convolution, stride S
		KerConvNxMStrideSxSy_Body_DP_fps	NxM convolution, stride Sx, Sy

		KerConvNxMDxDyStrideSxSy_Body_DP_fps	NxM convolution, dilation Dx,Dy, stride Sx, Sy
		

	Input feature maps, Output feature maps and filter on half words

		KerConv1x1Stride1_Body_DP_fp		1x1 convolution, stride 1
		KerConv1x1Stride2_Body_DP_fp		1x1 convolution, stride 2
		KerConv1x1StrideS_Body_DP_fp		1x1 convolution, stride S

		KerConv3x1Stride1x1_Body_DP_fp		3x1 convolution, stride 1x1
		KerConv3x1Stride2x1_Body_DP_fp		3x1 convolution, stride 2x1
		KerConv1x3Stride1x1_Body_DP_fp		1x3 convolution, stride 1x1
		KerConv1x3Stride1x2_Body_DP_fp		1x3 convolution, stride 1x2
		KerConv3x3Stride1_Body_DP_fp		3x3 convolution, stride 1
		KerConv3x3Stride2_Body_DP_fp		3x3 convolution, stride 2
		KerConv3x3StrideS_Body_DP_fp		3x3 convolution, stride S

		KerConv5x1Stride1x1_Body_DP_fp		5x1 convolution, stride 1x1
		KerConv5x1Stride2x1_Body_DP_fp		5x1 convolution, stride 2x1
		KerConv1x5Stride1x1_Body_DP_fp		1x5 convolution, stride 1x1
		KerConv1x5Stride1x2_Body_DP_fp		1x5 convolution, stride 1x2
		KerConv5x5Stride1_Body_DP_fp		5x5 convolution, stride 1
		KerConv5x5Stride2_Body_DP_fp		5x5 convolution, stride 2
		KerConv5x5StrideS_Body_DP_fp		5x5 convolution, stride S

		KerConvNxNStrideS_Body_DP_fp		NxN convolution, stride S
		KerConvNxMStrideSxSy_Body_DP_fp		NxM convolution, stride Sx, Sy

		KerConvNxMDxDyStrideSxSy_Body_DP_fp	NxM convolution, dilation Dx,Dy, stride Sx, Sy
*/

static void __attribute__ ((noinline)) KerConv1x1Stride1_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
	unsigned short int Stride = 1;
	unsigned short int PadL = Pad[0], PadT = Pad[2];

	int C0 = Filter[0];
	int IterW = Wo_L-Wo_F;

	switch (IterW&0x3) {
		case 0: for (unsigned int h=Ho_F; h<Ho_L; h++) {
				v2s *LineOut = (v2s *) (&Out[Wo*h+Wo_F]);
				v4s *LineIn = (v4s *) (In + (h*Stride-PadT)*W + (Wo_F*Stride-PadL));
				for (unsigned int w=0; w<(IterW/4); w++) {
					v2s O0 = LineOut[2*w], O1 = LineOut[2*w+1];
					v4s I = LineIn[w];
                                	int Acc0 = O0[0], Acc1 = O0[1], Acc2 = O1[0], Acc3 = O1[1];
                                	Acc0 = gap_macs(Acc0, I[0], C0); Acc1 = gap_macs(Acc1, I[1], C0);
                                	Acc2 = gap_macs(Acc2, I[2], C0); Acc3 = gap_macs(Acc3, I[3], C0);
                                	LineOut[2*w] =  gap_pack2(Acc0, Acc1); LineOut[2*w+1] = gap_pack2(Acc2, Acc3);
				}
			}
			break;
		case 1: for (unsigned int h=Ho_F; h<Ho_L; h++) {
				v2s *LineOut = (v2s *) (&Out[Wo*h+Wo_F]);
				v4s *LineIn = (v4s *) (In + (h*Stride-PadT)*W + (Wo_F*Stride-PadL));
				for (unsigned int w=0; w<(IterW/4); w++) {
					v2s O0 = LineOut[2*w], O1 = LineOut[2*w+1];
					v4s I = LineIn[w];
                                	int Acc0 = O0[0], Acc1 = O0[1], Acc2 = O1[0], Acc3 = O1[1];
                                	Acc0 = gap_macs(Acc0, I[0], C0); Acc1 = gap_macs(Acc1, I[1], C0);
                                	Acc2 = gap_macs(Acc2, I[2], C0); Acc3 = gap_macs(Acc3, I[3], C0);
                                	LineOut[2*w] =  gap_pack2(Acc0, Acc1); LineOut[2*w+1] = gap_pack2(Acc2, Acc3);
				}
                               	((short int *)LineOut)[IterW-1] = gap_macs(((short int *)LineOut)[IterW-1], ((signed char *)LineIn)[IterW-1], C0);
			}
			break;
		case 2: for (unsigned int h=Ho_F; h<Ho_L; h++) {
				v2s O;
				v4s I;
				v2s *LineOut = (v2s *) (&Out[Wo*h+Wo_F]);
				v4s *LineIn = (v4s *) (In + (h*Stride-PadT)*W + (Wo_F*Stride-PadL));
				for (unsigned int w=0; w<(IterW/4); w++) {
					v2s O0 = LineOut[2*w], O1 = LineOut[2*w+1];
					v4s I = LineIn[w];
                                	int Acc0 = O0[0], Acc1 = O0[1], Acc2 = O1[0], Acc3 = O1[1];
                                	Acc0 = gap_macs(Acc0, I[0], C0); Acc1 = gap_macs(Acc1, I[1], C0);
                                	Acc2 = gap_macs(Acc2, I[2], C0); Acc3 = gap_macs(Acc3, I[3], C0);
                                	LineOut[2*w] =  gap_pack2(Acc0, Acc1); LineOut[2*w+1] = gap_pack2(Acc2, Acc3);
				}
				O = LineOut[2*(IterW/4)]; I = LineIn[IterW/4];
                               	O[0] = gap_macs(O[0], I[0], C0); O[1] = gap_macs(O[1], I[1], C0);
				LineOut[2*(IterW/4)] = O;
			}
			break;
		case 3: for (unsigned int h=Ho_F; h<Ho_L; h++) {
				v2s O;
				v4s I;
				v2s *LineOut = (v2s *) (&Out[Wo*h+Wo_F]);
				v4s *LineIn = (v4s *) (In + (h*Stride-PadT)*W + (Wo_F*Stride-PadL));
				for (unsigned int w=0; w<(IterW/4); w++) {
					v2s O0 = LineOut[2*w], O1 = LineOut[2*w+1];
					v4s I = LineIn[w];
                                	int Acc0 = O0[0], Acc1 = O0[1], Acc2 = O1[0], Acc3 = O1[1];
                                	Acc0 = gap_macs(Acc0, I[0], C0); Acc1 = gap_macs(Acc1, I[1], C0);
                                	Acc2 = gap_macs(Acc2, I[2], C0); Acc3 = gap_macs(Acc3, I[3], C0);
                                	LineOut[2*w] =  gap_pack2(Acc0, Acc1); LineOut[2*w+1] = gap_pack2(Acc2, Acc3);
				}
				O = LineOut[2*(IterW/4)]; I = LineIn[IterW/4];
                               	O[0] = gap_macs(O[0], I[0], C0); O[1] = gap_macs(O[1], I[1], C0);
				LineOut[2*(IterW/4)] = O;
                               	((short int *)LineOut)[IterW-1] = gap_macs(((short int *)LineOut)[IterW-1], ((signed char *)LineIn)[IterW-1], C0);
			}
			break;
	}
}

static void __attribute__ ((noinline)) KerConv1x1Stride2_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
	unsigned short int PadL = Pad[0], PadT = Pad[2];

	int Stride = 2;
	int C0 = Filter[0];
	int Fw = 1, Fh = 1;
	short int *PtO = Out+Wo*Ho_F+Wo_F;
	signed char *PtC = Filter;
        for (unsigned int h=Ho_F; h<Ho_L; h++) {
		signed char *PtI = In + (h*Stride-PadT)*W + (Wo_F*Stride-PadL);
        	for (unsigned int w=Wo_F; w<Wo_L; w++) {
			int Acc = *PtO;
			int I = *PtI; PtI+=Stride;
			Acc += I*C0;
			*PtO = Acc; PtO++;
                }
		PtO = PtO + (Wo-Wo_L)+Wo_F;
        }
}

static void __attribute__ ((noinline)) KerConv1x1StrideS_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	int Stride,
	v4s Pad
	)

{
	unsigned short int PadL = Pad[0], PadT = Pad[2];
	int C0 = Filter[0];
	int Fw = 1, Fh = 1;
	short int *PtO = Out+Wo*Ho_F+Wo_F;
	signed char *PtC = Filter;
        for (unsigned int h=Ho_F; h<Ho_L; h++) {
		signed char *PtI = In + (h*Stride-PadT)*W + (Wo_F*Stride-PadL);
        	for (unsigned int w=Wo_F; w<Wo_L; w++) {
			int Acc = *PtO;
			int I = *PtI; PtI+=Stride;
			Acc += I*C0;
			*PtO = Acc; PtO++;
                }
		PtO = PtO + (Wo-Wo_L)+Wo_F;
        }
}

static void __attribute__ ((noinline)) KerConv3x1Stride1x1_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
        v4s C0 = *((v4s *) &Filter[0]);
        v4s V0;
	unsigned short int StrideX = 1;
	unsigned short int StrideY = 1;
	unsigned short int PadL = Pad[0], PadT = 0;
	short int *PtO1 = Out+Wo*Ho_F+Wo_F;
	C0[3]=0;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		v4s *PtI = (v4s *) (In + (Ho_F*StrideY-PadT)*W + (w*StrideX-PadL));
		short int *PtO = PtO1;
		V0 = *PtI; PtI = (v4s*) ((signed char *)PtI+W);
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int Acc = *PtO;
                        Acc = gap_sumdotp4(V0, C0, Acc);
			V0 = *PtI; PtI = (v4s*) ((signed char *)PtI+W);
			*PtO = Acc; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv3x1Stride2x1_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
        v4s C0 = *((v4s *) &Filter[0]);
        v4s V0;
	unsigned short int StrideX = 2;
	unsigned short int StrideY = 1;
	unsigned short int PadL = Pad[0], PadT = 0;
	short int *PtO1 = Out+Wo*Ho_F+Wo_F;
	C0[3]=0;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		v4s *PtI = (v4s *) (In + (Ho_F*StrideY-PadT)*W + (w*StrideX-PadL));
		short int *PtO = PtO1;
		V0 = *PtI; PtI = (v4s*) ((signed char *)PtI+W);
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int Acc = *PtO;
                        Acc = gap_sumdotp4(V0, C0, Acc);
			V0 = *PtI; PtI = (v4s*) ((signed char *)PtI+W);
			*PtO = Acc; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv1x3Stride1x1_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
        v4s C0 = *((v4s *) &Filter[0]);
        v4s V0;
	v4s Mask = (v4s) {1,2,4,0};
	unsigned short int StrideX = 1;
	unsigned short int StrideY = 1;
	unsigned short int PadL = 0, PadT = Pad[2];
	short int *PtO1 = Out+Wo*Ho_F+Wo_F;
	C0[3]=0;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		signed char *PtI = (In + (Ho_F*StrideY-PadT)*W + (w*StrideX-PadL));
		short int *PtO = PtO1;
		V0[1] = *PtI; PtI = ((signed char *)PtI+W);
		V0[2] = *PtI; PtI = ((signed char *)PtI+W);
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int Acc = *PtO;
			int X0 = *PtI; PtI = ((signed char *)PtI+W);
			V0 = __builtin_shuffle(V0, (v4s) X0, Mask);
                        Acc = gap_sumdotp4(V0, C0, Acc);
			*PtO = Acc; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv1x3Stride1x2_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
        v4s C0 = *((v4s *) &Filter[0]);
        v4s V0;
	v4s Mask = (v4s) {2,4,5,0};
	unsigned short int StrideX = 1;
	unsigned short int StrideY = 2;
	unsigned short int PadL = 0, PadT = Pad[2];
	short int *PtO1 = Out+Wo*Ho_F+Wo_F;
	C0[3]=0;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		signed char *PtI = (In + (Ho_F*StrideY-PadT)*W + (w*StrideX-PadL));
		short int *PtO = PtO1;
		V0[2] = *PtI; PtI = ((signed char *)PtI+W);
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int Acc = *PtO;
			unsigned int X0 = *(unsigned char *) PtI; PtI = ((signed char *)PtI+W);
			unsigned int X1 = *(unsigned char *) PtI; PtI = ((signed char *)PtI+W);
			X0 = X0 | (X1<<8);
			V0 = __builtin_shuffle(V0, (v4s) X0, Mask);
                        Acc = gap_sumdotp4(V0, C0, Acc);
			*PtO = Acc; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv3x3Stride1_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
        v4s C0 = *((v4s *) &Filter[0]), C1 = *((v4s *) &Filter[3]), C2 = *((v4s *) &Filter[6]);
        v4s V0, V1, V2;
	unsigned short int StrideX = 1;
	unsigned short int StrideY = 1;
	unsigned short int PadL = Pad[0], PadT = Pad[2];
	short int *PtO1 = Out+Wo*Ho_F+Wo_F;
	C0[3]=0; C1[3]=0; C2[3]=0;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		v4s *PtI = (v4s *) (In + (Ho_F*StrideY-PadT)*W + (w*StrideX-PadL));
		short int *PtO = PtO1;
		V0 = *PtI; PtI = (v4s*) ((signed char *)PtI+W);
		V1 = *PtI; PtI = (v4s*) ((signed char *)PtI+W);
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int Acc = *PtO;
			V2 = *PtI; PtI = (v4s*) ((signed char *)PtI+W);
                        Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc);
                        V0 = V1; V1 = V2;
			*PtO = Acc; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv3x3Stride2_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
        v4s C0 = *((v4s *) &Filter[0]), C1 = *((v4s *) &Filter[3]), C2 = *((v4s *) &Filter[6]);
        v4s V0, V1, V2;
	unsigned short int Stride = 2;
	unsigned short int PadL = Pad[0], PadT = Pad[2];
	short int *PtO1 = Out+Wo*Ho_F+Wo_F;
	C0[3]=0; C1[3]=0; C2[3]=0;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		v4s *PtI = (v4s *) (In + (Ho_F*Stride-PadT)*W + (w*Stride-PadL));
		short int *PtO = PtO1;
		V0 = *PtI; PtI = (v4s*) ((signed char *)PtI+W);
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int Acc = *PtO;
			V1 = *PtI; PtI = (v4s*) ((signed char *)PtI+W);
			V2 = *PtI; PtI = (v4s*) ((signed char *)PtI+W);
                        Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc);
                        V0 = V2;
			*PtO = Acc; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv3x3StrideS_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	int Stride,
	v4s Pad
	)

{
        v4s C0 = *((v4s *) &Filter[0]), C1 = *((v4s *) &Filter[3]), C2 = *((v4s *) &Filter[6]);
        v4s V0, V1, V2;
	unsigned short int PadL = Pad[0], PadT = Pad[2];
	short int *PtO1 = Out+Wo*Ho_F+Wo_F;
	C0[3]=0; C1[3]=0; C2[3]=0;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		v4s *PtI = (v4s *) (In + (Ho_F*Stride-PadT)*W + (w*Stride-PadL));
		short int *PtO = PtO1;
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int Acc = *PtO;
			V0 = *PtI; PtI = (v4s*) ((signed char *)PtI+W);
			V1 = *PtI; PtI = (v4s*) ((signed char *)PtI+W);
			V2 = *PtI; PtI = (v4s*) ((signed char *)PtI+(Stride-2)*W);
                        Acc = gap_sumdotp4(V0, C0, Acc); Acc = gap_sumdotp4(V1, C1, Acc); Acc = gap_sumdotp4(V2, C2, Acc);
			*PtO = Acc; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv5x1Stride1x1_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)
{
        v4s C0  = *((v4s *) &Filter[0]);
        signed char C1 = Filter[4];
        v4s V0;
	int StrideX = 1;
	int StrideY = 1;
	int PadL = Pad[0], PadT = 0;
	short int *PtO1 = Out+Wo*Ho_F+Wo_F;
	v4s Mask = (v4s) {1,2,3,4};

        for (int h=Ho_F; h<Ho_L; h++) {
                signed char *PtI = (In + (h*StrideY-PadT)*W + (Wo_F*StrideX-PadL));
                short int *PtO = PtO1;
                v4s V0 = ((v4s *)PtI)[0];
                int x0 = PtI[4];
                PtI += 5;
                for (int w=Wo_F; w<Wo_L; w++) {
                        int S = *PtO;
                        S = gap_sumdotp4(V0,  C0,  S); S += x0*C1;
                        V0 = __builtin_shuffle(V0, (v4s) x0, Mask); x0 = *PtI; PtI++;
                        *PtO = S; PtO++;
                }
                PtO1+=Wo;
        }
}

static void __attribute__ ((noinline)) KerConv5x1Stride2x1_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)
{
        v4s C0  = *((v4s *) &Filter[0]);
        signed char C1 = Filter[4];
        v4s V0;
	int StrideX = 2;
	int StrideY = 1;
	int PadL = Pad[0], PadT = 0;
	short int *PtO1 = Out+Wo*Ho_F+Wo_F;

       	for (int w=Wo_F; w<Wo_L; w++) {
		v4s *PtI = (v4s *) (In + (Ho_F*StrideY-PadT)*W + (w*StrideX-PadL));
		short int *PtO = PtO1;
                int x0;
        	for (int h=Ho_F; h<Ho_L; h++) {
			int S = *PtO;
			V0 = *PtI++; x0 = *((signed char *) PtI); PtI = (v4s*) ((signed char *)PtI+W-4);
                        S = gap_sumdotp4(V0,  C0,  S); S += x0*C1;
			*PtO = S; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv1x5Stride1x1_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)
{
        v4s C0  = *((v4s *) &Filter[0]);
        signed char C1 = Filter[4];
	v4s Mask = (v4s) {1,2,3,4};
        v4s V0;
	signed char V1;

	unsigned short int StrideX = 1;
	unsigned short int StrideY = 1;
	unsigned short int PadL = 0, PadT = Pad[2];
	short int *PtO1 = Out+Wo*Ho_F+Wo_F;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		signed char *PtI = (In + (Ho_F*StrideY-PadT)*W + (w*StrideX-PadL));
		short int *PtO = PtO1;
                int x0,x1,x2;
		x0 = *PtI; PtI = PtI+W;
		x1 = *PtI; PtI = PtI+W;
		x2 = *PtI; PtI = PtI+W;
		V0 = gap_pack4(0,x0,x1,x2);
		V1 = *PtI; PtI = PtI+W;
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int S = *PtO;
			V0 = __builtin_shuffle(V0, (v4s)((int) V1), Mask);
			V1 = (signed char)(*PtI); PtI = PtI+W;
                        S = gap_sumdotp4(V0,  C0,  S); S += V1*C1;
			*PtO = S; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv1x5Stride1x2_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)
{
        v4s C0  = *((v4s *) &Filter[0]);
        signed char C1 = Filter[4];
	v4s Mask = (v4s) {2,3,4,5};
        v4s V0, V1;

	unsigned short int StrideX = 1;
	unsigned short int StrideY = 2;
	unsigned short int PadL = 0, PadT = Pad[2];
	short int *PtO1 = Out+Wo*Ho_F+Wo_F;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		signed char *PtI = (In + (Ho_F*StrideY-PadT)*W + (w*StrideX-PadL));
		short int *PtO = PtO1;
                int x0,x1;
		x0 = *PtI; PtI = PtI+W;
		x1 = *PtI; PtI = PtI+W;
		V0 = gap_pack4(0,0,x0,x1);
		V1 = (v4s) ((int) (*PtI)); PtI = PtI+W;
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int S = *PtO;
			x0 = *PtI; PtI = PtI+W;
			V1[1] = x0; V0 = __builtin_shuffle(V0, (v4s)((int) V1), Mask);
			x1 = (*PtI); PtI = PtI+W; V1 = (v4s)((int)x1);
                        S = gap_sumdotp4(V0,  C0,  S); S += x1*C1;
			*PtO = S; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv5x5Stride1_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
        v4s C0  = *((v4s *) &Filter[0]),
            C1  = *((v4s *) &Filter[5]),
            C2  = *((v4s *) &Filter[10]),
            C3  = *((v4s *) &Filter[15]),
            C4  = *((v4s *) &Filter[20]),
            C5 = gap_pack4(Filter[4], Filter[9], Filter[14], Filter[19]),
            C6 = (v4s)(int)(*((unsigned char *) &Filter[24]));

        v4s V0, V1, V2, V3, V4, V5, V6;
        v4s Mask  = {1,2,3,4};

	unsigned short int Stride = 1;
	unsigned short int PadL = Pad[0], PadT = Pad[2];
	short int *PtO1 = Out+Wo*Ho_F+Wo_F;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		v4s *PtI = (v4s *) (In + (Ho_F*Stride-PadT)*W + (w*Stride-PadL));
		short int *PtO = PtO1;
                int x0,x1,x2,x3;
		V0 = *PtI++; x0 = *((signed char *) PtI); PtI = (v4s*) ((signed char *)PtI+W-4);
		V1 = *PtI++; x1 = *((signed char *) PtI); PtI = (v4s*) ((signed char *)PtI+W-4);
		V2 = *PtI++; x2 = *((signed char *) PtI); PtI = (v4s*) ((signed char *)PtI+W-4);
		V3 = *PtI++; x3 = *((signed char *) PtI); PtI = (v4s*) ((signed char *)PtI+W-4);
		V5 = gap_pack4(x0,x1,x2,x3);

        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int S = *PtO;
			V4 = *PtI++; V6 = (v4s) (int) (*((signed char *) PtI)); PtI = (v4s*) ((signed char *)PtI+W-4);
                        S = gap_sumdotp4(V0,  C0,  S); S = gap_sumdotp4(V1,  C1,  S);
                        S = gap_sumdotp4(V2,  C2,  S); S = gap_sumdotp4(V3,  C3,  S);
                        S = gap_sumdotp4(V4,  C4,  S); S = gap_sumdotp4(V5,  C5,  S); S = gap_sumdotp4(V6,  C6,  S);
                        V0 = V1; V1 = V2; V2 = V3; V3 = V4;
			V5 = __builtin_shuffle(V5, V6, Mask);
			*PtO = S; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv5x5Stride2_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
        v4s C0  = *((v4s *) &Filter[0]),
            C1  = *((v4s *) &Filter[5]),
            C2  = *((v4s *) &Filter[10]),
            C3  = *((v4s *) &Filter[15]),
            C4  = *((v4s *) &Filter[20]),
            C5 = gap_pack4(Filter[4], Filter[9], Filter[14], Filter[19]),
            C6 = (v4s)(int)(*((unsigned char *) &Filter[24]));

        v4s V0, V1, V2, V3, V4, V5, V6;
        v4s Mask  = {2,3,4,4};

	unsigned short int Stride = 2;
	unsigned short int PadL = Pad[0], PadT = Pad[2];
	short int *PtO1 = Out+Wo*Ho_F+Wo_F;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		v4s *PtI = (v4s *) (In + (Ho_F*Stride-PadT)*W + (w*Stride-PadL));
		short int *PtO = PtO1;
                int x0,x1,x2,x3;
		V0 = *PtI++; x0 = *((signed char *) PtI); PtI = (v4s*) ((signed char *)PtI+W-4);
		V1 = *PtI++; x1 = *((signed char *) PtI); PtI = (v4s*) ((signed char *)PtI+W-4);
		V2 = *PtI++; x2 = *((signed char *) PtI); PtI = (v4s*) ((signed char *)PtI+W-4);
		V5 = gap_pack4(x0,x1,x2,0);
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int S = *PtO;
			V3 = *PtI++; V5[3] = *((signed char *) PtI); PtI = (v4s*) ((signed char *)PtI+W-4);
			V4 = *PtI++; V6 = (v4s) (int) (*((signed char *) PtI)); PtI = (v4s*) ((signed char *)PtI+W-4);
                        S = gap_sumdotp4(V0,  C0,  S); S = gap_sumdotp4(V1,  C1,  S);
                        S = gap_sumdotp4(V2,  C2,  S); S = gap_sumdotp4(V3,  C3,  S);
                        S = gap_sumdotp4(V4,  C4,  S); S = gap_sumdotp4(V5,  C5,  S); S = gap_sumdotp4(V6,  C6,  S);
                        V0 = V2; V1 = V3; V2 = V4;
			V5 = __builtin_shuffle(V5, V6, Mask);
			*PtO = S; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv5x5StrideS_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	int Stride,
	v4s Pad
	)

{
        v4s C0  = *((v4s *) &Filter[0]),
            C1  = *((v4s *) &Filter[5]),
            C2  = *((v4s *) &Filter[10]),
            C3  = *((v4s *) &Filter[15]),
            C4  = *((v4s *) &Filter[20]),
            C5 = gap_pack4(Filter[4], Filter[9], Filter[14], Filter[19]),
            C6 = (v4s)(int)(*((unsigned char *) &Filter[24]));

        v4s V0, V1, V2, V3, V4, V5, V6;
        v4s Mask  = {2,3,4,4};

	unsigned short int PadL = Pad[0], PadT = Pad[2];
	short int *PtO1 = Out+Wo*Ho_F+Wo_F;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		v4s *PtI = (v4s *) (In + (Ho_F*Stride-PadT)*W + (w*Stride-PadL));
		short int *PtO = PtO1;
                int x0,x1,x2,x3;
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int S = *PtO;
			V0 = *PtI++; x0 = *((signed char *) PtI); PtI = (v4s*) ((signed char *)PtI+W-4);
			V1 = *PtI++; x1 = *((signed char *) PtI); PtI = (v4s*) ((signed char *)PtI+W-4);
			V2 = *PtI++; x2 = *((signed char *) PtI); PtI = (v4s*) ((signed char *)PtI+W-4);
			V3 = *PtI++; x3 = *((signed char *) PtI); PtI = (v4s*) ((signed char *)PtI+W-4); V5 = gap_pack4(x0,x1,x2,x3);
			V4 = *PtI++; V6 = (v4s) (int) (*((signed char *) PtI)); PtI = (v4s*) ((signed char *)PtI+(Stride-4)*W-4);
                        S = gap_sumdotp4(V0,  C0,  S); S = gap_sumdotp4(V1,  C1,  S);
                        S = gap_sumdotp4(V2,  C2,  S); S = gap_sumdotp4(V3,  C3,  S);
                        S = gap_sumdotp4(V4,  C4,  S); S = gap_sumdotp4(V5,  C5,  S); S = gap_sumdotp4(V6,  C6,  S);
			*PtO = S; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConvNxNStrideS_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int Fw,
	int Fh,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	int Stride,
	v4s Pad
	)
{
	unsigned short int PadL = Pad[0], PadT = Pad[2];

	short int *PtO = Out+Wo*Ho_F+Wo_F;
	signed char *PtC = Filter;
        for (unsigned int h=Ho_F; h<Ho_L; h++) {
        	for (unsigned int w=Wo_F; w<Wo_L; w++) {
			int Acc = *PtO;
                        for (unsigned int i=0; i<Fh; i++) {
                                for (unsigned int j=0; j<Fw; j++) Acc += In[(h*Stride-PadT+i)*W + (w*Stride-PadL+j)]*Filter[Fw*i+j];
                        }
                        *PtO = Acc; PtO++;
                }
		PtO = PtO + (Wo-Wo_L)+Wo_F;
        }
}

static void __attribute__ ((noinline)) KerConvNxMStrideSxSy_Body_DP_fps(
	signed char *__restrict__ In,
	short int *__restrict__ Out,
	signed char *__restrict__ Filter,
	int Fw,
	int Fh,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	int StrideX,
	int StrideY,
	v4s Pad
	)
{
	unsigned short int PadL = Pad[0], PadT = Pad[2];

	short int *PtO = Out+Wo*Ho_F+Wo_F;
	signed char *PtC = Filter;
        for (unsigned int h=Ho_F; h<Ho_L; h++) {
        	for (unsigned int w=Wo_F; w<Wo_L; w++) {
			int Acc = *PtO;
                        for (unsigned int i=0; i<Fh; i++) {
                                for (unsigned int j=0; j<Fw; j++) Acc += In[(h*StrideY-PadT+i)*W + (w*StrideX-PadL+j)]*Filter[Fw*i+j];
                        }
                        *PtO = Acc; PtO++;
                }
		PtO = PtO + (Wo-Wo_L)+Wo_F;
        }
}

static void __attribute__ ((noinline)) KerConvNxMDxDyStrideSxSy_Body_DP_fps(
        signed char *__restrict__ In,
        short int *__restrict__ Out,
        signed char *__restrict__ Filter,
        int Fw,
        int Fh,
        int Dw,
        int Dh,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        int StrideX,
        int StrideY,
        v4s Pad,
        int Norm
        )
{
        unsigned short int PadL = Pad[0], PadT = Pad[2];

        short int *PtO = Out+Wo*Ho_F+Wo_F;
        signed char *PtC = Filter;
        for (unsigned int h=Ho_F; h<Ho_L; h++) {
                for (unsigned int w=Wo_F; w<Wo_L; w++) {
                        int Acc = *PtO;
                        for (unsigned int i=0; i<Fh; i++) {
                                for (unsigned int j=0; j<Fw; j++) Acc += In[(h*StrideY-PadT+i*Dh)*W + (w*StrideX-PadL+j*Dw)]*Filter[Fw*i+j];
                        }
                        *PtO = Acc; PtO++;
                }
                PtO = PtO + (Wo-Wo_L)+Wo_F;
        }
}


static void __attribute__ ((noinline)) KerConv1x1Stride1_Body_DP_fp(
	short int *__restrict__ In,
	int *__restrict__ Out,
	short int *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
	unsigned short int Stride = 1;
	unsigned short int PadL = Pad[0], PadT = Pad[2];

	int C0 = Filter[0];
	int IterW = Wo_L-Wo_F;
       	for (unsigned int h=Ho_F; h<Ho_L; h++) {
		int *LineOut = (int *) (&Out[Wo*h+Wo_F]);
		short int *PtI = In + (h*Stride-PadT)*W + (Wo_F*Stride-PadL);
		for (unsigned int w=0; w<(IterW/2); w++) {
			int O0 = LineOut[2*w], O1 = LineOut[2*w+1];
			O0 += PtI[2*w]*C0; O1 += PtI[2*w+1]*C0;
			LineOut[2*w] = O0; LineOut[2*w+1] = O1;
		}
		if (IterW&0x1) Out[Wo*h+Wo_L-1] = Out[Wo*h+Wo_L-1] + PtI[IterW-1]*C0;
	}
}

static void __attribute__ ((noinline)) KerConv1x1Stride2_Body_DP_fp(
	short int *__restrict__ In,
	int *__restrict__ Out,
	short int *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
	unsigned short int Stride = 2;
	unsigned short int PadL = Pad[0], PadT = Pad[2];

	int C0 = Filter[0];
	int IterW = Wo_L-Wo_F;
       	for (unsigned int h=Ho_F; h<Ho_L; h++) {
		int *LineOut = (int *) (&Out[Wo*h+Wo_F]);
		short int *PtI = In + (h*Stride-PadT)*W + (Wo_F*Stride-PadL);
		for (unsigned int w=0; w<(IterW/2); w++) {
			int O0 = LineOut[2*w], O1 = LineOut[2*w+1];
			O0 += PtI[4*w]*C0; O1 += PtI[4*w+2]*C0;
			LineOut[2*w] = O0; LineOut[2*w+1] = O1;
		}
		if (IterW&0x1) Out[Wo*h+Wo_L-1] = Out[Wo*h+Wo_L-1] + PtI[2*(IterW-1)]*C0;
	}
}

static void __attribute__ ((noinline)) KerConv1x1StrideS_Body_DP_fp(
	short int *__restrict__ In,
	int *__restrict__ Out,
	short int *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	int Stride,
	v4s Pad
	)

{
	unsigned short int PadL = Pad[0], PadT = Pad[2];

	int C0 = Filter[0];
	int Fw = 1, Fh = 1;
	int *PtO = Out+Wo*Ho_F+Wo_F;
	short int *PtC = Filter;
        for (unsigned int h=Ho_F; h<Ho_L; h++) {
		short int *PtI = In + (h*Stride-PadT)*W + (Wo_F*Stride-PadL);
        	for (unsigned int w=Wo_F; w<Wo_L; w++) {
			int I;
			int Acc = *PtO;
			I = *PtI; PtI+=Stride;
			Acc += I*C0;
			*PtO = Acc; PtO++;
                }
		PtO = PtO + (Wo-Wo_L)+Wo_F;
        }
}

static void __attribute__ ((noinline)) KerConv3x1Stride1x1_Body_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad
        )

{
        v2s C0 = *((v2s *) &Filter[0]);
	short int C1 = Filter[2];
        v2s V0;
	int V1;
	int StrideX = 1;
	int StrideY = 1;
	int PadL = Pad[0], PadT = 0;
	int *PtO1 = Out+Wo*Ho_F+Wo_F;
        v2s Mask = (v2s) {1,2};

        for (int h=Ho_F; h<Ho_L; h++) {
                int *PtO = PtO1;
                short int *PtI = (In + (h*StrideY-PadT)*W + (Wo_F*StrideX-PadL));
                V0 = ((v2s *) PtI)[0]; V1 = PtI[2]; PtI+=3;
                for (int w=Wo_F; w<Wo_L; w++) {
                        int Acc = *PtO;
                        Acc = gap_sumdotp2(V0, C0, Acc); Acc += V1*C1;
                        *PtO = Acc; PtO++;
                        V0 = __builtin_shuffle(V0, (v2s) V1, Mask);
                        V1 = *PtI++;
                }
                PtO1+=Wo;
	}
}

static void __attribute__ ((noinline)) KerConv3x1Stride2x1_Body_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad
        )
{
        v2s C0 = *((v2s *) &Filter[0]);
	short int C1 = Filter[2];
        v2s V0;
	short int V1;
	unsigned short int StrideX = 2;
	unsigned short int StrideY = 1;
	unsigned short int PadL = Pad[0], PadT = 0;
	int *PtO1 = Out+Wo*Ho_F+Wo_F;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		v2s *PtI = (v2s *) (In + (Ho_F*StrideY-PadT)*W + (w*StrideX-PadL));
		int *PtO = PtO1;
		V0 = *PtI++; V1 = *(short int *) PtI; PtI = (v2s*) ((short int *)PtI+W-2);
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int Acc = *PtO;
                        Acc = gap_sumdotp2(V0, C0, Acc); Acc += V1*C1;
			V0 = *PtI++; V1 = *(short int *) PtI; PtI = (v2s*) ((short int *)PtI+W-2);
			*PtO = Acc; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv1x3Stride1x1_Body_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad
        )

{
        v2s C0 = *((v2s *) &Filter[0]);
	short int C1 = Filter[2];
        v2s V0;
	int V1;
	v2s Mask = (v2s) {1,2};
	int StrideX = 1;
	int StrideY = 1;
	int PadL = 0, PadT = Pad[2];
	int *PtO1 = Out+Wo*Ho_F+Wo_F;

       	for (int w=Wo_F; w<Wo_L; w++) {
		short int *PtI = (short int *) (In + (Ho_F*StrideY-PadT)*W + (w*StrideX-PadL));
		int *PtO = PtO1;
		V0[1] = *PtI; PtI = PtI+W;
		V1 = *PtI; PtI = PtI+W;
        	for (int h=Ho_F; h<Ho_L; h++) {
			int Acc = *PtO;
			V0 = __builtin_shuffle(V0, (v2s) V1, Mask);
			V1 = *PtI; PtI = PtI+W;
                        Acc = gap_sumdotp2(V0, C0, Acc); Acc += V1*C1;
			*PtO = Acc; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv1x3Stride1x2_Body_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad
        )

{
        v2s C0 = *((v2s *) &Filter[0]);
	short int C1 = Filter[2];
        v2s V0;
	int V1;
	v2s Mask = (v2s) {1,2};
	unsigned short int StrideX = 1;
	unsigned short int StrideY = 2;
	unsigned short int PadL = 0, PadT = Pad[2];
	int *PtO1 = Out+Wo*Ho_F+Wo_F;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		short int *PtI = (short int *) (In + (Ho_F*StrideY-PadT)*W + (w*StrideX-PadL));
		int *PtO = PtO1;
		V1 = *PtI; PtI = PtI+W;
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int Acc = *PtO;
			V0 = (v2s) gap_pack2(V1, *PtI); PtI = PtI+W;
			V1 = *PtI; PtI = PtI+W;
                        Acc = gap_sumdotp2(V0, C0, Acc); Acc += V1*C1;
			*PtO = Acc; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv3x3Stride1_Body_DP_fp(
	short int *__restrict__ In,
	int *__restrict__ Out,
	short int *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
        v2s C0 = *((v2s *) &Filter[0]),            C1 = *((v2s *) &Filter[3]),
            C2 = gap_pack2(Filter[2], Filter[5]), C3 = *((v2s *) &Filter[6]),
            C4 = gap_pack2(Filter[8], 0);
        v2s V0, V1, V2, V3, V4;
	unsigned short int Stride = 1;
	unsigned short int PadL = Pad[0], PadT = Pad[2];
	int *PtO1 = Out+Wo*Ho_F+Wo_F;
	int Off0 = 2*W-4;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		v2s *PtI = (v2s *) (In + (Ho_F*Stride-PadT)*W + (w*Stride-PadL));
		int *PtO = PtO1;
		V0 = *PtI++; V2 = *PtI; PtI = (v2s*) ((short int *)PtI+W-2);
		V1 = *PtI++; V3 = *PtI; PtI = (v2s*) ((short int *)PtI+W-2); V2 = __builtin_shuffle(V2, V3, (v2s){0,2});
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int Acc = *PtO;
			V3 = *PtI++; V4 = *PtI; PtI = (v2s*) ((short int *)PtI+W-2);
                        Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc);
                        Acc = gap_sumdotp2(V2, C2, Acc); Acc = gap_sumdotp2(V3, C3, Acc);
                        Acc = gap_sumdotp2(V4, C4, Acc);
                        V0 = V1; V1 = V3;
                        V2 = __builtin_shuffle(V2, V4, (v2s) {1, 2});
			*PtO = Acc; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv3x3Stride2_Body_DP_fp(
	short int *__restrict__ In,
	int *__restrict__ Out,
	short int *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
        v2s C0 = *((v2s *) &Filter[0]), C1 = gap_pack2(Filter[2], 0),
            C2 = *((v2s *) &Filter[3]), C3 = gap_pack2(Filter[5], 0),
            C4 = *((v2s *) &Filter[6]), C5 = gap_pack2(Filter[8], 0);
        v2s V0, V1, V2, V3, V4, V5;
	unsigned short int Stride = 2;
	unsigned short int PadL = Pad[0], PadT = Pad[2];
	int *PtO1 = Out+Wo*Ho_F+Wo_F;
	int Off0 = 2*W-4;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		v2s *PtI = (v2s *) (In + (Ho_F*Stride-PadT)*W + (w*Stride-PadL));
		int *PtO = PtO1;
		V0 = *PtI++; V1 = *PtI; PtI = (v2s*) ((short int *)PtI+W-2);
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int Acc = *PtO;
			V2 = *PtI++; V3 = *PtI; PtI = (v2s*) ((short int *)PtI+W-2);
			V4 = *PtI++; V5 = *PtI; PtI = (v2s*) ((short int *)PtI+W-2);
                        Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc);
                        Acc = gap_sumdotp2(V2, C2, Acc); Acc = gap_sumdotp2(V3, C3, Acc);
                        Acc = gap_sumdotp2(V4, C4, Acc); Acc = gap_sumdotp2(V5, C5, Acc);
                        V0 = V4; V1 = V5;
			*PtO = Acc; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv3x3StrideS_Body_DP_fp(
	short int *__restrict__ In,
	int *__restrict__ Out,
	short int *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	int Stride,
	v4s Pad
	)

{
	unsigned short int PadL = Pad[0], PadT = Pad[2];
	v2s Cv0 = *((v2s *)(Filter+0)),
	    Cv1 = *((v2s *)(Filter+3)),
	    Cv2 = *((v2s *)(Filter+6));
	int C0 = Filter[2],
	    C1 = Filter[5],
	    C2 = Filter[8];
	int Fw = 3, Fh = 3;
	int *PtO = Out+Wo*Ho_F+Wo_F;
	short int *PtC = Filter;
        for (unsigned int h=Ho_F; h<Ho_L; h++) {
        	for (unsigned int w=Wo_F; w<Wo_L; w++) {
			short int *PtI = In + (h*Stride-PadT)*W + (w*Stride-PadL);
			v2s Iv0;
			int I;
			int Acc = *PtO;
			Iv0 = *((v2s *) PtI); PtI+=2; I = *PtI; PtI+=W-(Fw-1);
			Acc = gap_sumdotp2(Iv0, Cv0, Acc); Acc += I*C0;
			Iv0 = *((v2s *) PtI); PtI+=2; I = *PtI; PtI+=W-(Fw-1);
			Acc = gap_sumdotp2(Iv0, Cv1, Acc); Acc += I*C1;
			Iv0 = *((v2s *) PtI); PtI+=2; I = *PtI; PtI+=W-(Fw-1);
			Acc = gap_sumdotp2(Iv0, Cv2, Acc);Acc += I*C2;
			*PtO = Acc; PtO++;
                }
		PtO = PtO + (Wo-Wo_L)+Wo_F;
        }
}

static void __attribute__ ((noinline)) KerConv5x1Stride1x1_Body_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad
        )
{
        v2s C0 = *((v2s *) &Filter[0]), C1 = *((v2s *) &Filter[2]);
	short int C2 = Filter[4];
        v2s V0, V1;
	v2s Mask = (v2s) {1,2};
	int V2;
	unsigned short int StrideX = 1;
	unsigned short int StrideY = 1;
	unsigned short int PadL = Pad[0], PadT = 0;
	int *PtO = Out+Wo*Ho_F+Wo_F;

       	for (unsigned int h=Ho_F; h<Ho_L; h++) {
		v2s *PtI = (v2s *) (In + (h*StrideY-PadT)*W + (Wo_F*StrideX-PadL));
		V0[1] = *((short int *)PtI);  PtI = (v2s *) ((short int *)PtI + 1);
		V1 = *PtI++;
		V2 = *((short int *) PtI); PtI = (v2s *) ((short int *)PtI + 1);
       		for (unsigned int w=Wo_F; w<Wo_L; w++) {
			int Acc = *PtO;
			V0 = __builtin_shuffle(V0, V1, Mask); V1 = __builtin_shuffle(V1, (v2s) V2, Mask);
			V2 = *((short int *) PtI); PtI = (v2s *) ((short int *)PtI + 1);
                        Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc); Acc += V2*C2;
			*PtO = Acc; PtO++;
		}
		PtO = PtO + (Wo-Wo_L)+Wo_F;
	}
}

static void __attribute__ ((noinline)) KerConv5x1Stride2x1_Body_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad
        )
{
        v2s C0 = *((v2s *) &Filter[0]), C1 = *((v2s *) &Filter[2]);
	short int C2 = Filter[4];
        v2s V0, V1;
	v2s Mask = (v2s) {1,2};
	int V2;
	unsigned short int StrideX = 2;
	unsigned short int StrideY = 1;
	unsigned short int PadL = Pad[0], PadT = 0;
	int *PtO = Out+Wo*Ho_F+Wo_F;

        for (unsigned int h=Ho_F; h<Ho_L; h++) {
                short int *PtI = (In + (h*StrideY-PadT)*W + (Wo_F*StrideX-PadL));
                V1 = *((v2s *)PtI); PtI+=2;
                V2 = *PtI; PtI++; 
                for (unsigned int w=Wo_F; w<Wo_L; w++) {
                        int Acc = *PtO;
                        V0 = V1; V1 = gap_pack2(V2, *PtI); PtI++; V2 = *PtI; PtI++;
                        Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc); Acc += V2*C2;
                        *PtO = Acc; PtO++;
                }
                PtO = PtO + (Wo-Wo_L)+Wo_F;
        }
}

static void __attribute__ ((noinline)) KerConv1x5Stride1x1_Body_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad
        )
{
        v2s C0 = *((v2s *) &Filter[0]), C1 = *((v2s *) &Filter[2]);
	short int C2 = Filter[4];
        v2s V0, V1;
	v2s Mask = (v2s) {1,2};
	int V2;
	unsigned short int StrideX = 1;
	unsigned short int StrideY = 1;
	unsigned short int PadL = 0, PadT = Pad[2];

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
                short int *PtI = (In + (Ho_F*StrideY-PadT)*W + (w*StrideX-PadL));
                int *PtO = Out+Wo*Ho_F+w;
                int x1,x2;
                V0[1] = *PtI; PtI = PtI + W;
                x1 = *PtI; PtI = PtI + W;
                x2 = *PtI; PtI = PtI + W;
                V1 = gap_pack2(x1, x2);
                V2 = *PtI; PtI = PtI + W;
                for (unsigned int h=Ho_F; h<Ho_L; h++) {
                        int Acc = *PtO;
                        V0 = __builtin_shuffle(V0, V1, Mask); V1 = __builtin_shuffle(V1, (v2s) V2, Mask);
                        V2 = *PtI; PtI = PtI + W;
                        Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc); Acc += V2*C2;
                        *PtO = Acc; PtO+=Wo;
                }
	}
}

static void __attribute__ ((noinline)) KerConv1x5Stride1x2_Body_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        v4s Pad
        )
{
        v2s C0 = *((v2s *) &Filter[0]), C1 = *((v2s *) &Filter[2]);
	short int C2 = Filter[4];
        v2s V0, V1;
	v2s Mask = (v2s) {1,2};
	int V2;
	unsigned short int StrideX = 1;
	unsigned short int StrideY = 2;
	unsigned short int PadL = 0, PadT = Pad[2];

        for (unsigned int w=Wo_F; w<Wo_L; w++) {
                short int *PtI = (In + (Ho_F*StrideY-PadT)*W + (w*StrideX-PadL));
                int *PtO = Out+Wo*Ho_F+w;
                int x1,x2;
                x1 = *PtI; PtI += W; x2 = *PtI; PtI += W; V1 = gap_pack2(x1, x2);
                V2 = *PtI; PtI += W;
                for (unsigned int h=Ho_F; h<Ho_L; h++) {
                        int Acc = *PtO;
                        V0 = V1; V1 = gap_pack2(V2, *PtI); PtI += W; V2 = *PtI; PtI += W;
                        Acc = gap_sumdotp2(V0, C0, Acc); Acc = gap_sumdotp2(V1, C1, Acc); Acc += V2*C2;
                        *PtO = Acc; PtO+=Wo;
                }
        }
}

static void __attribute__ ((noinline)) KerConv5x5Stride1_Body_DP_fp(
	short int *__restrict__ In,
	int *__restrict__ Out,
	short int *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
        v2s C0  = *((v2s *) &Filter[0]),            C1  = *((v2s *) &Filter[2]),
            C2  = *((v2s *) &Filter[5]),            C3  = *((v2s *) &Filter[7]),
            C4  = *((v2s *) &Filter[10]),           C5  = *((v2s *) &Filter[12]),
            C6  = *((v2s *) &Filter[15]),           C7  = *((v2s *) &Filter[17]),
            C8  = *((v2s *) &Filter[20]),           C9  = *((v2s *) &Filter[22]),
            C10 = gap_pack2(Filter[4], Filter[9]), C11 = gap_pack2(Filter[14], Filter[19]),
            C12 = gap_pack2(Filter[24], 0);

        v2s V0, V1, V2, V3, V4, V5, V6, V7, V8, V9, V10, V11, V12;
        v2s Mask  = {1,2};

	unsigned short int Stride = 1;
	unsigned short int PadL = Pad[0], PadT = Pad[2];
	int *PtO1 = Out+Wo*Ho_F+Wo_F;
	int Off0 = 2*W - 8;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		v2s *PtI = (v2s *) (In + (Ho_F*Stride-PadT)*W + (w*Stride-PadL));
		int *PtO = PtO1;
                int X, Y;
		V0 = *PtI++; V1 = *PtI++; X = *((short int *) PtI); PtI = (v2s*) ((short int *)PtI+W-4);
		V2 = *PtI++; V3 = *PtI++; Y = *((short int *) PtI); PtI = (v2s*) ((short int *)PtI+W-4); V10 = gap_pack2(X, Y);
		V4 = *PtI++; V5 = *PtI++; X = *((short int *) PtI); PtI = (v2s*) ((short int *)PtI+W-4);
		V6 = *PtI++; V7 = *PtI++; Y = *((short int *) PtI); PtI = (v2s*) ((short int *)PtI+W-4); V11 = gap_pack2(X, Y);
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			V8 = *PtI++; V9 = *PtI++; V12 = *PtI; PtI = (v2s*) ((short int *)PtI+W-4);
			int S = *PtO;
                        S = gap_sumdotp2(V0,  C0,  S); S = gap_sumdotp2(V1,  C1,  S); S = gap_sumdotp2(V10, C10, S);
                        S = gap_sumdotp2(V2,  C2,  S); S = gap_sumdotp2(V3,  C3,  S);
                        S = gap_sumdotp2(V4,  C4,  S); S = gap_sumdotp2(V5,  C5,  S); S = gap_sumdotp2(V11, C11, S);
                        S = gap_sumdotp2(V6,  C6,  S); S = gap_sumdotp2(V7,  C7,  S);
                        S = gap_sumdotp2(V8,  C8,  S); S = gap_sumdotp2(V9,  C9,  S); S = gap_sumdotp2(V12, C12, S);
                        V0 = V2; V1 = V3; V2 = V4; V3 = V5; V4 = V6; V5 = V7; V6 = V8; V7 = V9;
                        V10 = __builtin_shuffle(V10, V11, Mask); V11 = __builtin_shuffle(V11, V12, Mask);
			*PtO = S; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv5x5Stride2_Body_DP_fp(
	short int *__restrict__ In,
	int *__restrict__ Out,
	short int *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	v4s Pad
	)

{
        v2s C0  = *((v2s *) &Filter[0]),            C1  = *((v2s *) &Filter[2]),
            C2  = *((v2s *) &Filter[5]),            C3  = *((v2s *) &Filter[7]),
            C4  = *((v2s *) &Filter[10]),           C5  = *((v2s *) &Filter[12]),
            C6  = *((v2s *) &Filter[15]),           C7  = *((v2s *) &Filter[17]),
            C8  = *((v2s *) &Filter[20]),           C9  = *((v2s *) &Filter[22]),
            C10 = gap_pack2(Filter[4], Filter[9]), C11 = gap_pack2(Filter[14], Filter[19]),
            C12 = gap_pack2(Filter[24], 0);
        v2s V0, V1, V2, V3, V4, V5, V6, V7, V8, V9, V10, V11, V12;

	unsigned short int Stride = 2;
	unsigned short int PadL = Pad[0], PadT = Pad[2];
	int *PtO1 = Out+Wo*Ho_F+Wo_F;
	int Off0 = 2*W-8;

       	for (unsigned int w=Wo_F; w<Wo_L; w++) {
		v2s *PtI = (v2s *) (In + (Ho_F*Stride-PadT)*W + (w*Stride-PadL));
		int *PtO = PtO1;
                int X, Y;
		V0 = *PtI++; V1 = *PtI++; X = *((short int *) PtI); PtI = (v2s*) ((short int *)PtI+W-4);
		V2 = *PtI++; V3 = *PtI++; Y = *((short int *) PtI); PtI = (v2s*) ((short int *)PtI+W-4); V10 = gap_pack2(X, Y);
		V4 = *PtI++; V5 = *PtI++; X = *((short int *) PtI); PtI = (v2s*) ((short int *)PtI+W-4); V11 = gap_pack2(X, 0);
        	for (unsigned int h=Ho_F; h<Ho_L; h++) {
			int S = *PtO;
			V6 = *PtI++; V7 = *PtI++; X = *((short int *) PtI); PtI = (v2s*) ((short int *)PtI+W-4); V11 = gap_pack2((int)V11, X);
			V8 = *PtI++; V9 = *PtI++; V12 = *PtI;               PtI = (v2s*) ((short int *)PtI+W-4);
                        S = gap_sumdotp2(V0, C0, S); S = gap_sumdotp2(V1, C1, S); S = gap_sumdotp2(V10, C10, S);
                        S = gap_sumdotp2(V2, C2, S); S = gap_sumdotp2(V3, C3, S);
                        S = gap_sumdotp2(V4, C4, S); S = gap_sumdotp2(V5, C5, S); S = gap_sumdotp2(V11, C11, S);
                        S = gap_sumdotp2(V6, C6, S); S = gap_sumdotp2(V7, C7, S);
                        S = gap_sumdotp2(V8, C8, S); S = gap_sumdotp2(V9, C9, S); S = gap_sumdotp2(V12, C12, S); S = S;
			V10 = V11; V11 = V12; V0 = V4; V1 = V5; V2 = V6; V3 = V7; V4 = V8; V5 = V9;
			*PtO = S; PtO+=Wo;
		}
		PtO1++;
	}
}

static void __attribute__ ((noinline)) KerConv5x5StrideS_Body_DP_fp(
	short int *__restrict__ In,
	int *__restrict__ Out,
	short int *__restrict__ Filter,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	int Stride,
	v4s Pad
	)

{
	unsigned short int PadL = Pad[0], PadT = Pad[2];
	v2s Cv0 = *((v2s *)(Filter+ 0)), Cv1 = *((v2s *)(Filter+ 2)),
	    Cv2 = *((v2s *)(Filter+ 5)), Cv3 = *((v2s *)(Filter+ 7)),
	    Cv4 = *((v2s *)(Filter+10)), Cv5 = *((v2s *)(Filter+12)),
	    Cv6 = *((v2s *)(Filter+15)), Cv7 = *((v2s *)(Filter+17)),
	    Cv8 = *((v2s *)(Filter+20)), Cv9 = *((v2s *)(Filter+22));
	int C0 = Filter[ 4],
	    C1 = Filter[ 9],
	    C2 = Filter[14],
	    C3 = Filter[19],
	    C4 = Filter[24];
	int Fw = 5, Fh = 5;
	int *PtO = Out+Wo*Ho_F+Wo_F;
	short int *PtC = Filter;

        for (unsigned int h=Ho_F; h<Ho_L; h++) {
        	for (unsigned int w=Wo_F; w<Wo_L; w++) {
			short int *PtI = In + (h*Stride-PadT)*W + (w*Stride-PadL);
			v2s Iv0, Iv1;
			int I;
			int Acc = *PtO;
			Iv0 = *((v2s *) PtI); PtI+=2; Iv1 = *((v2s *) PtI); PtI+=2; I = *PtI; PtI+=W-(Fw-1);
			Acc = gap_sumdotp2(Iv0, Cv0, Acc); Acc = gap_sumdotp2(Iv1, Cv1, Acc); Acc += I*C0;
			Iv0 = *((v2s *) PtI); PtI+=2; Iv1 = *((v2s *) PtI); PtI+=2; I = *PtI; PtI+=W-(Fw-1);
			Acc = gap_sumdotp2(Iv0, Cv2, Acc); Acc = gap_sumdotp2(Iv1, Cv3, Acc); Acc += I*C1;
			Iv0 = *((v2s *) PtI); PtI+=2; Iv1 = *((v2s *) PtI); PtI+=2; I = *PtI; PtI+=W-(Fw-1);
			Acc = gap_sumdotp2(Iv0, Cv4, Acc); Acc = gap_sumdotp2(Iv1, Cv5, Acc); Acc += I*C2;
			Iv0 = *((v2s *) PtI); PtI+=2; Iv1 = *((v2s *) PtI); PtI+=2; I = *PtI; PtI+=W-(Fw-1);
			Acc = gap_sumdotp2(Iv0, Cv6, Acc); Acc = gap_sumdotp2(Iv1, Cv7, Acc); Acc += I*C3;
			Iv0 = *((v2s *) PtI); PtI+=2; Iv1 = *((v2s *) PtI); PtI+=2; I = *PtI; PtI+=W-(Fw-1);
			Acc = gap_sumdotp2(Iv0, Cv8, Acc); Acc = gap_sumdotp2(Iv1, Cv9, Acc); Acc += I*C4;
			*PtO = Acc; PtO++;
                }
		PtO = PtO + (Wo-Wo_L)+Wo_F;
        }
}

static void __attribute__ ((noinline)) KerConvNxNStrideS_Body_DP_fp(
	short int *__restrict__ In,
	int *__restrict__ Out,
	short int *__restrict__ Filter,
	int Fw,
	int Fh,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	int Stride,
	v4s Pad
	)
{
	unsigned short int PadL = Pad[0], PadT = Pad[2];

	int *PtO = Out+Wo*Ho_F+Wo_F;
	short int *PtC = Filter;
        for (unsigned int h=Ho_F; h<Ho_L; h++) {
        	for (unsigned int w=Wo_F; w<Wo_L; w++) {
			int Acc = *PtO;
                        for (unsigned int i=0; i<Fh; i++) {
                                for (unsigned int j=0; j<Fw; j++) Acc += In[(h*Stride-PadT+i)*W + (w*Stride-PadL+j)]*Filter[Fw*i+j];
                        }
                        *PtO = Acc; PtO++;
                }
		PtO = PtO + (Wo-Wo_L)+Wo_F;
        }
}

static void __attribute__ ((noinline)) KerConvNxMStrideSxSy_Body_DP_fp(
	short int *__restrict__ In,
	int *__restrict__ Out,
	short int *__restrict__ Filter,
	int Fw,
	int Fh,
	int W,
	int H,
	int Wo,
	int Wo_F,
	int Wo_L,
	int Ho,
	int Ho_F,
	int Ho_L,
	int StrideX,
	int StrideY,
	v4s Pad
	)
{
	unsigned short int PadL = Pad[0], PadT = Pad[2];

	int *PtO = Out+Wo*Ho_F+Wo_F;
	short int *PtC = Filter;
        for (unsigned int h=Ho_F; h<Ho_L; h++) {
        	for (unsigned int w=Wo_F; w<Wo_L; w++) {
			int Acc = *PtO;
                        for (unsigned int i=0; i<Fh; i++) {
                                for (unsigned int j=0; j<Fw; j++) Acc += In[(h*StrideY-PadT+i)*W + (w*StrideX-PadL+j)]*Filter[Fw*i+j];
                        }
                        *PtO = Acc; PtO++;
                }
		PtO = PtO + (Wo-Wo_L)+Wo_F;
        }
}

static void __attribute__ ((noinline)) KerConvNxMDxDyStrideSxSy_Body_DP_fp(
        short int *__restrict__ In,
        int *__restrict__ Out,
        short int *__restrict__ Filter,
        int Fw,
        int Fh,
        int Dw,
        int Dh,
        int W,
        int H,
        int Wo,
        int Wo_F,
        int Wo_L,
        int Ho,
        int Ho_F,
        int Ho_L,
        int StrideX,
        int StrideY,
        v4s Pad,
        int Norm
        )
{
        unsigned short int PadL = Pad[0], PadT = Pad[2];

        int *PtO = Out+Wo*Ho_F+Wo_F;
        short int *PtC = Filter;
        for (unsigned int h=Ho_F; h<Ho_L; h++) {
                for (unsigned int w=Wo_F; w<Wo_L; w++) {
                        int Acc = *PtO;
                        for (unsigned int i=0; i<Fh; i++) {
                                for (unsigned int j=0; j<Fw; j++) Acc += In[(h*StrideY-PadT+i*Dh)*W + (w*StrideX-PadL+j*Dw)]*Filter[Fw*i+j];
                        }
                        *PtO = Acc; PtO++;
                }
                PtO = PtO + (Wo-Wo_L)+Wo_F;
        }
}

/*
	Optionally 0 padded convolutions.

	Input, output features and filters are half words (_fp) Dim=1,3,5,N, Stride=1,2,S

	Output feature maps are evaluated in parallel, one per core

	Argument data type: KerConv_DP_fp_T

	KerParConv1x1Stride1_DP_fp
	KerParConv1x1Stride2_DP_fp
	KerParConv1x1StrideS_DP_fp

	KerParConv3x1Stride1x1_DP_fp
		|------	KerConv3x1Stride1x1_Body_DP_fp
		|------	KerConv3x1StrideNx1_Border_DP_fp
	KerParConv3x1Stride2x1_DP_fp
		|------	KerConv3x1Stride2x1_Body_DP_fp
		|------	KerConv3x1StrideNx1_Border_DP_fp
	KerParConv1x3Stride1x1_DP_fp
		|------	KerConv1x3Stride1x1_Body_DP_fp
		|------	KerConv1x3Stride1xN_Border_DP_fp
	KerParConv1x3Stride1x2_DP_fp
		|------	KerConv1x3Stride1x2_Body_DP_fp
		|------	KerConv1x3Stride1xN_Border_DP_fp
	KerParConv3x3Stride1_DP_fp
		|------	KerConv3x3Stride1_Body_DP_fp
		|------	KerConv3x3Stride1_Border_DP_fp
	KerParConv3x3Stride2_DP_fp
		|------	KerConv3x3Stride2_Body_DP_fp
		|------	KerConv3x3Stride2_Border_DP_fp
	KerParConv3x3StrideS_DP_fp
		|------	KerConv3x3StrideS_Body_DP_fp
		|------	KerConv3x3StrideS_Border_DP_fp

	KerParConv5x1Stride1x1_DP_fp
		|------	KerConv5x1Stride1x1_Body_DP_fp
		|------	KerConv5x1StrideNx1_Border_DP_fp
	KerParConv5x1Stride2x1_DP_fp
		|------	KerConv5x1Stride2x1_Body_DP_fp
		|------	KerConv5x1StrideNx1_Border_DP_fp
	KerParConv1x5Stride1x1_DP_fp
		|------	KerConv1x5Stride1x1_Body_DP_fp
		|------	KerConv1x5Stride1xN_Border_DP_fp
	KerParConv1x5Stride1x2_DP_fp
		|------	KerConv1x5Stride1x2_Body_DP_fp
		|------	KerConv1x5Stride1xN_Border_DP_fp
	KerParConv5x5Stride1_DP_fp
		|------	KerConv5x5Stride1_Body_DP_fp
		|------	KerConv5x5Stride1_Border_DP_fp
	KerParConv5x5Stride2_DP_fp
		|------	KerConv5x5Stride2_Body_DP_fp
		|------	KerConv5x5Stride2_Border_DP_fp
	KerParConv5x5StrideS_DP_fp
		|------	KerConv5x5StrideS_Body_DP_fp
		|------	KerConv5x5StrideS_Border_DP_fp

	KerParConvNxNStrideS_DP_fp
		|------	KerConvNxNStrideS_Body_DP_fp
		|------	KerConvNxNStrideS_Border_DP_fp

	KerParConvNxMStrideSxSy_DP_fp
		|------	KerConvNxMStrideSxSy_Body_DP_fp
		|------	KerConvNxMStrideSxSy_Border_DP_fp

	KerParConvNxMDxDyStrideSxSy_DP_fp
		|------	KerConvNxMDxDyStrideSxSy_Body_DP_fp
		|------	KerConvNxMDxDyStrideSxSy_Border_DP_fp

*/

void KerParConv1x1Stride1_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FS=1, S=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FS*FS*of;
			int *out = Out+Wo*Ho*of;
			KerConv1x1Stride1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv1x1Stride1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv1x1Stride2_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FS=1, S=2;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FS*FS*of;
			int *out = Out+Wo*Ho*of;
			KerConv1x1Stride2_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv1x1Stride2_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv1x1StrideS_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FS=1, S=Arg->S;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FS*FS*of;
			int *out = Out+Wo*Ho*of;
			KerConv1x1StrideS_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv1x1StrideS_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv3x1Stride1x1_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FSx=3, FSy=1, Sx=1, Sy=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			int *out = Out+Wo*Ho*of;
			KerConv3x1Stride1x1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv3x1BorderStrideNx1_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv3x1Stride1x1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv3x1BorderStrideNx1_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv3x1Stride2x1_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FSx=3, FSy=1, Sx=2, Sy=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			int *out = Out+Wo*Ho*of;
			KerConv3x1Stride2x1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv3x1BorderStrideNx1_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv3x1Stride2x1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv3x1BorderStrideNx1_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv1x3Stride1x1_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FSx=1, FSy=3, Sx=1, Sy=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			int *out = Out+Wo*Ho*of;
			KerConv1x3Stride1x1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv1x3BorderStride1xN_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv1x3Stride1x1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv1x3BorderStride1xN_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv1x3Stride1x2_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FSx=1, FSy=3, Sx=1, Sy=2;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			int *out = Out+Wo*Ho*of;
			KerConv1x3Stride1x2_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv1x3BorderStride1xN_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv1x3Stride1x2_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv1x3BorderStride1xN_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv3x3Stride1_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FS=3, S=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
	int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FS*FS*of;
			int *out = Out+Wo*Ho*of;
			KerConv3x3Stride1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv3x3BorderStride1_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv3x3Stride1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv3x3BorderStride1_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv3x3Stride2_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FS=3, S=2;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
	int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FS*FS*of;
			int *out = Out+Wo*Ho*of;
			KerConv3x3Stride2_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv3x3BorderStride2_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv3x3Stride2_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv3x3BorderStride2_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv3x3StrideS_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FS=3, S=Arg->S;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
	int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FS*FS*of;
			int *out = Out+Wo*Ho*of;
			KerConv3x3StrideS_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
			if ((int)PadIn) KerConv3x3BorderStrideS_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv3x3StrideS_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
				if ((int)PadIn) KerConv3x3BorderStrideS_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv5x1Stride1x1_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FSx=5, FSy=1, Sx=1, Sy=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			int *out = Out+Wo*Ho*of;
			KerConv5x1Stride1x1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv5x1BorderStrideNx1_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv5x1Stride1x1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv5x1BorderStrideNx1_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv5x1Stride2x1_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FSx=5, FSy=1, Sx=2, Sy=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			int *out = Out+Wo*Ho*of;
			KerConv5x1Stride2x1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv5x1BorderStrideNx1_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv5x1Stride2x1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv5x1BorderStrideNx1_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv1x5Stride1x1_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FSx=1, FSy=5, Sx=1, Sy=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			int *out = Out+Wo*Ho*of;
			KerConv1x5Stride1x1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv1x5BorderStride1xN_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv1x5Stride1x1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv1x5BorderStride1xN_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv1x5Stride1x2_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FSx=1, FSy=5, Sx=1, Sy=2;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			int *out = Out+Wo*Ho*of;
			KerConv1x5Stride1x2_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv1x5BorderStride1xN_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv1x5Stride1x2_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv1x5BorderStride1xN_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv5x5Stride1_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FS=5, S=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
	int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FS*FS*of;
			int *out = Out+Wo*Ho*of;
			KerConv5x5Stride1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv5x5BorderStride1_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv5x5Stride1_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv5x5BorderStride1_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv5x5Stride2_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FS=5, S=2;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
	int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FS*FS*of;
			int *out = Out+Wo*Ho*of;
			KerConv5x5Stride2_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv5x5BorderStride2_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv5x5Stride2_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv5x5BorderStride2_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv5x5StrideS_DP_fp(KerConv_DP_fp_T *Arg)

{
	unsigned int FS=5, S=Arg->S;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FS*FS*of;
			int *out = Out+Wo*Ho*of;
			KerConv5x5StrideS_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
			if ((int)PadIn) KerConv5x5BorderStrideS_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConv5x5StrideS_Body_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
				if ((int)PadIn) KerConv5x5BorderStrideS_DP_fp(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConvNxNStrideS_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FS=Arg->N, S=Arg->S;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;

        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FS*FS*of;
			int *out = Out+Wo*Ho*of;
			KerConvNxNStrideS_Body_DP_fp(in, out, filter, FS, FS, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
			if ((int)PadIn) KerConvNxNStrideS_Border_DP_fp(in, out, filter, FS, FS, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConvNxNStrideS_Body_DP_fp(in, out, filter, FS, FS, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
				if ((int)PadIn) KerConvNxNStrideS_Border_DP_fp(in, out, filter, FS, FS, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConvNxMStrideSxSy_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FSx=Arg->N, Sx=Arg->S;
        unsigned int FSy=Arg->Ny, Sy=Arg->Sy;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;

        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			short int *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			int *out = Out+Wo*Ho*of;
			KerConvNxMStrideSxSy_Body_DP_fp(in, out, filter, FSx, FSy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn);
			if ((int)PadIn) KerConvNxMStrideSxSy_Border_DP_fp(in, out, filter, FSx, FSy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				short int *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				int *out = Out+Wo*Ho*(of);
				KerConvNxMStrideSxSy_Body_DP_fp(in, out, filter, FSx, FSy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn);
				if ((int)PadIn) KerConvNxMStrideSxSy_Border_DP_fp(in, out, filter, FSx, FSy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConvNxMDxDyStrideSxSy_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FSx=Arg->N, Sx=Arg->S;
        unsigned int FSy=Arg->Ny, Sy=Arg->Sy;
        int Dx=Arg->D, Dy=Arg->Dy;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int Norm = Arg->Norm;

        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-(Dx*(FSx-1)+1)+PadIn[0]+PadIn[1])/Sx + 1;
        int Wo_F = Min(Wo, FirstDefinedOutput((Dx*(FSx-1)+1), PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, (Dx*(FSx-1)+1), PadIn[0], Sx));
        int Ho = (Arg->UsedH-(Dy*(FSy-1)+1)+PadIn[2]+PadIn[3])/Sy + 1;
        int Ho_F = Min(Ho, FirstDefinedOutput((Dy*(FSy-1)+1), PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, (Dy*(FSy-1)+1), PadIn[2], Sy));

        if (TotalInFeatures<0) {
                for (unsigned int of=First; of<Last; of++) {
                        short int *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			int *out = Out+Wo*Ho*of;
                        KerConvNxMDxDyStrideSxSy_Body_DP_fp(in, out, filter, FSx, FSy, Dx, Dy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, Norm);
                        if ((int)PadIn) KerConvNxMDxDyStrideSxSy_Border_DP_fp(in, out, filter, FSx, FSy, Dx, Dy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, PadIn, Norm);
                }
        } else {
                unsigned int InFeatures = Arg->InFeatures;

                TotalInFeatures = Arg->TotalInFeatures;
                for (unsigned int of=First; of<Last; of++)
                        for (unsigned int If=0; If<InFeatures; If++) {
                                short int *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of  + If);
				int *out = Out+Wo*Ho*(of);
                                KerConvNxMDxDyStrideSxSy_Body_DP_fp(in, out, filter, FSx, FSy, Dx, Dy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, Norm);
                                if ((int)PadIn) KerConvNxMDxDyStrideSxSy_Border_DP_fp(in, out, filter, FSx, FSy, Dx, Dy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, PadIn, Norm);
                        }
        }
        gap_waitbarrier(0);
}

/*
	Optionally 0 padded convolutions.

	Input, output features and filters are bytes (_DP_fps) Dim=1,3,5,N, Stride=1,2,S

	Output feature maps are evaluated in parallel, one per core

	Argument data type: KerConv_DP_fps_T

	KerParConv1x1Stride1_DP_fps
	KerParConv1x1Stride2_DP_fps
	KerParConv1x1StrideS_DP_fps

	KerParConv3x1Stride1x1_DP_fps
		|------	KerConv3x1Stride1x1_Body_DP_fps
		|------	KerConv3x1StrideNx1_Border_DP_fps
	KerParConv3x1Stride2x1_DP_fps
		|------	KerConv3x1Stride2x1_Body_DP_fps
		|------	KerConv3x1StrideNx1_Border_DP_fps
	KerParConv1x3Stride1x1_DP_fps
		|------	KerConv1x3Stride1x1_Body_DP_fps
		|------	KerConv1x3Stride1xN_Border_DP_fps
	KerParConv1x3Stride1x2_DP_fps
		|------	KerConv1x3Stride1x2_Body_DP_fps
		|------	KerConv1x3Stride1xN_Border_DP_fps
	KerParConv3x3Stride1_DP_fps
		|------	KerConv3x3Stride1_Body_DP_fps
		|------	KerConv3x3Stride1_Border_DP_fps
	KerParConv3x3Stride2_DP_fps
		|------	KerConv3x3Stride2_Body_DP_fps
		|------	KerConv3x3Stride2_Border_DP_fps
	KerParConv3x3StrideS_DP_fps
		|------	KerConv3x3StrideS_Body_DP_fps
		|------	KerConv3x3StrideS_Border_DP_fps

	KerParConv5x1Stride1x1_DP_fps
		|------	KerConv5x1Stride1x1_Body_DP_fps
		|------	KerConv5x1StrideNx1_Border_DP_fps
	KerParConv5x1Stride2x1_DP_fps
		|------	KerConv5x1Stride2x1_Body_DP_fps
		|------	KerConv5x1StrideNx1_Border_DP_fps
	KerParConv1x5Stride1x1_DP_fps
		|------	KerConv1x5Stride1x1_Body_DP_fps
		|------	KerConv1x5Stride1xN_Border_DP_fps
	KerParConv1x5Stride1x2_DP_fps
		|------	KerConv1x5Stride1x2_Body_DP_fps
		|------	KerConv1x5Stride1xN_Border_DP_fps
	KerParConv5x5Stride1_DP_fps
		|------	KerConv5x5Stride1_Body_DP_fps
		|------	KerConv5x5Stride1_Border_DP_fps
	KerParConv5x5Stride2_DP_fps
		|------	KerConv5x5Stride2_Body_DP_fps
		|------	KerConv5x5Stride2_Border_DP_fps
	KerParConv5x5StrideS_DP_fps
		|------	KerConv5x5StrideS_Body_DP_fps
		|------	KerConv5x5StrideS_Border_DP_fps

	KerParConvNxNStrideS_DP_fps
		|------	KerConvNxNStrideS_Body_DP_fps
		|------	KerConvNxNStrideS_Border_DP_fps

	KerParConvNxMStrideSxSy_DP_fps
		|------	KerConvNxMStrideSxSy_Body_DP_fps
		|------	KerConvNxMStrideSxSy_Border_DP_fps

	KerParConvNxMDxDyStrideSxSy_DP_fps
		|------	KerConvNxMDxDyStrideSxSy_Body_DP_fps
		|------	KerConvNxMDxDyStrideSxSy_Border_DP_fps
*/

void KerParConv1x1Stride1_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FS=1, S=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FS*FS*of;
			short int *out = Out+Wo*Ho*of;
			KerConv1x1Stride1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv1x1Stride1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv1x1Stride2_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FS=1, S=2;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FS*FS*of;
			short int *out = Out+Wo*Ho*of;
			KerConv1x1Stride2_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv1x1Stride2_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv1x1StrideS_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FS=1, S=Arg->S;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FS*FS*of;
			short int *out = Out+Wo*Ho*of;
			KerConv1x1StrideS_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv1x1StrideS_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv3x1Stride1x1_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FSx=3, FSy=1, Sx=1, Sy=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			short int *out = Out+Wo*Ho*of;
			KerConv3x1Stride1x1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv3x1BorderStrideNx1_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv3x1Stride1x1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv3x1BorderStrideNx1_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv3x1Stride2x1_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FSx=3, FSy=1, Sx=2, Sy=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			short int *out = Out+Wo*Ho*of;
			KerConv3x1Stride2x1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv3x1BorderStrideNx1_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv3x1Stride2x1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv3x1BorderStrideNx1_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv1x3Stride1x1_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FSx=1, FSy=3, Sx=1, Sy=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			short int *out = Out+Wo*Ho*of;
			KerConv1x3Stride1x1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv1x3BorderStride1xN_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv1x3Stride1x1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv1x3BorderStride1xN_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv1x3Stride1x2_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FSx=1, FSy=3, Sx=1, Sy=2;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			short int *out = Out+Wo*Ho*of;
			KerConv1x3Stride1x2_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv1x3BorderStride1xN_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv1x3Stride1x2_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv1x3BorderStride1xN_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv3x3Stride1_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FS=3, S=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FS*FS*of;
			short int *out = Out+Wo*Ho*of;
			KerConv3x3Stride1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv3x3BorderStride1_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) {
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv3x3Stride1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv3x3BorderStride1_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
                	}
		}
	}
        gap_waitbarrier(0);
}

void KerParConv3x3Stride2_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FS=3, S=2;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FS*FS*of;
			short int *out = Out+Wo*Ho*of;
			KerConv3x3Stride2_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv3x3BorderStride2_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv3x3Stride2_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv3x3BorderStride2_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv3x3StrideS_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FS=3, S=Arg->S;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FS*FS*of;
			short int *out = Out+Wo*Ho*of;
			KerConv3x3StrideS_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
			if ((int)PadIn) KerConv3x3BorderStrideS_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv3x3StrideS_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
				if ((int)PadIn) KerConv3x3BorderStrideS_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv5x1Stride1x1_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FSx=5, FSy=1, Sx=1, Sy=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			short int *out = Out+Wo*Ho*of;
			KerConv5x1Stride1x1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv5x1BorderStrideNx1_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv5x1Stride1x1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv5x1BorderStrideNx1_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv5x1Stride2x1_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FSx=5, FSy=1, Sx=2, Sy=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			short int *out = Out+Wo*Ho*of;
			KerConv5x1Stride2x1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv5x1BorderStrideNx1_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv5x1Stride2x1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv5x1BorderStrideNx1_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv1x5Stride1x1_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FSx=1, FSy=5, Sx=1, Sy=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			short int *out = Out+Wo*Ho*of;
			KerConv1x5Stride1x1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv1x5BorderStride1xN_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv1x5Stride1x1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv1x5BorderStride1xN_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 1, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv1x5Stride1x2_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FSx=1, FSy=5, Sx=1, Sy=2;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;


        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			short int *out = Out+Wo*Ho*of;
			KerConv1x5Stride1x2_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv1x5BorderStride1xN_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv1x5Stride1x2_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv1x5BorderStride1xN_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, 2, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv5x5Stride1_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FS=5, S=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FS*FS*of;
			short int *out = Out+Wo*Ho*of;
			KerConv5x5Stride1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv5x5BorderStride1_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv5x5Stride1_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv5x5BorderStride1_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv5x5Stride2_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FS=5, S=2;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FS*FS*of;
			short int *out = Out+Wo*Ho*of;
			KerConv5x5Stride2_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
			if ((int)PadIn) KerConv5x5BorderStride2_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv5x5Stride2_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn);
				if ((int)PadIn) KerConv5x5BorderStride2_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConv5x5StrideS_DP_fps(KerConv_DP_fps_T *Arg)

{
	unsigned int FS=5, S=Arg->S;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FS*FS*of;
			short int *out = Out+Wo*Ho*of;
			KerConv5x5StrideS_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
			if ((int)PadIn) KerConv5x5BorderStrideS_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConv5x5StrideS_Body_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
				if ((int)PadIn) KerConv5x5BorderStrideS_DP_fps(in, out, filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConvNxNStrideS_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FS=Arg->N, S=Arg->S;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;

        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FS*FS*of;
			short int *out = Out+Wo*Ho*of;
			KerConvNxNStrideS_Body_DP_fps(in, out, filter, FS, FS, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
			if ((int)PadIn) KerConvNxNStrideS_Border_DP_fps(in, out, filter, FS, FS, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FS*FS*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConvNxNStrideS_Body_DP_fps(in, out, filter, FS, FS, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn);
				if ((int)PadIn) KerConvNxNStrideS_Border_DP_fps(in, out, filter, FS, FS, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConvNxMStrideSxSy_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FSx=Arg->N, Sx=Arg->S;
        unsigned int FSy=Arg->Ny, Sy=Arg->Sy;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;

        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));

	if (TotalInFeatures<0) {
        	for (unsigned int of=First; of<Last; of++) {
			signed char *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			short int *out = Out+Wo*Ho*of;
			KerConvNxMStrideSxSy_Body_DP_fps(in, out, filter, FSx, FSy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn);
			if ((int)PadIn) KerConvNxMStrideSxSy_Border_DP_fps(in, out, filter, FSx, FSy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, PadIn);
		}
	} else {
		unsigned int InFeatures = Arg->InFeatures;
        	
		TotalInFeatures = Arg->TotalInFeatures;
        	for (unsigned int of=First; of<Last; of++) 
                	for (unsigned int If=0; If<InFeatures; If++) {
				signed char *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of + If);
				short int *out = Out+Wo*Ho*(of);
				KerConvNxMStrideSxSy_Body_DP_fps(in, out, filter, FSx, FSy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn);
				if ((int)PadIn) KerConvNxMStrideSxSy_Border_DP_fps(in, out, filter, FSx, FSy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, PadIn);
                	}
	}
        gap_waitbarrier(0);
}

void KerParConvNxMDxDyStrideSxSy_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FSx=Arg->N, Sx=Arg->S;
        unsigned int FSy=Arg->Ny, Sy=Arg->Sy;
        int Dx=Arg->D, Dy=Arg->Dy;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        int TotalInFeatures = Arg->TotalInFeatures;
        unsigned int OutFeatures = Arg->OutFeatures;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int Norm = Arg->Norm;

        unsigned int CoreId = gap_coreid();
        unsigned int Chunk = ChunkSize(OutFeatures);
        unsigned int First = Chunk*CoreId;
        unsigned int Last = Min(First+Chunk, OutFeatures);
        v4s PadIn = Arg->Pad;

        int Wo = (Arg->UsedW-(Dx*(FSx-1)+1)+PadIn[0]+PadIn[1])/Sx + 1;
        int Wo_F = Min(Wo, FirstDefinedOutput((Dx*(FSx-1)+1), PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, (Dx*(FSx-1)+1), PadIn[0], Sx));
        int Ho = (Arg->UsedH-(Dy*(FSy-1)+1)+PadIn[2]+PadIn[3])/Sy + 1;
        int Ho_F = Min(Ho, FirstDefinedOutput((Dy*(FSy-1)+1), PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, (Dy*(FSy-1)+1), PadIn[2], Sy));

        if (TotalInFeatures<0) {
                for (unsigned int of=First; of<Last; of++) {
                        signed char *in = In+W*H*of, *filter = Filter+FSx*FSy*of;
			short int *out = Out+Wo*Ho*of;
                        KerConvNxMDxDyStrideSxSy_Body_DP_fps(in, out, filter, FSx, FSy, Dx, Dy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, Norm);
                        if ((int)PadIn) KerConvNxMDxDyStrideSxSy_Border_DP_fps(in, out, filter, FSx, FSy, Dx, Dy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, PadIn, Norm);
                }
        } else {
                unsigned int InFeatures = Arg->InFeatures;

                TotalInFeatures = Arg->TotalInFeatures;
                for (unsigned int of=First; of<Last; of++)
                        for (unsigned int If=0; If<InFeatures; If++) {
                                signed char *in = In+W*H*If, *filter = Filter+FSx*FSy*(TotalInFeatures*of  + If);
				short int *out = Out+Wo*Ho*(of);
                                KerConvNxMDxDyStrideSxSy_Body_DP_fps(in, out, filter, FSx, FSy, Dx, Dy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, Norm);
                                if ((int)PadIn) KerConvNxMDxDyStrideSxSy_Border_DP_fps(in, out, filter, FSx, FSy, Dx, Dy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, PadIn, Norm);
                        }
        }
        gap_waitbarrier(0);
}

/*
	Optionally 0 padded convolutions.

	Input, output features and filters are half words (_fp) Dim=1,3,5,N, Stride=1,2,S

	A single feature map is evaluated in parallel on all cores

	Argument data type: KerConv_DP_fp_T

	KerConv1x1Stride1_DP_fp
	KerConv1x1Stride2_DP_fp
	KerConv1x1StrideS_DP_fp

	KerConv3x1Stride1x1_DP_fp
		|------	KerConv3x1Stride1x1_Body_DP_fp
		|------	KerConv3x1StrideNx1_Border_DP_fp
	KerConv3x1Stride2x1_DP_fp
		|------	KerConv3x1Stride2x1_Body_DP_fp
		|------	KerConv3x1StrideNx1_Border_DP_fp
	KerConv1x3Stride1x1_DP_fp
		|------	KerConv1x3Stride1x1_Body_DP_fp
		|------	KerConv1x3Stride1xN_Border_DP_fp
	KerConv1x3Stride1x2_DP_fp
		|------	KerConv1x3Stride1x2_Body_DP_fp
		|------	KerConv1x3Stride1xN_Border_DP_fp
	KerConv3x3Stride1_DP_fp
		|------	KerConv3x3Stride1_Body_DP_fp
		|------	KerConv3x3Stride1_Border_DP_fp
	KerConv3x3Stride2_DP_fp
		|------	KerConv3x3Stride2_Body_DP_fp
		|------	KerConv3x3Stride2_Border_DP_fp
	KerConv3x3StrideS_DP_fp
		|------	KerConv3x3StrideS_Body_DP_fp
		|------	KerConv3x3StrideS_Border_DP_fp

	KerConv5x1Stride1x1_DP_fp
		|------	KerConv5x1Stride1x1_Body_DP_fp
		|------	KerConv5x1StrideNx1_Border_DP_fp
	KerConv5x1Stride2x1_DP_fp
		|------	KerConv5x1Stride2x1_Body_DP_fp
		|------	KerConv5x1StrideNx1_Border_DP_fp
	KerConv1x5Stride1x1_DP_fp
		|------	KerConv1x5Stride1x1_Body_DP_fp
		|------	KerConv1x5Stride1xN_Border_DP_fp
	KerConv1x5Stride1x2_DP_fp
		|------	KerConv1x5Stride1x2_Body_DP_fp
		|------	KerConv1x5Stride1xN_Border_DP_fp
	KerConv5x5Stride1_DP_fp
		|------	KerConv5x5Stride1_Body_DP_fp
		|------	KerConv5x5Stride1_Border_DP_fp
	KerConv5x5Stride2_DP_fp
		|------	KerConv5x5Stride2_Body_DP_fp
		|------	KerConv5x5Stride2_Border_DP_fp
	KerConv5x5StrideS_DP_fp
		|------	KerConv5x5StrideS_Body_DP_fp
		|------	KerConv5x5StrideS_Border_DP_fp

	KerConvNxNStrideS_DP_fp
		|------	KerConvNxNStrideS_Body_DP_fp
		|------	KerConvNxNStrideS_Border_DP_fp

	KerConvNxMStrideSxSy_DP_fp
		|------	KerConvNxMStrideSxSy_Body_DP_fp
		|------	KerConvNxNStrideSxSy_Border_DP_fp

	KerConvNxMDxDyStrideSxSy_DP_fp
		|------	KerConvNxMDxDyStrideSxSy_Body_DP_fp
		|------	KerConvNxNDxDyStrideSxSy_Border_DP_fp

*/

void KerConv1x1Stride1_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FS=1, S=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) KerConv1x1Stride1_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
        gap_waitbarrier(0);
}

void KerConv1x1Stride2_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FS=1, S=2;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) KerConv1x1Stride2_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
        gap_waitbarrier(0);
}

void KerConv1x1StrideS_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FS=1, S=Arg->S;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) KerConv1x1StrideS_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadOrg);
        gap_waitbarrier(0);
}

void KerConv3x1Stride1x1_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FSx=3, Sx=1;
        unsigned int FSy=1, Sy=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv3x1Stride1x1_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv3x1BorderStrideNx1_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv3x1Stride2x1_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FSx=3, Sx=2;
        unsigned int FSy=1, Sy=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv3x1Stride2x1_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv3x1BorderStrideNx1_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv1x3Stride1x1_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FSx=1, Sx=1;
        unsigned int FSy=3, Sy=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv1x3Stride1x1_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv1x3BorderStride1xN_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sy, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv1x3Stride1x2_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FSx=1, Sx=1;
        unsigned int FSy=3, Sy=2;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv1x3Stride1x2_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv1x3BorderStride1xN_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sy, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv3x3Stride1_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FS=3, S=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv3x3Stride1_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv3x3BorderStride1_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv3x3Stride2_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FS=3, S=2;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv3x3Stride2_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv3x3BorderStride2_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv3x3StrideS_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FS=3, S=Arg->S;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv3x3StrideS_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadOrg);
		if ((int)PadIn) KerConv3x3BorderStrideS_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}


void KerConv5x1Stride1x1_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FSx=5, Sx=1;
        unsigned int FSy=1, Sy=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv5x1Stride1x1_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv5x1BorderStrideNx1_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv5x1Stride2x1_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FSx=5, Sx=2;
        unsigned int FSy=1, Sy=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv5x1Stride2x1_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv5x1BorderStrideNx1_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv1x5Stride1x1_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FSx=1, Sx=1;
        unsigned int FSy=5, Sy=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv1x5Stride1x1_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv1x5BorderStride1xN_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sy, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv1x5Stride1x2_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FSx=1, Sx=1;
        unsigned int FSy=5, Sy=2;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv1x5Stride1x2_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv1x5BorderStride1xN_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sy, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv5x5Stride1_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FS=5, S=1;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv5x5Stride1_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv5x5BorderStride1_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv5x5Stride2_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FS=5, S=2;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv5x5Stride2_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv5x5BorderStride2_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv5x5StrideS_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FS=5, S=Arg->S;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv5x5StrideS_Body_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadOrg);
		if ((int)PadIn) KerConv5x5BorderStrideS_DP_fp(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConvNxNStrideS_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FS=Arg->N, S=Arg->S;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConvNxNStrideS_Body_DP_fp(In, Out, Filter, FS, FS, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadOrg);
		if ((int)PadIn) KerConvNxNStrideS_Border_DP_fp(In, Out, Filter, FS, FS, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}


void KerConvNxMStrideSxSy_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FSx=Arg->N, Sx=Arg->S;
        unsigned int FSy=Arg->Ny, Sy=Arg->Sy;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConvNxMStrideSxSy_Body_DP_fp(In, Out, Filter, FSx, FSy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadOrg);
		if ((int)PadIn) KerConvNxMStrideSxSy_Border_DP_fp(In, Out, Filter, FSx, FSy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConvNxMDxDyStrideSxSy_DP_fp(KerConv_DP_fp_T *Arg)

{
        unsigned int FSx=Arg->N, Sx=Arg->S;
        unsigned int FSy=Arg->Ny, Sy=Arg->Sy;
        int Dx=Arg->D, Dy=Arg->Dy;
        short int * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        short int * __restrict__ Filter = Arg->Filter;
        int * __restrict__ Out = Arg->Out;
        unsigned int Norm = Arg->Norm;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-(Dx*(FSx-1)+1)+PadIn[0]+PadIn[1])/Sx + 1;
        int Wo_F = Min(Wo, FirstDefinedOutput((Dx*(FSx-1)+1), PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, (Dx*(FSx-1)+1), PadIn[0], Sx));
        int Ho = (Arg->UsedH-(Dy*(FSy-1)+1)+PadIn[2]+PadIn[3])/Sy + 1;
        int Ho_F = Min(Ho, FirstDefinedOutput((Dy*(FSy-1)+1), PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, (Dy*(FSy-1)+1), PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
        v4s PadOrg = PadIn;
        unsigned int Chunk, First, Last;

        if (Arg->Orientation) { // Horizontal
                Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
                PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
                Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
        } else {
                Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
                PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
                Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
        if (First<Last) {
                KerConvNxMDxDyStrideSxSy_Body_DP_fp(In, Out, Filter, FSx, FSy, Dx, Dy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadOrg, Norm);
                if ((int)PadIn) KerConvNxMDxDyStrideSxSy_Border_DP_fp(In, Out, Filter, FSx, FSy, Dx, Dy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, PadOrg, Norm);
        }
        gap_waitbarrier(0);
}

/*
	Optionally 0 padded convolutions.

	Input, output features and filters are bytes (_DP_fps) Dim=1,3,5,N, Stride=1,2,S

	A single feature map is evaluated in parallel on all cores

	Argument data type: KerConv_DP_fps_T

	KerConv1x1Stride1_DP_fps
	KerConv1x1Stride2_DP_fps
	KerConv1x1StrideS_DP_fps

	KerConv3x1Stride1x1_DP_fps
		|------	KerConv3x1Stride1x1_Body_DP_fps
		|------	KerConv3x1StrideNx1_Border_DP_fps
	KerConv3x1Stride2x1_DP_fps
		|------	KerConv3x1Stride2x1_Body_DP_fps
		|------	KerConv3x1StrideNx1_Border_DP_fps
	KerConv1x3Stride1x1_DP_fps
		|------	KerConv1x3Stride1x1_Body_DP_fps
		|------	KerConv1x3Stride1xN_Border_DP_fps
	KerConv1x3Stride1x2_DP_fps
		|------	KerConv1x3Stride1x2_Body_DP_fps
		|------	KerConv1x3Stride1xN_Border_DP_fps
	KerConv3x3Stride1_DP_fps
		|------	KerConv3x3Stride1_Body_DP_fps
		|------	KerConv3x3Stride1_Border_DP_fps
	KerConv3x3Stride2_DP_fps
		|------	KerConv3x3Stride2_Body_DP_fps
		|------	KerConv3x3Stride2_Border_DP_fps
	KerConv3x3StrideS_DP_fps
		|------	KerConv3x3StrideS_Body_DP_fps
		|------	KerConv3x3StrideS_Border_DP_fps

	KerConv5x1Stride1x1_DP_fps
		|------	KerConv5x1Stride1x1_Body_DP_fps
		|------	KerConv5x1StrideNx1_Border_DP_fps
	KerConv5x1Stride2x1_DP_fps
		|------	KerConv5x1Stride2x1_Body_DP_fps
		|------	KerConv5x1StrideNx1_Border_DP_fps
	KerConv1x5Stride1x1_DP_fps
		|------	KerConv1x5Stride1x1_Body_DP_fps
		|------	KerConv1x5Stride1xN_Border_DP_fps
	KerConv1x5Stride1x2_DP_fps
		|------	KerConv1x5Stride1x2_Body_DP_fps
		|------	KerConv1x5Stride1xN_Border_DP_fps
	KerConv5x5Stride1_DP_fps
		|------	KerConv5x5Stride1_Body_DP_fps
		|------	KerConv5x5Stride1_Border_DP_fps
	KerConv5x5Stride2_DP_fps
		|------	KerConv5x5Stride2_Body_DP_fps
		|------	KerConv5x5Stride2_Border_DP_fps
	KerConv5x5StrideS_DP_fps
		|------	KerConv5x5StrideS_Body_DP_fps
		|------	KerConv5x5StrideS_Border_DP_fps

	KerConvNxNStrideS_DP_fps
		|------	KerConvNxNStrideS_Body_DP_fps
		|------	KerConvNxNStrideS_Border_DP_fps

	KerConvNxMStrideSxSy_DP_fps
		|------	KerConvNxMStrideSxSy_Body_DP_fps
		|------	KerConvNxMStrideSxSy_Border_DP_fps

	KerConvNxMDxDyStrideSxSy_DP_fps
		|------	KerConvNxMDxDyStrideSxSy_Body_DP_fps
		|------	KerConvNxMDxDyStrideSxSy_Border_DP_fps
*/

void KerConv1x1Stride1_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FS=1, S=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) KerConv1x1Stride1_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
        gap_waitbarrier(0);
}

void KerConv1x1Stride2_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FS=1, S=2;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) KerConv1x1Stride2_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
        gap_waitbarrier(0);
}

void KerConv1x1StrideS_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FS=1, S=Arg->S;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) KerConv1x1StrideS_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadOrg);
        gap_waitbarrier(0);
}

void KerConv3x1Stride1x1_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FSx=3, Sx=1;
        unsigned int FSy=1, Sy=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv3x1Stride1x1_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv3x1BorderStrideNx1_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv3x1Stride2x1_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FSx=3, Sx=2;
        unsigned int FSy=1, Sy=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv3x1Stride2x1_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv3x1BorderStrideNx1_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv1x3Stride1x1_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FSx=1, Sx=1;
        unsigned int FSy=3, Sy=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv1x3Stride1x1_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv1x3BorderStride1xN_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sy, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv1x3Stride1x2_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FSx=1, Sx=1;
        unsigned int FSy=3, Sy=2;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv1x3Stride1x2_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv1x3BorderStride1xN_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sy, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv3x3Stride1_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FS=3, S=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv3x3Stride1_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv3x3BorderStride1_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv3x3Stride2_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FS=3, S=2;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv3x3Stride2_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv3x3BorderStride2_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv3x3StrideS_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FS=3, S=Arg->S;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv3x3StrideS_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadOrg);
		if ((int)PadIn) KerConv3x3BorderStrideS_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv5x1Stride1x1_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FSx=5, Sx=1;
        unsigned int FSy=1, Sy=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv5x1Stride1x1_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv5x1BorderStrideNx1_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv5x1Stride2x1_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FSx=5, Sx=2;
        unsigned int FSy=1, Sy=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv5x1Stride2x1_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv5x1BorderStrideNx1_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv1x5Stride1x1_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FSx=1, Sx=1;
        unsigned int FSy=5, Sy=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv1x5Stride1x1_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv1x5BorderStride1xN_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sy, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv1x5Stride1x2_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FSx=1, Sx=1;
        unsigned int FSy=5, Sy=2;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv1x5Stride1x2_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv1x5BorderStride1xN_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sy, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv5x5Stride1_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FS=5, S=1;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv5x5Stride1_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv5x5BorderStride1_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv5x5Stride2_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FS=5, S=2;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv5x5Stride2_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadOrg);
		if ((int)PadIn) KerConv5x5BorderStride2_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConv5x5StrideS_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FS=5, S=Arg->S;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConv5x5StrideS_Body_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadOrg);
		if ((int)PadIn) KerConv5x5BorderStrideS_DP_fps(In, Out, Filter, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConvNxNStrideS_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FS=Arg->N, S=Arg->S;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FS+PadIn[0]+PadIn[1])/S + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FS, PadIn[0], S)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FS, PadIn[0], S));
        int Ho = (Arg->UsedH-FS+PadIn[2]+PadIn[3])/S + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FS, PadIn[2], S)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FS, PadIn[2], S));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConvNxNStrideS_Body_DP_fps(In, Out, Filter, FS, FS, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadOrg);
		if ((int)PadIn) KerConvNxNStrideS_Border_DP_fps(In, Out, Filter, FS, FS, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, S, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConvNxMStrideSxSy_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FSx=Arg->N, Sx=Arg->S;
        unsigned int FSy=Arg->Ny, Sy=Arg->Sy;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-FSx+PadIn[0]+PadIn[1])/Sx + 1;
	int Wo_F = Min(Wo, FirstDefinedOutput(FSx, PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, FSx, PadIn[0], Sx));
        int Ho = (Arg->UsedH-FSy+PadIn[2]+PadIn[3])/Sy + 1;
	int Ho_F = Min(Ho, FirstDefinedOutput(FSy, PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, FSy, PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
	v4s PadOrg = PadIn;
	unsigned int Chunk, First, Last;

	if (Arg->Orientation) { // Horizontal
		Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
		PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
		Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
	} else {
		Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
		PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
		Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
	if (First<Last) {
		KerConvNxMStrideSxSy_Body_DP_fps(In, Out, Filter, FSx, FSy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadOrg);
		if ((int)PadIn) KerConvNxMStrideSxSy_Border_DP_fps(In, Out, Filter, FSx, FSy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, PadOrg);
	}
        gap_waitbarrier(0);
}

void KerConvNxMDxDyStrideSxSy_DP_fps(KerConv_DP_fps_T *Arg)

{
        unsigned int FSx=Arg->N, Sx=Arg->S;
        unsigned int FSy=Arg->Ny, Sy=Arg->Sy;
        int Dx=Arg->D, Dy=Arg->Dy;
        signed char * __restrict__ In = Arg->In;
        unsigned int W = Arg->W;
        unsigned int H = Arg->H;
        signed char * __restrict__ Filter = Arg->Filter;
        short int * __restrict__ Out = Arg->Out;
        unsigned int Norm = Arg->Norm;
        v4s PadIn = Arg->Pad;
        int Wo = (Arg->UsedW-(Dx*(FSx-1)+1)+PadIn[0]+PadIn[1])/Sx + 1;
        int Wo_F = Min(Wo, FirstDefinedOutput((Dx*(FSx-1)+1), PadIn[0], Sx)), Wo_L = Max(Wo_F, LastDefinedOutput(Arg->UsedW, (Dx*(FSx-1)+1), PadIn[0], Sx));
        int Ho = (Arg->UsedH-(Dy*(FSy-1)+1)+PadIn[2]+PadIn[3])/Sy + 1;
        int Ho_F = Min(Ho, FirstDefinedOutput((Dy*(FSy-1)+1), PadIn[2], Sy)), Ho_L = Max(Ho_F, LastDefinedOutput(Arg->UsedH, (Dy*(FSy-1)+1), PadIn[2], Sy));
        unsigned int CoreId = gap_coreid();
        v4s PadOrg = PadIn;
        unsigned int Chunk, First, Last;

        if (Arg->Orientation) { // Horizontal
                Chunk = ChunkSize(Wo); First = Chunk*CoreId; Last = Min(First+Chunk, Wo);
                PadIn[0] *= (First==0); PadIn[1] *= (Last==Wo);
                Wo_F = Max(First, Wo_F); Wo_L = Min(Last, Wo_L);
        } else {
                Chunk = ChunkSize(Ho); First = Chunk*CoreId; Last = Min(First+Chunk, Ho);
                PadIn[2] *= (First==0); PadIn[3] *= (Last==Ho);
                Ho_F = Max(First, Ho_F); Ho_L = Min(Last, Ho_L);
        }
        if (First<Last) {
                KerConvNxMDxDyStrideSxSy_Body_DP_fps(In, Out, Filter, FSx, FSy, Dx, Dy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadOrg, Norm);
                if ((int)PadIn) KerConvNxMDxDyStrideSxSy_Border_DP_fps(In, Out, Filter, FSx, FSy, Dx, Dy, W, H, Wo, Wo_F, Wo_L, Ho, Ho_F, Ho_L, Sx, Sy, PadIn, PadOrg, Norm);
        }
        gap_waitbarrier(0);
}

