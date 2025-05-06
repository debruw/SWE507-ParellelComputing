#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <mpi.h>
#include <string.h>
#include <time.h>

// BMP Header Structures
#pragma pack(push, 1)

typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BITMAPFILEHEADER;

typedef struct {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BITMAPINFOHEADER;

#pragma pack(pop)

// Apply the kernel with zero padding and overlapping
void apply_kernel_with_padding(uint8_t* image, uint8_t* output, int width, int height, float* kernel, int kernel_size) {
    int offset = kernel_size / 2;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float sum_r = 0.0f;
            float sum_g = 0.0f;
            float sum_b = 0.0f;

            for (int ky = 0; ky < kernel_size; ky++) {
                for (int kx = 0; kx < kernel_size; kx++) {
                    int pixel_x = x + kx - offset;
                    int pixel_y = y + ky - offset;

                    // Check bounds, apply zero padding
                    if (pixel_x >= 0 && pixel_x < width && pixel_y >= 0 && pixel_y < height) {
                        int idx = (pixel_y * width + pixel_x) * 3;
                        sum_r += image[idx] * kernel[ky * kernel_size + kx];
                        sum_g += image[idx + 1] * kernel[ky * kernel_size + kx];
                        sum_b += image[idx + 2] * kernel[ky * kernel_size + kx];
                    }
                }
            }

            int idx = (y * width + x) * 3;
            output[idx] = (uint8_t)(sum_r < 0 ? 0 : (sum_r > 255 ? 255 : sum_r));
            output[idx + 1] = (uint8_t)(sum_g < 0 ? 0 : (sum_g > 255 ? 255 : sum_g));
            output[idx + 2] = (uint8_t)(sum_b < 0 ? 0 : (sum_b > 255 ? 255 : sum_b));
        }
    }
}

// Function to load BMP image
int load_bmp(const char* filename, uint8_t** image, int* width, int* height) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Error opening file!\n");
        return -1;
    }

    BITMAPFILEHEADER fileHeader;
    BITMAPINFOHEADER infoHeader;

    fread(&fileHeader, sizeof(BITMAPFILEHEADER), 1, file);
    fread(&infoHeader, sizeof(BITMAPINFOHEADER), 1, file);

    *width = infoHeader.biWidth;
    *height = infoHeader.biHeight;

    int size = 3 * (*width) * (*height);
    *image = (uint8_t*)malloc(size);

    fseek(file, fileHeader.bfOffBits, SEEK_SET);
    fread(*image, size, 1, file);
    fclose(file);

    return 0;
}

// Function to save BMP image
int save_bmp(const char* filename, uint8_t* image, int width, int height) {
    FILE* file = fopen(filename, "wb");
    if (!file) {
        printf("Error opening file!\n");
        return -1;
    }

    BITMAPFILEHEADER fileHeader = {0x4D42, 0, 0, 0, 54}; // 'BM' header
    BITMAPINFOHEADER infoHeader = {40, width, height, 1, 24, 0, 0, 0, 0, 0};

    int size = 3 * width * height;
    fileHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + size;
    infoHeader.biSizeImage = size;

    fwrite(&fileHeader, sizeof(BITMAPFILEHEADER), 1, file);
    fwrite(&infoHeader, sizeof(BITMAPINFOHEADER), 1, file);

    fwrite(image, size, 1, file);
    fclose(file);

    return 0;
}

// Define the kernel (e.g., a 3x3 averaging kernel)
float kernel[3][3] = {
    {1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f},
    {1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f},
    {1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f}
};

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    const char* input_image = "lena.bmp";
    const char* output_image = "lenaout.bmp";

    uint8_t* image = NULL;
    int width, height;

    struct timeval  tv1, tv2;

    printf("Rank %d out of %d\n", rank, size);

    // Master process (rank 0) loads the image
    if (rank == 0) {
        if (load_bmp(input_image, &image, &width, &height) != 0) {
            MPI_Abort(MPI_COMM_WORLD, -1);
        }
    }

    // Broadcast image dimensions to all processes
    MPI_Bcast(&width, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&height, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // Calculate the chunk size for each process (divide the height of the image)
    int chunk_height = height / size;
    int remainder = height % size;

    // Add overlap to the chunk
    int overlap = 6;
    int start_row = rank * chunk_height + (rank < remainder ? rank : remainder);
    int end_row = (rank + 1) * chunk_height + (rank + 1 < remainder ? rank + 1 : remainder);

    // Allocate memory for the image chunk with overlap (add one row above and below)
    uint8_t* chunk = (uint8_t*)malloc(3 * width * (end_row - start_row + (2 * overlap)));

    // Master process sends image chunks to worker processes
    if (rank == 0)
    {
        for (int i = 1; i < size; i++) {
            int start = i * chunk_height + (i < remainder ? i : remainder) - overlap;
            int end = (i + 1) * chunk_height + (i + 1 < remainder ? i + 1 : remainder) + overlap;

            // Send chunk with overlap rows
            MPI_Send(&image[(start * width * 3)], 3 * width * (end - start), MPI_UNSIGNED_CHAR, i, 0, MPI_COMM_WORLD);
        }

        // Take start time
        gettimeofday(&tv1, NULL);
        //mpiexec -np 18 Project2.exe

        // Process the master chunk with overlap
        apply_kernel_with_padding(&image[start_row * width * 3], chunk, width, end_row - start_row + overlap, (float*)kernel, 3);
    } else
    {
        // Worker processes receive their image chunk
        MPI_Recv(chunk, 3 * width * (end_row - start_row + (2 * overlap)), MPI_UNSIGNED_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);


        // Apply the kernel to the received chunk
        apply_kernel_with_padding(chunk, chunk, width, end_row - start_row + (2 * overlap), (float*)kernel, 3);
    }

    // Gather all chunks back to the master node
    uint8_t* result_image = NULL;
    if (rank == 0) {
        // Take end time
        gettimeofday(&tv2,NULL);

        printf ("Elapsed time = %f seconds\n",
            (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
            (double) (tv2.tv_sec - tv1.tv_sec));

        result_image = (uint8_t*)malloc(3 * width * height);

        /*for(int i = 0; i < 3 * width * height; i++)
        {
            result_image[i] = 250;
        }*/

        memcpy(result_image + (start_row * width * 3), chunk, 3 * width * (end_row - start_row));

        // Gather results from all other processes
        for (int i = 1; i < size; i++) {
            int start = i * chunk_height + (i < remainder ? i : remainder);
            int end = (i + 1) * chunk_height + (i + 1 < remainder ? i + 1 : remainder);

            MPI_Recv(&result_image[start * width * 3], 3 * width * (end - start), MPI_UNSIGNED_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }

        // Save the resulting image
        save_bmp(output_image, result_image, width, height);
    } else {
        // Send the chunk back to the master
        MPI_Send(chunk + (overlap * 3 * width), 3 * width * (end_row - start_row), MPI_UNSIGNED_CHAR, 0, 0, MPI_COMM_WORLD);
    }

    // Clean up
    free(chunk);
    if (rank == 0) {
        free(image);
        free(result_image);
    }

    MPI_Finalize();
    return 0;
}
