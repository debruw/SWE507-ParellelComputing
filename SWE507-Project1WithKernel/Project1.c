#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#pragma pack(push, 1)
typedef struct {
    unsigned short bfType;        // File type (should be 'BM')
    unsigned int bfSize;          // Size of the file
    unsigned short bfReserved1;
    unsigned short bfReserved2;
    unsigned int bfOffBits;       // Offset to the pixel data
} BITMAPFILEHEADER;

typedef struct {
    unsigned int biSize;          // Size of the header
    int biWidth;                  // Width of the image
    int biHeight;                 // Height of the image
    unsigned short biPlanes;
    unsigned short biBitCount;    // Bits per pixel (24 for color)
    unsigned int biCompression;
    unsigned int biSizeImage;     // Image size
    int biXPelsPerMeter;
    int biYPelsPerMeter;
    unsigned int biClrUsed;
    unsigned int biClrImportant;
} BITMAPINFOHEADER;
#pragma pack(pop)

unsigned char *image;    // Pointer to image data
unsigned char *padded_image; // Pointer to zero-padded image
int width, height;      // Image dimensions
int channels = 3;       // Number of channels (RGB)
unsigned int row_padded; // Row size, padded to be a multiple of 4
int num_threads = 12;  // Number of threads to use
/*
// Sharpening kernel
float kernel[3][3] = {
    {0, -1, 0},
    {-1, 5, -1},
    {0, -1, 0}
};
*/

// Box blur kernel
float kernel[3][3] = {
    {1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f},
    {1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f},
    {1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f}
};

/*
// Edge detection kernel
float kernel[3][3] = {
    {-1, -1, -1},
    {-1, 8, -1},
    {-1, -1, -1}
};
*/
// Function to load a BMP image
int load_bmp(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("Error: Failed to open BMP file.\n");
        return 0;
    }

    BITMAPFILEHEADER fileHeader;
    BITMAPINFOHEADER infoHeader;

    fread(&fileHeader, sizeof(BITMAPFILEHEADER), 1, file);
    fread(&infoHeader, sizeof(BITMAPINFOHEADER), 1, file);

    width = infoHeader.biWidth;
    height = infoHeader.biHeight;

    row_padded = (width * 3 + 3) & (~3);  // Row size padded to be multiple of 4

    // Allocate memory for image and padded image
    image = (unsigned char*)malloc(row_padded * height);
    padded_image = (unsigned char*)malloc((width + 2) * 3 * (height + 2)); // Zero-padded image

    if (!image || !padded_image) {
        printf("Error: Failed to allocate memory for image.\n");
        fclose(file);
        return 0;
    }

    fseek(file, fileHeader.bfOffBits, SEEK_SET);
    fread(image, sizeof(unsigned char), row_padded * height, file);

    fclose(file);
    return 1;
}

// Function to save a BMP image
void save_bmp(const char *filename) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        printf("Error: Failed to save BMP file.\n");
        return;
    }

    BITMAPFILEHEADER fileHeader;
    BITMAPINFOHEADER infoHeader;

    fileHeader.bfType = 0x4D42;  // BM
    fileHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + row_padded * height;
    fileHeader.bfReserved1 = fileHeader.bfReserved2 = 0;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = width;
    infoHeader.biHeight = height;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 24;  // 24 bits per pixel
    infoHeader.biCompression = 0;  // No compression
    infoHeader.biSizeImage = row_padded * height;
    infoHeader.biXPelsPerMeter = 0;
    infoHeader.biYPelsPerMeter = 0;
    infoHeader.biClrUsed = 0;
    infoHeader.biClrImportant = 0;

    fwrite(&fileHeader, sizeof(BITMAPFILEHEADER), 1, file);
    fwrite(&infoHeader, sizeof(BITMAPINFOHEADER), 1, file);

    // Write the image data
    fwrite(image, sizeof(unsigned char), row_padded * height, file);

    fclose(file);
}

// Apply kernel to each color channel (Red, Green, Blue) with zero padding
void apply_kernel_to_channels(int x, int y, unsigned char *new_pixel_values) {
    for (int color = 0; color < 3; color++) {  // 0 = Red, 1 = Green, 2 = Blue
        float sum = 0.0;

        // Apply the kernel
        for (int ky = -1; ky <= 1; ky++) {
            for (int kx = -1; kx <= 1; kx++) {
                int ix = x + kx;  // x position in the padded image
                int iy = y + ky;  // y position in the padded image

                // Zero padding: If we're out of bounds, use 0 for the pixel
                if (ix < 0 || ix >= width) ix = 0;
                if (iy < 0 || iy >= height) iy = 0;

                // Apply the kernel to the padded image
                int pixel_index = ((iy + 1) * (width + 2) + (ix + 1)) * 3;  // Offset by 1 for the padding
                unsigned char pixel_value = padded_image[pixel_index + color];
                sum += pixel_value * kernel[ky + 1][kx + 1];
            }
        }

        // Clamp the value to ensure it's within the valid range [0, 255]
        sum = sum < 0 ? 0 : (sum > 255 ? 255 : sum);  // Clamp the sum to the valid range
        new_pixel_values[color] = (unsigned char)sum;
    }
}

// Zero padding function to create padded image
void zero_padding() {
    // Initialize padded image to zero
    for (int y = 0; y < height + 2; y++) {
        for (int x = 0; x < (width + 2) * 3; x++) {
            padded_image[y * (width + 2) * 3 + x] = 0;
        }
    }

    // Copy the original image to the center of the padded image
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width * 3; x++) {
            padded_image[(y + 1) * (width + 2) * 3 + (x + 3)] = image[y * row_padded + x];
        }
    }
}

// Thread function for applying the kernel
void *apply_kernel_thread(void *arg) {
    int thread_id = *(int *)arg;
    int rows_per_thread = height / num_threads;
    int start_row = thread_id * rows_per_thread;
    int end_row = (thread_id == num_threads - 1) ? height : (thread_id + 1) * rows_per_thread;

    // Apply kernel to the assigned rows
    for (int y = start_row; y < end_row; y++) {
        for (int x = 0; x < width; x++) {
            unsigned char new_pixel_values[3];  // To store the new pixel values for R, G, B
            apply_kernel_to_channels(x, y, new_pixel_values);

            // Write the new pixel values back to the image
            int pixel_index = (y * row_padded + x * 3);
            image[pixel_index + 0] = new_pixel_values[0];  // Red
            image[pixel_index + 1] = new_pixel_values[1];  // Green
            image[pixel_index + 2] = new_pixel_values[2];  // Blue
        }
    }

    return NULL;
}

// Main function to load the image, apply the kernel, and save the result
int main() {
    // Load the BMP image
    if (!load_bmp("lena.bmp")) {
        return 1;
    }

    // Apply zero padding to the image
    zero_padding();

    // Create pthreads to apply the kernel
    pthread_t threads[num_threads];
    int thread_ids[num_threads];
    struct timeval  tv1, tv2;

    //take start time
    gettimeofday(&tv1, NULL);

    for (int i = 0; i < num_threads; i++) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, apply_kernel_thread, (void *)&thread_ids[i]);
    }

    // Join all threads
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    //take end time
    gettimeofday(&tv2,NULL);

    printf ("Elapsed time = %f seconds\n",
        (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
        (double) (tv2.tv_sec - tv1.tv_sec));

    // Save the processed image as BMP
    save_bmp("lenaout.bmp");

    // Free the image data
    free(image);
    free(padded_image);

    return 0;
}
