/**
 * WinPR: Windows Portable Runtime
 * Debugging Utils
 *
 * Copyright 2014 Armin Novak <armin.novak@thincast.com>
 * Copyright 2014 Thincast Technologies GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <winpr/config.h>

#include <stdio.h>
#include <fcntl.h>
#include <winpr/string.h>

#if defined(HAVE_EXECINFO_H)
#include <execinfo/debug.h>
#endif

#if defined(ANDROID)
#include <corkscrew/backtrace.h>
#endif

#if defined(_WIN32) || defined(_WIN64)
#include <windows/debug.h>
#endif

#include <winpr/crt.h>
#include <winpr/wlog.h>
#include <winpr/debug.h>

#ifndef MIN
#define MIN(a, b) (a) < (b) ? (a) : (b)
#endif

#define TAG "com.winpr.utils.debug"
#define LOGT(...)                                           \
	do                                                      \
	{                                                       \
		WLog_Print(WLog_Get(TAG), WLOG_TRACE, __VA_ARGS__); \
	} while (0)
#define LOGD(...)                                           \
	do                                                      \
	{                                                       \
		WLog_Print(WLog_Get(TAG), WLOG_DEBUG, __VA_ARGS__); \
	} while (0)
#define LOGI(...)                                          \
	do                                                     \
	{                                                      \
		WLog_Print(WLog_Get(TAG), WLOG_INFO, __VA_ARGS__); \
	} while (0)
#define LOGW(...)                                          \
	do                                                     \
	{                                                      \
		WLog_Print(WLog_Get(TAG), WLOG_WARN, __VA_ARGS__); \
	} while (0)
#define LOGE(...)                                           \
	do                                                      \
	{                                                       \
		WLog_Print(WLog_Get(TAG), WLOG_ERROR, __VA_ARGS__); \
	} while (0)
#define LOGF(...)                                           \
	do                                                      \
	{                                                       \
		WLog_Print(WLog_Get(TAG), WLOG_FATAL, __VA_ARGS__); \
	} while (0)

static const char* support_msg = "Invalid stacktrace buffer! check if platform is supported!";

#if defined(ANDROID)
#include <pthread.h>
#include <dlfcn.h>
#include <unistd.h>

typedef struct
{
	backtrace_frame_t* buffer;
	size_t max;
	size_t used;
} t_corkscrew_data;

typedef struct
{
	void* hdl;
	ssize_t (*unwind_backtrace)(backtrace_frame_t* backtrace, size_t ignore_depth,
	                            size_t max_depth);
	ssize_t (*unwind_backtrace_thread)(pid_t tid, backtrace_frame_t* backtrace, size_t ignore_depth,
	                                   size_t max_depth);
	ssize_t (*unwind_backtrace_ptrace)(pid_t tid, const ptrace_context_t* context,
	                                   backtrace_frame_t* backtrace, size_t ignore_depth,
	                                   size_t max_depth);
	void (*get_backtrace_symbols)(const backtrace_frame_t* backtrace, size_t frames,
	                              backtrace_symbol_t* backtrace_symbols);
	void (*get_backtrace_symbols_ptrace)(const ptrace_context_t* context,
	                                     const backtrace_frame_t* backtrace, size_t frames,
	                                     backtrace_symbol_t* backtrace_symbols);
	void (*free_backtrace_symbols)(backtrace_symbol_t* backtrace_symbols, size_t frames);
	void (*format_backtrace_line)(unsigned frameNumber, const backtrace_frame_t* frame,
	                              const backtrace_symbol_t* symbol, char* buffer,
	                              size_t bufferSize);
} t_corkscrew;

static pthread_once_t initialized = PTHREAD_ONCE_INIT;
static t_corkscrew* fkt = NULL;

void load_library(void)
{
	static t_corkscrew lib;
	{
		lib.hdl = dlopen("libcorkscrew.so", RTLD_LAZY);

		if (!lib.hdl)
		{
			LOGF("dlopen error %s", dlerror());
			goto fail;
		}

		lib.unwind_backtrace = dlsym(lib.hdl, "unwind_backtrace");

		if (!lib.unwind_backtrace)
		{
			LOGF("dlsym error %s", dlerror());
			goto fail;
		}

		lib.unwind_backtrace_thread = dlsym(lib.hdl, "unwind_backtrace_thread");

		if (!lib.unwind_backtrace_thread)
		{
			LOGF("dlsym error %s", dlerror());
			goto fail;
		}

		lib.unwind_backtrace_ptrace = dlsym(lib.hdl, "unwind_backtrace_ptrace");

		if (!lib.unwind_backtrace_ptrace)
		{
			LOGF("dlsym error %s", dlerror());
			goto fail;
		}

		lib.get_backtrace_symbols = dlsym(lib.hdl, "get_backtrace_symbols");

		if (!lib.get_backtrace_symbols)
		{
			LOGF("dlsym error %s", dlerror());
			goto fail;
		}

		lib.get_backtrace_symbols_ptrace = dlsym(lib.hdl, "get_backtrace_symbols_ptrace");

		if (!lib.get_backtrace_symbols_ptrace)
		{
			LOGF("dlsym error %s", dlerror());
			goto fail;
		}

		lib.free_backtrace_symbols = dlsym(lib.hdl, "free_backtrace_symbols");

		if (!lib.free_backtrace_symbols)
		{
			LOGF("dlsym error %s", dlerror());
			goto fail;
		}

		lib.format_backtrace_line = dlsym(lib.hdl, "format_backtrace_line");

		if (!lib.format_backtrace_line)
		{
			LOGF("dlsym error %s", dlerror());
			goto fail;
		}

		fkt = &lib;
		return;
	}
fail:
{
	if (lib.hdl)
		dlclose(lib.hdl);

	fkt = NULL;
}
}
#endif

void winpr_backtrace_free(void* buffer)
{
	if (!buffer)
		return;

#if defined(HAVE_EXECINFO_H)
	winpr_execinfo_backtrace_free(buffer);
#elif defined(ANDROID)
	t_corkscrew_data* data = (t_corkscrew_data*)buffer;
	free(data->buffer);
	free(data);
#elif defined(_WIN32) || defined(_WIN64)
	winpr_win_backtrace_free(buffer);
#else
	LOGF(support_msg);
#endif
}

void* winpr_backtrace(DWORD size)
{
#if defined(HAVE_EXECINFO_H)
	return winpr_execinfo_backtrace(size);
#elif defined(ANDROID)
	t_corkscrew_data* data = calloc(1, sizeof(t_corkscrew_data));

	if (!data)
		return NULL;

	data->buffer = calloc(size, sizeof(backtrace_frame_t));

	if (!data->buffer)
	{
		free(data);
		return NULL;
	}

	pthread_once(&initialized, load_library);
	data->max = size;
	data->used = fkt->unwind_backtrace(data->buffer, 0, size);
	return data;
#elif (defined(_WIN32) || defined(_WIN64)) && !defined(_UWP)
	return winpr_win_backtrace(size);
#else
	LOGF(support_msg);
	return NULL;
#endif
}

char** winpr_backtrace_symbols(void* buffer, size_t* used)
{
	if (used)
		*used = 0;

	if (!buffer)
	{
		LOGF(support_msg);
		return NULL;
	}

#if defined(HAVE_EXECINFO_H)
	return winpr_execinfo_backtrace_symbols(buffer, used);
#elif defined(ANDROID)
	t_corkscrew_data* data = (t_corkscrew_data*)buffer;

	if (!data)
		return NULL;

	pthread_once(&initialized, load_library);

	if (!fkt)
	{
		LOGF(support_msg);
		return NULL;
	}
	else
	{
		size_t line_len = (data->max > 1024) ? data->max : 1024;
		size_t i;
		size_t array_size = data->used * sizeof(char*);
		size_t lines_size = data->used * line_len;
		char** vlines = calloc(1, array_size + lines_size);
		backtrace_symbol_t* symbols = calloc(data->used, sizeof(backtrace_symbol_t));

		if (!vlines || !symbols)
		{
			free(vlines);
			free(symbols);
			return NULL;
		}

		/* Set the pointers in the allocated buffer's initial array section */
		for (i = 0; i < data->used; i++)
			vlines[i] = (char*)vlines + array_size + i * line_len;

		fkt->get_backtrace_symbols(data->buffer, data->used, symbols);

		for (i = 0; i < data->used; i++)
			fkt->format_backtrace_line(i, &data->buffer[i], &symbols[i], vlines[i], line_len);

		fkt->free_backtrace_symbols(symbols, data->used);
		free(symbols);

		if (used)
			*used = data->used;

		return vlines;
	}

