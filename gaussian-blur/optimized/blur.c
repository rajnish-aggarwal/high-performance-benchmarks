/**
 * @file blur.c
 * @author Nick Kiesel (nkiesel)
 *
 * This file contains the optimized implementation of Gaussian blur for the
 * Intel Haswell CPU architecture
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <strings.h>
#include <immintrin.h>
#include <limits.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../third-party/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third-party/stb_image_write.h"

#define KERNEL_WIDTH 8
#define KERNEL_HEIGHT 7

static __inline__ unsigned long long rdtsc(void)
{
  unsigned hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

/**
 * Stores an image in row-major format. Each color channel is accessed from
 * the top level (e.g. data[channel][pixel]) so that pixels of the same channel
 * are contiguous in memory.
 */
typedef struct Image {
    int channels;
    int height;
    int width;
    unsigned char **data;
} Image;

typedef struct fImage {
    int channels;
    int height;
    int width;
    float **data;
} fImage;

void optKernel(float *outImage, int oStride, float *inImage, int iStride,
        float *filter, int filterWidth)
{
    __m256 in[KERNEL_HEIGHT];
    __m256 out[KERNEL_HEIGHT];
    __m256 mask;

    // load in output elements
    out[0] = _mm256_loadu_ps(outImage);
    out[1] = _mm256_loadu_ps(outImage+oStride);
    out[2] = _mm256_loadu_ps(outImage+2*oStride);
    out[3] = _mm256_loadu_ps(outImage+3*oStride);
    out[4] = _mm256_loadu_ps(outImage+4*oStride);
    out[5] = _mm256_loadu_ps(outImage+5*oStride);
    out[6] = _mm256_loadu_ps(outImage+6*oStride);

    int i, j;
    for (i = 0; i < filterWidth; i++) {
        for (j = 0; j < filterWidth; j++) {
            // load up appropriate element of the mask
            mask = _mm256_broadcast_ss(filter + i*filterWidth + j);

            // load in input image elements
            in[0] = _mm256_loadu_ps(inImage + i*iStride + j);
            in[1] = _mm256_loadu_ps(inImage + (i+1)*iStride + j);
            in[2] = _mm256_loadu_ps(inImage + (i+2)*iStride + j);
            in[3] = _mm256_loadu_ps(inImage + (i+3)*iStride + j);
            in[4] = _mm256_loadu_ps(inImage + (i+4)*iStride + j);
            in[5] = _mm256_loadu_ps(inImage + (i+5)*iStride + j);
            in[6] = _mm256_loadu_ps(inImage + (i+6)*iStride + j);

            out[0] = _mm256_fmadd_ps(in[0], mask, out[0]);
            out[1] = _mm256_fmadd_ps(in[1], mask, out[1]);
            out[2] = _mm256_fmadd_ps(in[2], mask, out[2]);
            out[3] = _mm256_fmadd_ps(in[3], mask, out[3]);
            out[4] = _mm256_fmadd_ps(in[4], mask, out[4]);
            out[5] = _mm256_fmadd_ps(in[5], mask, out[5]);
            out[6] = _mm256_fmadd_ps(in[6], mask, out[6]);
        }
    }

    _mm256_storeu_ps(outImage, out[0]);
    _mm256_storeu_ps(outImage+oStride, out[1]);
    _mm256_storeu_ps(outImage+2*oStride, out[2]);
    _mm256_storeu_ps(outImage+3*oStride, out[3]);
    _mm256_storeu_ps(outImage+4*oStride, out[4]);
    _mm256_storeu_ps(outImage+5*oStride, out[5]);
    _mm256_storeu_ps(outImage+6*oStride, out[6]);
}

void blur(fImage *outImage, fImage *inImage, fImage *filter) {
    int c, ih, iw;
    for (c = 0; c < outImage->channels; c++) {
        for (ih = 0; ih < outImage->height; ih += KERNEL_HEIGHT) {
            for (iw = 0; iw < outImage->width; iw += KERNEL_WIDTH) {
                optKernel(outImage->data[c] + ih*outImage->width + iw,
                          outImage->width,
                          inImage->data[c] + ih*inImage->width + iw,
                          inImage->width,
                          filter->data[0],
                          filter->width);
            }
        }
    }
}

/**
 * Loads a png image from disk. If the image has 4 channels (i.e. is RGBA),
 * the transparency channel is ignored. If the image has more than 4 channels,
 * an error is thrown and the program exits.
 */
void loadImage(Image* image, const char *filename)
{
    int x, y, n;
    unsigned char *imageData = stbi_load(filename, &x, &y, &n, 0);

    // store data in custom container
    image->channels = n;
    image->height = y;
    image->width = x;

    image->data =
        (unsigned char**)malloc(image->channels * sizeof(unsigned char*));
    int i, j, k;
    for (i = 0; i < image->channels; i++) {
        image->data[i] = (unsigned char*)malloc(x*y * sizeof(unsigned char));
        for (j = 0; j < y; j++) {
            for (k = 0; k < x; k++) {
                image->data[i][j*x + k] = imageData[n*(j*x + k) + i];
            }
        }
    }
    
    fprintf(stderr, "image details: %dx%d, %d channel(s)\n", image->width,
            image->height, image->channels);
    free(imageData);
}

/**
 * Saves png image to disk. Must be a 3-channel image (i.e. RGB).
 */
