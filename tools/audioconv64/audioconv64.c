#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>

bool flag_verbose = false;

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	#define LE32_TO_HOST(i) __builtin_bswap32(i)
	#define HOST_TO_LE32(i) __builtin_bswap32(i)
	#define LE16_TO_HOST(i) __builtin_bswap16(i)
	#define HOST_TO_LE16(i) __builtin_bswap16(i)

	#define BE32_TO_HOST(i) (i)
	#define HOST_TO_BE32(i) (i)
	#define LE16_TO_HOST(i) (i)
	#define HOST_TO_BE16(i) (i)
#else
	#define BE32_TO_HOST(i) __builtin_bswap32(i)
	#define HOST_TO_BE32(i) __builtin_bswap32(i)
	#define BE16_TO_HOST(i) __builtin_bswap16(i)
	#define HOST_TO_BE16(i) __builtin_bswap16(i)

	#define LE32_TO_HOST(i) (i)
	#define HOST_TO_LE32(i) (i)
	#define HOST_TO_LE16(i) (i)
	#define LE16_TO_HOST(i) (i)
#endif

__attribute__((noreturn, format(printf, 1, 2)))
void fatal(const char *str, ...) {
	va_list va;
	va_start(va, str);
	vfprintf(stderr, str, va);
	va_end(va);
	exit(1);
}


/************************************************************************************
 *  CONVERTERS
 ************************************************************************************/

#include "conv_wav64.c"
#include "conv_xm64.c"

/************************************************************************************
 *  MAIN
 ************************************************************************************/

void usage(void) {
	printf("audioconv64 -- Audio conversion tool for libdragon\n");
	printf("\n");
	printf("Usage:\n");
	printf("   audioconv64 [flags] <file-or-dir> [[flags] <file-or-dir>..]\n");
	printf("\n");
	printf("Supported conversions:\n");
	printf("   * WAV => WAV64 (Waveforms)\n");
	printf("   * XM  => XM64  (MilkyTracker, OpenMPT)\n");
	printf("\n");
	printf("Global options:\n");
	printf("   -o / --output <dir>       Specify output directory\n");
	printf("   -v / --verbose            Verbose mode\n");
	printf("\n");
	printf("WAV options:\n");
	printf("   --wav-loop <true|false>   Activate playback loop by default\n");
	printf("   --wav-loop-offset <N>     Set looping offset (in samples; default: 0)\n");
	printf("\n");
}

char* changeext(char* fn, char *ext) {
	char buf[4096];
	strcpy(buf, fn);
	*strrchr(buf, '.') = '\0';
	strcat(buf, ext);
	return strdup(buf);
}

void convert(char *infn, char *outfn1) {
	char *ext = strrchr(infn, '.');
	if (!ext) {
		fprintf(stderr, "unknown file type: %s\n", infn);
		return;
	}

	if (strcmp(ext, ".wav") == 0 || strcmp(ext, ".WAV") == 0) {
		char *outfn = changeext(outfn1, ".wav64");
		wav_convert(infn, outfn);
		free(outfn);
	} else if (strcmp(ext, ".xm") == 0 || strcmp(ext, ".XM") == 0) {
		char *outfn = changeext(outfn1, ".xm64");
		printf("convert: %s %s %s\n", infn, outfn1, outfn);
		xm_convert(infn, outfn);
		free(outfn);
	} else {
		fprintf(stderr, "WARNING: ignoring unknown file: %s\n", infn);
	}
}

bool isfile(const char *path) {
	struct stat st;
	stat(path, &st);
	return (st.st_mode & S_IFREG) != 0;
}

bool isdir(const char *path) {
	struct stat st;
	stat(path, &st);
	return (st.st_mode & S_IFDIR) != 0;
}

void walkdir(char *inpath, char *outpath, void (*func)(char *, char*)) {
	if (isdir(inpath)) {
		// We're walking a directory. Make sure there's also a matching
		// output directory or create it otherwise.
		if (!isdir(outpath)) {
			// If there's an obstructing file, exit with an error.
			if (isfile(outpath)) {				
				fprintf(stderr, "ERROR: %s is a file but should be a directory\n", outpath);
				return;
			}
			#ifndef __MINGW32__
			mkdir(outpath, 0777);
			#else
			mkdir(outpath);
			#endif
		}
		DIR* d = opendir(inpath);
		struct dirent *de;
		while ((de = readdir(d))) {
			if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
				continue;
			char *inpathsub, *outpathsub;
			asprintf(&inpathsub, "%s/%s", inpath, de->d_name);
			asprintf(&outpathsub, "%s/%s", outpath, de->d_name);
			walkdir(inpathsub, outpathsub, func);
			free(inpathsub);
			free(outpathsub);
		}
		closedir(d);
	} else if (isfile(inpath)) {
		if (isdir(outpath)) {
			// We support the format "audioconv64 -o <dir> <file>" as special case
			char *outpathsub;
			char *basename = strrchr(inpath, '/');
			if (!basename) basename = inpath;
			asprintf(&outpathsub, "%s/%s", outpath, basename);

			func(inpath, outpathsub);

			free(outpathsub);
		} else {
			func(inpath, outpath);
		}
	} else {
		fprintf(stderr, "WARNING: ignoring special file: %s\n", inpath);
	}
}
int main(int argc, char *argv[]) {
	if (argc < 2) {
		usage();
		return 1;
	}

	char *outdir = ".";

	int i;
	for (i=1; i<argc; i++) {
		if (argv[i][0] == '-') {	
			if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) {
				flag_verbose = true;
			} else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
				if (++i == argc) {
					fprintf(stderr, "missing argument for -o/--output\n");
					return 1;
				}
				outdir = argv[i];
			} else if (!strcmp(argv[i], "--wav-loop")) {
				if (++i == argc) {
					fprintf(stderr, "missing argument for --wav-loop\n");
					return 1;
				}
				if (!strcmp(argv[i], "true") || !strcmp(argv[i], "1"))
					flag_wav_looping = true;
				else if (!strcmp(argv[i], "false") || !strcmp(argv[i], "0"))
					flag_wav_looping = true;
				else {
					fprintf(stderr, "invalid boolean argument for --wav-loop: %s\n", argv[i]);
					return 1;
				}
			} else if (!strcmp(argv[i], "--wav-loop-offset")) {
				if (++i == argc) {
					fprintf(stderr, "missing argument for --wav-loop-offset\n");
					return 1;
				}
				char extra;
				if (sscanf(argv[i], "%d%c", &flag_wav_looping_offset, &extra) != 1) {
					fprintf(stderr, "invalid integer arugment for --wav-loop-offset: %s\n", argv[i]);
					return 1;
				}
				flag_wav_looping = true;
			} else {
				fprintf(stderr, "invalid option: %s\n", argv[i]);
				return 1;
			}
		} else {
			// Positional argument. It's either a file or a directory. Convert it
			walkdir(argv[i], outdir, convert);
		}
	}

	return 0;
}
