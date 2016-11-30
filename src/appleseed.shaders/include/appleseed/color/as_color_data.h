
//
// This source file is part of appleseed.
// Visit http://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2016 Luis Barrancos, The appleseedhq Organization
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#ifndef AS_COLOR_DATA_H
#define AS_COLOR_DATA_H

///////////////////////////////////////////////////////////////////////////////

// The xyz chromaticity coordinates for the RGB color spaces (z=1-x-y), and Y.

// ITU-R/BT.601 https://www.itu.int/rec/R-REC-BT.601-7-201103-I/en
#define BT601_CHROMATICITIES_Rxyz           0.640, 0.330, 0.030
#define BT601_CHROMATICITIES_Gxyz           0.290, 0.600, 0.110
#define BT601_CHROMATICITIES_Bxyz           0.150, 0.060, 0.790

#define BT601_LUMINANCE_COEFFS              0.299, 0.587, 0.114
i
// ITU-R/BT.709 https://www.itu.int/rec/R-REC-BT.709/en
#define BT709_CHROMATICITIES_Rxyz           0.640, 0.330, 0.030
#define BT709_CHROMATICITIES_Gxyz           0.300, 0.600, 0.100
#define BT709_CHROMATICITIES_Bxyz           0.150, 0.060, 0.790

#define BT709_LUMINANCE_COEFFS              0.212656, 0.715158, 0.072186  

// ITU-R/BT.1886 https://www.itu.int/rec/R-REC-BT.1886-0-201103-I/en
// BT.1886 and BT.709 share primaries chromaticity coordinates, but not Y
#define BT1886_CHROMATICITIES_Rxyz          BT709_CHROMATICITIES_Rxyz
#define BT1886_CHROMATICITIES_Gxyz          BT709_CHROMATICITIES_Gxyz
#define BT1886_CHROMATICITIES_Bxyz          BT709_CHROMATICITIES_Bxyz

#define BT1886_LUMINANCE_COEFFS             0.2627, 0.6780, 0.0593

// ITU-R/BT.2020 https://www.itu.int/rec/R-REC-BT.2020/en
#define BT2020_CHROMATICITIES_Rxyz          0.708, 0.292, 0.000
#define BT2020_CHROMATICITIES_Gxyz          0.170, 0.797, 0.033
#define BT2020_CHROMATICITIES_Bxyz          0.131, 0.046, 0.823

#define BT2020_LUMINANCE_COEFFS             0.2627, 0.6780, 0.0593

// ITU-R/BT.2100 https://www.itu.int/rec/R-REC-BT.2100
// BT.2100 and BT.2020 share primaries chromaticity coordinates, Y. 
#define BT2100_CHROMATICITIES_Rxyz          BT2020_CHROMATICITIES_Rxyz
#define BT2100_CHROMATICITIES_Gxyz          BT2020_CHROMATICITIES_Gxyz
#define BT2100_CHROMATICITIES_Bxyz          BT2020_CHROMATICITIES_Bxyz

#define BT2100_LUMINANCE_COEFFS             BT2020_LUMINANCE_COEFFS

// sRGB https://www.w3.org/Graphics/Color/sRGB.html
// sRGB and BT.709 share primaries chromaticity coordinates, Y.
#define SRGB_CHROMATICITIES_Rxyz            BT709_CHROMATICITIES_Rxyz
#define SRGB_CHROMATICITIES_Gxyz            BT709_CHROMATICITIES_Gxyz
#define SRGB_CHROMATICITIES_Bxyz            BT709_CHROMATICITIES_Bxyz

#define SRGB_LUMINANCE_COEFFS               BT709_LUMINANCE_COEFFS

// AdobeRGB http://www.adobe.com/digitalimag/pdfs/AdobeRGB1998.pdf
#define ADOBERGB98_CHROMATICITIES_x         0.640, 0.330, 0.030
#define ADOBERGB98_CHROMATICITIES_y         0.210, 0.710, 0.080
#define ADOBERGB98_CHROMATICITIES_w         0.150, 0.060, 0.790

#define ADOBERGB98_LUMINANCE_COEFFS         0.297361, 0.627355, 0.075284

// White point xyz chromaticity coordinates, and XYZ coordinates,
// CIE1931 2° standard observer, Hernandez 1999 method (WXYZ = (1/Wy)Wxyz).
#define D55_WHITEPOINT_CIE1931_2DEG_xyz     0.33242, 0.34743, 0.32015
#define D60_WHITEPOINT_CIE1931_2DEG_xyz     0.32168, 0.33767, 0.34065
#define D65_WHITEPOINT_CIE1931_2DEG_xyz     0.31270, 0.32900, 0.35830
#define DCIP3_WHITEPOINT_CIE1931_2DEG_xyz   0.31400, 0.35100, 0.33500

#define D55_WHITEPOINT_CIE1931_2DEG_XYZ     0.956797, 1.000000, 0.921480
#define D60_WHITEPOINT_CIE1931_2DEG_XYZ     0.952646, 1.000000, 1.008825
#define D65_WHITEPOINT_CIE1931_2DEG_XYZ     0.950455, 1.000000, 1.089057
#define DCIP3_WHITEPOINT_CIE1931_2DEG_XYZ   0.894586, 1.000000, 0.954415

// The XYZ<>RGB 3x3 matrices for given color space and white points.
// http://www.brucelindbloom.com/index.html?ChromAdaptEval.html
#define RGB2XYZ_D65_ADOBE_RGB98             \
    0.5767309 , 0.1855540 , 0.1881852 ,     \
    0.2973769 , 0.6273491 , 0.0752741 ,     \
    0.0270343 , 0.0706872 , 0.9911085

#define RGB2XYZ_D65_SRGB                    \
    0.4124564 , 0.3575671 , 0.1804375 ,     \
    0.2126729 , 0.7151522 , 0.0721750 ,     \
    0.0193339 , 0.1191920 , 0.9503041

#define XYZ2RGB_D65_ADOBE_RGB98             \
    2.0413690 ,-0.5649464 ,-0.3446944 ,     \
   -0.9692660 , 1.8760108 , 0.0415560 ,     \
    0.0134474 ,-0.1183897 , 1.0154096

#define XYZ2RGB_D65_SRGB                    \
    3.2404542 ,-1.5371385 ,-0.4985314 ,     \
   -0.9692660 , 1.8760108 , 0.0415560 ,     \
    0.0556434 ,-0.2040259 , 1.0572252

#define XYZ2RGB_D65_BT709                   \
    0.5766690,  0.1855582,  0.1882286,      \
    0.2973450,  0.6273636,  0.0752915,      \
    0.0270314,  0.0706889,  0.9913375

#define RGB2XYZ_D65_BT709                   \
    2.0415879, -0.5650070, -0.3447314,      \
   -0.9692436,  1.8759675,  0.0415551,      \
    0.0134443, -0.1183624,  1.0151750

///////////////////////////////////////////////////////////////////////////////
#endif /// AS_COLOR_DATA_H
    