void saveImage(Image* image, const char *filename)
{
    unsigned char *imageData;
    int numPixels = image->height * image->width * image->channels;
    imageData = (unsigned char*)malloc(numPixels * sizeof(unsigned char));

    int y, x, c;
    for (y = 0; y < image->height; y++) {
        for (x = 0; x < image->width; x++) {
            for (c = 0; c < image->channels; c++) {
                imageData[image->channels*(y*image->width + x) + c] =
                    image->data[c][y*image->width + x];
            }
        }
    }

    stbi_write_png(filename, image->width, image->height, image->channels,
            imageData, image->channels*image->width*sizeof(unsigned char));

    free(imageData);
}

/**
 * Generates a Gaussian filter of the desired radius. Radius is the number of
 * rows/columns on either side of the center. E.g. if radius is 2, then a 5x5
 * mask is made (2 rows/columns in each direction). If radius is 4, then
 * a 9x9 mask is made.
 *
 * This is a once-and-done event, so no optimizations here.
 */
void generateGaussian(fImage *filter, int radius, float sigma)
{
    filter->channels = 1;
    filter->height = filter->width = 2*radius + 1;

    filter->data = (float**)malloc(sizeof(float*));
    int numElements = filter->height * filter->width;
    filter->data[0] = (float*)malloc(numElements * sizeof(float));

    float twoSigmaSqrd = 2.0*sigma*sigma;
    float sum = 0.0;
    int i, j;
    for (i = 0; i < filter->height; i++) {
        float y = i - radius;
        for (j = 0; j < filter->width; j++) {
            float x = j - radius;
            filter->data[0][i*filter->width + j] =
                exp(-((x*x + y*y)/twoSigmaSqrd)) / (M_PI * twoSigmaSqrd);
            sum += filter->data[0][i*filter->width + j];
        }
    }

    // normalize
    for (i = 0; i < filter->height; i++) {
        for (j = 0; j < filter->width; j++) {
            filter->data[0][i*filter->width + j] /= sum;
        }
    }
}

int main(int argc, char *argv[])
{
    // do multiple iterations to get a steady time
    int iterations = atoi(argv[1]);
    int filterRadius = atoi(argv[2]);
    unsigned long long start, end, sum = 0;

    int i, j;

    // load in image to be blurred
    Image image;
    loadImage(&image, "image.png");

    // convert image to floats and add padding
    fImage inImage;
    inImage.channels = image.channels;
    inImage.height = image.height + 2*filterRadius;
    inImage.width = image.width + 2*filterRadius;
    inImage.data = (float**)malloc(inImage.channels * sizeof(float*));
    int numPixelsPerChannel = inImage.width * inImage.height;
    for (i = 0; i < inImage.channels; i++) {
        inImage.data[i] = (float*)malloc(numPixelsPerChannel * sizeof(float));
        bzero(inImage.data[i], numPixelsPerChannel * sizeof(float));
    }

    int c;
    for (c = 0; c < image.channels; c++) {
        for (i = 0; i < image.height; i++) {
            for (j = 0; j < image.width; j++) {
                inImage.data[c][(i+filterRadius)*inImage.width + j+filterRadius] =
                    (float)image.data[c][i*image.width + j] / (float)UCHAR_MAX;
            }
        }
    }

    // create output space
    fImage outImage;
    outImage.channels = image.channels;
    outImage.height = image.height;
    outImage.width = image.width;
    outImage.data = (float**)malloc(outImage.channels * sizeof(float*));
    numPixelsPerChannel = outImage.width * outImage.height;
    for (i = 0; i < outImage.channels; i++) {
        outImage.data[i] = (float*)malloc(numPixelsPerChannel * sizeof(float));
    }

    // generate gaussian filter: store it as a single channel image
    fImage filter;
    generateGaussian(&filter, filterRadius, 1.0f);

    fprintf(stderr, "performing %d blur(s)...\n", iterations);
    // do the blur
    for (i = 0; i < iterations; i++) {
        // reset data
        for (j = 0; j < outImage.channels; j++) {
            bzero(outImage.data[j], numPixelsPerChannel * sizeof(float));
        }

        start = rdtsc();
        blur(&outImage, &inImage, &filter);
        end = rdtsc();
        sum += (end - start);
    }

    Image outImageChar;
    outImageChar.channels = outImage.channels;
    outImageChar.height = outImage.height;
    outImageChar.width = outImage.width;
    outImageChar.data =
        (unsigned char**)malloc(outImageChar.channels * sizeof(unsigned char*));
    for (c = 0; c < outImageChar.channels; c++) {
        outImageChar.data[c] =
            (unsigned char*)malloc(numPixelsPerChannel * sizeof(unsigned char));
    }

    // scale the image back to unsigned char
    for (c = 0; c < outImage.channels; c++) {
        for (i = 0; i < outImage.height; i++) {
            for (j = 0; j < outImage.width; j++) {
                outImageChar.data[c][i*outImage.width + j] =
                    outImage.data[c][i*outImage.width + j] * UCHAR_MAX;
            }
        }
    }

    fprintf(stderr, "saving blurred iamge...\n");
    saveImage(&outImageChar, "newImage.png");

    // just print out number of cycles
    printf("%f\n", (float)sum * (3.2 / 2.4) / (float)iterations);

    // free here
    // image
    for (i = 0; i < image.channels; i++) {
        free(image.data[i]);
    }
    free(image.data);
    // filter - we know it's 1-D, so no loop
    free(filter.data[0]);
    free(filter.data);
    // out image
    for (i = 0; i < outImage.channels; i++) {
        free(outImage.data[i]);
    }
    free(outImage.data);

    return 0;
}
