/*
 * Minimal AMI library used by sikit's AMI loader tests.
 *
 * Implements the three AMI entry points per IBIS spec §10.  Behaviour:
 *
 *   * AMI_Init   — applies a single-tap FFE: scales the impulse response
 *                  by 0.8 to simulate a simple equalizer effect, then
 *                  reports back the chosen tap value.
 *   * AMI_GetWave — adds a small bias (+0.05 V) to the waveform so the
 *                   test can detect that the function was actually
 *                   called and the data round-tripped.
 *   * AMI_Close  — frees the heap-allocated handle.
 *
 * Compile as a shared library:
 *   gcc -shared -fPIC -o stub_ami.so stub_ami.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Portable shared-library export marker. Windows wants __declspec
   on the function decl; ELF/Mach-O use the GCC visibility attribute. */
#ifdef _WIN32
#  define EXPORT __declspec(dllexport)
#else
#  define EXPORT __attribute__((visibility("default")))
#endif

static char* xstrdup(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

EXPORT long AMI_Init(double* impulse_matrix,
                      long row_size,
                      long aggressors,
                      double sample_interval,
                      double bit_time,
                      const char* ami_parameters_in,
                      char** ami_parameters_out,
                      void** AMI_memory_handle,
                      char** msg) {
    (void)sample_interval;
    (void)bit_time;
    (void)ami_parameters_in;

    /* Apply a flat 0.8 gain to the impulse response so the test can
       detect that the function was actually invoked. */
    if (impulse_matrix) {
        long total = row_size * aggressors;
        for (long i = 0; i < total; ++i) impulse_matrix[i] *= 0.8;
    }

    *ami_parameters_out = xstrdup("(stub_ami (Tap1 0.8))");
    *AMI_memory_handle  = malloc(8);  /* opaque handle */
    *msg                = xstrdup("stub_ami init ok");
    return 1;  /* success */
}

EXPORT long AMI_GetWave(double* wave,
                         long wave_size,
                         double* clock_times,
                         char** ami_parameters_out,
                         void* AMI_memory_handle) {
    (void)clock_times;
    (void)AMI_memory_handle;

    /* Add a small bias so the wave is observably modified. */
    if (wave) {
        for (long i = 0; i < wave_size; ++i) wave[i] += 0.05;
    }

    *ami_parameters_out = xstrdup("");
    return 1;
}

EXPORT long AMI_Close(void* AMI_memory_handle) {
    if (AMI_memory_handle) free(AMI_memory_handle);
    return 1;
}
