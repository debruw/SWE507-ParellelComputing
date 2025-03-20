#include<stdio.h>
#include<malloc.h>
#include<pthread.h>
#include<math.h>
#include<stdint.h>
#include<time.h>
#include <stdbool.h>

typedef uint8_t  BYTE;
typedef uint32_t DWORD;
typedef int32_t  LONG;
typedef uint16_t WORD;

typedef struct
{
    WORD   bfType;
    DWORD  bfSize;
    WORD   bfReserved1;
    WORD   bfReserved2;
    DWORD  bfOffBits;
} __attribute__((__packed__))
BITMAPFILEHEADER;

typedef struct
{
    DWORD  biSize;
    LONG   biWidth;
    LONG   biHeight;
    WORD   biPlanes;
    WORD   biBitCount;
    DWORD  biCompression;
    DWORD  biSizeImage;
    LONG   biXPelsPerMeter;
    LONG   biYPelsPerMeter;
    DWORD  biClrUsed;
    DWORD  biClrImportant;
} __attribute__((__packed__))
BITMAPINFOHEADER;

typedef struct
{
    BYTE  rgbtBlue;
    BYTE  rgbtGreen;
    BYTE  rgbtRed;
} __attribute__((__packed__))
RGBTRIPLE;

typedef struct {
    int height;
    int width;
    BYTE(*temp)[512];
    int startW;
    int startH;
    int endW;
    int endH;
}__attribute__((__packed__))
PIXELTHREADARGS;

void WriteRGBTRIPLE(int height, int width, BITMAPFILEHEADER bf, BITMAPINFOHEADER bi, char* offbits, RGBTRIPLE image[height][width]);
void blurSeq(int height, int width, RGBTRIPLE image[height][width]);
void *blurThreadPixel(void *args);

