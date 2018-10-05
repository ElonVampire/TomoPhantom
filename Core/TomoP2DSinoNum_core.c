/*
 * Copyright 2017 Daniil Kazantsev
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <math.h>
#include <stdlib.h>
#include <memory.h>
#include <stdio.h>
#include "omp.h"

#include "utils.h"
#include "TomoP2DSinoNum_core.h"

#define M_PI 3.14159265358979323846
#define EPS 0.000000001
#define MAXCHAR 1000

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

/* C OMP implementation of the forward projection (The Radon Transform)
 * by rotating a padded image and summing over columns (rotation-based projector 
 * for parallel beam)
 *
 * Input Parameters:
 * 1. Phantom to calculate projections from [required]
 * 2. Detector array size P (in pixels) [required]
 * 3. Projection angles Theta (in degrees) [required]
 *
 * Output:
 * Sinogram [No. angles] x [No. detectors]
 */
 

float TomoP2DSinoNum_core(float *Sinogram, float *Phantom, int dimX, int DetSize, float *Theta, int ThetaLength, int sys)
{    
	int i, j, k, padXY;
	float *Phantom_pad=NULL, *B=NULL,  angC, sumSin;
    
    padXY = ceil(0.5f*(DetSize - dimX)); /*Size for padding the phantom*/ 
    
    /*Perform padding of the phantom to the size [DetSize x DetSize] */
    Phantom_pad = (float*) calloc(DetSize*DetSize,sizeof(float));  /*allocating space*/
    padding(Phantom, Phantom_pad, DetSize, dimX, padXY, sys);
    
    /*calculation of the Center of Gravity of an image */
    float COG_X=0.0f, COG_Y=0.0f, total=0.0f;
    for (i=0; i < DetSize; i++) {
        for (j=0; j < DetSize; j++) {
         COG_X += Phantom_pad[j*DetSize+i]*i;
         COG_Y += Phantom_pad[j*DetSize+i]*j;
         total += Phantom_pad[j*DetSize+i];
        }
    }
    COG_X = COG_X/total;
    COG_Y = COG_Y/total;    
    printf("%f %f \n", COG_X, COG_Y);
    
    
    /* setting OMP here */
    #pragma omp parallel for shared (Phantom_pad, Sinogram, dimX, DetSize, Theta, ThetaLength, COG_X, COG_Y) private(B, k, j, i, sumSin, angC)    
    for (k=0; k < ThetaLength; k++) {
        
        B = (float*) calloc(DetSize*DetSize,sizeof(float));
        
        angC = Theta[k]*M_PI/180.0f;
        //ct = cos(angC + 0.5f*M_PI);
        //st = sin(angC + 0.5f*M_PI);
        
        // BilinearInterpolation(Phantom_pad, B, DetSize, ct, st); /* perform interpolation to rotate image on angle angC */
        BilinearInterpolationV2(Phantom_pad, B, DetSize, angC, COG_X, COG_Y); /* perform interpolation to rotate an image on the angle angC */
        
        for (j=0; j < DetSize; j++) {
            sumSin = 0.0f;
            Sinogram[j*ThetaLength + k] = 0.0f;
            for (i=0; i < DetSize; i++) sumSin += B[i*DetSize+j];
            Sinogram[j*ThetaLength + k] = sumSin;
        }
        free(B);
    }    
    
    /*freeing the memory*/
   free(Phantom_pad);
   return  *Sinogram;     
}    

