// Optional arguments:
//  -r <img_size>
//  -b <max iterations>
//  -i <implementation: {"scalar", "vector", "vector_ilp", "vector_multicore",
//  "vector_multicore_multithread", "vector_multicore_multithread_ilp", "all"}>

#include <cmath>
#include <cstdint>
#include <immintrin.h>
#include <pthread.h>

constexpr float window_zoom = 1.0 / 10000.0f;
constexpr float window_x = -0.743643887 - 0.5 * window_zoom;
constexpr float window_y = 0.131825904 - 0.5 * window_zoom;
constexpr uint32_t default_max_iters = 2000;

// CPU Scalar Mandelbrot set generation.
// Based on the "optimized escape time algorithm" in
// https://en.wikipedia.org/wiki/Plotting_algorithms_for_the_Mandelbrot_set
void mandelbrot_cpu_scalar(uint32_t img_size, uint32_t max_iters, uint32_t *out) {
    for (uint64_t i = 0; i < img_size; ++i) {
        for (uint64_t j = 0; j < img_size; ++j) {
            float cx = (float(j) / float(img_size)) * window_zoom + window_x;
            float cy = (float(i) / float(img_size)) * window_zoom + window_y;

            float x2 = 0.0f;
            float y2 = 0.0f;
            float w = 0.0f;
            uint32_t iters = 0;
            while (x2 + y2 <= 4.0f && iters < max_iters) {
                float x = x2 - y2 + cx;
                float y = w - (x2 + y2) + cy;
                x2 = x * x;
                y2 = y * y;
                float z = x + y;
                w = z * z;
                ++iters;
            }

            // Write result.
            out[i * img_size + j] = iters;
        }
    }
}

