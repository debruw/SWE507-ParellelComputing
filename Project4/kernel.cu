#include <stdio.h>
#include <stdlib.h>
#include <cuda_runtime.h>

// Structure for BMP Header
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

unsigned char* image;
int width, height;

// Constant memory for kernel
__constant__ float d_kernel_const[3][3];  // For 3x3 kernels

// Function to load a BMP image
int load_bmp(const char* filename) {
	FILE* file = fopen(filename, "rb");
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

	image = (unsigned char*)malloc(width * height * 3);
	if (!image) {
		printf("Error: Failed to allocate memory for image.\n");
		fclose(file);
		return 0;
	}

	fseek(file, fileHeader.bfOffBits, SEEK_SET);
	fread(image, sizeof(unsigned char), width * height * 3, file);
	fclose(file);
	return 1;
}

// Function to save a BMP image
void save_bmp(const char* filename, unsigned char* imageData) {
	FILE* file = fopen(filename, "wb");
	if (!file) {
		printf("Error: Failed to save BMP file.\n");
		return;
	}

	BITMAPFILEHEADER fileHeader;
	BITMAPINFOHEADER infoHeader;

	fileHeader.bfType = 0x4D42;
	fileHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + width * height * 3;
	fileHeader.bfReserved1 = fileHeader.bfReserved2 = 0;
	fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

	infoHeader.biSize = sizeof(BITMAPINFOHEADER);
	infoHeader.biWidth = width;
	infoHeader.biHeight = height;
	infoHeader.biPlanes = 1;
	infoHeader.biBitCount = 24;
	infoHeader.biCompression = 0;
	infoHeader.biSizeImage = width * height * 3;
	infoHeader.biXPelsPerMeter = 0;
	infoHeader.biYPelsPerMeter = 0;
	infoHeader.biClrUsed = 0;
	infoHeader.biClrImportant = 0;

	fwrite(&fileHeader, sizeof(BITMAPFILEHEADER), 1, file);
	fwrite(&infoHeader, sizeof(BITMAPINFOHEADER), 1, file);
	fwrite(imageData, sizeof(unsigned char), width * height * 3, file);

	fclose(file);
}

// Clamp function on device
__device__ int clamp(int val, int min, int max) {
	if (val < min) return min;
	if (val > max) return max;
	return val;
}

// CUDA kernel using constant memory
__global__ void d_applyConvolutionKernel(unsigned char* d_input, unsigned char* d_output, int imageWidth, int imageHeight, int kernelSize) {
	int x = blockIdx.x * blockDim.x + threadIdx.x;
	int y = blockIdx.y * blockDim.y + threadIdx.y;
	int kernelRadius = kernelSize / 2;

	if (x < imageWidth && y < imageHeight) {
		float valueR = 0.0f, valueG = 0.0f, valueB = 0.0f;

		for (int ky = -kernelRadius; ky <= kernelRadius; ++ky) {
			for (int kx = -kernelRadius; kx <= kernelRadius; ++kx) {
				int imageX = x + kx;
				int imageY = y + ky;
				int kernelX = kx + kernelRadius;
				int kernelY = ky + kernelRadius;

				float kernelVal = d_kernel_const[kernelY][kernelX];

				if (imageX < 0 || imageX >= imageWidth || imageY < 0 || imageY >= imageHeight) {
					valueR += 0.0f * kernelVal;
					valueG += 0.0f * kernelVal;
					valueB += 0.0f * kernelVal;
				}
				else {
					int idx = (imageY * imageWidth + imageX) * 3;
					valueR += d_input[idx + 2] * kernelVal;
					valueG += d_input[idx + 1] * kernelVal;
					valueB += d_input[idx + 0] * kernelVal;
				}
			}
		}

		int outputIdx = (y * imageWidth + x) * 3;
		d_output[outputIdx + 2] = clamp(int(valueR), 0, 255);
		d_output[outputIdx + 1] = clamp(int(valueG), 0, 255);
		d_output[outputIdx + 0] = clamp(int(valueB), 0, 255);
	}
}

int main() {
	// Define a 3x3 box blur kernel
	float kernel[3][3] = {
		{1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f},
		{1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f},
		{1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f}
	};

	/*float kernel[3][3] = {
	{0.0f, -1.0f, 0.0f},
	{-1.0f, 5.0f, -1.0f},
	{0.0f, -1.0f, 0.0f}
	};*/
	int kernelSize = 3;

	if (!load_bmp("lena.bmp")) {
		return 1;
	}

	// Copy kernel to constant memory
	cudaMemcpyToSymbol(d_kernel_const, kernel, sizeof(float) * 3 * 3);

	unsigned char* d_inputImage, * d_outputImage;
	size_t imageSize = width * height * 3;

	cudaMalloc(&d_inputImage, imageSize);
	cudaMalloc(&d_outputImage, imageSize);
	cudaMemcpy(d_inputImage, image, imageSize, cudaMemcpyHostToDevice);

	dim3 blockDim(16, 16);
	dim3 gridDim((width + blockDim.x - 1) / blockDim.x, (height + blockDim.y - 1) / blockDim.y);

	// CUDA event timing
	cudaEvent_t start, stop;
	float milliseconds = 0;
	cudaEventCreate(&start);
	cudaEventCreate(&stop);
	cudaEventRecord(start);

	// Launch kernel
	d_applyConvolutionKernel << <gridDim, blockDim >> > (d_inputImage, d_outputImage, width, height, kernelSize);

	cudaEventRecord(stop);
	cudaDeviceSynchronize();
	cudaEventSynchronize(stop);
	cudaEventElapsedTime(&milliseconds, start, stop);
	printf("Kernel execution time: %.6f second\n", (milliseconds / 1000.0f));

	// Copy result back
	unsigned char* outputImage = (unsigned char*)malloc(imageSize);
	cudaMemcpy(outputImage, d_outputImage, imageSize, cudaMemcpyDeviceToHost);
	save_bmp("lenaout.bmp", outputImage);

	// Free memory
	free(image);
	free(outputImage);
	cudaFree(d_inputImage);
	cudaFree(d_outputImage);
	cudaEventDestroy(start);
	cudaEventDestroy(stop);

	return 0;
}
