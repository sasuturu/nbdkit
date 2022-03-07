#include <config.h>
#include "global.hpp"
#include <stdint.h>
#include <string.h>
#include <unistd.h>


////////// NBDKIT IO //////////
static int chunknheader_pread(void *handle, void *buf, uint32_t count, uint64_t offset, uint32_t flags) {
	try {
		//nbdkit_debug("chunknheader_pread");
		//uint64_t firstChunk = offset / CHUNKSIZE;
		//uint64_t lastByte = offset + count - 1;
		//uint64_t lastChunk = lastByte / CHUNKSIZE;
		//uint64_t lastChunkEnd = lastByte % CHUNKSIZE;

		char *buffer = (char *) buf;
		while(count > 0) {
			uint64_t chunkId = offset / CHUNKSIZE;
			uint64_t fileOffset = offset % CHUNKSIZE;
			uint32_t fileCount = MIN(count, CHUNKSIZE - fileOffset);

			chunk *chunk = global::getOrCreate(chunkId);
			chunk->pread(buffer, fileOffset, fileCount);
			global::ioDone(chunk, false);

			buffer += fileCount;
			count -= fileCount;
			offset += fileCount;
		}
		return 0;
	} catch(char const* err) {
		nbdkit_error("Got my own: %s", err);
	} catch(...) {
		nbdkit_error("Got something else...");
	}
	global::ERROR = true;
	return -1;
}

static int chunknheader_pwrite(void *handle, const void *buf, uint32_t count, uint64_t offset, uint32_t flags) {
	int64_t usec = global::ioSleepMicros();
	if(usec > 0) {
		if(usec > 1000000) nbdkit_debug("PWRITE: sleeping for %lu", usec);
		usleep(usec);
	}

	if(global::ERROR) {
		return -1;
	}

	try {
		//nbdkit_debug("chunknheader_pwrite");

		const char *buffer = (const char *) buf;
		while(count > 0) {
			uint64_t chunkId = offset / CHUNKSIZE;
			uint64_t fileOffset = offset % CHUNKSIZE;
			uint32_t fileCount = MIN(count, CHUNKSIZE - fileOffset);

			chunk *chunk = global::getOrCreate(chunkId, true);
			chunk->pwrite(buffer, fileOffset, fileCount);
			global::ioDone(chunk, true);

			buffer += fileCount;
			count -= fileCount;
			offset += fileCount;
		}
		return 0;
	} catch(char const* err) {
		nbdkit_error("Got my own: %s", err);
	} catch(...) {
		nbdkit_error("Got something else...");
	}
	global::ERROR = true;
	return -1;
}

static int chunknheader_flush (void *handle, uint32_t flags) {
	global::FLUSH();
	return 0;
}

////////// NBDKIT MGMT //////////


static void chunknheader_load(void) {
	nbdkit_debug("chunknheader_load");
	global::INIT();
	chunk::INIT();

	global::apply_config();
}

static void chunknheader_unload(void) {
	nbdkit_debug("chunknheader_unload");
	global::shutdown();
}

static int chunknheader_config(const char *key, const char *value) {
	global::apply_config();
	nbdkit_debug("got config %s = %s", key, value);
	return 0;
}

static int chunknheader_can_multi_conn (void *handle) {
	nbdkit_debug("CAN_MULTI_CONN");
	return 1;
}

static void* chunknheader_open(int readonly) {
	nbdkit_debug("chunknheader_open");
	return NBDKIT_HANDLE_NOT_NEEDED;
}
static void chunknheader_close(void *handle) {
	nbdkit_debug("chunknheader_close");
}
static int64_t chunknheader_get_size(void *handle) {
	return global::config.EXPORT_SIZE;
}
static int chunknheader_after_fork() {
	global::START();
	return 0;
}

static struct nbdkit_plugin plugin = {
		.name = "treefiles",
		.version = PACKAGE_VERSION,
		.load = chunknheader_load,
		.unload = chunknheader_unload,
		.config = chunknheader_config,
		.open = chunknheader_open,
		.close = chunknheader_close,
		.get_size = chunknheader_get_size,
		.pread = chunknheader_pread,
		.pwrite = chunknheader_pwrite,
		.flush = chunknheader_flush,
		//.can_fua = chunknheader_can_fua,
		.can_multi_conn = chunknheader_can_multi_conn,
		.after_fork = chunknheader_after_fork,
};

NBDKIT_REGISTER_PLUGIN(plugin)