uint32_t ceil_div(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

/// <--- your code here --->

/*
    // OPTIONAL: Uncomment this block to include your CPU vector implementation
    // from Lab 1 for easy comparison.
    //
    // (If you do this, you'll need to update your code to use the new constants
    // 'window_zoom', 'window_x', and 'window_y'.)

    #define HAS_VECTOR_IMPL // <~~ keep this line if you want to benchmark the vector kernel!

    ////////////////////////////////////////////////////////////////////////////////
    // Vector

    void mandelbrot_cpu_vector(uint32_t img_size, uint32_t max_iters, uint32_t *out) {
        // your code here...
    }
*/

void mandelbrot_cpu_vector(uint32_t img_size, uint32_t max_iters, uint32_t *out) {
    const __m512 x_scale = _mm512_set1_ps(2.5f / float(img_size));
    const __m512 y_scale = _mm512_set1_ps(2.5f / float(img_size));
    const __m512 x_shift = _mm512_set1_ps(-2.0f);
    const __m512 y_shift = _mm512_set1_ps(-1.25f);

    const __m512 four = _mm512_set1_ps(4.0f);
    const __m512i one = _mm512_set1_epi32(1);

    __m512 j_vec = _mm512_set_ps(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);

    for (uint32_t i = 0; i < img_size; i++){
        __m512 cy = _mm512_fmadd_ps(_mm512_set1_ps(float(i)), y_scale, y_shift);
        for (uint32_t j = 0; j < img_size; j +=16){
            __m512 cx = _mm512_fmadd_ps(_mm512_add_ps(j_vec, _mm512_set1_ps(float(j))), x_scale, x_shift);
            __m512 x = _mm512_setzero_ps();
            __m512 y = _mm512_setzero_ps();
            __m512 x2 = _mm512_setzero_ps();
            __m512 y2 = _mm512_setzero_ps();
            
            __m512i iters = _mm512_setzero_si512();
            __mmask16 mask = 0xFFFF; // 16-bit mask

            for (uint32_t iter = 0; iter < max_iters; ++iter){
                __m512 xy = _mm512_mul_ps(x, y);

                y = _mm512_fmadd_ps(_mm512_add_ps(x, x), y, cy);
                x = _mm512_add_ps(_mm512_sub_ps(x2, y2), cx);

                x2 = _mm512_mul_ps(x, x);
                y2 = _mm512_mul_ps(y, y);

                __mmask16 cmp_mask = _mm512_cmp_ps_mask(_mm512_add_ps(x2, y2), four, _CMP_LE_OQ);
                mask &= cmp_mask;

                if (mask == 0) break;  // All pixels have escaped

                iters = _mm512_mask_add_epi32(iters, mask, iters, one);

            }

            _mm512_storeu_si512(out + i * img_size + j, iters);

        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Vector + ILP

void mandelbrot_cpu_vector_ilp(uint32_t img_size, uint32_t max_iters, uint32_t *out) {
    
    const __m512 x_scale = _mm512_set1_ps(window_zoom / float(img_size));
    const __m512 y_scale = _mm512_set1_ps(window_zoom / float(img_size));
    const __m512 x_shift = _mm512_set1_ps(window_x);
    const __m512 y_shift = _mm512_set1_ps(window_y);
    const __m512 four = _mm512_set1_ps(4.0f);
    const __m512i one = _mm512_set1_epi32(1);

    __m512 j_vec = _mm512_set_ps(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);

    for (uint32_t i = 0; i < img_size; i++){
        __m512 cy = _mm512_fmadd_ps(_mm512_set1_ps(float(i)), y_scale, y_shift);
        for (uint32_t j = 0; j < img_size; j += 64){
            __m512 cx1 = _mm512_fmadd_ps(_mm512_add_ps(j_vec, _mm512_set1_ps(float(j))), x_scale, x_shift);
            __m512 cx2 = _mm512_fmadd_ps(_mm512_add_ps(j_vec, _mm512_add_ps(_mm512_set1_ps(float(j)), _mm512_set1_ps(16))), x_scale, x_shift);
            __m512 cx3 = _mm512_fmadd_ps(_mm512_add_ps(j_vec, _mm512_add_ps(_mm512_set1_ps(float(j)), _mm512_set1_ps(32))), x_scale, x_shift);
            __m512 cx4 = _mm512_fmadd_ps(_mm512_add_ps(j_vec, _mm512_add_ps(_mm512_set1_ps(float(j)), _mm512_set1_ps(48))), x_scale, x_shift);  

            __m512 x1 = _mm512_setzero_ps(), y1 = _mm512_setzero_ps(), x2_1 = _mm512_setzero_ps(), y2_1 = _mm512_setzero_ps();
            __m512 x2 = _mm512_setzero_ps(), y2 = _mm512_setzero_ps(), x2_2 = _mm512_setzero_ps(), y2_2 = _mm512_setzero_ps();
            __m512 x3 = _mm512_setzero_ps(), y3 = _mm512_setzero_ps(), x2_3 = _mm512_setzero_ps(), y2_3 = _mm512_setzero_ps();
            __m512 x4 = _mm512_setzero_ps(), y4 = _mm512_setzero_ps(), x2_4 = _mm512_setzero_ps(), y2_4 = _mm512_setzero_ps();

            __m512i iters1 = _mm512_setzero_si512();
            __m512i iters2 = _mm512_setzero_si512();
            __m512i iters3 = _mm512_setzero_si512();
            __m512i iters4 = _mm512_setzero_si512();

            __mmask16 mask1 = 0xFFFF, mask2 = 0xFFFF, mask3 = 0xFFFF, mask4 = 0xFFFF;

            for (uint32_t iter = 0; iter < max_iters; ++iter){
                __m512 xy1 = _mm512_mul_ps(x1, y1);
                y1 = _mm512_fmadd_ps(_mm512_add_ps(x1, x1), y1, cy);
                x1 = _mm512_add_ps(_mm512_sub_ps(x2_1, y2_1), cx1);
                x2_1 = _mm512_mul_ps(x1, x1);
                y2_1 = _mm512_mul_ps(y1, y1);
                __mmask16 cmp_mask1 = _mm512_cmp_ps_mask(_mm512_add_ps(x2_1, y2_1), four, _CMP_LE_OQ);
                mask1 &= cmp_mask1;
                
                iters1 = _mm512_mask_add_epi32(iters1, mask1, iters1, one);

                __m512 xy2 = _mm512_mul_ps(x2, y2);
                y2 = _mm512_fmadd_ps(_mm512_add_ps(x2, x2), y2, cy);
                x2 = _mm512_add_ps(_mm512_sub_ps(x2_2, y2_2), cx2);
                x2_2 = _mm512_mul_ps(x2, x2);
                y2_2 = _mm512_mul_ps(y2, y2);
                __mmask16 cmp_mask2 = _mm512_cmp_ps_mask(_mm512_add_ps(x2_2, y2_2), four, _CMP_LE_OQ);
                mask2 &= cmp_mask2;
                
                iters2 = _mm512_mask_add_epi32(iters2, mask2, iters2, one);

                __m512 xy3 = _mm512_mul_ps(x3, y3);
                y3 = _mm512_fmadd_ps(_mm512_add_ps(x3, x3), y3, cy);
                x3 = _mm512_add_ps(_mm512_sub_ps(x2_3, y2_3), cx3);
                x2_3 = _mm512_mul_ps(x3, x3);
                y2_3 = _mm512_mul_ps(y3, y3);
                __mmask16 cmp_mask3 = _mm512_cmp_ps_mask(_mm512_add_ps(x2_3, y2_3), four, _CMP_LE_OQ);
                mask3 &= cmp_mask3;
               
                iters3 = _mm512_mask_add_epi32(iters3, mask3, iters3, one);

                __m512 xy4 = _mm512_mul_ps(x4, y4);
                y4 = _mm512_fmadd_ps(_mm512_add_ps(x4, x4), y4, cy);
                x4 = _mm512_add_ps(_mm512_sub_ps(x2_4, y2_4), cx4);
                x2_4 = _mm512_mul_ps(x4, x4);
                y2_4 = _mm512_mul_ps(y4, y4);
                __mmask16 cmp_mask4 = _mm512_cmp_ps_mask(_mm512_add_ps(x2_4, y2_4), four, _CMP_LE_OQ);
                mask4 &= cmp_mask4;
                
                iters4 = _mm512_mask_add_epi32(iters4, mask4, iters4, one);

                if ( (mask1 | mask2 | mask3 | mask4) == 0) break;   


            }

            _mm512_storeu_si512(out + i * img_size + j, iters1);
            _mm512_storeu_si512(out + i * img_size + j + 16, iters2);
            _mm512_storeu_si512(out + i * img_size + j + 32, iters3);
            _mm512_storeu_si512(out + i * img_size + j + 48, iters4);

        }
    }
}


////////////////////////////////////////////////////////////////////////////////
// Vector + Multi-core

void mandelbrot_cpu_vector_multicore(
    uint32_t img_size,
    uint32_t max_iters,
    uint32_t *out) {
    // TODO: Implement this function.
}

////////////////////////////////////////////////////////////////////////////////
// Vector + Multi-core + Multi-thread-per-core

void mandelbrot_cpu_vector_multicore_multithread(
    uint32_t img_size,
    uint32_t max_iters,
    uint32_t *out) {
    // TODO: Implement this function.
}

////////////////////////////////////////////////////////////////////////////////
// Vector + Multi-core + Multi-thread-per-core + ILP

void mandelbrot_cpu_vector_multicore_multithread_ilp(
    uint32_t img_size,
    uint32_t max_iters,
    uint32_t *out) {
    // TODO: Implement this function.
}

/// <--- /your code here --->

////////////////////////////////////////////////////////////////////////////////
///          YOU DO NOT NEED TO MODIFY THE CODE BELOW HERE.                  ///
////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/types.h>
#include <vector>

// Useful functions and structures.
enum MandelbrotImpl {
    SCALAR,
    VECTOR,
    VECTOR_ILP,
    VECTOR_MULTICORE,
    VECTOR_MULTICORE_MULTITHREAD,
    VECTOR_MULTICORE_MULTITHREAD_ILP,
    ALL
};

// Command-line arguments parser.
int ParseArgsAndMakeSpec(
    int argc,
    char *argv[],
    uint32_t *img_size,
    uint32_t *max_iters,
    MandelbrotImpl *impl) {
    char *implementation_str = nullptr;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) {
            if (i + 1 < argc) {
                *img_size = atoi(argv[++i]);
                if (*img_size % 32 != 0) {
                    std::cerr << "Error: Image width must be a multiple of 32"
                              << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: No value specified for -r" << std::endl;
                return 1;
            }
        } else if (strcmp(argv[i], "-b") == 0) {
            if (i + 1 < argc) {
                *max_iters = atoi(argv[++i]);
            } else {
                std::cerr << "Error: No value specified for -b" << std::endl;
                return 1;
            }
        } else if (strcmp(argv[i], "-i") == 0) {
            if (i + 1 < argc) {
                implementation_str = argv[++i];
                if (strcmp(implementation_str, "scalar") == 0) {
                    *impl = SCALAR;
                } else if (strcmp(implementation_str, "vector") == 0) {
                    *impl = VECTOR;
                } else if (strcmp(implementation_str, "vector_ilp") == 0) {
                    *impl = VECTOR_ILP;
                } else if (strcmp(implementation_str, "vector_multicore") == 0) {
                    *impl = VECTOR_MULTICORE;
                } else if (
                    strcmp(implementation_str, "vector_multicore_multithread") == 0) {
                    *impl = VECTOR_MULTICORE_MULTITHREAD;
                } else if (
                    strcmp(implementation_str, "vector_multicore_multithread_ilp") == 0) {
                    *impl = VECTOR_MULTICORE_MULTITHREAD_ILP;
                } else if (strcmp(implementation_str, "all") == 0) {
                    *impl = ALL;
                } else {
                    std::cerr << "Error: unknown implementation" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: No value specified for -i" << std::endl;
                return 1;
            }
        } else {
            std::cerr << "Unknown flag: " << argv[i] << std::endl;
            return 1;
        }
    }
    std::cout << "Testing with image size " << *img_size << "x" << *img_size << " and "
              << *max_iters << " max iterations." << std::endl;

    return 0;
}

// Output image writers: BMP file header structure
#pragma pack(push, 1)
struct BMPHeader {
    uint16_t fileType{0x4D42};   // File type, always "BM"
    uint32_t fileSize{0};        // Size of the file in bytes
    uint16_t reserved1{0};       // Always 0
    uint16_t reserved2{0};       // Always 0
    uint32_t dataOffset{54};     // Start position of pixel data
    uint32_t headerSize{40};     // Size of this header (40 bytes)
    int32_t width{0};            // Image width in pixels
    int32_t height{0};           // Image height in pixels
    uint16_t planes{1};          // Number of color planes
    uint16_t bitsPerPixel{24};   // Bits per pixel (24 for RGB)
    uint32_t compression{0};     // Compression method (0 for uncompressed)
    uint32_t imageSize{0};       // Size of raw bitmap data
    int32_t xPixelsPerMeter{0};  // Horizontal resolution
    int32_t yPixelsPerMeter{0};  // Vertical resolution
    uint32_t colorsUsed{0};      // Number of colors in the color palette
    uint32_t importantColors{0}; // Number of important colors
};
#pragma pack(pop)

void writeBMP(const char *fname, uint32_t img_size, const std::vector<uint8_t> &pixels) {
    uint32_t width = img_size;
    uint32_t height = img_size;

    BMPHeader header;
    header.width = width;
    header.height = height;
    header.imageSize = width * height * 3;
    header.fileSize = header.dataOffset + header.imageSize;

    std::ofstream file(fname, std::ios::binary);
    file.write(reinterpret_cast<const char *>(&header), sizeof(header));
    file.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
}

std::vector<uint8_t> iters_to_colors(
    uint32_t img_size,
    uint32_t max_iters,
    const std::vector<uint32_t> &iters) {
    uint32_t width = img_size;
    uint32_t height = img_size;
    uint32_t min_iters = max_iters;
    for (uint32_t i = 0; i < img_size; i++) {
        for (uint32_t j = 0; j < img_size; j++) {
            min_iters = std::min(min_iters, iters[i * img_size + j]);
        }
    }
    float log_iters_min = log2f(static_cast<float>(min_iters));
    float log_iters_range =
        log2f(static_cast<float>(max_iters) / static_cast<float>(min_iters));
    auto pixel_data = std::vector<uint8_t>(width * height * 3);
    for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
            uint32_t iter = iters[i * width + j];

            uint8_t r = 0, g = 0, b = 0;
            if (iter < max_iters) {
                auto log_iter = log2f(static_cast<float>(iter)) - log_iters_min;
                auto intensity = static_cast<uint8_t>(log_iter * 222 / log_iters_range);
                r = 32;
                g = 32 + intensity;
                b = 32;
            }

            auto index = (i * width + j) * 3;
            pixel_data[index] = b;
            pixel_data[index + 1] = g;
            pixel_data[index + 2] = r;
        }
    }
    return pixel_data;
}

// Benchmarking macros and configuration.
static constexpr size_t kNumOfOuterIterations = 10;
static constexpr size_t kNumOfInnerIterations = 1;
#define BENCHPRESS(func, ...) \
    do { \
        std::cout << std::endl << "Running " << #func << " ...\n"; \
        std::vector<double> times(kNumOfOuterIterations); \
        for (size_t i = 0; i < kNumOfOuterIterations; ++i) { \
            auto start = std::chrono::high_resolution_clock::now(); \
            for (size_t j = 0; j < kNumOfInnerIterations; ++j) { \
                func(__VA_ARGS__); \
            } \
            auto end = std::chrono::high_resolution_clock::now(); \
            times[i] = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start) \
                           .count() / \
                kNumOfInnerIterations; \
        } \
        std::sort(times.begin(), times.end()); \
        std::stringstream sstream; \
        sstream << std::fixed << std::setw(6) << std::setprecision(2) \
                << times[0] / 1'000'000; \
        std::cout << "  Runtime: " << sstream.str() << " ms" << std::endl; \
    } while (0)

double difference(
    uint32_t img_size,
    uint32_t max_iters,
    std::vector<uint32_t> &result,
    std::vector<uint32_t> &ref_result) {
    int64_t diff = 0;
    for (uint32_t i = 0; i < img_size; i++) {
        for (uint32_t j = 0; j < img_size; j++) {
            diff +=
                abs(int(result[i * img_size + j]) - int(ref_result[i * img_size + j]));
        }
    }
    return diff / double(img_size * img_size * max_iters);
}

void dump_image(
    const char *fname,
    uint32_t img_size,
    uint32_t max_iters,
    const std::vector<uint32_t> &iters) {
    // Dump result as an image.
    auto pixel_data = iters_to_colors(img_size, max_iters, iters);
    writeBMP(fname, img_size, pixel_data);
}

// Main function.
// Compile with:
//  g++ -march=native -O3 -Wall -Wextra -o mandelbrot mandelbrot_cpu.cc
int main(int argc, char *argv[]) {
    // Get Mandelbrot spec.
    uint32_t img_size = 1024;
    uint32_t max_iters = default_max_iters;
    enum MandelbrotImpl impl = ALL;
    if (ParseArgsAndMakeSpec(argc, argv, &img_size, &max_iters, &impl))
        return -1;

    // Allocate memory.
    std::vector<uint32_t> result(img_size * img_size);
    std::vector<uint32_t> ref_result(img_size * img_size);

    // Compute the reference solution
    mandelbrot_cpu_scalar(img_size, max_iters, ref_result.data());

    // Test the desired kernels.
    if (impl == SCALAR || impl == ALL) {
        memset(result.data(), 0, sizeof(uint32_t) * img_size * img_size);
        BENCHPRESS(mandelbrot_cpu_scalar, img_size, max_iters, result.data());
        dump_image("out/mandelbrot_cpu_scalar.bmp", img_size, max_iters, result);
    }

#ifdef HAS_VECTOR_IMPL
    if (impl == VECTOR || impl == ALL) {
        memset(result.data(), 0, sizeof(uint32_t) * img_size * img_size);
        BENCHPRESS(mandelbrot_cpu_vector, img_size, max_iters, result.data());
        dump_image("out/mandelbrot_cpu_vector.bmp", img_size, max_iters, result);

        std::cout << "  Correctness: average output difference from reference = "
                  << difference(img_size, max_iters, result, ref_result) << std::endl;
    }
#endif

    if (impl == VECTOR_ILP || impl == ALL) {
        memset(result.data(), 0, sizeof(uint32_t) * img_size * img_size);
        BENCHPRESS(mandelbrot_cpu_vector_ilp, img_size, max_iters, result.data());
        dump_image("out/mandelbrot_cpu_vector_ilp.bmp", img_size, max_iters, result);

        std::cout << "  Correctness: average output difference from reference = "
                  << difference(img_size, max_iters, result, ref_result) << std::endl;
    }

    if (impl == VECTOR_MULTICORE || impl == ALL) {
        memset(result.data(), 0, sizeof(uint32_t) * img_size * img_size);
        BENCHPRESS(mandelbrot_cpu_vector_multicore, img_size, max_iters, result.data());
        dump_image(
            "out/mandelbrot_cpu_vector_multicore.bmp",
            img_size,
            max_iters,
            result);

        std::cout << "  Correctness: average output difference from reference = "
                  << difference(img_size, max_iters, result, ref_result) << std::endl;
    }

    if (impl == VECTOR_MULTICORE_MULTITHREAD || impl == ALL) {
        memset(result.data(), 0, sizeof(uint32_t) * img_size * img_size);
        BENCHPRESS(
            mandelbrot_cpu_vector_multicore_multithread,
            img_size,
            max_iters,
            result.data());
        dump_image(
            "out/mandelbrot_cpu_vector_multicore_multithread.bmp",
            img_size,
            max_iters,
            result);

        std::cout << "  Correctness: average output difference from reference = "
                  << difference(img_size, max_iters, result, ref_result) << std::endl;
    }

    if (impl == VECTOR_MULTICORE_MULTITHREAD_ILP || impl == ALL) {
        memset(result.data(), 0, sizeof(uint32_t) * img_size * img_size);
        BENCHPRESS(
            mandelbrot_cpu_vector_multicore_multithread_ilp,
            img_size,
            max_iters,
            result.data());
        dump_image(
            "out/mandelbrot_cpu_vector_multicore_multithread_ilp.bmp",
            img_size,
            max_iters,
            result);

        std::cout << "  Correctness: average output difference from reference = "
                  << difference(img_size, max_iters, result, ref_result) << std::endl;
    }

    return 0;
}