int main()
{
    FILE *fin = fopen("lena.bmp", "rb");
    if(!fin) { printf("cannot open input\n"); return 0; }

    fseek(fin, 0, SEEK_END);
    int filesize = ftell(fin);
    if(filesize <= 54)
    {
        printf("wrong filesize\n");
        return 0;
    }
    rewind(fin);

    // Read infile's BITMAPFILEHEADER
    BITMAPFILEHEADER bf;
    fread(&bf, sizeof(BITMAPFILEHEADER), 1, fin);

    // Read infile's BITMAPINFOHEADER
    BITMAPINFOHEADER bi;
    fread(&bi, sizeof(BITMAPINFOHEADER), 1, fin);

    // Get image's dimensions
    int height = abs(bi.biHeight);
    int width = bi.biWidth;

    // Read offbits
    char *offbits = malloc(bf.bfOffBits - 54);
    fread(offbits, 1, bf.bfOffBits - 54, fin);

    RGBTRIPLE(*image)[width] = calloc(height, width * sizeof(RGBTRIPLE));
    for (int i = 0; i < height; i++)
    {
        // Read row into pixel array
        fread(image[i], sizeof(RGBTRIPLE), width, fin);
    }

    // divide R, G, B and
    // Create temp arrays as we require original values
    BYTE(*tempR)[width] = calloc(height, width * sizeof(BYTE));
    BYTE(*tempG)[width] = calloc(height, width * sizeof(BYTE));
    BYTE(*tempB)[width] = calloc(height, width * sizeof(BYTE));

    // split R,G,B to arrays
    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j++)
        {

            tempR[i][j] = image[i][j].rgbtRed;
            tempG[i][j] = image[i][j].rgbtGreen;
            tempB[i][j] = image[i][j].rgbtBlue;
        }
    }

    int t_count;

    t_count = 9;

    printf("Using %d thread!\n", t_count);

    if(t_count == 1)
    {
        blurSeq(height, width, image);

        printf("Blur applied!\n");
    }
    else if(t_count == 3)
    {
        // create arguments for threads
        PIXELTHREADARGS args[t_count];

        args[0].height = height;
        args[0].width = width;
        args[0].temp = tempR;
        args[0].startW = 0;
        args[0].startH = 0;
        args[0].endW = width;
        args[0].endH = height;

        args[1].height = height;
        args[1].width = width;
        args[1].temp = tempG;
        args[1].startW = 0;
        args[1].startH = 0;
        args[1].endW = width;
        args[1].endH = height;

        args[2].height = height;
        args[2].width = width;
        args[2].temp = tempB;
        args[2].startW = 0;
        args[2].startH = 0;
        args[2].endW = width;
        args[2].endH = height;

        struct timeval  tv1, tv2;

        pthread_t tid[t_count];

        //take start time
        gettimeofday(&tv1, NULL);

        // create threads
        for(int t = 0; t < t_count; t++)
        {
            (void) pthread_create(&tid[t], NULL, blurThreadPixel, (void *) &args[t]);
        }

        // join threads
        for(int t = 0; t < t_count; t++)
        {
            (void) pthread_join(tid[0], NULL);
        }

        //take end time
        gettimeofday(&tv2,NULL);

        printf ("Elapsed time = %f seconds\n",
            (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
            (double) (tv2.tv_sec - tv1.tv_sec));

        //merge R,G,B back to image
        for (int i = 0; i < height; i++)
        {
            for (int j = 0; j < width; j++)
            {
                image[i][j].rgbtRed = tempR[i][j];
                image[i][j].rgbtGreen = tempG[i][j];
                image[i][j].rgbtBlue = tempB[i][j];
            }
        }

        printf("Blur applied!\n");
    }
    else if(t_count == 6)
    {
        // create arguments for threads
        PIXELTHREADARGS args[t_count];

        args[0].height = height;
        args[0].width = width;
        args[0].temp = tempR;
        args[0].startW = 0;
        args[0].startH = 0;
        args[0].endW = width;
        args[0].endH = height / 2;

        args[1].height = height;
        args[1].width = width;
        args[1].temp = tempG;
        args[1].startW = 0;
        args[1].startH = 0;
        args[1].endW = width;
        args[1].endH = height / 2;

        args[2].height = height;
        args[2].width = width;
        args[2].temp = tempB;
        args[2].startW = 0;
        args[2].startH = 0;
        args[2].endW = width;
        args[2].endH = height / 2;

        args[3].height = height;
        args[3].width = width;
        args[3].temp = tempR;
        args[3].startW = 0;
        args[3].startH = height / 2;
        args[3].endW = width;
        args[3].endH = height;

        args[4].height = height;
        args[4].width = width;
        args[4].temp = tempG;
        args[4].startW = 0;
        args[4].startH = height / 2;
        args[4].endW = width;
        args[4].endH = height;

        args[5].height = height;
        args[5].width = width;
        args[5].temp = tempB;
        args[5].startW = 0;
        args[5].startH = height / 2;
        args[5].endW = width;
        args[5].endH = height;

        struct timeval  tv1, tv2;

        pthread_t tid[t_count];

        //take start time
        gettimeofday(&tv1, NULL);

        // create threads
        for(int t = 0; t < t_count; t++)
        {
            (void) pthread_create(&tid[t], NULL, blurThreadPixel, (void *) &args[t]);
        }

        // join threads
        for(int t = 0; t < t_count; t++)
        {
            (void) pthread_join(tid[0], NULL);
        }

        //take end time
        gettimeofday(&tv2,NULL);

        printf ("Elapsed time = %f seconds\n",
            (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
            (double) (tv2.tv_sec - tv1.tv_sec));

        //merge R,G,B back to image
        for (int i = 0; i < height; i++)
        {
            for (int j = 0; j < width; j++)
            {
                image[i][j].rgbtRed = tempR[i][j];
                image[i][j].rgbtGreen = tempG[i][j];
                image[i][j].rgbtBlue = tempB[i][j];
            }
        }

        printf("Blur applied!\n");
    }
    else if(t_count == 9)
    {
        // create arguments for threads
        PIXELTHREADARGS args[t_count];

        int divide = height / 3;
        int dividex2 = divide * 2;
        printf("%d\n", divide);
        printf("%d\n", dividex2);

        args[0].height = height;
        args[0].width = width;
        args[0].temp = tempR;
        args[0].startW = 0;
        args[0].startH = 0;
        args[0].endW = width;
        args[0].endH = divide;

        args[1].height = height;
        args[1].width = width;
        args[1].temp = tempG;
        args[1].startW = 0;
        args[1].startH = 0;
        args[1].endW = width;
        args[1].endH = divide;

        args[2].height = height;
        args[2].width = width;
        args[2].temp = tempB;
        args[2].startW = 0;
        args[2].startH = 0;
        args[2].endW = width;
        args[2].endH = divide;

        args[3].height = height;
        args[3].width = width;
        args[3].temp = tempR;
        args[3].startW = 0;
        args[3].startH = divide;
        args[3].endW = width;
        args[3].endH = dividex2;

        args[4].height = height;
        args[4].width = width;
        args[4].temp = tempG;
        args[4].startW = 0;
        args[4].startH = divide;
        args[4].endW = width;
        args[4].endH = dividex2;

        args[5].height = height;
        args[5].width = width;
        args[5].temp = tempB;
        args[5].startW = 0;
        args[5].startH = divide;
        args[5].endW = width;
        args[5].endH = dividex2;

        args[6].height = height;
        args[6].width = width;
        args[6].temp = tempR;
        args[6].startW = 0;
        args[6].startH = dividex2;
        args[6].endW = width;
        args[6].endH = height;

        args[7].height = height;
        args[7].width = width;
        args[7].temp = tempG;
        args[7].startW = 0;
        args[7].startH = dividex2;
        args[7].endW = width;
        args[7].endH = height;

        args[8].height = height;
        args[8].width = width;
        args[8].temp = tempB;
        args[8].startW = 0;
        args[8].startH = dividex2;
        args[8].endW = width;
        args[8].endH = height;

        struct timeval  tv1, tv2;

        pthread_t tid[t_count];

        //take start time
        gettimeofday(&tv1, NULL);

        // create threads
        for(int t = 0; t < t_count; t++)
        {
            (void) pthread_create(&tid[t], NULL, blurThreadPixel, (void *) &args[t]);
        }

        // join threads
        for(int t = 0; t < t_count; t++)
        {
            (void) pthread_join(tid[0], NULL);
        }

        //take end time
        gettimeofday(&tv2,NULL);

        printf ("Elapsed time = %f seconds\n",
            (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
            (double) (tv2.tv_sec - tv1.tv_sec));

        //merge R,G,B back to image
        for (int i = 0; i < height; i++)
        {
            for (int j = 0; j < width; j++)
            {
                image[i][j].rgbtRed = tempR[i][j];
                image[i][j].rgbtGreen = tempG[i][j];
                image[i][j].rgbtBlue = tempB[i][j];
            }
        }

        printf("Blur applied!\n");
    }
    else if(t_count == 12)
    {
        // create arguments for threads
        PIXELTHREADARGS args[t_count];

        args[0].height = height;
        args[0].width = width;
        args[0].temp = tempR;
        args[0].startW = 0;
        args[0].startH = 0;
        args[0].endW = width / 2;
        args[0].endH = height / 2;

        args[1].height = height;
        args[1].width = width;
        args[1].temp = tempG;
        args[1].startW = 0;
        args[1].startH = 0;
        args[1].endW = width / 2;
        args[1].endH = height / 2;

        args[2].height = height;
        args[2].width = width;
        args[2].temp = tempB;
        args[2].startW = 0;
        args[2].startH = 0;
        args[2].endW = width / 2;
        args[2].endH = height / 2;

        args[3].height = height;
        args[3].width = width;
        args[3].temp = tempR;
        args[3].startW = width / 2;
        args[3].startH = 0;
        args[3].endW = width;
        args[3].endH = height / 2;

        args[4].height = height;
        args[4].width = width;
        args[4].temp = tempG;
        args[4].startW = width / 2;
        args[4].startH = 0;
        args[4].endW = width;
        args[4].endH = height / 2;

        args[5].height = height;
        args[5].width = width;
        args[5].temp = tempB;
        args[5].startW = width / 2;
        args[5].startH = 0;
        args[5].endW = width;
        args[5].endH = height / 2;

        args[6].height = height;
        args[6].width = width;
        args[6].temp = tempR;
        args[6].startW = 0;
        args[6].startH = height / 2;
        args[6].endW = width / 2;
        args[6].endH = height;

        args[7].height = height;
        args[7].width = width;
        args[7].temp = tempG;
        args[7].startW = 0;
        args[7].startH = height / 2;
        args[7].endW = width / 2;
        args[7].endH = height;

        args[8].height = height;
        args[8].width = width;
        args[8].temp = tempB;
        args[8].startW = 0;
        args[8].startH = height / 2;
        args[8].endW = width / 2;
        args[8].endH = height;

        args[9].height = height;
        args[9].width = width;
        args[9].temp = tempR;
        args[9].startW = width / 2;
        args[9].startH = height / 2;
        args[9].endW = width;
        args[9].endH = height;

        args[10].height = height;
        args[10].width = width;
        args[10].temp = tempG;
        args[10].startW = width / 2;
        args[10].startH = height / 2;
        args[10].endW = width;
        args[10].endH = height;

        args[11].height = height;
        args[11].width = width;
        args[11].temp = tempB;
        args[11].startW = width / 2;
        args[11].startH = height / 2;
        args[11].endW = width;
        args[11].endH = height;

        struct timeval  tv1, tv2;

        pthread_t tid[t_count];

        //take start time
        gettimeofday(&tv1, NULL);

        // create threads
        for(int t = 0; t < t_count; t++)
        {
            (void) pthread_create(&tid[t], NULL, blurThreadPixel, (void *) &args[t]);
        }

        // join threads
        for(int t = 0; t < t_count; t++)
        {
            (void) pthread_join(tid[0], NULL);
        }

        //take end time
        gettimeofday(&tv2,NULL);

        printf ("Elapsed time = %f seconds\n",
            (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
            (double) (tv2.tv_sec - tv1.tv_sec));

        //merge R,G,B back to image
        for (int i = 0; i < height; i++)
        {
            for (int j = 0; j < width; j++)
            {
                image[i][j].rgbtRed = tempR[i][j];
                image[i][j].rgbtGreen = tempG[i][j];
                image[i][j].rgbtBlue = tempB[i][j];
            }
        }

        printf("Blur applied!\n");
    }

    WriteRGBTRIPLE(height, width, bf, bi, offbits, image);
    fclose(fin);

    return 0;
}

void WriteRGBTRIPLE(int height, int width, BITMAPFILEHEADER bf, BITMAPINFOHEADER bi, char* offbits, RGBTRIPLE image[height][width])
{
    FILE *fout = fopen("lenaout.bmp", "wb");
    if(!fout) { printf("cannot open output\n"); return 0; }

    // Write outfile's BITMAPFILEHEADER
    fwrite(&bf, sizeof(BITMAPFILEHEADER), 1, fout);

    // Write outfile's BITMAPINFOHEADER
    fwrite(&bi, sizeof(BITMAPINFOHEADER), 1, fout);

    // Write offbits
    fwrite(offbits, 1, bf.bfOffBits - 54, fout);

    // Write image
    for (int i = 0; i < height; i++)
    {
        // Write row to outfile
        fwrite(image[i], sizeof(RGBTRIPLE), width, fout);
    }

    fclose(fout);
    free(image);
}

void blurSeq(int height, int width, RGBTRIPLE image[height][width])
{
    struct timeval  tv1, tv2;

    //take start time
    gettimeofday(&tv1, NULL);

    // Create temp array as we require original values
    RGBTRIPLE temp[height][width];
    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j++)
        {
            temp[i][j] = image[i][j];
        }
    }

    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j++) // went to a pixel
        {
            // Initialise values for each i,j pixel
            float sum_red;
            float sum_blue;
            float sum_green;
            int counter; // to count no. of surr. pixels
            sum_red = sum_blue = sum_green = counter = 0;

            for (int k = i - 1; k <= i + 1; k++) // for surr. pixels
            {
                for (int l = j - 1; l <= j + 1; l++)
                { // cases of surr. ipixel

                    if (k >= 0 && l >= 0 && k < height && l < width)
                    { // if that ipixel is in img.

                        sum_red += temp[k][l].rgbtRed;
                        sum_blue += temp[k][l].rgbtBlue;
                        sum_green += temp[k][l].rgbtGreen;
                        counter++;
                    }
                }
            }

            // take average
            image[i][j].rgbtRed = round(sum_red / counter);
            image[i][j].rgbtGreen = round(sum_green / counter);
            image[i][j].rgbtBlue = round(sum_blue / counter);
        }
    }

    //take end time
    gettimeofday(&tv2,NULL);

    printf ("Elapsed time = %f seconds\n",
         (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
         (double) (tv2.tv_sec - tv1.tv_sec));

    return;
}

void *blurThreadPixel(void *arg)
{
    PIXELTHREADARGS *args = arg;

    int height = args->height;
    int width = args->width;

    int startW = args->startW;
    int startH = args->startH;
    int endW = args->endW;
    int endH = args->endH;

    BYTE(*temp)[width] = args->temp;


    for (int i = startH; i < endH; i++)
    {
        for (int j = startW; j < endW; j++) // went to a pixel
        {
            // Initialise values for each i,j pixel
            float sum;
            int counter; // to count no. of surr. pixels
            sum = counter = 0;

            for (int k = i - 1; k <= i + 1; k++) // for surr. pixels
            {
                for (int l = j - 1; l <= j + 1; l++)
                { // cases of surr. ipixel
                    if (k >= 0 && l >= 0 && k < height && l < width)
                    { // if that ipixel is in img.
                        sum += temp[k][l];
                        counter++;
                    }
                }
            }
            // take average
            temp[i][j] = round(sum / counter);
        }
    }
}