/*
float BilinearInterpolation(float *Phantom_pad, float *B, int DetSize, float radian, float st)
{
	
    int i, j, k, i0, j0, i1, j1;
    float *xs, H_x, s_min, s_max, stepS, x_rs, y_rs, dhalf, xbar, ybar;
    dhalf = 0.5f*DetSize;
    
    xs = (float*)calloc(DetSize,sizeof(float));   
    H_x = 1.0f/DetSize;
    s_min = -1.0f + H_x;
    s_max = 1.0f - H_x;
    stepS = 2.0f/DetSize;
    for (k=0; k < DetSize; k++) {xs[k] = s_min + (k)*(stepS);}
    
    for (i=0; i < DetSize; i++) {
        for (j=0; j < DetSize; j++) {
            
            x_rs = dhalf*(xs[j]*ct - xs[i]*st + s_max);
            y_rs = dhalf*(xs[j]*st + xs[i]*ct + s_max);
            
            i1 = MIN(floor(y_rs), DetSize+2);
            j1 = MIN(floor(x_rs), DetSize+2);
            i0 = MAX(i1, 0);
            j0 = MAX(j1, 0);
            
            xbar = x_rs - j0;
            ybar = y_rs - i0;
            
            i0 = i0+1; j0=j0+1;
            
            if ((i0 < DetSize) && (j0 < DetSize)) {
                B[i*DetSize+j] = Phantom_pad[i0*DetSize+j0]*(1.-xbar)*(1.-ybar)+Phantom_pad[(i0+1)*DetSize+j0]*ybar*(1.-xbar)+Phantom_pad[i0*DetSize+(j0+1)]*xbar*(1.-ybar)+Phantom_pad[(i0+1)*DetSize+(j0+1)]*xbar*ybar;
            }
        }}    
    free(xs);
    return *B;
}
*/
float BilinearInterpolationV2(float *Phantom_pad, float *B, int DetSize, float radian, float COG_X, float COG_Y)
{
    // radian = angle as rad (2*PI*{angle in deg})/360)
	// COGPosX & COGPosY are the Centre of Gravity pos for the Input Matrix
	// SliceMatrix is the un-rotated input Matrix
	int y,x, rotX, rotY, xfloor, yfloor;
    float rotatedX, rotatedY, p1, p2, p3, p4, f1, f2, fval, COGPosX, COGPosY;
    
    float cosine = (float)cos(radian);
	float sine = (float)sin(radian);
	
	COGPosX = COG_X;
	COGPosY = COG_Y;

	for(y=0;y<DetSize;y++)	{
		for(x=0;x<DetSize;x++)    {
			
        // Calculate rotated Matrix positions
        rotatedX=(float)((x-COGPosX)*cosine)-((y-COGPosY)*sine)+COGPosX;
        rotatedY=(float)((x-COGPosX)*sine)+((y-COGPosY)*cosine)+COGPosY;

                
        rotX = (int)floor(rotatedX);
        rotY = (int)floor(rotatedY);

        xfloor = floor(rotatedX);
        yfloor = floor(rotatedY);
                
        if(rotX >=0 && rotY < DetSize-1 && rotY >=0 && rotY < DetSize-1 )
        {
            // Bilinear interpolation part
            p1 = Phantom_pad[rotX+(DetSize*rotY)];         // 0,0
            p2 = Phantom_pad[rotX+(DetSize*(rotY+1))];     // 0,1
            p3 = Phantom_pad[(rotX+1)+(DetSize*rotY)];     // 1,0
            p4 = Phantom_pad[(rotX+1)+(DetSize*(rotY+1))]; // 1,1

            f1 = p1 + (p3-p1)*(rotatedX-xfloor);
            f2 = p2 + (p4-p2)*(rotatedX-xfloor);
            fval = f1 + (f2-f1)*(rotatedY-yfloor);

            B[x+(DetSize*y)]= fval; 
        }    
    }
}       
    return *B;
}



float padding(float *Phantom, float *Phantom_pad, int DetSize, int PhantSize, int padXY, int sys)
{
    int i,j;   
    #pragma omp parallel for shared (Phantom_pad, Phantom) private(i, j)
    for (i=0; i < DetSize; i++) {
        for (j=0; j < DetSize; j++) {
            Phantom_pad[j*DetSize+i] = 0.0f;
             if (((i >= padXY+1) && (i < DetSize-padXY)) &&  ((j >= padXY) && (j < DetSize-padXY))) {
                  if (sys == 0) Phantom_pad[j*DetSize+i] = Phantom[(i-padXY)*PhantSize + (j-padXY)];
                  else Phantom_pad[j*DetSize+i] = Phantom[(j-padXY)*PhantSize + (i-padXY)];
             }
        }}
    return *Phantom_pad;
}