#elif (defined(_WIN32) || defined(_WIN64)) && !defined(_UWP)
	return winpr_win_backtrace_symbols(buffer, used);
#else
	LOGF(support_msg);
	return NULL;
#endif
}

void winpr_backtrace_symbols_fd(void* buffer, int fd)
{
	if (!buffer)
	{
		LOGF(support_msg);
		return;
	}

#if defined(HAVE_EXECINFO_H)
	winpr_execinfo_backtrace_symbols_fd(buffer, fd);
#elif !defined(ANDROID)
	{
		size_t i;
		size_t used = 0;
		char** lines = winpr_backtrace_symbols(buffer, &used);

		if (!lines)
			return;

		for (i = 0; i < used; i++)
			write(fd, lines[i], (unsigned)strnlen(lines[i], UINT32_MAX));
	}
#else
	LOGF(support_msg);
#endif
}

void winpr_log_backtrace(const char* tag, DWORD level, DWORD size)
{
	winpr_log_backtrace_ex(WLog_Get(tag), level, size);
}

void winpr_log_backtrace_ex(wLog* log, DWORD level, DWORD size)
{
	size_t used, x;
	char** msg;
	void* stack = winpr_backtrace(20);

	if (!stack)
	{
		WLog_Print(log, WLOG_ERROR, "winpr_backtrace failed!\n");
		goto fail;
	}

	msg = winpr_backtrace_symbols(stack, &used);

	if (msg)
	{
		for (x = 0; x < used; x++)
			WLog_Print(log, level, "%" PRIuz ": %s\n", x, msg[x]);
	}
	free(msg);

fail:
	winpr_backtrace_free(stack);
}

char* winpr_strerror(DWORD dw, char* dmsg, size_t size)
{
#if defined(_WIN32)
	return winpr_win_strerror(dw, dmsg, size);
#else  /* defined(_WIN32) */
	_snprintf(dmsg, size, "%s", strerror(dw));
#endif /* defined(_WIN32) */
	return dmsg;
}
