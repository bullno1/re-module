#ifndef REMODULE_H
#define REMODULE_H

#include <stddef.h>

#define REMODULE_VAR(TYPE, NAME) \
	extern TYPE NAME; \
	const remodule_var_info_t remodule__##NAME##_info = { \
		.name = #NAME, \
		.name_length = sizeof(#NAME) - 1, \
		.value_addr = &NAME, \
		.value_size = sizeof(NAME), \
	}; \
	REMODULE__SECTION_BEGIN \
	const remodule_var_info_t* const remodule__##NAME##_info_ptr = &remodule__##NAME##_info; \
	REMODULE__SECTION_END \
	TYPE NAME

#if defined(_MSC_VER)
#	define REMODULE__SECTION_BEGIN \
	__pragma(data_seg(push)); \
	__pragma(section("remodule$data", read)); \
	__declspec(allocate("remodule$data"))
#elif defined(__APPLE__)
#	define REMODULE__SECTION_BEGIN __attribute__((used, section("__DATA,remodule")))
#elif defined(__unix__)
#	define REMODULE__SECTION_BEGIN __attribute__((used, section("remodule")))
#else
#	error Unsupported compiler
#endif

#if defined(_MSC_VER)
#	define REMODULE__SECTION_END __pragma(data_seg(pop));
#elif defined(__APPLE__)
#	define REMODULE__SECTION_END
#elif defined(__unix__)
#	define REMODULE__SECTION_END
#endif

#ifndef REMODULE_ASSERT
#include <stdlib.h>
#include <stdio.h>

#define REMODULE_ASSERT(COND, MSG) \
	do { \
		if (!(COND)) { \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, MSG); \
			abort(); \
		} \
	} while(0)
#endif

#if defined(_MSC_VER)
#	define REMODULE_DYNLIB_EXT ".dll"
#elif defined(__APPLE__)
#	define REMODULE_DYNLIB_EXT ".dylib"
#elif defined(__unix__)
#	define REMODULE_DYNLIB_EXT ".so"
#endif

typedef struct remodule_s remodule_t;
typedef struct remodule_var_info_s remodule_var_info_t;

typedef enum remodule_op_e {
	REMODULE_OP_LOAD,
	REMODULE_OP_UNLOAD,
	REMODULE_OP_BEFORE_RELOAD,
	REMODULE_OP_AFTER_RELOAD,
} remodule_op_t;

struct remodule_var_info_s {
	const char* name;
	size_t name_length;
	void* value_addr;
	size_t value_size;
};

remodule_t*
remodule_load(const char* path, void* userdata);

void
remodule_reload(remodule_t* mod);

void
remodule_unload(remodule_t* mod);

#endif

#if defined(REMODULE_PLUGIN_IMPLEMENTATION) || defined(REMODULE_HOST_IMPLEMENTATION)

#define REMODULE_INFO_SYMBOL remodule__plugin_info
#define REMODULE_INFO_SYMBOL_STR REMODULE_STRINGIFY(REMODULE_INFO_SYMBOL)
#define REMODULE_STRINGIFY(X) REMODULE_STRINGIFY2(X)
#define REMODULE_STRINGIFY2(X) #X

typedef struct remodule_plugin_info_s {
	const remodule_var_info_t* const* var_info_begin;
	const remodule_var_info_t* const* var_info_end;
	void(*entry)(remodule_op_t op, void* userdata);
} remodule_plugin_info_t;

#endif

#ifdef REMODULE_PLUGIN_IMPLEMENTATION

#if defined(_MSC_VER)
#	define REMODULE_EXPORT __declspec(dllexport)
#else
#	define REMODULE_EXPORT __attribute__((visibility("default")))
#endif

#if defined(_MSC_VER)
__pragma(section("remodule$begin", read));
__pragma(section("remodule$data", read));
__pragma(section("remodule$end", read));
__declspec(allocate("remodule$begin")) extern const remodule_var_info_t* const remodule_var_info_begin = NULL;
__declspec(allocate("remodule$end")) extern const remodule_var_info_t* const remodule_var_info_end = NULL;
#elif defined(__APPLE__)
extern const remodule_var_info_t* const __start_remodule __asm("section$start$__DATA$remodule");
extern const remodule_var_info_t* const __stop_remodule __asm("section$end$__DATA$remodule");
__attribute__((used, section("__DATA,remodule"))) const remodule_var_info_t* const remodule__dummy = NULL;
#elif defined(__unix__)
extern const remodule_var_info_t* const __start_remodule;
extern const remodule_var_info_t* const __stop_remodule;
__attribute__((used, section("remodule"))) const remodule_var_info_t* const remodule__dummy = NULL;
#endif

#if defined(_MSC_VER)
#	define REMODULE_VAR_INFO_BEGIN (&remodule_var_info_begin + 1)
#	define REMODULE_VAR_INFO_END (&remodule_var_info_end)
#elif defined(__unix__) || defined(__APPLE__)
#	define REMODULE_VAR_INFO_BEGIN (&__start_remodule)
#	define REMODULE_VAR_INFO_END (&__stop_remodule)
#endif

void
remodule_entry(remodule_op_t op, void* userdata);

REMODULE_EXPORT remodule_plugin_info_t
REMODULE_INFO_SYMBOL(void) {
	return (remodule_plugin_info_t){
		.entry = &remodule_entry,
		.var_info_begin = REMODULE_VAR_INFO_BEGIN,
		.var_info_end = REMODULE_VAR_INFO_END,
	};
}

