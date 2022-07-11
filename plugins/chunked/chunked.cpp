#include "global.hpp"
#include "fileops.hpp"
#include <config.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>


////////// NBDKIT IO //////////
static int chunked_pread(void *handle, void *buf, uint32_t count, uint64_t offset, uint32_t flags) {
	try {
		char *buffer = (char *) buf;
		while(count > 0) {
			int64_t chunkId;
			uint64_t fileOffset;
			uint32_t fileCount;

			if(offset < HEADERSIZE) {
				chunkId = -1;
				fileOffset = offset;
				fileCount = MIN(count, HEADERSIZE - fileOffset);
			} else {
				chunkId = (offset - HEADERSIZE) / CHUNKSIZE;
				fileOffset = (offset - HEADERSIZE) % CHUNKSIZE;
				fileCount = MIN(count, CHUNKSIZE - fileOffset);
			}

			ch_state state = global::getChunkForRead(chunkId);
			readFile(state.fd, buffer, fileOffset, fileCount);
			global::finishedOp(chunkId);

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

static int chunked_pwrite(void *handle, const void *buf, uint32_t count, uint64_t offset, uint32_t flags) {
	if(global::ERROR) {
		return -1;
	}

	try {
		//nbdkit_debug("chunked_pwrite");

		const char *buffer = (const char *) buf;
		while(count > 0) {
			int64_t chunkId;
			uint64_t fileOffset;
			uint32_t fileCount;

			if(offset < HEADERSIZE) {
				chunkId = -1;
				fileOffset = offset;
				fileCount = MIN(count, HEADERSIZE - fileOffset);
			} else {
				chunkId = (offset - HEADERSIZE) / CHUNKSIZE;
				fileOffset = (offset - HEADERSIZE) % CHUNKSIZE;
				fileCount = MIN(count, CHUNKSIZE - fileOffset);
			}

			ch_state state = global::getChunkForWrite(chunkId);
			writeFile(state.fd, buffer, fileOffset, fileCount);
			global::finishedOp(chunkId);

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

static int chunked_flush (void *handle, uint32_t flags) {
	global::closeAllOpenFiles();
	return 0;
}

////////// NBDKIT MGMT //////////


static void chunked_load(void) {
	nbdkit_debug("chunked_load");
	global::INIT();

	global::apply_config();
}

static void chunked_unload(void) {
	nbdkit_debug("chunked_unload");
	global::closeAllOpenFiles();
}

static int chunked_config(const char *key, const char *value) {
	global::apply_config();
	nbdkit_debug("got config %s = %s", key, value);
	return 0;
}

static int chunked_can_multi_conn (void *handle) {
	nbdkit_debug("CAN_MULTI_CONN");
	return 1;
}

static void* chunked_open(int readonly) {
	nbdkit_debug("chunked_open");
	return NBDKIT_HANDLE_NOT_NEEDED;
}
static void chunked_close(void *handle) {
	nbdkit_debug("chunked_close");
	global::closeAllOpenFiles();
}
static int64_t chunked_get_size(void *handle) {
	return global::config.NUM_CHUNKS * CHUNKSIZE;
}
static int chunked_after_fork() {
	global::START();
	return 0;
}

static struct nbdkit_plugin plugin = {
		.name = "chunked",
		.version = PACKAGE_VERSION,
		.load = chunked_load,
		.unload = chunked_unload,
		.config = chunked_config,
		.open = chunked_open,
		.close = chunked_close,
		.get_size = chunked_get_size,
		.pread = chunked_pread,
		.pwrite = chunked_pwrite,
		.flush = chunked_flush,
		//.can_fua = chunked_can_fua,
		.can_multi_conn = chunked_can_multi_conn,
		.after_fork = chunked_after_fork,
};

NBDKIT_REGISTER_PLUGIN(plugin)
