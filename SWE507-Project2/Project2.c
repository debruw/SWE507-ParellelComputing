#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <time.h>

#pragma pack(push, 1)
typedef struct {
    unsigned short bfType;
    unsigned int bfSize;
    unsigned short bfReserved1;
    unsigned short bfReserved2;
    unsigned int bfOffBits;
} BITMAPFILEHEADER;

typedef struct {
    unsigned int biSize;
    int biWidth;
    int biHeight;
    unsigned short biPlanes;
    unsigned short biBitCount;
    unsigned int biCompression;
    unsigned int biSizeImage;
    int biXPelsPerMeter;
    int biYPelsPerMeter;
    unsigned int biClrUsed;
    unsigned int biClrImportant;
} BITMAPINFOHEADER;
#pragma pack(pop)

unsigned char *image;
unsigned char *padded_image;
int width, height;
int channels = 3;
unsigned int row_padded;

float kernel[3][3] = {
    {1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f},
    {1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f},
    {1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f}
};

// Function to load BMP image
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

    row_padded = (width * 3 + 3) & (~3);

    image = (unsigned char*)malloc(row_padded * height);
    padded_image = (unsigned char*)malloc((width + 2) * 3 * (height + 2));

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

// Function to save BMP image
void save_bmp(const char *filename) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        printf("Error: Failed to save BMP file.\n");
        return;
    }

    BITMAPFILEHEADER fileHeader;
    BITMAPINFOHEADER infoHeader;

    fileHeader.bfType = 0x4D42;
    fileHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + row_padded * height;
    fileHeader.bfReserved1 = fileHeader.bfReserved2 = 0;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = width;
    infoHeader.biHeight = height;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 24;
    infoHeader.biCompression = 0;
    infoHeader.biSizeImage = row_padded * height;
    infoHeader.biXPelsPerMeter = 0;
    infoHeader.biYPelsPerMeter = 0;
    infoHeader.biClrUsed = 0;
    infoHeader.biClrImportant = 0;

    fwrite(&fileHeader, sizeof(BITMAPFILEHEADER), 1, file);
    fwrite(&infoHeader, sizeof(BITMAPINFOHEADER), 1, file);
    fwrite(image, sizeof(unsigned char), row_padded * height, file);

    fclose(file);
}

// Apply kernel to each color channel
void apply_kernel_to_channels(int x, int y, unsigned char *new_pixel_values) {
    for (int color = 0; color < 3; color++) {
        float sum = 0.0;

        // Apply the kernel
        for (int ky = -1; ky <= 1; ky++) {
            for (int kx = -1; kx <= 1; kx++) {
                int ix = x + kx;
                int iy = y + ky;

                if (ix < 0 || ix >= width) ix = 0;
                if (iy < 0 || iy >= height) iy = 0;

                int pixel_index = ((iy + 1) * (width + 2) + (ix + 1)) * 3;
                unsigned char pixel_value = padded_image[pixel_index + color];
                sum += pixel_value * kernel[ky + 1][kx + 1];
            }
        }

        sum = sum < 0 ? 0 : (sum > 255 ? 255 : sum);
        new_pixel_values[color] = (unsigned char)sum;
    }
}

// Zero padding function
void zero_padding() {
    for (int y = 0; y < height + 2; y++) {
        for (int x = 0; x < (width + 2) * 3; x++) {
            padded_image[y * (width + 2) * 3 + x] = 0;
        }
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width * 3; x++) {
            padded_image[(y + 1) * (width + 2) * 3 + (x + 3)] = image[y * row_padded + x];
        }
    }
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    if (!load_bmp("lena.bmp")) {
        MPI_Finalize();
        return 1;
    }

    zero_padding();

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    printf("%d rank from %d\n", rank, size);

    struct timeval  tv1, tv2;

    if(rank == 0) {
        // Take start time
        gettimeofday(&tv1, NULL);
    }

    // Divide image rows among processes
    int rows_per_process = height / size;
    int remaining_rows = height % size;

    int start_row = rank * rows_per_process + (rank < remaining_rows ? rank : remaining_rows);
    int end_row = (rank + 1) * rows_per_process + (rank + 1 < remaining_rows ? rank + 1 : remaining_rows);

    int local_rows = end_row - start_row;
    int local_size = row_padded * local_rows;

    printf("start : %d // end : %d\n", start_row, end_row);

    unsigned char *local_image = (unsigned char*)malloc(local_size * sizeof(unsigned char));

    // Each process applies the kernel to its portion of the image
    for (int y = start_row; y < end_row; y++) {
        for (int x = 0; x < width; x++) {
            unsigned char new_pixel_values[3];
            apply_kernel_to_channels(x, y, new_pixel_values);

            int pixel_index = (y - start_row) * row_padded + x * 3;
            local_image[pixel_index + 0] = new_pixel_values[0];
            local_image[pixel_index + 1] = new_pixel_values[1];
            local_image[pixel_index + 2] = new_pixel_values[2];
        }
    }

    if(rank == 0) {
        // Take end time
        gettimeofday(&tv2,NULL);

        printf ("Elapsed time = %f seconds\n",
            (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
            (double) (tv2.tv_sec - tv1.tv_sec));
    }

    unsigned char *final_image = NULL;

    if (rank == 0) {
        final_image = (unsigned char*)malloc(row_padded * height);
    }

    // Gather data using MPI_Gatherv
    int *recvcounts = NULL;
    int *displs = NULL;

    if (rank == 0) {
        recvcounts = (int*)malloc(size * sizeof(int));
        displs = (int*)malloc(size * sizeof(int));

        int total_size = 0;
        for (int i = 0; i < size; i++) {
            int rows = height / size + (i < height % size ? 1 : 0);
            recvcounts[i] = row_padded * rows;
            displs[i] = total_size;
            total_size += recvcounts[i];
        }
    }

    MPI_Gatherv(local_image, local_size, MPI_UNSIGNED_CHAR,
                image, recvcounts, displs, MPI_UNSIGNED_CHAR,
                0, MPI_COMM_WORLD);

    // Root process (rank 0) saves the final image
    if(rank == 0) {
        save_bmp("lenaout.bmp");
    }

    free(local_image);
    if (rank == 0) {
        free(recvcounts);
        free(displs);
        free(final_image);
    }

    free(image);
    free(padded_image);

    MPI_Finalize();
    return 0;
}