#endif

#ifdef REMODULE_HOST_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef HMODULE remodule_dynlib_t;

static remodule_dynlib_t
remodule_dynlib_open(const char* path) {
	return LoadLibraryA(path);
}

static void*
remodule_dynlib_find(remodule_dynlib_t lib, const char* name) {
	return GetProcAddress(lib, name);
}

static void
remodule_dynlib_close(remodule_dynlib_t lib) {
	FreeLibrary(lib);
}

#elif defined(__unix__) || defined(__APPLE__)

#include <dlfcn.h>

typedef void* remodule_dynlib_t;

static remodule_dynlib_t
remodule_dynlib_open(const char* path) {
	return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static void*
remodule_dynlib_find(remodule_dynlib_t lib, const char* name) {
	return dlsym(lib, name);
}

static void
remodule_dynlib_close(remodule_dynlib_t lib) {
	dlclose(lib);
}

#endif

typedef remodule_plugin_info_t(*remodule_info_fn_t)(void);

typedef struct remodule_tmp_var_storage_s {
	char* name;
	void* value;
	size_t name_length;
	size_t value_size;
} remodule_tmp_var_storage_t;

struct remodule_s {
	const char* path;
	void* userdata;
	remodule_plugin_info_t info;
	remodule_dynlib_t lib;
};

remodule_t*
remodule_load(const char* path, void* userdata) {
	remodule_dynlib_t lib = remodule_dynlib_open(path);
	REMODULE_ASSERT(lib != NULL, "Could not load library");

	remodule_info_fn_t info_fn = (remodule_info_fn_t)remodule_dynlib_find(lib, REMODULE_INFO_SYMBOL_STR);
	REMODULE_ASSERT(info_fn != NULL, "Module does not export info function");

	remodule_plugin_info_t plugin_info = info_fn();
	plugin_info.entry(REMODULE_OP_LOAD, userdata);

	remodule_t* mod = malloc(sizeof(remodule_t));
	*mod = (remodule_t){
		.path = path,
		.userdata = userdata,
		.info = plugin_info,
		.lib = lib,
	};
	return mod;
}

void
remodule_reload(remodule_t* mod) {
	mod->info.entry(REMODULE_OP_BEFORE_RELOAD, mod->userdata);

	// Store all static vars in a host-allocated buffer
	int num_vars = 0;
	size_t val_buffer_size = 0;
	size_t name_buffer_size = 0;
	for (
		const remodule_var_info_t* const* itr = mod->info.var_info_begin;
		itr != mod->info.var_info_end;
		++itr
	) {
		if (*itr == NULL) { continue; }
		remodule_var_info_t var_info = **itr;

		++num_vars;
		val_buffer_size += var_info.value_size;
		name_buffer_size += var_info.name_length;
	}

	void* tmp_buf = malloc(
		num_vars * sizeof(remodule_tmp_var_storage_t)
		+ val_buffer_size
		+ name_buffer_size
	);
	remodule_tmp_var_storage_t* entry_ptr = tmp_buf;
	char* data_ptr = (char*)(entry_ptr + num_vars);

	for (
		const remodule_var_info_t* const* itr = mod->info.var_info_begin;
		itr != mod->info.var_info_end;
		++itr
	) {
		if (*itr == NULL) { continue; }
		remodule_var_info_t var_info = **itr;

		remodule_tmp_var_storage_t* entry = entry_ptr++;

		entry->name = data_ptr;
		entry->name_length = var_info.name_length;
		data_ptr += var_info.name_length;

		entry->value = data_ptr;
		entry->value_size = var_info.value_size;
		data_ptr += var_info.value_size;

		memcpy(entry->name, var_info.name, var_info.name_length);
		memcpy(entry->value, var_info.value_addr, var_info.value_size);
	}

	remodule_dynlib_close(mod->lib);
	mod->lib = remodule_dynlib_open(mod->path);
	REMODULE_ASSERT(mod->lib != NULL, "Failed to reload");

	remodule_info_fn_t info_fn = (remodule_info_fn_t)remodule_dynlib_find(mod->lib, REMODULE_INFO_SYMBOL_STR);
	REMODULE_ASSERT(info_fn != NULL, "Module does not export info function");
	mod->info = info_fn();

	// Copy vars back in
	remodule_tmp_var_storage_t* tmp_storage = (remodule_tmp_var_storage_t*)tmp_buf;
	for (
		const remodule_var_info_t* const* var_itr = mod->info.var_info_begin;
		var_itr != mod->info.var_info_end;
		++var_itr
	) {
		if (*var_itr == NULL) { continue; }
		remodule_var_info_t var_info = **var_itr;

		for (
			int storage_index = 0; storage_index < num_vars; ++storage_index
		) {
			remodule_tmp_var_storage_t* storage = &tmp_storage[storage_index];
			if (
				storage->name_length == var_info.name_length
				&& storage->value_size == var_info.value_size
				&& memcmp(storage->name, var_info.name, storage->name_length) == 0
			) {
				memcpy(var_info.value_addr, storage->value, storage->value_size);
				break;
			}
		}
	}
	free(tmp_buf);

	mod->info.entry(REMODULE_OP_AFTER_RELOAD, mod->userdata);
}

void
remodule_unload(remodule_t* mod) {
	mod->info.entry(REMODULE_OP_UNLOAD, mod->userdata);
	remodule_dynlib_close(mod->lib);
	free(mod);
}

#endif
